#!/usr/bin/env python3
"""Describe successive dry em_b_wave RK3 reference differences without an order claim."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path

import numpy as np

import benchmark_wrf_accuracy_cost as benchmark

TIMES = [f'0001-01-01_00:{m//60:02d}:{m%60:02d}' for m in (0, 60, 120, 180, 240)]
FIELDS = ('U', 'V', 'W', 'PH', 'T', 'MU')
RAW_FIELDS = ('U', 'V', 'W', 'PH', 'PHB', 'T', 'MU', 'MUB')
REQUIRED_LABELS = ('coarse_h3p75', 'middle_h1p875', 'fine_h0p9375')
EXPECTED_H_SECONDS = {'coarse_h3p75': 3.75, 'middle_h1p875': 1.875, 'fine_h0p9375': 0.9375}


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def raw_initial(run_dir: Path) -> dict[str, np.ndarray]:
    """Read raw t=0 arrays using the authoritative loader's NetCDF helpers."""
    files = sorted(run_dir.glob('wrfout_d01*'))
    if not files:
        raise ValueError(f'no WRF output files in {run_dir}')
    with benchmark.Dataset(files[0], 'r') as dataset:
        timestamps = [benchmark._time_text(row) for row in dataset.variables['Times'][:]]
        if TIMES[0] not in timestamps:
            raise ValueError(f't=0 absent from first WRF output in {run_dir}')
        index = timestamps.index(TIMES[0])
        return {name: benchmark._float_array(dataset.variables[name], index) for name in RAW_FIELDS}


def stats(delta: np.ndarray) -> dict:
    a = np.asarray(delta, dtype=np.float64)
    flat = a.reshape(-1)
    rms = float(np.sqrt(np.mean(flat * flat)))
    shell = np.zeros(a.shape, dtype=bool)
    for axis, length in enumerate(a.shape):
        if length:
            idx0 = [slice(None)] * a.ndim
            idx1 = [slice(None)] * a.ndim
            idx0[axis] = 0
            idx1[axis] = -1
            shell[tuple(idx0)] = True
            shell[tuple(idx1)] = True
    interior = a[~shell]
    energy = float(np.dot(flat, flat))
    shell_energy = float(np.dot(a[shell].reshape(-1), a[shell].reshape(-1)))
    return {
        'rms': rms,
        'max_abs': float(np.max(np.abs(flat))),
        'mean_change': float(np.mean(flat)),
        'l2_norm': float(np.sqrt(energy)),
        'owned_outer_shell': {
            'definition': 'Union of first/last indices along each dimension after the authoritative V3 ownership crop; descriptive concentration only.',
            'point_count': int(shell.sum()),
            'energy_fraction': shell_energy / energy if energy > 0 else None,
            'rms': float(np.sqrt(np.mean(a[shell] ** 2))) if shell.any() else None,
            'interior_point_count': int(interior.size),
            'interior_rms': float(np.sqrt(np.mean(interior ** 2))) if interior.size else None,
        },
        'vertical_rms_by_owned_level': [float(np.sqrt(np.mean(a[k] ** 2))) for k in range(a.shape[0])] if a.ndim == 3 else None,
        'vertical_coordinate_note': 'Owned vertical index 0 is the first retained level; for W/PH the fixed lower face was already excluded by the V3 crop.' if a.ndim == 3 else 'Not applicable: MU is a two-dimensional column field.',
    }


def comparison(d1: np.ndarray, d2: np.ndarray) -> dict:
    x = np.asarray(d1, dtype=np.float64).reshape(-1)
    y = np.asarray(d2, dtype=np.float64).reshape(-1)
    xx = float(np.dot(x, x))
    yy = float(np.dot(y, y))
    xy = float(np.dot(x, y))
    if xx > 0.0 and yy > 0.0:
        alpha = xy / xx
        residual = y - alpha * x
        return {
            'd2_to_d1_l2_norm_ratio': float(np.sqrt(yy / xx)),
            'cosine_d1_d2': float(xy / np.sqrt(xx * yy)),
            'projection_scale_alpha_for_d2_onto_d1': float(alpha),
            'best_scale_residual_rms': float(np.sqrt(np.mean(residual * residual))),
            'best_scale_residual_norm_fraction_of_d2': float(np.sqrt(np.dot(residual, residual) / yy)),
            'status': 'defined',
        }
    return {
        'd2_to_d1_l2_norm_ratio': None,
        'cosine_d1_d2': None,
        'projection_scale_alpha_for_d2_onto_d1': None,
        'best_scale_residual_rms': None,
        'best_scale_residual_norm_fraction_of_d2': None,
        'status': 'undefined_zero_difference_norm',
    }


def analyze(plan_path: Path, contract_path: Path, out_path: Path) -> dict:
    plan = json.loads(plan_path.read_text())
    contract = json.loads(contract_path.read_text())
    rows = plan.get('runs')
    if not isinstance(rows, list) or len(rows) != len(REQUIRED_LABELS):
        raise ValueError(f'plan must contain exactly three runs: {REQUIRED_LABELS}')
    by_label = {row.get('label'): row for row in rows}
    if set(by_label) != set(REQUIRED_LABELS):
        raise ValueError(f'plan labels must be exactly {REQUIRED_LABELS}')
    contract_input = contract['case']['input_sha256']
    if not contract['contract_id'].startswith('dry-em-b-wave-16cubed-240s-'):
        raise ValueError('unexpected frozen contract/case')
    loaded = {label: benchmark._load_run(by_label[label], plan_path.parent) for label in REQUIRED_LABELS}
    for label, expected_h in EXPECTED_H_SECONDS.items():
        run = loaded[label]
        if run['h_seconds'] != expected_h:
            raise ValueError(f"{label}: expected h_seconds={expected_h}, got {run['h_seconds']!r}")
        if run['method'] != 'RK3':
            raise ValueError(f"{label}: expected method='RK3', got {run['method']!r}")
    expected_times = set(TIMES)
    for label, run in loaded.items():
        if set(run['timestamps']) != expected_times:
            raise ValueError(f'{label}: exact expected timestamps not found: {run["timestamps"]}')
        if run['input_sha256']['sha256'] != contract_input:
            raise ValueError(f'{label}: input SHA differs from frozen case')
    if not (loaded['coarse_h3p75']['grid'] == loaded['middle_h1p875']['grid'] == loaded['fine_h0p9375']['grid']):
        raise ValueError('grid metadata differs between the three resolutions')

    raw0 = {label: raw_initial(Path(by_label[label]['path']) if Path(by_label[label]['path']).is_absolute() else (plan_path.parent / by_label[label]['path']).resolve()) for label in REQUIRED_LABELS}
    initial_checks = []
    first = raw0['coarse_h3p75']
    for label in ('middle_h1p875', 'fine_h0p9375'):
        per_field = {k: bool(np.array_equal(first[k], raw0[label][k])) for k in RAW_FIELDS}
        initial_checks.append({'pair': f'coarse_h3p75_vs_{label}', 'exact_raw_8_field_t0': all(per_field.values()), 'per_field': per_field})
    if not all(row['exact_raw_8_field_t0'] for row in initial_checks):
        raise ValueError('raw initial eight-field state is not bitwise identical across resolutions')

    arrays = {}
    metrics = []
    times_out = TIMES[1:]
    for timestamp in times_out:
        coarse = loaded['coarse_h3p75']['frames'][timestamp]
        middle = loaded['middle_h1p875']['frames'][timestamp]
        fine = loaded['fine_h0p9375']['frames'][timestamp]
        for field in FIELDS:
            d1 = np.asarray(middle[field] - coarse[field], dtype=np.float64)
            d2 = np.asarray(fine[field] - middle[field], dtype=np.float64)
            arrays[f'd1_{timestamp[-8:].replace(":", "")}_{field}'] = d1
            arrays[f'd2_{timestamp[-8:].replace(":", "")}_{field}'] = d2
            metrics.append({
                'time': timestamp, 'field': field, 'units': benchmark.VARIABLE_UNITS[field],
                'owned_point_count': int(d1.size), 'owned_shape_zyx': list(d1.shape),
                'd1_middle_minus_coarse': stats(d1),
                'd2_fine_minus_middle': stats(d2),
                'successive_difference_shape': comparison(d1, d2),
            })
    out_path.parent.mkdir(parents=True, exist_ok=True)
    npz_path = out_path.with_name(out_path.stem + '_deltas.npz')
    np.savez_compressed(npz_path, **arrays)
    result = {
        'schema': 'rk3-reference-shape-analysis-v1',
        'scope': 'Archived WRF outputs only; no model run, build, or forecast. Descriptive successive-difference geometry, not a claimed convergence order or truth bound.',
        'base_revision': plan.get('base_revision'),
        'source': {'analysis_script': str(Path(__file__).resolve()), 'analysis_script_sha256': sha(Path(__file__).resolve()), 'authoritative_loader': str(Path(benchmark.__file__).resolve()), 'authoritative_loader_sha256': sha(Path(benchmark.__file__).resolve())},
        'frozen_case_contract': {'path': str(contract_path.resolve()), 'sha256': sha(contract_path), 'contract_id': contract['contract_id'], 'input_sha256': contract_input},
        'input_plan': {'path': str(plan_path.resolve()), 'sha256': sha(plan_path)},
        'source_receipts': plan.get('source_receipts', {}),
        'producer_executable_hash_policy': 'Producer executable SHA-256 values are carried as historical metadata from the input plan; this portable analyzer does not rehash executables. Input, namelist, and output file hashes are independently checked by the shared loader.',
        'resolution_chain': [
            {'label': label, 'h_seconds': loaded[label]['h_seconds'], 'method': loaded[label]['method'], 'run_path': loaded[label]['path'], 'input_sha256': loaded[label]['input_sha256']['sha256'], 'namelist_sha256': loaded[label]['namelist_sha256']['sha256'], 'producer_executable_sha256': by_label[label].get('producer_executable_sha256'), 'wrfout_files': loaded[label]['wrfout_files']}
            for label in REQUIRED_LABELS
        ],
        'exact_times': {'required': TIMES, 'all_three_match_exactly': True, 'analysis_checkpoints': times_out},
        'exact_raw_initial_state': {'fields': list(RAW_FIELDS), 'pairs': initial_checks, 'all_exact': True},
        'ownership': {'source': 'benchmark_wrf_accuracy_cost.py::_owned, called via _load_run', 'description': {'U': 'periodic duplicate east face excluded', 'V': 'both physical y wall faces included', 'W': 'fixed lower face excluded, top retained', 'PH': 'fixed lower face excluded, top retained', 'T': 'all mass points', 'MU': 'all mass points'}, 'outer_shell_statistic': 'union of first/last indices along each dimension after the ownership crop; descriptive energy concentration only'},
        'difference_definitions': {'d1': 'middle h=1.875 minus coarse h=3.75', 'd2': 'fine h=0.9375 minus middle h=1.875', 'norm_ratio': 'L2(d2)/L2(d1) for equal-size arrays; equal to RMS(d2)/RMS(d1)', 'projection': 'alpha=dot(d1,d2)/dot(d1,d1), least-squares projection of d2 onto d1', 'residual': 'RMS(d2-alpha*d1), plus residual L2 fraction of L2(d2)', 'mean_change': 'spatial mean of signed difference'},
        'metrics': metrics,
        'raw_delta_npz': {'path': str(npz_path.resolve()), 'sha256': sha(npz_path), 'arrays': len(arrays), 'dtype': 'float64'},
        'interpretation_limit': 'The ratios, directions, cosine, projection, and residual describe whether successive difference fields align and shrink over these archived resolutions/times. They do not establish an observed order, asymptotic regime, or distance to the exact solution.'
    }
    out_path.write_text(json.dumps(result, indent=2) + '\n')
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan', required=True, type=Path, help='JSON with exactly the coarse_h3p75, middle_h1p875, fine_h0p9375 run rows')
    parser.add_argument('--contract', required=True, type=Path, help='frozen V3 case contract JSON')
    parser.add_argument('--out', required=True, type=Path, help='analysis JSON output; compressed deltas NPZ is written adjacent')
    args = parser.parse_args()
    result = analyze(args.plan.resolve(), args.contract.resolve(), args.out.resolve())
    print(json.dumps({'status': 'written', 'out': str(args.out.resolve()), 'npz': result['raw_delta_npz']['path'], 'metrics': len(result['metrics']), 'all_initial_8_fields_exact': result['exact_raw_initial_state']['all_exact'], 'times_exact': result['exact_times']['all_three_match_exactly']}, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
