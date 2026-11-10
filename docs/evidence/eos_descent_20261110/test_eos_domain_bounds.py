import copy
from fractions import Fraction as F
from eos_domain_bounds import evaluate, I

def fixture():
    return dict(a=1.,t0=300.,rd=287.,p0=100000.,mub=[100.,100.],mu=[0.,0.],
        B_t_absmax=1.,B_ph_absmax=1.,B_mu_absmax=1.,
        K0_t_absmax=0.,K1_t_absmax=0.,K0_ph_absmax=0.,K1_ph_absmax=0.,
        K0_mu_absmax=0.,K1_mu_absmax=0.,
        levels=[dict(t=[0.,0.],dph=[0.,0.],alb=[1.,1.],c1=1.,c2=0.,rdnw_abs=1.)])

a=fixture(); assert evaluate(a)['status']=='bounded_positive'
b=copy.deepcopy(a); b['levels'][0]['c2']=-100.;assert evaluate(b)['status']=='unresolved'
# Positive total thickness can coexist with negative EOS numerator: PHB thickness
# is not an operand of calc_p_rho_wrf. Base total thickness 1000 + dph(-200)>0.
b=copy.deepcopy(a);b['levels'][0]['dph']=[-200.,-200.];assert 1000-200>0 and evaluate(b)['status']=='unresolved'
# Signed interval multiplication must include the negative/positive extrema.
x=I(F(-2),F(3)).mul(I(F(-4),F(5)));assert x.lo < -12 and x.hi > 15
# Accepted endpoint box widens temperature interval and can invalidate domain.
b=copy.deepcopy(a);b['K1_t_absmax']=400.;assert evaluate(b)['status']=='unresolved'
# Magnitude envelope must fail closed.
b=copy.deepcopy(a);b['B_ph_absmax']=2**39
try:evaluate(b);raise AssertionError('magnitude accepted')
except ValueError:pass
# Nonfinite input is rejected.
b=copy.deepcopy(a);b['levels'][0]['c1']=float('nan')
assert evaluate(b)['status']=='unresolved'
print('PASS: positive, denominator, total-PH counterexample, signed product, endpoint box, envelope, nonfinite')
