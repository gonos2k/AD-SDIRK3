from copy import deepcopy
from fractions import Fraction as F
from widen_alb_tube import evaluate
r = {'eos_scalars': {'rd': 1, 'p0': 1, 'levels': 1, 'c1h': [1], 'c2h': [0]},
     'mub_min': 4, 'mub_absmax': 4, 'a': 1,
     'K_before_mu_absmax': 0, 'K_after_mu_absmax': 0, 'mu_min': 0, 'mu_absmax': 0}
d = {'status': 'bounded_positive', 'levels': [{'status': 'bounded_positive', 'level': 0,
     'hybrid_denominator': {'lower_exact': '2'},
     'alt': {'lower_exact': '1/2', 'upper_exact': '1'},
     'theta': {'lower_exact': '2', 'upper_exact': '3'}}]}
a = {'status': 'passed', 'shape': [1, 1, 1], 'analysis': {'upstream_alb': {
     'per_level_linf_upper': [0.125], 'per_level_linf_upper_binary64_hex': [float(.125).hex()]}}}
x = evaluate(r, d, a)['levels'][0]
assert F(x['alpha_lower_exact']) == F(1, 4)
assert F(x['alpha_upper_exact']) == F(5, 4)
assert F(x['pow_base_lower_exact']) == F(8, 5)
assert F(x['pow_base_upper_exact']) == 12
assert F(x['al_pert_shift_upper_exact']) == F(1, 16)
b = deepcopy(a)
b['analysis']['upstream_alb']['per_level_linf_upper'] = [1.0]
b['analysis']['upstream_alb']['per_level_linf_upper_binary64_hex'] = [float(1).hex()]
try:
    evaluate(r, d, b)
    raise AssertionError('cross-zero widening accepted')
except ValueError:
    pass
print('PASS exact rational widening, al_pert sensitivity, cross-zero refusal')
