"""Exact-real EOS domain after widening the recorded fixed-alb tube.

This uses the prior state-box/basic-arithmetic assumptions. It does not bound a
counterfactual floating-point evaluation or certify full residual error.
"""
import argparse
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path

# This proof is registered for one historical receipt and its original state box.
EXPECTED_RECEIPT_SHA256 = '9d252ca255cef90546f34119aefeb8df03c573413115241f9b09252aa956bd56'
EXPECTED_DOMAIN_SHA256 = '684aac4c879859731bede2367e27245c4de4fc20cec3f6121b2a1f7e4c73800b'


def exact(x):
    if isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x):
        raise ValueError('expected finite binary scalar')
    return F.from_float(float(x))


def evaluate(receipt, domain, alb):
    if domain['status'] != 'bounded_positive' or alb['status'] != 'passed':
        raise ValueError('unresolved prerequisite')
    levels = domain['levels']
    errors = alb['analysis']['upstream_alb']
    values = errors['per_level_linf_upper']
    encodings = errors['per_level_linf_upper_binary64_hex']
    if not levels or len(levels) != len(values) or len(values) != len(encodings):
        raise ValueError('level mismatch')
    e = receipt['eos_scalars']
    if len(values) != e['levels'] or alb['shape'][1] != e['levels']:
        raise ValueError('source level mismatch')
    rd, p0 = exact(e['rd']), exact(e['p0'])
    mblo, mbhi = exact(receipt['mub_min']), exact(receipt['mub_absmax'])
    if rd <= 0 or p0 <= 0 or mblo > mbhi:
        raise ValueError('unsupported coefficient bounds')
    a = exact(receipt['a'])
    k0 = exact(receipt['K_before_mu_absmax'])
    k1 = exact(receipt['K_after_mu_absmax'])
    if a <= 0 or k0 < 0 or k1 < 0:
        raise ValueError('unsupported rate bounds')
    radius = a * (k0 + k1) + 1
    mulo = exact(receipt['mu_min']) - radius
    muhi = exact(receipt['mu_absmax']) + radius
    rows = []
    for j, old in enumerate(levels):
        if old['status'] != 'bounded_positive' or old['level'] != j:
            raise ValueError('unresolved original level')
        value = values[j]
        if float.fromhex(encodings[j]) != value or exact(value) < 0:
            raise ValueError('invalid outward binary64 error encoding')
        ea = exact(value)
        c1, c2 = exact(e['c1h'][j]), exact(e['c2h'][j])
        dlo = F(old['hybrid_denominator']['lower_exact'])
        if dlo <= 0:
            raise ValueError('nonpositive denominator')
        beta_bound = max(abs(c1 * mblo + c2), abs(c1 * mbhi + c2)) / dlo
        alpha_shift = beta_bound * ea
        alpert_shift = max(abs(c1 * mulo), abs(c1 * muhi)) / dlo * ea
        lo = F(old['alt']['lower_exact']) - alpha_shift
        hi = F(old['alt']['upper_exact']) + alpha_shift
        theta_lo = F(old['theta']['lower_exact'])
        theta_hi = F(old['theta']['upper_exact'])
        if lo <= 0 or theta_lo <= 0 or theta_lo > theta_hi:
            raise ValueError('widened real domain not positive')
        rows.append({'level': j, 'alb_error_upper_exact': str(ea),
                     'beta_over_D_abs_upper_exact': str(beta_bound),
                     'alpha_shift_upper_exact': str(alpha_shift),
                     'al_pert_shift_upper_exact': str(alpert_shift),
                     'alpha_lower_exact': str(lo), 'alpha_upper_exact': str(hi),
                     'pow_base_lower_exact': str(rd * theta_lo / (p0 * hi)),
                     'pow_base_upper_exact': str(rd * theta_hi / (p0 * lo))})
    return {'status': 'bounded_positive', 'levels': rows,
            'scope': 'exact-real function with reconstructed real-alb input; original state-box and input-identity assumptions; not a counterfactual binary64 run or full RHS bound',
            'al_pert_scope': 'input-sensitivity component only; native al_pert arithmetic was not retained'}


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('receipt', type=Path)
    p.add_argument('domain', type=Path)
    p.add_argument('alb_analysis', type=Path)
    a = p.parse_args()
    sha = lambda x: hashlib.sha256(x.read_bytes()).hexdigest()
    receipt = json.loads(a.receipt.read_text())
    domain = json.loads(a.domain.read_text())
    alb = json.loads(a.alb_analysis.read_text())
    if sha(a.receipt) != EXPECTED_RECEIPT_SHA256 or sha(a.domain) != EXPECTED_DOMAIN_SHA256:
        raise ValueError('receipt/domain is not the registered historical pair')
    if alb['receipt_sha256'] != sha(a.receipt):
        raise ValueError('analysis belongs to another receipt')
    result = evaluate(receipt, domain, alb)
    result['input_sha256'] = {k: sha(getattr(a, k)) for k in
                              ('receipt', 'domain', 'alb_analysis')}
    print(json.dumps(result, indent=2))
