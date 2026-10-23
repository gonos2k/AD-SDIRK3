#!/usr/bin/env python3
"""Run one frozen native idealized-twin forward or 32-step pullback prefix."""
from __future__ import annotations

import hashlib
import importlib.util
import json
import os
import re
import shutil
import signal
import subprocess
import time
from datetime import datetime
from pathlib import Path

import numpy as np

W = Path(__file__).resolve().parents[4]
DEFAULT_E = Path('/Users/yhlee/AD-SDIRK3-review-evidence/native-idealized-twin-20261022')
HELPER = W / 'external/libtorch_wrf/sdirk3/tests/native_two_time_prefix.py'
HELPER_SHA = 'b11948b406c323616c0f00179533dbb8c889ba07ba5f9ac2c4ef8f844eaaa9d8'
N, TOTAL, H, WINDOW = 24531, 256, 0.9375, 32
CAP, WALL = 18 * 1024**3, 7200
INPUT_NAMES = ('wrfinput_d01', 'namelist.input', 'diagnostics.txt')


def _load_helper():
    if not HELPER.is_file() or _sha(HELPER) != HELPER_SHA:
        raise ValueError('pinned two-time helper is missing or changed')
    spec = importlib.util.spec_from_file_location('native_two_time_prefix_b119', HELPER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as source:
        for block in iter(lambda: source.read(1 << 20), b''):
            digest.update(block)
    return digest.hexdigest()


def _need(ok, message):
    if not bool(ok):
        raise ValueError(message)


def _pin(pin, label):
    path = Path(pin.get('path', '')).resolve()
    _need(path.is_file() and (pin.get('bytes') is None or path.stat().st_size == pin.get('bytes')) and
          _sha(path) == pin.get('sha256'), f'{label} pin mismatch: {path}')
    return path


def _load_binding(evidence_root):
    e = Path(evidence_root).resolve()
    protocol_path, binding_path = e / 'protocol.json', e / 'launch_binding.json'
    _need(protocol_path.is_file() and binding_path.is_file(), 'frozen protocol/binding missing')
    protocol = json.loads(protocol_path.read_text())
    binding = json.loads(binding_path.read_text())
    protocol_sha = _sha(protocol_path)
    _need(binding.get('protocol') == {'path': str(protocol_path), 'sha256': protocol_sha},
          'binding does not pin the protocol')
    _need(Path(binding.get('source_case', '')).resolve().is_dir(), 'pinned source case is missing')
    source = Path(binding['source_case']).resolve()
    input_pins = binding.get('input_pins', {})
    _need(set(input_pins) == set(INPUT_NAMES), 'binding input pin set mismatch')
    for name in INPUT_NAMES:
        _need((source / name).is_file() and _sha(source / name) == input_pins[name],
              f'registered input mismatch: {name}')

    helper = _load_helper()
    code_files = binding.get('code_files', [])
    code_map = {str(Path(item['path']).resolve()): item['sha256'] for item in code_files}
    _need(code_map.get(str(Path(__file__).resolve())) == _sha(__file__),
          'binding does not pin this runner')
    _need(code_map.get(str(HELPER.resolve())) == HELPER_SHA,
          'binding does not pin the reused helper')
    for item in binding.get('source_files', []) + code_files:
        path = Path(item['path']).resolve()
        _need(path.is_file() and _sha(path) == item['sha256'], f'source/code pin mismatch: {path}')

    executable = _pin(binding.get('executable', {}), 'native executable')
    manifest_path = _pin(binding.get('artifact_manifest', {}), 'artifact manifest')
    manifest = json.loads(manifest_path.read_text())
    _need(manifest.get('status') == 'generated_problem_setup_no_model_run',
          'problem artifact manifest has an unexpected status')
    for key in ('truth_source', 'wrfinput_source', 'problem_npz', 'observations_json'):
        _pin(manifest.get(key, {}), f'artifact manifest {key}')
    _need(manifest.get('packed_layout', {}).get('packed_size') == N and
          manifest.get('truth_source', {}).get('shape') == [TOTAL + 1, N],
          'artifact packed layout/truth ledger shape mismatch')

    target = protocol.get('target', {})
    _need(protocol.get('schema') == 'native_idealized_twin_execution_protocol_v1' and
          protocol.get('status') == 'frozen_ready',
          'native twin protocol is not frozen for execution')
    _need(target.get('total_steps') == TOTAL and target.get('h_seconds') == H and
          target.get('window_steps') == WINDOW and target.get('packed_dimension') == N and
          target.get('ranks') == 1 and target.get('omp') == 1 and
          target.get('rss_cap_bytes') == CAP and target.get('wall_cap_seconds') == WALL,
          'native twin protocol target mismatch')
    return {'e': e, 'protocol': protocol, 'binding': binding, 'protocol_sha': protocol_sha,
            'binding_sha': _sha(binding_path), 'source': source, 'input_pins': input_pins,
            'exe': executable, 'manifest_path': manifest_path, 'manifest_sha': _sha(manifest_path),
            'helper': helper, 'helper_sha': HELPER_SHA}


def _as_seed(seed):
    value = np.asarray(seed)
    _need(value.shape == (N,) and value.dtype == np.dtype('float64'),
          'initial control seed must be a FP64 packed vector of length 24531')
    value = np.ascontiguousarray(value, dtype='<f8')
    _need(np.isfinite(value).all(), 'initial control seed contains nonfinite values')
    return value


def _file_array(value, dtype, shape, name):
    if isinstance(value, (str, Path)):
        path = Path(value).resolve()
        size = int(np.prod(shape)) * np.dtype(dtype).itemsize
        _need(path.is_file() and path.stat().st_size == size, f'{name} file has wrong size')
        array = np.fromfile(path, dtype=dtype).reshape(shape)
    else:
        array = np.asarray(value)
        _need(array.shape == shape and array.dtype == np.dtype(dtype),
              f'{name} must have shape {shape} and dtype {np.dtype(dtype)}')
        array = np.ascontiguousarray(array, dtype=dtype)
    _need(np.isfinite(array).all(), f'{name} contains nonfinite values')
    return np.ascontiguousarray(array, dtype=dtype)


def _prepare_reference(reference, seed):
    _need(isinstance(reference, (str, Path)), 'prefix reference must be a path to a full checkpoint ledger')
    path = Path(reference).resolve()
    _need(path.is_file() and path.stat().st_size == (TOTAL + 1) * N * 8,
          'reference must contain all 257 packed FP64 checkpoints')
    ledger = np.memmap(path, mode='r', dtype='<f8', shape=(TOTAL + 1, N))
    _need(np.isfinite(ledger).all(), 'reference checkpoint ledger contains nonfinite values')
    with path.open('rb') as f:
        first = f.read(N * 8)
    _need(first == seed.tobytes(), 'reference checkpoint zero differs from the requested seed')
    return path


def _validate_case(outdir, mode, horizon, seed, seedpath, reference, lambda_in,
                   return_code, stop_reason, elapsed, rss_samples, log_text):
    h = _load_helper()
    actual = {'return_code': return_code, 'runner_stop_reason': stop_reason,
              'elapsed_seconds': elapsed, 'native_rss_monitor_has_samples': bool(rss_samples),
              'peak_sampled_native_rss_bytes': max((s['rss_bytes'] for s in rss_samples), default=0),
              'success_marker': 'SUCCESS COMPLETE WRF' in log_text,
              'step_telemetry_count': log_text.count('[STEP TELEMETRY]'),
              'newton_convergence_record_count': log_text.count('[Newton] CONVERGED'),
              'stage_csv_count': len(list(outdir.glob('accepted_ark_w_step_*.csv')))}
    try:
        cp = outdir / 'terminal.checkpoints_f64.bin'
        cp_bytes = (horizon + 1) * N * 8
        _need(cp.is_file() and cp.stat().st_size == cp_bytes, 'accepted checkpoint ledger has wrong size')
        ledger = np.memmap(cp, mode='r', dtype='<f8', shape=(horizon + 1, N))
        _need(np.isfinite(ledger).all(), 'accepted checkpoint ledger contains nonfinite values')
        _need(ledger[0].tobytes() == seed.tobytes(), 'checkpoint zero differs bitwise from supplied seed')
        internal = outdir / 'terminal.x0_internal_f64.bin'
        published = outdir / 'terminal.x0_published_f32.bin'
        _need(internal.is_file() and internal.read_bytes() == seedpath.read_bytes() and
              published.is_file() and published.read_bytes() == seed.astype('<f4').tobytes(),
              'native solver-entry seed or FP32 publication differs from requested seed')
        if mode == 'prefix':
            _need(h.same_prefix(cp, reference, cp_bytes), 'prefix checkpoints differ from their trajectory reference')
        actual.update(checkpoint_bytes=cp_bytes,
                      checkpoint_zero_sha256=hashlib.sha256(ledger[0].tobytes()).hexdigest(),
                      prefix_reference_byte_equal=(mode == 'prefix'))

        times, ncfiles, finite = [], [], True
        minima = {'theta_K': float('inf'), 'column_mass_Pa': float('inf'), 'layer_dph': float('inf')}
        for nc in sorted(outdir.glob('wrfout_d01_*')):
            with h.Dataset(nc) as ds:
                stamps = h.chartostring(ds.variables['Times'][:]).tolist()
                times.extend(stamps)
                for field in ('U', 'V', 'W', 'PH', 'T', 'THM', 'MU', 'MUB', 'PHB'):
                    finite &= bool(np.isfinite(ds.variables[field][:]).all())
                minima['theta_K'] = min(minima['theta_K'],
                    float(np.min(np.asarray(ds.variables['THM'][:], np.float64) + 300.0)))
                minima['column_mass_Pa'] = min(minima['column_mass_Pa'],
                    float(np.min(np.asarray(ds.variables['MU'][:], np.float64) +
                                  np.asarray(ds.variables['MUB'][:], np.float64))))
                phi = np.asarray(ds.variables['PH'][:], np.float64) + np.asarray(ds.variables['PHB'][:], np.float64)
                minima['layer_dph'] = min(minima['layer_dph'], float(np.min(np.diff(phi, axis=1))))
            ncfiles.append({'path': nc.name, 'bytes': nc.stat().st_size, 'sha256': _sha(nc), 'times': stamps})
        actual.update(netcdf_times=times, netcdf_files=ncfiles, all_fields_finite=finite,
                      physical_minima=minima)
        result_files = {name: outdir / f'terminal.{name}.json' for name in ('prepare', 'endpoint', 'result')}
        meta = {name: json.loads(path.read_text()) for name, path in result_files.items() if path.is_file()}
        actual.update({f'terminal_{name}': value for name, value in meta.items()})
        prep, endpoint, result = meta.get('prepare', {}), meta.get('endpoint', {}), meta.get('result', {})
        _need(prep.get('mode') == mode and prep.get('expected_steps') == horizon and
              prep.get('eta') == 0 and prep.get('initial_seed_file_used') is True and
              prep.get('initial_seed_file_path') == str(seedpath),
              'prepare mode/horizon/seed metadata mismatch')
        _need(endpoint.get('mode') == mode and endpoint.get('expected_steps') == horizon and
              endpoint.get('host_steps') == horizon and endpoint.get('endpoint_count_including_seed') == horizon + 1 and
              endpoint.get('initial_seed_file_used') is True and
              endpoint.get('initial_seed_file_path') == str(seedpath) and
              endpoint.get('initial_control_seed_applied') is (horizon > WINDOW) and
              endpoint.get('control_input64_calls') == horizon and
              endpoint.get('suffix_seed_bitwise_match') is True,
              'endpoint mode/count/seed metadata mismatch')
        _need(return_code == 0 and stop_reason is None and bool(rss_samples) and
              actual['success_marker'] and actual['step_telemetry_count'] == horizon and
              actual['newton_convergence_record_count'] == 3 * horizon and
              actual['stage_csv_count'] == 0 and times == h.expected_times(horizon) and finite and
              all(x > 0 for x in minima.values()),
              'native completion, telemetry, 3N, history, or admissibility gate failed')

        if mode == 'forward':
            _need(result.get('mode') == 'forward' and result.get('forward_only') is True and
                  result.get('initial_seed_file_path') == str(seedpath) and
                  result.get('pullback_checked_by_fortran') is False and
                  endpoint.get('objective_is_registered_terminal') is False and
                  endpoint.get('reference_checkpoints_expected') == 0,
                  'forward-only run metadata indicates a pullback/objective/reference')
            gradient_path = None
        else:
            nref = result.get('reference_checkpoints_expected')
            _need(result.get('mode') == 'prefix' and result.get('chain') == 'twin' and
                  result.get('initial_seed_file_path') == str(seedpath) and
                  result.get('pullback_checked_by_fortran') is True and
                  result.get('expected_steps') == horizon and result.get('gradient_control_step') == horizon - WINDOW and
                  result.get('endpoint_reference_exact') is True and nref == horizon + 1 and
                  result.get('reference_checkpoints_matched') == horizon + 1 and
                  endpoint.get('reference_checkpoints_matched') == horizon + 1,
                  'prefix chain/reference/Fortran pullback metadata gate failed')
            lambda_path = outdir / 'terminal.lambda_in_f32.bin'
            grad_path = outdir / 'terminal.gradient_f32.bin'
            wrapper_path = outdir / 'terminal.wrapper_gradient_f64.bin'
            lambda_out = outdir / 'terminal.lambda_out_f32.bin'
            _need(lambda_in is not None and lambda_path.is_file() and lambda_path.read_bytes() == lambda_in.tobytes(),
                  'Fortran incoming cotangent differs from requested FP32 file')
            _need(grad_path.is_file() and grad_path.stat().st_size == N * 4 and
                  wrapper_path.is_file() and wrapper_path.stat().st_size == N * 8,
                  'prefix gradient outputs missing or wrong size')
            grad = np.fromfile(grad_path, dtype='<f4')
            wrapper = np.fromfile(wrapper_path, dtype='<f8')
            _need(np.isfinite(grad).all() and np.isfinite(wrapper).all() and
                  result.get('wrapper_gradient_f32_equals_cast_f64') is True and
                  grad.tobytes() == wrapper.astype('<f4').tobytes() and
                  lambda_out.is_file() and lambda_out.read_bytes() == grad.tobytes(),
                  'Fortran wrapper gradient is not the FP32 cast of captured FP64')
            gradient_path = grad_path
        actual['output_hashes'] = {p.name: {'bytes': p.stat().st_size, 'sha256': _sha(p)}
            for p in sorted(outdir.iterdir()) if p.is_file() and p.name not in ('run_receipt.json', 'rss_samples.json')}
        actual['validation_error'] = None
    except Exception as exc:
        actual['validation_error'] = str(exc)
        gradient_path = None
    actual['gradient_f32_path'] = str(gradient_path) if gradient_path else None
    return actual


class NativeRunner:
    def __init__(self, evidence_root=DEFAULT_E):
        self.evidence_root = Path(evidence_root).resolve()
        self.frozen = _load_binding(self.evidence_root)

    def run(self, seed64, label, horizon=TOTAL, reference=None, lambda_in=None):
        f = _load_binding(self.evidence_root)
        _need(f['protocol_sha'] == self.frozen['protocol_sha'] and
              f['binding_sha'] == self.frozen['binding_sha'],
              'frozen protocol/binding changed after runner construction')
        seed = _as_seed(seed64)
        _need(isinstance(label, str) and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', label) is not None,
              'case label contains unsupported characters')
        prefix = lambda_in is not None
        mode = 'prefix' if prefix else 'forward'
        _need(horizon == TOTAL if not prefix else WINDOW <= horizon <= TOTAL and horizon % WINDOW == 0,
              'forward requires 256 steps; prefix horizon must be a multiple of 32 in [32,256]')
        _need((reference is not None) == prefix, 'prefix requires a reference and forward forbids one')
        lambda_array = None if not prefix else _file_array(lambda_in, '<f4', (N,), 'incoming FP32 cotangent')
        outdir = f['e'] / 'runs' / label
        refpath = None
        if prefix:
            refpath = _prepare_reference(reference, seed)
        _need(not outdir.exists(), f'refuse to overwrite existing case: {outdir}')
        outdir.mkdir(parents=True)
        seedpath = outdir / 'initial_seed_f64.bin'
        seed.tofile(seedpath)
        lambdapath = None
        if prefix:
            lambdapath = outdir / 'incoming_lambda_f32.bin'
            lambda_array.tofile(lambdapath)
        source = f['source']
        for name in ('wrfinput_d01', 'diagnostics.txt'):
            shutil.copy2(source / name, outdir / name)
        helper = f['helper']
        nml = helper.prefix_namelist((source / 'namelist.input').read_text(), horizon)
        (outdir / 'namelist.input').write_text(nml)
        (outdir / 'namelist.diff').write_text(
            'Registered graph settings enabled; run_seconds shortened only to this horizon.\n')
        for name in ('wrfinput_d01', 'diagnostics.txt'):
            _need(_sha(outdir / name) == f['input_pins'][name], f'copied registered input changed: {name}')
        overrides = {'OMP_NUM_THREADS': '1', 'WRF_SDIRK3_RHS_COUNT': '1',
            'WRF_SDIRK3_STAGE_OPERAND_DIAG': '0',
            'WRF_SDIRK3_NATIVE_TERMINAL_MODE': mode,
            'WRF_SDIRK3_NATIVE_TERMINAL_ETA': '0',
            'WRF_SDIRK3_NATIVE_TERMINAL_STEPS': str(horizon),
            'WRF_SDIRK3_NATIVE_TERMINAL_OUT': str(outdir / 'terminal'),
            'WRF_SDIRK3_NATIVE_TWIN_SEED_F64': str(seedpath)}
        if prefix:
            overrides.update({'WRF_SDIRK3_NATIVE_TERMINAL_CHAIN': 'twin',
                'WRF_SDIRK3_NATIVE_TERMINAL_REFERENCE': str(refpath),
                'WRF_SDIRK3_NATIVE_TERMINAL_LAMBDA32_IN': str(lambdapath)})
        env = os.environ.copy()
        for key in tuple(env):
            if key.startswith(('WRF_SDIRK3_', 'WRF_PARITY_')):
                del env[key]
        env.update(overrides)
        command = ['/usr/bin/time', '-l', '-o', 'time.log', '/opt/homebrew/bin/mpirun',
                   '-np', '1', '--bind-to', 'none', str(f['exe'])]
        bound = [{'role': 'initial_control_seed_f64', 'path': str(seedpath),
                  'bytes': seedpath.stat().st_size, 'sha256': _sha(seedpath)}]
        if prefix:
            bound += [{'role': 'same_trajectory_reference_f64', 'path': str(refpath),
                       'bytes': refpath.stat().st_size, 'sha256': _sha(refpath)},
                      {'role': 'incoming_cotangent_f32', 'path': str(lambdapath),
                       'bytes': lambdapath.stat().st_size, 'sha256': _sha(lambdapath)}]
        receipt_path = outdir / 'run_receipt.json'
        receipt = {'schema': 'native_idealized_twin_run_receipt_v1', 'label': label,
            'mode': mode, 'chain': 'twin' if prefix else 'forward', 'status': 'prepared',
            'created_at': datetime.now().astimezone().isoformat(),
            'protocol_sha256': f['protocol_sha'], 'binding_sha256': f['binding_sha'],
            'artifact_manifest_sha256': f['manifest_sha'], 'executable': f['binding']['executable'],
            'source_files': f['binding']['source_files'], 'code_files': f['binding']['code_files'],
            'source_case': str(source), 'input_pins': f['input_pins'],
            'input_sha256': f['input_pins']['wrfinput_d01'],
            'namelist_sha256': _sha(outdir / 'namelist.input'),
            'initial_seed_f64_sha256': _sha(seedpath), 'initial_seed_f64_bytes': seedpath.stat().st_size,
            'incoming_cotangent_f32_sha256': _sha(lambdapath) if lambdapath else None,
            'reference_sha256': _sha(refpath) if refpath else None,
            'expected_steps': horizon, 'expected_implicit_stages': 3 * horizon,
            'h_seconds': H, 'new_model_timestep': False, 'command': command,
            'environment_overrides': overrides, 'bound_files': bound}
        receipt_path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n')
        start, samples, stop_reason = time.monotonic(), [], None
        with (outdir / 'run.log').open('wb') as log:
            process = subprocess.Popen(command, cwd=outdir, env=env, stdout=log,
                                       stderr=subprocess.STDOUT, start_new_session=True)
            receipt.update(status='running', pid=process.pid)
            receipt_path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n')
            while process.poll() is None:
                native = helper.native_rss(process.pid)
                elapsed = time.monotonic() - start
                samples.extend({'elapsed_seconds': elapsed, 'pid': pid, 'rss_bytes': rss}
                               for pid, rss, _ in native)
                if any(rss > CAP for _, rss, _ in native):
                    stop_reason = 'native RSS exceeded 18 GiB cap'
                elif elapsed > WALL:
                    stop_reason = 'two-hour per-case wall budget exceeded'
                if stop_reason:
                    for pid, _, _ in native:
                        try:
                            os.kill(pid, signal.SIGTERM)
                        except ProcessLookupError:
                            pass
                    os.killpg(process.pid, signal.SIGTERM)
                    try:
                        process.wait(timeout=10)
                    except subprocess.TimeoutExpired:
                        os.killpg(process.pid, signal.SIGKILL)
                        process.wait()
                    break
                time.sleep(.5)
            return_code = process.wait()
        (outdir / 'rss_samples.json').write_text(json.dumps(samples, indent=2) + '\n')
        run_log = (outdir / 'run.log').read_text(errors='replace') if (outdir / 'run.log').is_file() else ''
        error_log = (outdir / 'rsl.error.0000').read_text(errors='replace') if (outdir / 'rsl.error.0000').is_file() else ''
        telemetry_log = error_log or run_log
        if 'SUCCESS COMPLETE WRF' in run_log and 'SUCCESS COMPLETE WRF' not in telemetry_log:
            telemetry_log += '\nSUCCESS COMPLETE WRF\n'
        actual = _validate_case(outdir, mode, horizon, seed, seedpath, refpath, lambda_array,
            return_code, stop_reason, time.monotonic() - start, samples, telemetry_log)
        receipt.update(status='completed' if actual.get('validation_error') is None else 'failed',
                       actual=actual, ended_at=datetime.now().astimezone().isoformat())
        receipt_path.write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n')
        _need(actual.get('validation_error') is None,
              actual.get('validation_error', 'native twin case failed'))
        result = {'directory': str(outdir),
                  'checkpoints_path': str(outdir / 'terminal.checkpoints_f64.bin'),
                  'receipt_path': str(receipt_path)}
        if prefix:
            result['gradient_f32_path'] = str(outdir / 'terminal.gradient_f32.bin')
        return result
