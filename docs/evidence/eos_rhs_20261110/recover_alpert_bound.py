"""Recover native al_pert's conditional error without inventing stored samples."""
import argparse
from fractions import Fraction as F
import hashlib
import importlib.util
import json
import math
from pathlib import Path


def binary_bound(row):
    x = row['linf_upper']
    if not math.isfinite(x) or x < 0 or float.fromhex(row['linf_upper_binary64_hex']) != x:
        raise ValueError('invalid outward binary64 bound')
    return F.from_float(x)


def upper_float(x):
    y = float(x)
    return math.nextafter(y, math.inf) if F.from_float(y) < x else y


def recover(packet, domain, alb, ops):
    if packet['status'] != 'completed' or packet['mask_scope'] != 'identity':
        raise ValueError('unsupported packet')
    if domain['status'] != 'bounded_positive' or alb['status'] != 'passed':
        raise ValueError('unresolved prerequisite')
    e = packet['eos_scalars']
    n = e['levels']
    if n <= 0 or len(domain['levels']) != n or alb['shape'][1] != n:
        raise ValueError('level mismatch')
    raw = {x['name']: x for x in packet['raw_eos']['files']}
    for name in ('base_ph', 'accepted_ph', 'base_alt', 'accepted_alt', 'alb'):
        if name not in raw or raw[name]['source_dtype'] != 7:
            raise ValueError('FP64 operands/output required')
    if len(alb['analysis']['states']) != 2:
        raise ValueError('two endpoint bounds required')
    a = ops.exact(packet['a'])
    if a <= 0:
        raise ValueError('nonpositive stage coefficient')
    rad_mu = a * (ops.exact(packet['K_before_mu_absmax']) + ops.exact(packet['K_after_mu_absmax'])) + 1
    rad_ph = 2 * a * (ops.exact(e['K0_ph_absmax']) + ops.exact(e['K1_ph_absmax'])) + 1
    mu = ops.expand(ops.interval([packet['mu_min'], packet['mu_absmax']]), rad_mu)
    mb = ops.interval([packet['mub_min'], packet['mub_absmax']])
    rows = []
    for k, old in enumerate(domain['levels']):
        native_alb = e['base']['alb'][k]
        if native_alb != e['accepted']['alb'][k]:
            raise ValueError('changing alb')
        b = ops.interval([native_alb['min'], native_alb['max']])
        c1, c2, r = (ops.I(ops.exact(e[key][k])) for key in ('c1h', 'c2h', 'rdnw'))
        if r.lo <= 0:
            raise ValueError('nonpositive rdnw')
        d = e['base']['dphi_pert_computed'][k]
        dph = ops.expand(ops.interval([d['min'], d['max']]), rad_ph)
        den = c1.mul(mb.add(mu)).add(c2)
        if den.lo <= 0:
            raise ValueError('nonpositive denominator')
        u = b.mul(c1).mul(mu).add(r.neg().mul(dph)).neg().div(den)
        rounded_alpha = u.add(b)
        if den.bounds() != old['hybrid_denominator'] or rounded_alpha.bounds() != old['alt']:
            raise ValueError('rounded-operation tube does not match original domain')
        for ep in ('base', 'accepted'):
            observed = e[ep]['alt'][k]
            if not rounded_alpha.lo <= ops.exact(observed['min']) <= ops.exact(observed['max']) <= rounded_alpha.hi:
                raise ValueError('captured alpha outside rounded tube')
        sum_lo, sum_hi = u.lo + b.lo, u.hi + b.hi
        if max(abs(sum_lo), abs(sum_hi), abs(rounded_alpha.lo), abs(rounded_alpha.hi)) >= 8:
            raise ValueError('addition-binade envelope not established')
        rows.append({'level': k, 'native_al_pert_interval': u.bounds(),
                     'pre_round_sum_lower_exact': str(sum_lo),
                     'pre_round_sum_upper_exact': str(sum_hi),
                     'rounded_alpha_interval': rounded_alpha.bounds()})
    add_error = F(1, 2**50)
    upstream = binary_bound(alb['analysis']['upstream_alb'])
    states = []
    for name, state in zip(('base', 'accepted'), alb['analysis']['states']):
        exact = binary_bound(state['composed_native_alpha']) + upstream + add_error
        upper = upper_float(exact)
        if F.from_float(upper) >= F(4, 10**15):
            raise ValueError('registered 4e-15 component target not met')
        states.append({'name': name, 'error_upper_exact': str(exact),
                       'error_upper_binary64': upper, 'error_upper_binary64_hex': upper.hex()})
    return {'status': 'bounded_conditional', 'levels': rows,
            'addition_error_upper_exact': str(add_error), 'states': states,
            'identity': 'u_hat-u_star=(alpha_hat-alpha_star)-(alb_hat-alb_star)-eta_add',
            'scope': 'native al_pert endpoint component only; not sampled native al_pert or full RHS error',
            'assumptions': ['original fixed state box and faithful source-matched inputs',
                            'theta reconstruction remains fingerprint-qualified',
                            'separate finite binary64 operations in source order and FP64 output',
                            'one-ULP basic-operation contract; no reassociation or hidden intermediate precision',
                            'trusted previously recorded directed MPFR component bounds']}


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('receipt', type=Path)
    p.add_argument('domain', type=Path)
    p.add_argument('alb_analysis', type=Path)
    a = p.parse_args()
    sha = lambda x: hashlib.sha256(x.read_bytes()).hexdigest()
    packet = json.loads(a.receipt.read_text())
    domain = json.loads(a.domain.read_text())
    alb = json.loads(a.alb_analysis.read_text())
    if sha(a.receipt) != '9d252ca255cef90546f34119aefeb8df03c573413115241f9b09252aa956bd56' or sha(a.domain) != '684aac4c879859731bede2367e27245c4de4fc20cec3f6121b2a1f7e4c73800b':
        raise ValueError('not the registered historical receipt/domain pair')
    if sha(a.alb_analysis) != '2f9f9de1110de620c1725c2bb7727ee87c3773fa69aca6cc68b5ff4d8db051a2':
        raise ValueError('not the registered composed EOS analysis')
    if alb['receipt_sha256'] != sha(a.receipt):
        raise ValueError('alb analysis belongs to another pair')
    module = a.receipt.parent.parent / 'eos_domain_bounds.py'
    if sha(module) != '4c1b7d0f78054b3699ec91c18e8aa4510ecbfb41e35d919f75d86e58516cce08':
        raise ValueError('interval implementation changed')
    spec = importlib.util.spec_from_file_location('recorded_eos_intervals', module)
    ops = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ops)
    out = recover(packet, domain, alb, ops)
    out['input_sha256'] = {key: sha(getattr(a, key)) for key in ('receipt', 'domain', 'alb_analysis')}
    out['interval_source_sha256'] = sha(module)
    print(json.dumps(out, indent=2))
