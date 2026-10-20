#!/usr/bin/env python3
"""Independent bounded NetCDF recomputation of the three-level reference and conditional h15 screen."""
import hashlib
import json
from pathlib import Path

import numpy as np
from netCDF4 import Dataset, chartostring

E = Path('/Users/yhlee/AD-SDIRK3-review-evidence/three-level-w-reference-20261020')
PROTOCOL = E / 'root_final_protocol.json'
PIN = '7a3055189a01233837a67e8c7e7044ad34b165a42218c5f8160076657a9f83e0'
FIELDS = ('U', 'V', 'W', 'PH', 'T', 'MU')


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_case(role):
    directory = {'coarse': Path('/Users/yhlee/AD-SDIRK3-review-evidence/strict-reference-240s-20261020/green_execution/h0p46875_0_240s_newton_tol1e-9'), 'fine': E / 'green_execution/h0p234375_0_240s_newton_tol1e-9', 'candidate': Path('/Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_execution/post_cf/postfix_240_e71')}[role]
    frames, sources = {}, []
    for path in sorted(directory.glob('wrfout_d01*')):
        with Dataset(path) as ds:
            times = chartostring(ds.variables['Times'][:]).tolist()
            for index, stamp in enumerate(times):
                if stamp in frames:
                    raise ValueError(f'duplicate timestamp: {stamp}')
                values = {}
                for field in FIELDS + ('MUB',):
                    variable = ds.variables[field]
                    if np.dtype(variable.dtype) != np.dtype('float32'):
                        raise ValueError(f'{path}: {field} is not FP32')
                    data = np.ma.asarray(variable[index])
                    if np.ma.getmaskarray(data).any():
                        raise ValueError(f'{path}: masked {field}')
                    values[field] = np.asarray(data, dtype=np.float64)
                    if not np.isfinite(values[field]).all():
                        raise ValueError(f'{path}: nonfinite {field}')
                values['T'] = values['T'] + 300.0
                values['MU'] = values['MU'] + values.pop('MUB')
                values['U'] = values['U'][:, :, :-1]
                for field in ('W', 'PH'):
                    values[field] = values[field][1:, :, :]
                frames[stamp] = values
        sources.append({'path': str(path), 'sha256': sha(path)})
    expected = [f'0001-01-01_00:{s // 60:02d}:{s % 60:02d}' for s in range(0, 241, 15)]
    if role != 'candidate' and sorted(frames) != expected:
        raise ValueError(f'{role}: exact 17 output timestamps missing')
    return frames, sources


if __name__ == '__main__':
    if sha(PROTOCOL) != PIN:
        raise ValueError('frozen protocol hash differs')
    protocol = json.loads(PROTOCOL.read_text())
    receipt = json.loads((E / 'green_execution/h0p234375_0_240s_newton_tol1e-9/run_receipt.json').read_text())
    if receipt['status'] != 'completed':
        raise ValueError('Run must complete before analysis')
    coarse, coarse_sources = read_case('coarse')
    fine, fine_sources = read_case('fine')
    rows = []
    for seconds in (60, 120, 180, 240):
        stamp = f'0001-01-01_00:{seconds // 60:02d}:00'
        for field in FIELDS:
            difference = fine[stamp][field] - coarse[stamp][field]
            count = protocol['official_screen']['owner_counts'][field]
            if difference.size != count:
                raise ValueError(f'{field}: physical owner count differs')
            rms = float(np.sqrt(np.mean(difference * difference)))
            limit = protocol['official_screen']['epsilon_over_3'][field]
            rows.append({'time_seconds': seconds, 'field': field, 'count': count,
                         'rms': rms, 'max_abs': float(np.max(np.abs(difference))),
                         'limit': limit, 'ratio': rms / limit, 'pass': rms <= limit})
    result = {'script_sha256': sha(__file__), 'protocol_sha256': PIN,
              'source_netcdf_files': {'coarse': coarse_sources, 'fine': fine_sources},
              'rows': rows, 'pass_count': sum(row['pass'] for row in rows),
              'interpretation': 'Fixed named-case sensitivity screen, not a true-error bound.'}
    if result['pass_count'] == 24:
        candidate, sources = read_case('candidate')
        candidate_rows = []
        for seconds in (60,120,180,240):
            stamp = f'0001-01-01_00:{seconds//60:02d}:00'
            for field in FIELDS:
                d = candidate[stamp][field] - fine[stamp][field]
                lim = protocol['conditional_archived_candidate']['full_epsilon_limits'][field]
                rms = float(np.sqrt(np.mean(d*d)))
                candidate_rows.append({'time_seconds':seconds,'field':field,'rms':rms,'limit':lim,'ratio':rms/lim,'pass':rms<=lim})
        result['candidate_rows'] = candidate_rows
        result['candidate_pass_count'] = sum(r['pass'] for r in candidate_rows)
        result['source_netcdf_files']['candidate'] = sources
    output = E / 'root_official_screen.json'
    output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
    print(json.dumps({'output': str(output), 'pass_count': result['pass_count'], 'rows': rows}, indent=2))
