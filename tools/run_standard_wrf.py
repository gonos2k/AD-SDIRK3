#!/usr/bin/env python3
"""One accepted quiet five-day ARK600 forecast; requires explicit resource authorization; no retry/restart."""
import argparse
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

HELPER = Path(__file__).resolve().with_name('benchmark_wrf_accuracy_cost.py')
FALLBACK_HELPER_SHA = '823ed95d2b312eb7e9ec3719dd13cc6d84f84ab0c87439ffd0895d21f9dd15c3'
GRID = {'nx':40, 'ny':80, 'nz':64}
POLL_SECONDS = 1.0
MONITOR_TIMEOUT_LIMIT = 3
ROOT = None


def sha(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def processes():
    result = subprocess.run(['/bin/ps', '-axo', 'pid=,ppid=,rss=,stat=,lstart='],
                            capture_output=True, text=True, check=True, timeout=3)
    rows = {}
    for line in result.stdout.splitlines():
        if not line.strip():
            continue
        fields = line.split(None, 4)
        if len(fields) != 5:
            raise RuntimeError(f'malformed process-table row: {line!r}')
        rows[int(fields[0])] = {'ppid':int(fields[1]), 'rss':int(fields[2])*1024,
                                'state':fields[3], 'birth':fields[4]}
    if not rows:
        raise RuntimeError('empty process table')
    return rows


def family(rows, root_pid):
    found = {root_pid}
    for _ in range(8):
        found.update(pid for pid, row in rows.items() if row['ppid'] in found)
    return {pid:rows[pid] for pid in found if pid in rows}


def terminal_wrapper_calls_ok(rows, expected_calls):
    """Accept cumulative samples, but never hide a failure or decreasing counter."""
    if not isinstance(expected_calls, int) or expected_calls < 0:
        return False
    if expected_calls == 0:
        return not rows
    if not rows:
        return False
    previous = 0
    for calls, failed in rows:
        if calls < previous or calls > expected_calls or failed != 0:
            return False
        previous = calls
    return rows[-1] == (expected_calls, 0)


def monitor_timeout_transition(consecutive_timeouts, sample_succeeded):
    count = 0 if sample_succeeded else consecutive_timeouts + 1
    return count, count >= MONITOR_TIMEOUT_LIMIT


def resource_stop_reason(root, start, wall0, prior_wall, case_wall_s, total_wall_s,
                         peak, rss_cap, reserve_disk):
    mono_elapsed = time.monotonic() - start
    wall_elapsed = (time.time_ns() - wall0) / 1e9
    elapsed = max(mono_elapsed, wall_elapsed)
    reason = None
    if elapsed > case_wall_s or prior_wall + elapsed > total_wall_s:
        reason = 'wall_resource_cap'
    elif peak > rss_cap:
        reason = 'rss_resource_cap'
    elif shutil.disk_usage(root).free < reserve_disk:
        reason = 'disk_resource_cap'
    return reason, mono_elapsed, wall_elapsed


def save(path, value):
    path.write_text(json.dumps(value, indent=2)+'\n')


def get_helper():
    spec = importlib.util.spec_from_file_location('benchmark_frozen', HELPER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expected_helper_sha(manifest):
    pin = manifest.get('analysis_helper')
    if isinstance(pin, dict):
        if pin.get('path') and Path(pin['path']).name != HELPER.name:
            raise ValueError(f'manifest helper pin names a different file: {pin["path"]}')
        if pin.get('sha256'):
            return pin['sha256']
    return manifest.get('analysis_helper_sha256', FALLBACK_HELPER_SHA)


def terminal_wrapper_rows(stdout):
    return [(int(calls), int(failed)) for calls, failed in re.findall(
        r'Tile\s+1:\s+Steps=\s*(\d+),\s+Failed=\s*(\d+)', stdout)]


def closed_t0(cwd):
    file = cwd/'wrfout_d01_0001-01-01_00:00:00'
    ready = cwd/'wrfoutReady_d01_0001-01-01_00:00:00'
    if not file.exists() or not ready.exists():
        return None
    before=file.stat(); time.sleep(.1); after=file.stat()
    if (before.st_size,before.st_mtime_ns)!=(after.st_size,after.st_mtime_ns):
        return None
    opened=subprocess.run(['/usr/sbin/lsof','-F','pa','--',str(file)],
                          capture_output=True,text=True,timeout=3)
    if opened.returncode not in (0,1):
        raise RuntimeError('cannot verify t0 writer closure')
    if any(line in ('aw','au') for line in opened.stdout.splitlines()):
        return None
    return file


def initial_gate(key, cwd, helper, manifest, case):
    file = closed_t0(cwd)
    if file is None:
        return None
    view = cwd/'controller_initial_views'/'current'
    view.mkdir(parents=True, exist_ok=False)
    (view/'wrfinput_d01').symlink_to(cwd/'wrfinput_d01')
    (view/file.name).symlink_to(file)
    contract = manifest['analysis_contract']
    grid = contract.get('expected_grid', GRID)
    state = helper.load_initial_state(view, grid,
                                      dry_case_no_qv=contract.get('dry_case_no_qv', True),
                                      use_theta_m=contract.get('use_theta_m', 1),
                                      source='wrfout')
    if key == 'rk3_h600':
        result = {'status':'canonical_post_init_history', 'source_kind':state['source_kind']}
    else:
        reference_pin = case.get('reference_t0')
        if not isinstance(reference_pin, dict):
            raise ValueError('noncanonical case must pin reference_t0')
        reference_path = Path(reference_pin['path'])
        if not reference_path.is_absolute():
            reference_path = ROOT / reference_path
        if sha(reference_path) != reference_pin['sha256']:
            raise ValueError('reference t0 history SHA-256 mismatch')
        ready_path = Path(reference_pin['ready_path'])
        if not ready_path.is_absolute():
            ready_path = ROOT / ready_path
        if sha(ready_path) != reference_pin['ready_sha256']:
            raise ValueError('reference t0 ready marker SHA-256 mismatch')
        ref_view = cwd/'controller_initial_views'/'reference'
        ref_view.mkdir(parents=True, exist_ok=False)
        (ref_view/'wrfinput_d01').symlink_to(cwd/'wrfinput_d01')
        (ref_view/reference_path.name).symlink_to(reference_path)
        reference = helper.load_initial_state(ref_view, grid,
                      dry_case_no_qv=contract.get('dry_case_no_qv', True),
                      use_theta_m=contract.get('use_theta_m', 1), source='wrfout')
        result = helper.compare_initial_states({'rk3_h600':reference,key:state},
                                               require_input_hash=True)
    result.update(history=str(file), history_sha256=sha(file), input_sha256=state['input_sha256'])
    save(cwd/'initial_gate.json', result)
    return result


def cleanup(proc, tracked, exe):
    """Reap our launcher; kill only mapped, birth-matched known rank children."""
    report = {'signals':[], 'errors':[]}
    def owned_now():
        rows=processes(); found=family(rows,proc.pid)
        for p,r in rows.items():
            try:
                if os.getsid(p)==proc.pid:
                    found[p]=r
            except ProcessLookupError:
                pass
            except PermissionError:
                if p in tracked:
                    raise
        tracked.update(found)
        return {p:r for p,r in rows.items() if p in tracked and
                r['birth']==tracked[p]['birth'] and not r['state'].startswith('Z')}
    oldmask = signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGINT, signal.SIGTERM})
    try:
        try:
            owned_now()  # Covers a child missed by the first normal RSS sample.
        except Exception as exc:
            report['errors'].append('initial cleanup snapshot:'+repr(exc))
        if proc.poll() is None:
            proc.terminate(); report['signals'].append('launcher_TERM')
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill(); report['signals'].append('launcher_KILL'); proc.wait(timeout=5)
        proc.wait(timeout=5)
        remaining={}
        for _ in range(2):
            try:
                remaining=owned_now()
            except Exception as exc:
                report['errors'].append('cleanup rescan:'+repr(exc)); break
            if not remaining:
                break
            for p in remaining:
                try:
                    mapped=subprocess.run(['/usr/sbin/lsof','-a','-p',str(p),'-d','txt','-Fn'],
                                          capture_output=True,text=True,timeout=3)
                    fresh=processes().get(p)
                    if fresh is None or fresh['state'].startswith('Z'):
                        continue
                    same=fresh['birth']==remaining[p]['birth'] and os.getsid(p)==proc.pid
                    if same and 'n'+str(Path(exe).resolve()) in mapped.stdout.splitlines():
                        os.kill(p,signal.SIGKILL); report['signals'].append(f'verified_rank_KILL:{p}')
                    else:
                        report['errors'].append(f'unverified owned session member:{p}')
                except ProcessLookupError:
                    pass
                except Exception as exc:
                    report['errors'].append(f'rank cleanup:{p}:'+repr(exc))
            time.sleep(.2)
        try:
            report['remaining_owned']=owned_now()
        except Exception as exc:
            report['remaining_owned']=remaining
            report['errors'].append('final cleanup snapshot:'+repr(exc))
        return report
    except Exception as exc:
        report['errors'].append('owned reap:'+repr(exc))
        report['remaining_owned']=tracked
        return report
    finally:
        signal.pthread_sigmask(signal.SIG_SETMASK, oldmask)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--manifest-sha256', required=True)
    parser.add_argument('--case', required=True)
    parser.add_argument('--go', action='store_true')
    args = parser.parse_args(argv)
    if not args.go:
        parser.error('--go is required; no launch was attempted')

    global ROOT
    manifest_path = Path(args.manifest).resolve()
    ROOT = manifest_path.parent
    if sha(manifest_path) != args.manifest_sha256:
        raise ValueError('manifest SHA-256 mismatch')
    manifest = json.loads(manifest_path.read_text())
    if manifest.get('resource_authorized') is not True:
        raise ValueError('resource authorization is not selected')
    if manifest.get('no_retries_or_cold_restart', True) is not True:
        raise ValueError('manifest must prohibit retries and cold restarts')
    if args.case not in manifest.get('cases', {}):
        raise KeyError(f'unknown case: {args.case}')
    key = args.case
    case = manifest['cases'][key]
    limits = manifest['limits']
    case_wall_s = float(limits['case_wall_seconds'])
    total_wall_s = float(limits['total_wall_seconds'])
    rss_cap = int(limits.get('rss_bytes', 32 * 1024**3))
    reserve_disk = int(limits.get('reserve_disk_bytes', 20 * 1024**3))
    if not (math.isfinite(case_wall_s) and math.isfinite(total_wall_s)
            and 0 < case_wall_s <= total_wall_s and rss_cap > 0 and reserve_disk >= 0):
        raise ValueError('invalid resource limits')
    runtime_policy = manifest.get('runtime_policy', {})
    if runtime_policy.get('ranks', 1) != 1:
        raise ValueError('this controller requires one MPI rank')

    cwd = Path(case['directory'])
    if not cwd.is_absolute():
        cwd = ROOT / cwd
    cwd = cwd.resolve()
    try:
        cwd.relative_to(ROOT.resolve())
    except ValueError as exc:
        raise ValueError(f'case directory is outside manifest root: {cwd}') from exc
    exe = Path(manifest['executable']['path']).resolve()
    if sha(exe) != manifest['executable']['sha256']:
        raise ValueError('executable SHA-256 mismatch')
    helper_sha = expected_helper_sha(manifest)
    if sha(HELPER) != helper_sha:
        raise ValueError('adjacent benchmark helper SHA-256 mismatch')
    input_sha = manifest['common_input']['sha256']
    if sha(cwd/'wrfinput_d01') != input_sha:
        raise ValueError('shared wrfinput SHA-256 mismatch')
    if sha(cwd/'namelist.input') != case['namelist_sha256']:
        raise ValueError('namelist SHA-256 mismatch')
    protected = ('forecast_intent.json','forecast_receipt.json','initial_gate.json',
                 'launcher_pid.json','launcher.stdout.log','launcher.stderr.log',
                 'monitor_samples.jsonl','rsl.out.0000','rsl.error.0000','namelist.output')
    collisions = [name for name in protected if (cwd/name).exists()]
    collisions += sorted({path.name for pattern in
                          ('wrfout_d01*','wrfoutReady_d01*','wrfrst_d01*')
                          for path in cwd.glob(pattern)
                          if path.exists() or path.is_symlink()})
    if collisions:
        raise FileExistsError(f'refusing to overwrite case outputs: {collisions}')
    trace_path = case.get('environment_overrides', {}).get('NATIVE_MPI_FINALIZE_TRACE')
    if trace_path and Path(trace_path).exists():
        raise FileExistsError(f'refusing to overwrite trace file: {trace_path}')
    if (cwd/'controller_initial_views').exists():
        raise FileExistsError('refusing to overwrite initial-state views')
    if shutil.disk_usage(ROOT).free < reserve_disk:
        raise OSError('manifest disk reserve is not available')
    bad = [name for name in os.environ if name.startswith((
        'PMIX_', 'PMI_', 'OMPI_COMM_WORLD_', 'OMPI_MCA_', 'PRTE_RANK', 'PRTE_MCA_', 'OPAL_MCA_'))]
    if bad:
        raise EnvironmentError(f'foreign MPI/runtime environment: {bad}')

    others = list((ROOT/'runs').glob('*/forecast_receipt.json'))
    prior_wall = sum(float(json.loads(path.read_text()).get('wall_seconds', 0.0)) for path in others)
    if prior_wall >= total_wall_s:
        raise TimeoutError('total wall-time limit is already exhausted')
    launch_argv = runtime_policy.get('argv')
    if launch_argv is None:
        command = ['/opt/homebrew/bin/mpirun', '-np', '1', '--bind-to', 'none', str(exe)]
    elif not isinstance(launch_argv, list) or len(launch_argv) < 2:
        raise ValueError('runtime_policy.argv must be a launcher argv')
    else:
        command = [*launch_argv[:-1], str(exe)]
    env = {k:v for k,v in os.environ.items() if not k.startswith(('WRF_SDIRK3_', 'WRF_PARITY_'))}
    overrides = {**manifest['common_environment'], **case.get('environment_overrides', {})}
    env.update({str(k):str(v) for k,v in overrides.items()})
    helper = get_helper()
    record = {'case':key,'command':command,'manifest_path':str(manifest_path),
              'manifest_sha256':args.manifest_sha256,'helper_path':str(HELPER),
              'helper_sha256':helper_sha,
              'input_sha256':input_sha,'executable_path':str(exe),
              'executable_sha256':sha(exe),'namelist_sha256':case['namelist_sha256'],
              'environment_overrides':overrides,'expected_steps':case['expected_steps'],
              'expected_duration_seconds':case['expected_duration_seconds'],
              'clock_scope':'OS-waited launcher wall; RSL main-step time is separate'}
    save(cwd/'forecast_intent.json', record)

    proc=None; tracked={}; peak=0; reason=None; gate=None; threads=None
    monitoring_samples=[]; consecutive_timeouts=0
    start=time.monotonic(); wall0=time.time_ns(); launcher_pid=None
    with (cwd/'monitor_samples.jsonl').open('x') as monitor_log, \
         (cwd/'launcher.stdout.log').open('xb') as out, \
         (cwd/'launcher.stderr.log').open('xb') as err:
        try:
            proc=subprocess.Popen(command,cwd=cwd,env=env,stdout=out,stderr=err,start_new_session=True)
            launcher_pid=proc.pid
            save(cwd/'launcher_pid.json', {'pid':proc.pid,'wall_start_ns':wall0,
                                            'monotonic_start':start,'manifest_sha256':args.manifest_sha256})
            while True:
                stop, mono_elapsed, wall_elapsed = resource_stop_reason(
                    ROOT, start, wall0, prior_wall, case_wall_s, total_wall_s,
                    peak, rss_cap, reserve_disk)
                if stop:
                    reason=stop; break
                if proc.poll() is not None:
                    break
                try:
                    rows=processes()
                except subprocess.TimeoutExpired as exc:
                    consecutive_timeouts, unavailable=monitor_timeout_transition(consecutive_timeouts, False)
                    sample={'status':'missing_monitor_sample','reason':'ps_timeout',
                            'timeout':repr(exc),'consecutive_timeouts':consecutive_timeouts,
                            'monotonic_elapsed_seconds':mono_elapsed,'wall_elapsed_seconds':wall_elapsed}
                    monitoring_samples.append(sample); monitor_log.write(json.dumps(sample)+'\n'); monitor_log.flush()
                    stop, _, _ = resource_stop_reason(
                        ROOT, start, wall0, prior_wall, case_wall_s, total_wall_s,
                        peak, rss_cap, reserve_disk)
                    if stop:
                        reason=stop; break
                    if proc.poll() is not None:
                        break
                    if unavailable:
                        reason='monitoring_unavailable'; break
                    time.sleep(POLL_SECONDS)
                    continue
                consecutive_timeouts, _=monitor_timeout_transition(consecutive_timeouts, True)
                if proc.poll() is not None:
                    break
                if not rows or proc.pid not in rows:
                    raise RuntimeError('process table is empty or omitted the live owned launcher')
                own=family(rows,proc.pid); tracked.update(own)
                tree_rss=sum(row['rss'] for row in own.values()); peak=max(peak,tree_rss)
                sample={'status':'sampled','monotonic_elapsed_seconds':mono_elapsed,
                        'wall_elapsed_seconds':wall_elapsed,'owned_pid_count':len(own),
                        'tree_rss_bytes':tree_rss,'peak_tree_rss_bytes':peak}
                monitoring_samples.append(sample); monitor_log.write(json.dumps(sample)+'\n'); monitor_log.flush()
                # Recheck caps and liveness before the next process-table query.
                stop, _, _ = resource_stop_reason(
                    ROOT, start, wall0, prior_wall, case_wall_s, total_wall_s,
                    peak, rss_cap, reserve_disk)
                if stop:
                    reason=stop; break
                if proc.poll() is not None:
                    break
                if gate is None:
                    try:
                        gate=initial_gate(key,cwd,helper,manifest,case)
                    except Exception as exc:
                        reason='initialization_mismatch_or_gate_error:'+repr(exc); break
                if threads is None and gate is not None:
                    natives=[pid for pid in own if pid!=proc.pid]
                    if natives:
                        stop, _, _ = resource_stop_reason(
                            ROOT, start, wall0, prior_wall, case_wall_s, total_wall_s,
                            peak, rss_cap, reserve_disk)
                        if stop:
                            reason=stop; break
                        if proc.poll() is not None:
                            break
                        try:
                            q=subprocess.run(['/bin/ps','-M','-p',str(natives[-1])],
                                             capture_output=True,text=True,timeout=3)
                            if q.returncode:
                                raise RuntimeError(f'thread sample ps returned {q.returncode}')
                            threads={'pid':natives[-1],'ps_M_return_code':q.returncode,'raw':q.stdout}
                        except subprocess.TimeoutExpired as exc:
                            threads={'pid':natives[-1],'status':'missing_monitor_sample','reason':'thread_ps_timeout','error':repr(exc)}
                        sample={'status':'thread_sample','result':threads}
                        monitoring_samples.append(sample); monitor_log.write(json.dumps(sample)+'\n'); monitor_log.flush()
                        stop, _, _ = resource_stop_reason(
                            ROOT, start, wall0, prior_wall, case_wall_s, total_wall_s,
                            peak, rss_cap, reserve_disk)
                        if stop:
                            reason=stop; break
                        if proc.poll() is not None:
                            break
                time.sleep(POLL_SECONDS)
        except BaseException as exc:
            reason='controller_exception:'+repr(exc)
        finally:
            cleanup_result=cleanup(proc,tracked,exe) if proc is not None else {'signals':[],'errors':[],'remaining_owned':{}}

    elapsed=time.monotonic()-start; wall=time.time_ns()-wall0; rc=proc.returncode if proc else None
    cap_elapsed=max(elapsed,wall/1e9)
    if cap_elapsed>case_wall_s or prior_wall+cap_elapsed>total_wall_s:
        reason=reason or 'late_wall_resource_cap'
    if cleanup_result.get('remaining_owned') or cleanup_result.get('errors'):
        reason=reason or 'cleanup_incomplete'
    if gate is None and reason is None:
        try:
            gate=initial_gate(key,cwd,helper,manifest,case)
        except Exception as exc:
            reason='initialization_gate_error:'+repr(exc)
    stdout=(cwd/'rsl.out.0000').read_text(errors='replace') if (cwd/'rsl.out.0000').exists() else ''
    stderr=(cwd/'rsl.error.0000').read_text(errors='replace') if (cwd/'rsl.error.0000').exists() else ''
    steps=len(re.findall(r'Timing for main:',stdout)); converged=stderr.count('[Newton] CONVERGED')
    aborted=len(re.findall(r'final_update_aborted=[1-9]|outcome=HARD_STAGE_ABORT',stderr))
    success_marker='SUCCESS COMPLETE WRF' in stdout or 'SUCCESS COMPLETE WRF' in stderr
    output_check=None
    if rc==0 and reason is None:
        try:
            row={'label':key,'path':str(cwd),'method':case['method'],'h_seconds':case['h_seconds'],
                 'input_sha256':input_sha,'executable_sha256':sha(exe),
                 'executable_path':str(exe),'namelist_sha256':case['namelist_sha256']}
            checked=helper._load_run(row,ROOT,manifest['analysis_contract'])
            output_check={k:checked[k] for k in ('grid','physical_minima_over_all_frames','physical_minimum_total_pressure_Pa','timestamps','step_logs')}
        except Exception as exc:
            reason='output_validation_error:'+repr(exc)
    wrapper_rows=terminal_wrapper_rows(stdout)
    expected_calls=case.get('expected_fortran_wrapper_calls')
    if expected_calls is None and case.get('method')=='ARK324':
        expected_calls=3*int(case['expected_steps'])
    if expected_calls is None:
        expected_calls=0
    terminal_ok=terminal_wrapper_calls_ok(wrapper_rows,int(expected_calls))
    normal=(rc==0 and reason is None and gate is not None and success_marker
            and steps==case['expected_steps'] and aborted==0 and terminal_ok)
    quiet_labels=('[JVP DIAG]','[FD DIAG]','[FWAD_CHECK]','[SOLVER TELEMETRY]',
                  '[Newton] JVP calls:','[Newton] CONVERGED','RHS_DIGEST',
                  '[GMRES] CONVERGED','[FGMRES] CONVERGED','SDIRK3 SOLVE DEBUG:',
                  'SDIRK3: First 5 msfty','SDIRK3: msfty has')
    quiet_labels+=('[FORTRAN DEBUG]','=== FORTRAN DEBUG:','=== SDIRK3 FORTRAN:',
                   '=== BEFORE sdirk3_initialize_solvers','=== AFTER sdirk3_initialize_solvers',
                   '=== BEFORE set_base_state_sdirk3','=== AFTER set_base_state_sdirk3',
                   'SDIRK3 init_implicit: kde from pb array','for JVP computation',
                   'SDIRK3 DEBUG:','SDIRK3 INIT DEBUG:','[FP64 STATE CARRY]',
                   '[SDIRK3] U west halo max:','[SDIRK3] V west halo max:',
                   '[JVP FORWARD]','[Newton] JVP method:','[JVP FD]','[JVP AUTO-BENCH]')
    quiet_labels+=('SDIRK3 init: WRF-consistent nz=','SDIRK3 solve_em: Using nx/ny/nz=',
                   'SDIRK3: First 5 rdnw values from Fortran:','SDIRK3: First 5 rdn values from Fortran:',
                   'SDIRK3: First 5 znw values from Fortran:','SDIRK3: First 5 dnw values from Fortran:',
                   '=== DEBUG: After calc_cq in solve_em ===','[PARITY sub','[PARITY stage','[WRF-STOCK USTEP]')
    diagnostic_counts={label:(stdout+stderr).count(label) for label in quiet_labels}
    normal=normal and not any(diagnostic_counts.values())
    record.update(return_code=rc,stop_reason=reason,elapsed_seconds=elapsed,wall_seconds=wall/1e9,
                  suspend_or_clock_discrepancy_seconds=wall/1e9-elapsed,
                  peak_sampled_tree_rss_bytes=peak,initial_gate=gate,
                  actual_outer_step_count=steps,newton_converged_stage_log_records=converged,
                  quiet_diagnostic_counts=diagnostic_counts,terminal_fortran_wrapper_calls=wrapper_rows,
                  expected_fortran_wrapper_calls=expected_calls,terminal_wrapper_calls_ok=terminal_ok,
                  monitoring_samples=monitoring_samples,consecutive_monitoring_timeouts=consecutive_timeouts,
                  threads_snapshot=threads,cleanup=cleanup_result,output_check=output_check,
                  success_marker=success_marker,status='completed' if normal else 'failed_or_incomplete',
                  cost_eligible=normal and abs(wall/1e9-elapsed)<5,
                  accuracy_pass_claim=False,same_accuracy_speedup_claim=False)
    save(cwd/'forecast_receipt.json',record)
    print(json.dumps({k:record[k] for k in ('case','status','return_code','stop_reason','elapsed_seconds',
                                            'actual_outer_step_count','terminal_fortran_wrapper_calls',
                                            'terminal_wrapper_calls_ok','cost_eligible')},indent=2))
    return 0 if normal else 2


if __name__=='__main__':
    raise SystemExit(main())
