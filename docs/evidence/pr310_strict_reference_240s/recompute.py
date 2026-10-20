#!/usr/bin/env python3
"""Recompute the named-case screen from the compact physical-field arrays."""
import hashlib
import json
from pathlib import Path

import numpy as np

HERE = Path(__file__).resolve().parent
PROTOCOL_SHA = 'af8e50eeba754a9c2e95b99495b4649374cf4afa66f3d6c48506761e6549e3ad'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


if __name__ == '__main__':
    protocol_path = HERE / 'protocol_final.json'
    if digest(protocol_path) != PROTOCOL_SHA:
        raise ValueError('frozen protocol hash mismatch')
    protocol = json.loads(protocol_path.read_text())
    manifest = json.loads((HERE / 'manifest.json').read_text())
    array_path = HERE / 'published_fields_and_W.npz'
    if digest(array_path) != manifest['files'][array_path.name]['sha256']:
        raise ValueError('compact array hash mismatch')
    rows = []
    with np.load(array_path, allow_pickle=False) as arrays:
        for seconds in (60, 120, 180, 240):
            for field in protocol['official_screen']['fields']:
                # Stored arrays already use the frozen physical owners and
                # T+300 / MU+MUB transforms; do not apply those a second time.
                coarse = arrays[f'{field}_t{seconds}_coarse']
                fine = arrays[f'{field}_t{seconds}_fine']
                expected = protocol['official_screen']['owner_counts'][field]
                if (coarse.dtype != np.float64 or fine.dtype != np.float64
                        or coarse.shape != fine.shape or coarse.size != expected):
                    raise ValueError(f'{field}: physical-field shape/dtype mismatch')
                delta = fine - coarse
                if not np.isfinite(delta).all():
                    raise ValueError(f'{field}: nonfinite difference')
                rms = float(np.sqrt(np.mean(delta * delta)))
                limit = protocol['official_screen']['epsilon_over_3'][field]
                rows.append({'time_seconds': seconds, 'field': field,
                             'rms': rms, 'limit': limit, 'ratio': rms / limit,
                             'pass': rms <= limit})
        coarse = arrays['coarse_W_internal_projected_kji']
        fine = arrays['fine_W_internal_projected_kji']
        if coarse.shape != (257, 16, 16, 16) or fine.shape != (513, 16, 16, 16):
            raise ValueError('internal W endpoint shape mismatch')
        delta = fine[::2] - coarse
        if not np.isfinite(delta).all():
            raise ValueError('nonfinite internal W difference')
        trajectory_rms = np.sqrt(np.mean(delta * delta, axis=(1, 2, 3)))
        maximum_index = int(np.argmax(trajectory_rms))
    print(json.dumps({'official_pass_count': sum(r['pass'] for r in rows),
                      'official_rows': rows,
                      'internal_W_max_RMS_m_s': float(trajectory_rms[maximum_index]),
                      'internal_W_max_time_seconds': maximum_index * 15 / 16,
                      'scope': 'Named-case sensitivity screen; no true-error bound.'},
                     indent=2, allow_nan=False))
