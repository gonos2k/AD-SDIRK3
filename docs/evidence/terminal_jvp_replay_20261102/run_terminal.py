#!/usr/bin/env python3
"""One terminal diagnostic attempt. This script does not claim a forecast result."""
import hashlib
import json
import math
import os
import pathlib
import re
import shutil
import signal
import subprocess
import sys
import time

W = pathlib.Path('/private/tmp/terminal-jvp-replay-20261102')
E = pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102')
P = pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101')
OLD = P / 'runs/control'
BUILD = E / 'native_terminal/build_receipt.json'
EXE = E / 'native_terminal/wrf.exe'
FLOOR = 5 * 1024**3
CAP = 4 * 1024**3
INPUTS = {'wrfinput_d01': 'c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912',
          'namelist.input': '68c2b63dc98de0a1d313919b8c6a57fd383a5bbf8508d4076dae0db15d4589cf'}
REF = P / 'reference/wrfout_d01_0001-01-01_00:00:00'
REF_SHA = '7a9c52880ee5491126f4ea76ddc4c1c045416c8a4af562feb99f590d5b3aef50'


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


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: run_terminal.py TARGET_SHA REG_PATH REG_SHA')
    target, regname, regsha = sys.argv[1:]
    reg = pathlib.Path(regname)
    assert sha(reg) == regsha
    policy = json.loads(reg.read_text())
    assert policy['status'] == 'registered' and policy['source_commit'] == target
    assert policy['maximum_native_attempts'] == 1 and policy['retry'] is False
    assert policy['capture']['selected_ordinal'] == 5 and policy['capture']['repeat_count'] == 5
    assert policy['capture']['original_count'] == 1 and policy['capture']['pass_exit_code'] == 86
    for name, digest in policy.get('immutable_inputs', {}).items():
        assert sha(name) == digest, name
    receipt = json.loads(BUILD.read_text())
    assert receipt['source_commit'] == target and receipt['registration_sha256'] == regsha
    assert sha(EXE) == receipt['executable_sha256']
    source_check(target, receipt)
    assert sha(REF) == REF_SHA
    for name, digest in INPUTS.items():
        assert sha(OLD / name) == digest, name
    free = shutil.disk_usage(E).free
    assert free >= FLOOR
    run = E / 'run'
    assert not run.exists(), 'single attempt already staged or executed; no retry'
    run.mkdir()
    for name in INPUTS:
        shutil.copyfile(OLD / name, run / name)
        assert sha(run / name) == INPUTS[name]
    cmd = ['/opt/homebrew/bin/mpirun', '--timeout', '360', '--report-state-on-timeout',
           '-np', '1', '--bind-to', 'none', '/usr/bin/time', '-l', '-o',
           str(run / 'rank_rusage.log'), str(EXE)]
    env = {k: os.environ[k] for k in ('HOME', 'USER', 'LOGNAME', 'TMPDIR', 'LANG', 'LC_ALL') if k in os.environ}
    env['PATH'] = '/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin'
    overrides = {'OMP_NUM_THREADS': '1', 'OPENBLAS_NUM_THREADS': '1',
                 'VECLIB_MAXIMUM_THREADS': '1', 'MKL_NUM_THREADS': '1',
                 'WRF_SDIRK3_STAGE_OPERAND_DIAG': '0', 'WRF_SDIRK3_EW_ETA_INITIAL': '0.1',
                 'WRF_SDIRK3_GMRES_ESTIMATE_PROBE': '0'}
    env.update(overrides)
    attempt = {'one_attempt': True, 'retry': False, 'source_commit': target,
               'registration_sha256': regsha, 'build_receipt_sha256': sha(BUILD),
               'executable_sha256': sha(EXE), 'inputs': INPUTS, 'rk3_t0_sha256': REF_SHA,
               'disk_free_start': free, 'command': cmd, 'effective_environment': env,
               'start_epoch': time.time()}
    with (run / 'attempt.json').open('x') as f:
        json.dump(attempt, f, indent=2); f.write('\n'); f.flush(); os.fsync(f.fileno())
    result = {'status': 'inconclusive', 'model_completed': False, 'endpoint_claimed': False,
              'initial_state_comparison': 'pending separate numerical validation', 'errors': []}
    start = time.monotonic(); wall = time.time(); rc = None
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
                    os.killpg(proc.pid, signal.SIGKILL)
                    rc = proc.wait()
    except Exception as exc:
        result['errors'].append('launcher_exception: ' + repr(exc))
    result['launcher_return_code'] = rc
    result['monotonic_elapsed_seconds'] = time.monotonic() - start
    result['wall_elapsed_seconds'] = time.time() - wall
    result['clock_gap_seconds'] = result['wall_elapsed_seconds'] - result['monotonic_elapsed_seconds']
    def read(name):
        p = run / name
        return p.read_text(errors='replace') if p.exists() else ''
    text = '\n'.join(read(n) for n in ('rsl.out.0000', 'rsl.error.0000', 'launcher.stdout.log', 'launcher.stderr.log'))
    markers = [line for line in text.splitlines() if '[TERMINAL_JVP]' in line]
    result['marker_lines'] = markers
    result['steps'] = len(re.findall(r'Timing for main:', read('rsl.out.0000')))
    result['converged_stages'] = [int(n) for n in re.findall(r'\[Newton\] CONVERGED stage=(\d+)', read('rsl.out.0000'))]
    result['normal_forecast_success'] = 'SUCCESS COMPLETE WRF' in text
    rss = re.findall(r'^\s*(\d+)\s+maximum resident set size\s*$', read('rank_rusage.log'), re.M)
    result['rank_peak_rss_bytes'] = int(rss[0]) if len(rss) == 1 else None
    diagnostic_path = run / 'terminal_jvp_probe.json'
    diagnostic = None
    if diagnostic_path.is_file():
        try:
            diagnostic = json.loads(diagnostic_path.read_text())
        except Exception as exc:
            result['errors'].append('diagnostic_json_invalid: ' + repr(exc))
    result['diagnostic'] = diagnostic
    result['diagnostic_file_sha256'] = sha(diagnostic_path) if diagnostic_path.is_file() else None
    status = diagnostic.get('status') if isinstance(diagnostic, dict) else None
    if status not in ('terminal_diagnostic_passed', 'terminal_diagnostic_failed'):
        result['errors'].append('missing_or_invalid_diagnostic_status')
    expected_marker = f"[TERMINAL_JVP] status={status} exit_code={86 if status == 'terminal_diagnostic_passed' else 87} forecast_complete=false"
    if len(markers) != 1 or markers[0].strip() != expected_marker:
        result['errors'].append('terminal_marker_mismatch')
    if status == 'terminal_diagnostic_passed' and (diagnostic.get('sample_count') != 6 or len(diagnostic.get('repeats', [])) != 5):
        result['errors'].append('sample_count')
    if status == 'terminal_diagnostic_passed':
        metadata = diagnostic.get('metadata', '')
        if not all(x in metadata for x in ('stage=4', 'newton=0', 'solve_ordinal=5', 'timestep=0', 'original_plus_5', 'native_forward_ad', 'terminal_no_continuation')):
            result['errors'].append('diagnostic_metadata')
        original = diagnostic.get('original', {})
        if diagnostic.get('forecast_complete') is not False or diagnostic.get('context_scope') != 'observed_subset':
            result['errors'].append('diagnostic_scope')
        if any(not isinstance(diagnostic.get(k), str) or not diagnostic[k].isdigit() or int(diagnostic[k]) == 0 for k in ('u_hash', 'v_hash')):
            result['errors'].append('input_hashes')
        def valid_sample(sample):
            return (sample.get('valid') is True and sample.get('clock_valid') is True
                    and isinstance(sample.get('context'), str) and bool(sample['context'])
                    and all(isinstance(sample.get(k), (int, float)) and not isinstance(sample[k], bool)
                            and math.isfinite(sample[k]) and sample[k] > 0 for k in ('wall_seconds', 'cpu_seconds')))
        if not valid_sample(original):
            result['errors'].append('original_context_or_timing')
        if original.get('path_count') != 1 or original.get('fallback_count') != 0 or original.get('internal_fd_count') != 0:
            result['errors'].append('original_path')
        for i, sample in enumerate(diagnostic.get('repeats', []), 2):
            if (not valid_sample(sample) or sample.get('context') != original.get('context')
                    or sample.get('path_count') != i or sample.get('fallback_count') != 0 or sample.get('internal_fd_count') != 0
                    or not all(sample.get(k) is True for k in ('primal_exact', 'tangent_exact', 'inputs_unchanged'))):
                result['errors'].append('repeat_validation')
                break
    if rc not in (86, 87):
        result['errors'].append('unexpected_launcher_return_code')
    elif (rc, status) not in ((86, 'terminal_diagnostic_passed'), (87, 'terminal_diagnostic_failed')):
        result['errors'].append('exit_status_mismatch')
    if result['steps'] != 0 or len(result['converged_stages']) > 2 or result['normal_forecast_success']:
        result['errors'].append('unexpected_forecast_progress')
    if result['rank_peak_rss_bytes'] is None or result['rank_peak_rss_bytes'] > CAP:
        result['errors'].append('rank_rss_missing_or_exceeded')
    if abs(result['clock_gap_seconds']) >= 1:
        result['errors'].append('clock_gap')
    result['disk_free_end'] = shutil.disk_usage(E).free
    if result['disk_free_end'] < FLOOR:
        result['errors'].append('postrun_disk_floor')
    try:
        assert sha(reg) == regsha and sha(EXE) == receipt['executable_sha256']
        assert sha(BUILD) == attempt['build_receipt_sha256'] and sha(REF) == REF_SHA
        for name, digest in INPUTS.items():
            assert sha(run / name) == digest and sha(OLD / name) == digest
        for name, digest in policy.get('immutable_inputs', {}).items():
            assert sha(name) == digest, name
        source_check(target, receipt)
    except Exception as exc:
        result['errors'].append('postrun_identity_failure: ' + repr(exc))
    result['artifacts'] = {p.name: {'sha256': sha(p), 'bytes': p.stat().st_size}
                           for p in sorted(run.iterdir()) if p.is_file() and p.name != 'result.json'}
    if not result['errors']:
        result['status'] = status
    with (run / 'result.json').open('x') as f:
        json.dump(result, f, indent=2, allow_nan=False); f.write('\n')
    print(json.dumps({'status': result['status'], 'errors': result['errors'], 'launcher_return_code': rc}))
    return 0 if result['status'] == 'terminal_diagnostic_passed' else 1


if __name__ == '__main__':
    sys.exit(main())
