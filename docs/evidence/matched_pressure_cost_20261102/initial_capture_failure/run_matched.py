#!/usr/bin/env python3
"""One registered terminal diagnostic attempt; no forecast claim or retry."""
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import struct
import sys
import time

W = Path('/private/tmp/matched-pressure-cost-20261102')
E = Path('/Users/yhlee/AD-SDIRK3-review-evidence/matched-pressure-cost-20261102')
OLD = Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102/run')
sys.dont_write_bytecode = True
sys.path.insert(0, str(W / 'tools'))
from sdirk3_run_measurement import clock_endpoint, clock_measurement, collect_stages, STREAMS

BUILD, EXE = E / 'native_matched/build_receipt.json', E / 'native_matched/wrf.exe'
INPUT_SHA = 'c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912'
NML_SHA = 'a43ddbc716a08430e0302be8b30845c568af241a80db24e596bc9cddff81dda6'
OLD_NML_SHA = '68c2b63dc98de0a1d313919b8c6a57fd383a5bbf8508d4076dae0db15d4589cf'
FLOOR, CAP = 5 * 1024**3, 4 * 1024**3


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def output(args, **kw):
    return subprocess.check_output(list(map(str, args)), text=True, **kw).strip()


def source_check(target, receipt):
    assert output(['git', 'rev-parse', 'HEAD'], cwd=W) == target
    assert not output(['git', 'status', '--porcelain', '--untracked-files=all'], cwd=W)
    for name, digest in receipt['source_hashes'].items():
        assert sha(W / name) == digest, name


def number(x, minimum=0):
    return isinstance(x, (int, float)) and not isinstance(x, bool) and math.isfinite(x) and x >= minimum


def valid_timing(t):
    return (isinstance(t, dict) and t.get('clock_valid') is True and
            all(number(t.get(k)) for k in ('wall', 'cpu')))


def validate_diagnostic(d):
    """Validate the emitted common-prepared-operator receipt independently."""
    if not isinstance(d, dict):
        return ['missing_diagnostic']
    errors = []
    def require(condition, name):
        if not condition:
            errors.append(name)
    require(d.get('status') == 'matched_prepared_diagnostic_passed' and d.get('phase') == 'complete', 'diagnostic_status')
    require(d.get('forecast_complete') is False and d.get('stage') == 2 and d.get('newton_iteration') == 0 and d.get('timestep') == 0 and d.get('dt') == 600, 'capture_scope')
    require(d.get('order') == ['P0', 'P1', 'P1', 'P0'] and d.get('single_tape') is True and d.get('no_rhs_interleaving_required') is True and d.get('rhs_interleaved') is False, 'operator_context')
    require(d.get('scope') == 'common_prepared_A_plus_M_not_native_FWAD_cost' and d.get('cost_screen_only') is True and isinstance(d.get('cost_screen'), bool), 'cost_scope')
    require(d.get('tape_constructor_rhs_calls') == 1 and d.get('tape_validation_errors') == 0 and d.get('tape_validation_calls') == 27 and d.get('native_calls') == 15, 'tape_and_native_calls')
    require(d.get('N') == 1080491, 'state_size')
    hashes = d.get('frozen_hashes')
    require(isinstance(hashes, list) and len(hashes) == 6 and all(isinstance(x, int) and not isinstance(x, bool) and x > 0 for x in hashes), 'frozen_hashes')
    require(all(valid_timing(d.get(k)) for k in ('common', 'tape_preparation', 'oracle_verification', 'boundary_guards', 'tape_validation_time')), 'common_timing')
    tol = d.get('tolerance')
    expected_tol = struct.unpack('<f', struct.pack('<f', 1e-7))[0]
    require(number(tol) and tol == expected_tol, 'tolerance')
    arms = d.get('arms')
    if not isinstance(arms, list) or len(arms) != 4 or any(not isinstance(a, dict) for a in arms):
        return errors + ['arm_count']
    require([a.get('kind') for a in arms] == ['P0', 'P1', 'P1', 'P0'], 'arm_order')
    for i, arm in enumerate(arms):
        require(all(arm.get(k) is True for k in ('solved', 'native_checked', 'success')), f'arm_{i}_solved')
        scaled = arm.get('native_scaled_rel')
        require(number(scaled) and number(tol) and tol > 0 and scaled < tol and number(arm.get('native_physical_rel')), f'arm_{i}_native_residual')
        blocks = arm.get('block_scaled_residual_norms')
        require(isinstance(blocks, list) and len(blocks) == 6 and all(number(x) for x in blocks), f'arm_{i}_blocks')
        require(arm.get('guard_events') == 0 and type(arm.get('arnoldi_spent')) is int and arm['arnoldi_spent'] > 1 and arm.get('sketch_count') == arm['arnoldi_spent'] and arm.get('stopping_metric') == 1 and arm.get('tolerance') == expected_tol, f'arm_{i}_contract')
        require(all(valid_timing(arm.get(k)) for k in ('setup', 'solve', 'native_residual', 'A_time', 'M_time', 'sketch_overhead', 'guard_overhead', 'charged_total', 'accounted_total')), f'arm_{i}_timing')
    ratios = cost_ratios(d)
    require(ratios is not None and d.get('cost_screen') == all(v is not None and v >= 2 for v in ratios.values()), 'cost_screen_consistency')
    return errors


def cost_ratios(d):
    try:
        arms = d['arms']
        ratios = {}
        for total in ('charged_total', 'accounted_total'):
            for clock in ('wall', 'cpu'):
                for p0, p1 in ((0, 1), (3, 2)):
                    x, y = arms[p0][total][clock], arms[p1][total][clock]
                    if not number(x) or not number(y):
                        return None
                    ratio = x / y if y > 0 else None
                    ratios[f'{total}_{clock}_{p0}_over_{p1}'] = ratio if ratio is not None and math.isfinite(ratio) else None
        return ratios
    except (KeyError, IndexError, TypeError, ZeroDivisionError):
        return None


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: run_matched.py TARGET_SHA REG_PATH REG_SHA')
    target, regname, regsha = sys.argv[1:]
    reg = Path(regname)
    assert sha(reg) == regsha
    policy = json.loads(reg.read_text())
    assert policy['status'] == 'registered' and policy['source_commit'] == target
    assert policy['maximum_native_attempts'] == 1 and policy['retry'] is False
    for path, digest in policy['immutable_inputs'].items():
        assert sha(path) == digest, path
    assert policy['immutable_inputs'][str(E / 'inputs/namelist.input')] == NML_SHA
    receipt = json.loads(BUILD.read_text())
    assert receipt['source_commit'] == target and receipt['registration_sha256'] == regsha
    assert sha(EXE) == receipt['executable_sha256']
    source_check(target, receipt)
    assert sha(OLD / 'wrfinput_d01') == INPUT_SHA and sha(OLD / 'namelist.input') == OLD_NML_SHA
    assert sha(E / 'inputs/namelist.input') == NML_SHA
    old_nml = (OLD / 'namelist.input').read_bytes()
    assert old_nml.count(b'sdirk3_reuse_linearization = .false.') == 1
    assert (E / 'inputs/namelist.input').read_bytes() == old_nml.replace(b'sdirk3_reuse_linearization = .false.', b'sdirk3_reuse_linearization = .true.')
    free = shutil.disk_usage(E).free
    assert free >= FLOOR
    run = E / 'run'
    run.mkdir()  # Fails if any attempt was previously staged; no retry.
    shutil.copyfile(OLD / 'wrfinput_d01', run / 'wrfinput_d01')
    shutil.copyfile(E / 'inputs/namelist.input', run / 'namelist.input')
    assert sha(run / 'wrfinput_d01') == INPUT_SHA and sha(run / 'namelist.input') == NML_SHA
    cmd = ['/opt/homebrew/bin/mpirun', '--timeout', '360', '--report-state-on-timeout', '-np', '1', '--bind-to', 'none', '/usr/bin/time', '-l', '-o', str(run / 'rank_rusage.log'), str(EXE)]
    env = {k: os.environ[k] for k in ('HOME', 'USER', 'LOGNAME', 'TMPDIR', 'LANG', 'LC_ALL') if k in os.environ}
    env['PATH'] = '/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin'
    env.update({'OMP_NUM_THREADS': '1', 'OPENBLAS_NUM_THREADS': '1', 'VECLIB_MAXIMUM_THREADS': '1', 'MKL_NUM_THREADS': '1', 'WRF_SDIRK3_STAGE_OPERAND_DIAG': '0', 'WRF_SDIRK3_EW_ETA_INITIAL': '0.1', 'WRF_SDIRK3_GMRES_ESTIMATE_PROBE': '0', 'WRF_SDIRK3_REUSE_LINEARIZATION': '1'})
    clock_info = {name: vars(time.get_clock_info(name)) for name in ('monotonic', 'time')}
    attempt = {'one_attempt': True, 'retry': False, 'source_commit': target, 'registration_sha256': regsha, 'build_receipt_sha256': sha(BUILD), 'executable_sha256': sha(EXE), 'input_sha256': INPUT_SHA, 'namelist_sha256': NML_SHA, 'disk_free_start': free, 'command': cmd, 'effective_environment': env, 'clock_info': clock_info, 'max_endpoint_span_ns': 10_000_000, 'gap_limit_ns': 1_000_000_000}
    with (run / 'attempt.json').open('x') as f:
        json.dump(attempt, f, indent=2); f.write('\n'); f.flush(); os.fsync(f.fileno())
    result = {'status': 'inconclusive', 'cost_decision': 'INCONCLUSIVE', 'cost_eligible_scope': 'local modeled prepared-operator first point; no forecast or native kernel performance claim', 'model_completed': False, 'endpoint_claimed': False, 'initial_state_comparison': 'pending separate numerical validation', 'errors': []}
    start = clock_endpoint(); rc = None
    try:
        with (run / 'launcher.stdout.log').open('xb') as stdout, (run / 'launcher.stderr.log').open('xb') as stderr:
            proc = subprocess.Popen(cmd, cwd=run, env=env, stdout=stdout, stderr=stderr, start_new_session=True)
            try:
                rc = proc.wait(timeout=390)
            except subprocess.TimeoutExpired:
                result['errors'].append('launcher_wait_timeout')
                os.killpg(proc.pid, signal.SIGTERM)
                try:
                    rc = proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    os.killpg(proc.pid, signal.SIGKILL); rc = proc.wait()
    except Exception as exc:
        result['errors'].append('launcher_exception: ' + repr(exc))
    end = clock_endpoint()
    try:
        result['clock'] = clock_measurement(start, end)
        if result['clock']['classification'] != 'consistent':
            result['errors'].append('clock_ineligible')
    except Exception as exc:
        result['clock'] = {'start': start, 'end': end, 'classification': 'invalid', 'error': repr(exc)}
        result['errors'].append('clock_invalid')
    result['launcher_return_code'] = rc
    def read(name):
        path = run / name
        return path.read_bytes() if path.exists() else b''
    streams = {name: read(name) for name in STREAMS}
    prefix = b'[MATCHED_PRESSURE] '
    markers = [(name, line[len(prefix):] + b'\n') for name, data in streams.items() for line in data.splitlines() if line.startswith(prefix)]
    result['marker_streams'] = [name for name, _ in markers]
    path = run / 'matched_pressure_probe.json'
    diagnostic = None
    try:
        raw = path.read_bytes()
        if len(markers) != 1 or markers[0][1] != raw:
            result['errors'].append('diagnostic_marker_bytes_mismatch')
        diagnostic = json.loads(raw, parse_constant=lambda s: (_ for _ in ()).throw(ValueError(s)))
    except Exception as exc:
        result['errors'].append('diagnostic_read: ' + repr(exc))
    result['diagnostic'] = diagnostic
    result['computed_cost_ratios'] = cost_ratios(diagnostic) if isinstance(diagnostic, dict) else None
    result['errors'].extend(validate_diagnostic(diagnostic))
    if rc != 88:
        result['errors'].append('launcher_exit_not_pass_88')
    result['stages'] = collect_stages(run)
    text = b'\n'.join(streams.values())
    result['steps'] = len(re.findall(rb'Timing for main:', streams['rsl.out.0000']))
    result['normal_forecast_success'] = b'SUCCESS COMPLETE WRF' in text
    if result['stages']['stage_count'] != 0 or result['steps'] != 0 or result['normal_forecast_success']:
        result['errors'].append('unexpected_forecast_progress')
    rss = re.findall(rb'^\s*(\d+)\s+maximum resident set size\s*$', read('rank_rusage.log'), re.M)
    result['rank_peak_rss_bytes'] = int(rss[0]) if len(rss) == 1 else None
    if result['rank_peak_rss_bytes'] is None or result['rank_peak_rss_bytes'] > CAP:
        result['errors'].append('rank_rss_missing_or_exceeded')
    result['disk_free_end'] = shutil.disk_usage(E).free
    if result['disk_free_end'] < FLOOR:
        result['errors'].append('postrun_disk_floor')
    try:
        assert sha(reg) == regsha and sha(EXE) == receipt['executable_sha256'] and sha(BUILD) == attempt['build_receipt_sha256']
        assert sha(OLD / 'wrfinput_d01') == sha(run / 'wrfinput_d01') == INPUT_SHA
        assert sha(OLD / 'namelist.input') == OLD_NML_SHA
        assert sha(E / 'inputs/namelist.input') == sha(run / 'namelist.input') == NML_SHA
        for name, digest in policy['immutable_inputs'].items():
            assert sha(name) == digest, name
        source_check(target, receipt)
    except Exception as exc:
        result['errors'].append('postrun_identity_failure: ' + repr(exc))
    result['artifacts'] = {p.name: {'sha256': sha(p), 'bytes': p.stat().st_size} for p in sorted(run.iterdir()) if p.is_file() and p.name != 'result.json'}
    if not result['errors']:
        result['status'] = 'diagnostic_completed'
        result['cost_decision'] = 'COST_LOCAL_GO' if diagnostic['cost_screen'] else 'NO_GO'
    with (run / 'result.json').open('x') as f:
        json.dump(result, f, indent=2, allow_nan=False); f.write('\n')
    print(json.dumps({'status': result['status'], 'cost_decision': result['cost_decision'], 'errors': result['errors'], 'launcher_return_code': rc}))
    return 0 if result['status'] == 'diagnostic_completed' else 1


if __name__ == '__main__':
    sys.exit(main())
