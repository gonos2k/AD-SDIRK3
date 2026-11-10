"""Conditional binary64 EOS domain bounds; no pow accuracy or RHS error claim.

Intervals enclose ideal arithmetic and each computed basic operation assuming finite,
no-overflow binary64 results within one ULP of exact, including subnormals. Inputs
are captured binary64 extrema and fixed binary64 coefficients, not real estimates.
Q for the mass-grid blocks must be the source copy map (infinity norm 1).
"""
from fractions import Fraction as F
import math


def exact(x):
    if isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x):
        raise ValueError('expected finite scalar')
    return F.from_float(float(x))


def outward(x, direction):
    """Two representable steps beyond exact-to-nearest binary64."""
    try:
        y = float(x)
    except OverflowError as exc:
        raise ValueError('binary64 overflow') from exc
    if not math.isfinite(y):
        raise ValueError('binary64 overflow')
    target = -math.inf if direction < 0 else math.inf
    y = math.nextafter(math.nextafter(y, target), target)
    if not math.isfinite(y):
        raise ValueError('outward bound overflow')
    return F.from_float(y)


class I:
    def __init__(self, lo, hi=None):
        self.lo, self.hi = lo, lo if hi is None else hi
        if self.lo > self.hi:
            raise ValueError('inverted interval')

    def add(self, other):
        return I(outward(self.lo + other.lo, -1), outward(self.hi + other.hi, 1))

    def neg(self):
        return I(-self.hi, -self.lo)

    def sub(self, other):
        return self.add(other.neg())

    def mul(self, other):
        p = [self.lo * other.lo, self.lo * other.hi,
             self.hi * other.lo, self.hi * other.hi]
        return I(outward(min(p), -1), outward(max(p), 1))

    def div(self, other):
        if other.lo <= 0 <= other.hi:
            raise ValueError('denominator interval contains zero')
        p = [self.lo / other.lo, self.lo / other.hi,
             self.hi / other.lo, self.hi / other.hi]
        return I(outward(min(p), -1), outward(max(p), 1))

    def bounds(self):
        return {'lower_exact': str(self.lo), 'upper_exact': str(self.hi)}


def interval(pair):
    if len(pair) != 2:
        raise ValueError('expected [min,max]')
    return I(exact(pair[0]), exact(pair[1]))


def expand(x, radius):
    return I(x.lo - radius, x.hi + radius)


def evaluate(data):
    """Evaluate fixed per-level EOS coefficient intervals and segment enclosure.

    Required keys a,t0,rd,p0,B_t_absmax,B_ph_absmax,B_mu_absmax,
    K0_t_absmax,K1_t_absmax,K0_ph_absmax,K1_ph_absmax,
    K0_mu_absmax,K1_mu_absmax,mub=[lo,hi],mu=[lo,hi], levels.
    Each level has t,dph,alb intervals and scalar c1,c2,rdnw_abs.
    Base t, dph, mu are captured after projection. All extrema and coefficients
    must remain fixed and be source-matched to the accepted base/trial pair.
    """
    a,t0,rd,p0 = (exact(data[k]) for k in ('a','t0','rd','p0'))
    if a <= 0 or rd <= 0 or p0 <= 0:
        raise ValueError('a, rd and p0 must be positive')
    def vmax(k):
        v = exact(data[k])
        if v < 0:
            raise ValueError('negative absolute maximum')
        return v
    k = {field:(vmax('K0_'+field+'_absmax'),vmax('K1_'+field+'_absmax'))
         for field in ('t','ph','mu')}
    for field, extra in [('t',abs(t0)),('ph',F(0)),('mu',max(abs(interval(data['mub']).lo),abs(interval(data['mub']).hi)))]:
        envelope = vmax('B_'+field+'_absmax') + a*max(k[field]) + extra + 1
        if envelope >= 2**39:
            raise ValueError('state magnitude envelope not established for '+field)
    # One K-unit perturbation margin, predeclared; all 5 and 10 basic operation
    # budgets are <1 when every intermediate is below 2^40.
    mu = expand(interval(data['mu']), a*sum(k['mu'])+1)
    mub = interval(data['mub'])
    levels=[]
    if not data['levels']:
        raise ValueError('missing levels')
    for idx, level in enumerate(data['levels']):
        try:
            t = expand(interval(level['t']),a*sum(k['t'])+1)
            dph = expand(interval(level['dph']),2*a*sum(k['ph'])+1)
            alb=interval(level['alb'])
            c1,c2,r = (I(exact(level[name])) for name in ('c1','c2','rdnw_abs'))
            if r.lo <= 0:
                raise ValueError('rdnw_abs not positive')
            # Source order: muts=mub+mu; dph=phi1-phi0;
            # al_pert=-(alb*c1*mu + (-rdnw_abs)*dph)/(c1*muts+c2);
            # alt=al_pert+alb; pow base=rd*(t+t0)/(p0*alt).
            muts=mub.add(mu)
            den=c1.mul(muts).add(c2)
            if den.lo <= 0:
                raise ValueError('hybrid denominator not positive')
            numer=alb.mul(c1).mul(mu).add(r.neg().mul(dph))
            alt=numer.neg().div(den).add(alb)
            theta=t.add(I(t0))
            if alt.lo <= 0 or theta.lo <= 0:
                raise ValueError('alt or theta not positive')
            pow_den=I(p0).mul(alt)
            pow_num=I(rd).mul(theta)
            if pow_den.lo <= 0 or pow_num.lo <= 0:
                raise ValueError('pow numerator or denominator not positive')
            power_base=pow_num.div(pow_den)
            if power_base.lo <= 0:
                raise ValueError('pow base not positive')
            levels.append({'level':idx,'status':'bounded_positive','theta':theta.bounds(),
                           'hybrid_denominator':den.bounds(),'alt':alt.bounds(),
                           'pow_base':power_base.bounds()})
        except ValueError as exc:
            levels.append({'level':idx,'status':'unresolved','reason':str(exc)})
    return {'status':'bounded_positive' if all(x['status']=='bounded_positive' for x in levels) else 'unresolved',
            'scope':'conditional basic-operation EOS domain only; no pow error, pressure or full RHS error bound',
            'assumptions':['binary64 basic operations finite, no overflow, within one ULP including subnormal handling',
                           'fixed captured coefficients and B; Q mass copy infinity norm one',
                           'captured extrema and accepted K endpoints correspond to the same production pair',
                           'state magnitude envelopes and predeclared margins apply'],
            'levels':levels}
