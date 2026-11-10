"""W-pressure input sensitivity using observed consumer operands and metric.

The real intermediate model holds captured alpha_w/msfty/velocity_mass operands
fixed. Native divisions/products and formation relative to the full target G*
remain separate remainder terms; no level_mass alias identity is assumed.
"""
import argparse
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path
import struct

EXPECTED_EOS_NAMES = {'base_ph','base_t','base_mu','base_alt','base_p_pert',
                      'accepted_ph','accepted_t','accepted_mu','accepted_alt','accepted_p_pert',
                      'mu_base','alb','p_base','c1h','c2h','rdnw'}


def exact(v):
    if isinstance(v, bool) or not isinstance(v, (int, float)) or not math.isfinite(v):
        raise ValueError('finite numeric input required')
    return F.from_float(float(v))


def f32(v):
    exact(v)
    b = struct.pack('<f', v)
    if struct.unpack('<f', b)[0] != v:
        raise ValueError('not an exact promoted binary32 operand')
    return b


def levels(consumer, pressure_errors, weight_max):
    n = len(pressure_errors)
    rdn = consumer['rdn_values']
    if consumer['rdn_source_dtype'] != 6 or len(rdn) != consumer['rdn_count'] or not n <= len(rdn):
        raise ValueError('invalid consumed RDN shape/type')
    if consumer['rdn_used_begin'] != 1 or consumer['rdn_used_end_exclusive'] != n:
        raise ValueError('used vertical slice differs')
    h = 14695981039346656037
    for value in rdn:
        for byte in f32(value):
            h = ((h ^ byte) * 1099511628211) & ((1 << 64) - 1)
    if str(h) != consumer['rdn_fnv64_f32']:
        raise ValueError('RDN value/hash mismatch')
    for value in (consumer['gravity_f32'], consumer['top_rdnw_f32']):
        f32(value)
    if not consumer['canonical_horizontal'] or not consumer['cq1_all_one']:
        raise ValueError('unsupported real W pressure path')
    for prefix in ('msfty', 'alpha_w', 'level_mass_w', 'velocity_mass_w'):
        lo, hi = exact(consumer[prefix + '_min']), exact(consumer[prefix + '_max'])
        if lo <= 0 or hi < lo:
            raise ValueError('invalid consumer denominator/range')
    # This is the actual alpha_w operand, not the separately extended level_mass.
    denom = exact(consumer['msfty_min']) * exact(consumer['alpha_w_min'])
    g, top = exact(consumer['gravity_f32']), exact(consumer['top_rdnw_f32'])
    if g <= 0 or top <= 0 or any(exact(v) <= 0 for v in rdn[1:n]):
        raise ValueError('invalid consumed coefficient sign')
    errors = [exact(v) for v in pressure_errors]
    if not errors or any(v < 0 for v in errors) or weight_max <= 0:
        raise ValueError('invalid input error or inverse weight')
    bound = [F(0)]
    for k in range(1, n):
        bound.append(g * exact(rdn[k]) * (errors[k] + errors[k-1]) / denom)
    bound.append(2 * g * top * errors[-1] / denom)
    maximum = max(bound)
    # Same packed Nw/N < 1/4, after Qw's infinity-norm copy/zero bound.
    if not 4 * 215865 < 1080491:
        raise ValueError('packed RMS factor does not apply')
    return {'per_level_error_exact': [str(v) for v in bound],
            'effective_denominator_lower_exact': str(denom),
            'physical_linf_upper_exact': str(maximum),
            'weight_upper_exact': str(weight_max),
            'weighted_rms_upper_exact': str(weight_max * maximum / 2)}


def analyze(packet, prior, alb, run):
    if alb.get('status') != 'passed' or set(alb.get('files', {})) != EXPECTED_EOS_NAMES:
        raise ValueError('exactly the 16 registered EOS files are required')
    if len(alb.get('analysis', {}).get('states', [])) != 2:
        raise ValueError('exactly two endpoint bounds are required')
    if run['status'] != 'diagnostic_completed' or run['errors'] or run['original_pair_target'] != 'matched':
        raise ValueError('native observation is not eligible')
    if run['diagnostic'] != packet or run['steps'] != 0 or run['stage_3_completed'] is not False:
        raise ValueError('run scope or receipt mismatch')
    for key in ('base_K_hash', 'base_R_hash', 'post_K_hash', 'post_R_hash', 'S_inv_hash', 'reference_hash',
                'N', 'native_scaled_rms_before', 'native_scaled_rms_after', 'base_context_hashes'):
        if packet[key] != prior[key]:
            raise ValueError('not the prior accepted pair/context: ' + key)
    if packet['mask_scope'] != 'identity' or packet['N'] != 1080491:
        raise ValueError('different metric or mask')
    for key in ('rd', 'cv', 'cp', 'p0', 't0'):
        if packet['eos_scalars'][key] != prior['eos_scalars'][key]:
            raise ValueError('changed EOS constant')
    for name, sha in alb['files'].items():
        if run['artifacts']['eos_raw/' + name + '.npy']['sha256'] != sha:
            raise ValueError('EOS raw input/output differs from prior MPFR analysis')
    runtime = packet['w_runtime']
    if runtime['scope'] != 'actual_W_consumers_and_cloned_metric' or not runtime['fixed_coefficients_equal']:
        raise ValueError('missing runtime W provenance')
    b, a = runtime['base'], runtime['accepted']
    for key in ('rdn_values', 'gravity_f32', 'top_rdnw_f32', 'msfty_min', 'msfty_max'):
        if b[key] != a[key]:
            raise ValueError('changing W fixed operand')
    metric = runtime['metric']
    if (metric['block'], metric['start'], metric['count'], metric['dtype']) != ('rw', 432896, 215865, 7):
        raise ValueError('wrong inverse-metric block')
    if metric['full_S_inv_fnv64'] != packet['S_inv_hash'] or not 0 < exact(metric['min']) <= exact(metric['max']):
        raise ValueError('inconsistent cloned inverse metric')
    weight = exact(metric['max'])
    result = []
    for name, c, s in zip(('base', 'accepted'), (b, a), alb['analysis']['states']):
        row = s['composed_native_p_pert']
        errors, hexcodes = row['per_level_linf_upper'], row['per_level_linf_upper_binary64_hex']
        if len(errors) != 64 or len(hexcodes) != 64 or any(float.fromhex(h) != v for v,h in zip(errors,hexcodes)):
            raise ValueError('invalid prior pressure error encoding')
        r = levels(c, errors, weight)
        r['name'] = name
        result.append(r)
    return {'status': 'selected_pair_observed_operand_bound',
            'runtime_rdn_values_observed': True, 'gravity_values_observed': True,
            'top_float32_rdnw_observed': True, 'inverse_weight_max_observed': True,
            'coefficient_reference': 'captured native alpha_w and msfty operands; exact-real intermediate pressure kernel',
            'states': result, 'full_rhs_or_native_error_certified': False,
            'scope': 'same-pair EOS-pressure input sensitivity only; native ratio/division arithmetic and coefficient formation versus G* remain E_rest',
            'limits': ['source-ordered faithful consumer capture and prior theta/MPFR assumptions',
                       'Qw copy/reflection/zero topology and same packed metric',
                       'observed max is not a norm/backend error certificate',
                       'selected base/accepted calls only, not all Newton iterations or forecasts']}


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('packet', type=Path)
    p.add_argument('prior_packet', type=Path)
    p.add_argument('prior_alb', type=Path)
    p.add_argument('run_result', type=Path)
    a = p.parse_args()
    sha = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
    if sha(a.prior_packet) != '9d252ca255cef90546f34119aefeb8df03c573413115241f9b09252aa956bd56':
        raise ValueError('different historical pair')
    if sha(a.prior_alb) != '2f9f9de1110de620c1725c2bb7727ee87c3773fa69aca6cc68b5ff4d8db051a2':
        raise ValueError('not the registered composed EOS bound file')
    alb = json.loads(a.prior_alb.read_text())
    if alb['receipt_sha256'] != sha(a.prior_packet):
        raise ValueError('prior MPFR analysis does not match historical pair')
    out = analyze(json.loads(a.packet.read_text()), json.loads(a.prior_packet.read_text()), alb,
                  json.loads(a.run_result.read_text()))
    out['input_sha256'] = {key: sha(getattr(a, key)) for key in ('packet','prior_packet','prior_alb','run_result')}
    print(json.dumps(out, indent=2))
