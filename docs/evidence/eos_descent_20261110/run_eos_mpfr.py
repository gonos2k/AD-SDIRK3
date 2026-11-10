#!/usr/bin/env python3
"""Verify a native EOS receipt and save all-cell MPFR error bounds."""
import argparse
import hashlib
import json
import math
import pathlib
import subprocess
import numpy as np

E = pathlib.Path(__file__).resolve().parent
NAMES = ['base_ph', 'base_t', 'base_mu', 'base_alt', 'base_p_pert',
         'accepted_ph', 'accepted_t', 'accepted_mu', 'accepted_alt', 'accepted_p_pert',
         'mu_base', 'alb', 'p_base', 'c1h', 'c2h', 'rdnw']


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def fnv(data):
    result = 14695981039346656037
    for byte in data:
        result = ((result ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return str(result)


def add_binary64_hex(bounds):
    for key in ('linf_upper', 'l2_upper'):
        value = bounds[key]
        assert isinstance(value, (int, float)) and math.isfinite(value) and value >= 0
        bounds[key + '_binary64_hex'] = float(value).hex()
        assert float.fromhex(bounds[key + '_binary64_hex']) == value
    values = bounds['per_level_linf_upper']
    assert all(math.isfinite(v) and v >= 0 for v in values)
    bounds['per_level_linf_upper_binary64_hex'] = [float(v).hex() for v in values]
    assert all(float.fromhex(h) == v for h, v in zip(bounds['per_level_linf_upper_binary64_hex'], values))


parser = argparse.ArgumentParser()
parser.add_argument('receipt', type=pathlib.Path)
parser.add_argument('output', type=pathlib.Path)
args = parser.parse_args()
receipt = json.loads(args.receipt.read_text())
raw = receipt['raw_eos']
scalars = receipt['eos_scalars']
assert raw['scope'] == 'rounded_EOS_subroutine_inputs_and_outputs_only'
items = {item['name']: item for item in raw['files']}
assert set(items) == set(NAMES) and len(raw['files']) == len(NAMES)
shape = items['base_t']['shape']
assert len(shape) == 3
ny, nz, nx = shape

for name in NAMES:
    item = items[name]
    expected_shape = ([ny, nz + 1, nx] if name.endswith('_ph') else
                      [ny, nx] if name.endswith('_mu') or name == 'mu_base' else
                      [nz] if name in ('c1h', 'c2h', 'rdnw') else [ny, nz, nx])
    assert item['shape'] == expected_shape, (name, item['shape'], expected_shape)
    assert item['source_dtype'] in (6, 7), (name, 'unsupported source dtype')
    if name.startswith(('base_', 'accepted_')):
        assert item['source_dtype'] == 7, (name, 'state/output must be FP64')
    assert item['path'] == f'eos_raw/{name}.npy'
    path = args.receipt.parent / item['path']
    data = np.load(path, allow_pickle=False)
    assert data.dtype == np.dtype('<f8') and data.ndim == 1 and data.flags.c_contiguous
    assert data.size == math.prod(expected_shape) == item['elements']
    assert data.nbytes == item['data_bytes'] and np.isfinite(data).all()
    if item['source_dtype'] == 6:
        with np.errstate(over='ignore', invalid='ignore'):
            roundtrip = data.astype(np.float32).astype(np.float64)
        assert np.isfinite(roundtrip).all()
        assert np.array_equal(data.view(np.uint64), roundtrip.view(np.uint64)), (name, 'not exact FP32 promotion')
    assert fnv(data.tobytes()) == item['fnv64'], (name, 'data hash mismatch')
    item['verified_sha256'] = sha(path)

constants = [scalars[key] for key in ('rd', 'cv', 'cp', 'p0', 't0')]
assert all(isinstance(value, (float, int)) and math.isfinite(value) for value in constants)
cmd = [str(E / 'eos_mpfr'), str(ny), str(nz), str(nx),
       *[repr(value) for value in constants],
       *[str(args.receipt.parent / items[name]['path']) for name in NAMES]]
run = subprocess.run(cmd, text=True, capture_output=True)
assert run.returncode == 0, run.stderr
analysis = json.loads(run.stdout)
assert analysis['cells_per_state'] == ny * nz * nx and len(analysis['states']) == 2
for state in analysis['states']:
    for field in ('alt', 'p_pert'):
        assert len(state[field]['per_level_linf_upper']) == nz
        add_binary64_hex(state[field])

out = {
    'status': 'passed',
    'scope': 'MPFR intervals for calc_p_rho_wrf at captured rounded EOS inputs, not full RHS/state formation/forcing',
    'receipt_sha256': sha(args.receipt),
    'checker_source_sha256': sha(E / 'eos_mpfr.cpp'),
    'checker_binary_sha256': sha(E / 'eos_mpfr'),
    'files': {name: items[name]['verified_sha256'] for name in NAMES},
    'constants': dict(zip(('rd', 'cv', 'cp', 'p0', 't0'), constants)),
    'shape': shape,
    'bound_encoding': 'MPFR upper bounds converted to binary64 with RNDU; decimal JSON numbers require binary64 parsing and must match accompanying exact binary64 hexadecimal strings',
    'assumptions': ['trusted correctly rounded MPFR at 256 bits',
                    'input files faithfully encode captured native arguments and outputs',
                    'positive EOS power base, hybrid mass strictly positive, rdnw strictly positive',
                    'near-zero hybrid mass <=1e-100 rejected'],
    'analysis': analysis,
}
with args.output.open('x') as output:
    json.dump(out, output, indent=2, allow_nan=False)
    output.write('\n')
