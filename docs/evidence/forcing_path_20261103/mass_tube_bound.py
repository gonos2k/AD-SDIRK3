"""Exact-rational certificate for the mu-floor predicate only, not full RHS error.
Inputs are finite binary64 scalar extrema captured from the actual base/trial pair.
Assumes binary64 basic +/-/* rounded within one ULP, no overflow, fixed a and base mass,
and source-proven Q_mu copy extension (infinity norm <=1).
"""
from fractions import Fraction as F
import math,json,sys

def exact(x):
    if isinstance(x,bool) or not isinstance(x,(int,float)) or not math.isfinite(x):raise ValueError('nonfinite input')
    return F.from_float(float(x))
def lower_float(x):
    y=float(x)
    return math.nextafter(y,-math.inf) if F.from_float(y)>x else y

def certify(d):
    names=('mu_min','mu_absmax','mub_min','mub_absmax','B_mu_absmax','K_before_mu_absmax','K_after_mu_absmax','a')
    v={n:exact(d[n])for n in names}
    if v['a']<=0 or any(v[n]<0 for n in names if n.endswith('absmax')):raise ValueError('invalid extrema/coefficient')
    if abs(v['mu_min'])>v['mu_absmax'] or abs(v['mub_min'])>v['mub_absmax']:raise ValueError('inconsistent extrema')
    kmax=max(v['K_before_mu_absmax'],v['K_after_mu_absmax'])
    # Exact magnitude test leaves enormous room for all rounding below 2^40.
    envelope=v['B_mu_absmax']+v['a']*kmax+v['mub_absmax']+1
    magnitude_ok=envelope<F(2)**39 and v['mu_absmax']<F(2)**39
    # Each construction has a*K, +B and +mub; base captured mu has only first 2.
    # Below 2^40, one-ULP error per binary64 basic operation <=2^-12 Pa.
    # Thus 2 base + 3 trial operations cost <=5/4096 Pa < chosen 1 Pa allowance.
    rounding_allowance=F(1)
    lower=v['mu_min']+v['mub_min']-v['a']*(v['K_before_mu_absmax']+v['K_after_mu_absmax'])-rounding_allowance
    ok=magnitude_ok and lower>0
    return {'scope':'mu floor along real accepted segment and binary64 states in its coordinate box only',
      'conditional_on':['fixed a, B, MUB and source Q_mu copy map with infinity norm <=1','finite IEEE binary64 basic multiply/add within one ULP; no overflow; exact conversion of captured extrema','stored accepted K/R pair is the checked production pair'],
      'magnitude_condition':magnitude_ok,'magnitude_envelope_exact':str(envelope),'magnitude_limit':str(F(2)**39),
      'five_operation_rounding_budget_pa':'5/4096','conservative_rounding_allowance_pa':1,
      'mass_lower_exact_pa':str(lower),'mass_lower_outward_float_pa':lower_float(lower),
      'mu_floor_inactive_under_stated_conditions':ok,'full_RHS_or_forcing_certified':False}

if __name__=='__main__':
    print(json.dumps(certify(json.load(open(sys.argv[1]))),indent=2))
