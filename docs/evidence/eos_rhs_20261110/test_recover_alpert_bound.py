"""Exact scalar checks; argv[1] is the supplied prior interval implementation."""
import copy
from fractions import Fraction as F
import importlib.util
from pathlib import Path
import sys
from recover_alpert_bound import recover

spec = importlib.util.spec_from_file_location('test_intervals', Path(sys.argv[1]))
ops = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ops)


def row(x):
    return {'min': x, 'max': x, 'absmax': abs(x)}


def fixture(b):
    endpoint = {'alb': [row(b)], 'dphi_pert_computed': [row(0)], 'alt': [row(b)]}
    packet = {'status': 'completed', 'mask_scope': 'identity', 'a': 1,
              'mu_min': 0, 'mu_absmax': 0, 'mub_min': 100, 'mub_absmax': 100,
              'K_before_mu_absmax': 0, 'K_after_mu_absmax': 0,
              'raw_eos': {'files': [{'name': x, 'source_dtype': 7} for x in
                                   ('base_ph', 'accepted_ph', 'base_alt', 'accepted_alt', 'alb')]},
              'eos_scalars': {'levels': 1, 'base': endpoint, 'accepted': copy.deepcopy(endpoint),
                             'K0_ph_absmax': 0, 'K1_ph_absmax': 0,
                             'c1h': [0], 'c2h': [10], 'rdnw': [1]}}
    data = {'a': 1, 't0': 300, 'rd': 287, 'p0': 100000, 'mu': [0, 0], 'mub': [100, 100],
            'B_mu_absmax': 0, 'B_ph_absmax': 0, 'B_t_absmax': 0,
            **{k: 0 for k in ('K0_mu_absmax', 'K1_mu_absmax', 'K0_ph_absmax', 'K1_ph_absmax', 'K0_t_absmax', 'K1_t_absmax')},
            'levels': [{'t': [0, 0], 'dph': [0, 0], 'alb': [b, b], 'c1': 0, 'c2': 10, 'rdnw_abs': 1}]}
    return packet, ops.evaluate(data)


def bound(x):
    return {'linf_upper': x, 'linf_upper_binary64_hex': x.hex()}


alb = {'status': 'passed', 'shape': [1, 1, 1], 'analysis': {
       'upstream_alb': bound(1e-15), 'states': [{'composed_native_alpha': bound(1e-15)}] * 2}}
p, d = fixture(1)
r = recover(p, d, alb, ops)
expected = 2 * F.from_float(1e-15) + F(1, 2**50)
assert all(F(s['error_upper_exact']) == expected and
           F.from_float(s['error_upper_binary64']) >= expected for s in r['states'])
def reject(packet, domain, bounds):
    try:
        recover(packet, domain, bounds, ops)
        raise AssertionError('invalid binade or bound encoding accepted')
    except ValueError:
        pass


reject(*fixture(10), alb)
bad = copy.deepcopy(alb)
bad['analysis']['upstream_alb']['linf_upper_binary64_hex'] = (1.0).hex()
reject(*fixture(1), bad)
print('PASS exact addition recovery, upward conversion, binade refusal and hex mismatch')
