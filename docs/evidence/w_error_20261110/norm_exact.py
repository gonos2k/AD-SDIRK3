"""Exact-dyadic norm enclosure for native stored residuals; no backend gamma."""
import argparse
from fractions import Fraction as F
import hashlib
import json
import math
from pathlib import Path

import numpy as np


def exact(x):
    if isinstance(x, bool) or not isinstance(x, (int, float)) or not math.isfinite(x):
        raise ValueError('finite scalar required')
    return F.from_float(float(x))


def data_fnv(data):
    h = 14695981039346656037
    for byte in data.tobytes(order='C'):
        h = ((h ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return str(h)


def validate_residual(data, info, n, expected_hash):
    if (data.dtype != np.dtype('<f8') or data.shape != (n,) or
        info['elements'] != n or info['data_bytes'] != data.nbytes or
        data_fnv(data) != info['fnv64'] or info['fnv64'] != expected_hash):
        raise ValueError('residual bytes/inventory do not match the recorded pair')


def sum_squares(values):
    total, power = 0, 0
    for value in values:
        n, d = float(value).as_integer_ratio()
        if not n:
            continue
        p = 2 * (d.bit_length() - 1)
        if p > power:
            total <<= p - power
            power = p
        total += (n*n) << (power - p)
    return F(total, 1 << power)


def sqrt_bounds(x, precision=256):
    if x < 0:
        raise ValueError('negative squared norm')
    scaled = x.numerator << (2*precision)
    m = math.isqrt(scaled // x.denominator)
    hi = m if m*m*x.denominator == scaled else m+1
    return F(m, 1 << precision), F(hi, 1 << precision)


def enclosure(residual, layout, native_rms):
    n = len(residual)
    if n <= 0 or len(layout) != 6 or not np.isfinite(residual).all():
        raise ValueError('invalid residual/layout')
    low_sq, high_sq, next_start = F(0), F(0), 0
    for block, name in zip(layout, ('ru','rv','rw','ph','t','mu')):
        start, count = block['start'], block['size']
        lo, hi = exact(block['sinv_min']), exact(block['sinv_max'])
        if block['name'] != name or start != next_start or count <= 0 or start+count > n or not 0 < lo <= hi:
            raise ValueError('layout must cover stored metric contiguously')
        square = sum_squares(residual[start:start+count])
        low_sq += lo*lo*square
        high_sq += hi*hi*square
        next_start = start+count
    if next_start != n:
        raise ValueError('incomplete metric cover')
    lower = sqrt_bounds(low_sq/n)[0]
    upper = sqrt_bounds(high_sq/n)[1]
    reported = exact(native_rms)
    if reported < 0:
        raise ValueError('negative native RMS')
    nu = max(abs(reported-lower), abs(reported-upper))
    display = float(nu)
    if F.from_float(display) < nu:
        display = math.nextafter(display, math.inf)
    return {'mathematical_rms_lower_exact': str(lower),
            'mathematical_rms_upper_exact': str(upper),
            'reported_rms_exact': str(reported), 'nu_upper_exact': str(nu),
            'nu_upper_binary64': display, 'nu_upper_binary64_hex': display.hex()}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('packet', type=Path)
    p.add_argument('run_result', type=Path)
    p.add_argument('run_dir', type=Path)
    a = p.parse_args()
    packet = json.loads(a.packet.read_text())
    run = json.loads(a.run_result.read_text())
    if run['status'] != 'diagnostic_completed' or run['errors'] or run['diagnostic'] != packet:
        raise ValueError('ineligible run or mixed receipt')
    if packet['mask_scope'] != 'identity' or packet['N'] != 1080491 or run['original_pair_target'] != 'matched':
        raise ValueError('unsupported pair or mask')
    if packet['w_runtime']['metric']['full_S_inv_fnv64'] != packet['S_inv_hash']:
        raise ValueError('metric receipt mismatch')
    files = packet['w_reference']['files']
    names = {'B','K0','K1','R0','R1','theta_base','c1f','c2f','mass_rdnw'}
    if len(files) != 9 or {x['name'] for x in files} != names:
        raise ValueError('duplicated or incomplete reference inventory')
    records = {x['name']: x for x in files}
    states = []
    for name, rms_key, hash_key in [('R0','native_scaled_rms_before','base_R_hash'),
                                    ('R1','native_scaled_rms_after','post_R_hash')]:
        info = records[name]
        if info['path'] != 'w_reference/'+name+'.npy' or info['source_dtype'] != 7 or info['shape'] != [packet['N']]:
            raise ValueError('incorrect residual metadata')
        path = a.run_dir / info['path']
        if sha(path) != run['artifacts'][info['path']]['sha256'] or info['fnv64'] != packet[hash_key]:
            raise ValueError('residual artifact mismatch')
        data = np.load(path, allow_pickle=False)
        validate_residual(data, info, packet['N'], packet[hash_key])
        state = enclosure(data, packet['w_reference']['layout'], packet[rms_key])
        state['name'] = name
        states.append(state)
    print(json.dumps({'status':'bounded_stored_input_norm_gap','states':states,
          'input_sha256':{'packet':sha(a.packet),'run_result':sha(a.run_result)},
          'scope':'nu only for fixed stored residual/metric; not epsilon_G or convergence',
          'method':'exact binary integer sum of squares, positive block weight extrema, 256-bit dyadic isqrt enclosure'},indent=2))
