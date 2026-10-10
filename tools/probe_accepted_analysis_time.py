#!/usr/bin/env python3
"""Compare the accepted local-chart state at h=.625 with its saved h=.3125 run."""
from __future__ import annotations
import argparse, hashlib, json, subprocess, time, zipfile
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
import sys
sys.path.insert(0,str(ROOT/'tools'))
import test_fine_wave_inverse as inverse
import probe_fine_split_pullbacks as split
import probe_small_step_inverse as driver

GRID=(16,12,8); SIGMA=3.0e-4; TIMES=(150,300)

def sha(path: Path) -> str: return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path: Path, data: dict) -> None: path.write_text(json.dumps(data,indent=2,allow_nan=False)+'\n')
def main() -> int:
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--exe',type=Path,required=True);ap.add_argument('--accepted-zip',type=Path,required=True)
    ap.add_argument('--accepted-sha256',required=True);ap.add_argument('--outdir',type=Path,required=True)
    a=ap.parse_args();out=a.outdir.resolve();out.mkdir(parents=True,exist_ok=True)
    report_path=out/'report.json'; receipt={'schema':'pr284-accepted-state-analysis-time-v1','status':'preflight','native_calls_started':False}
    save(report_path,receipt)
    try:
        exe=a.exe.resolve()
        if not exe.is_file() or not exe.is_absolute(): raise ValueError('--exe must be an existing absolute executable')
        parent,root,_=driver.copy_resume_bundle(a.accepted_zip,a.accepted_sha256,out/'accepted')
        if (parent.get('schema')!='pr284-small-step-local-chart-continuation-v1' or
            parent.get('status') not in ('completed_bounded_continuation','bounded_continuation_gradient_threshold_stop') or
            parent.get('steps')!=960 or parent.get('dt')!=0.3125 or parent.get('grid')!=list(GRID) or
            parent.get('sigma')!=SIGMA or parent.get('fixture_sha256')!=driver.FIXTURE_SHA or
            parent.get('fd_zip_sha256')!=driver.FD_ZIP_SHA):
            raise ValueError('accepted artifact does not match the fixed h=.3125 local-chart profile')
        label=parent['terminal_gradient']['call_label']; terminal=parent['terminal_gradient']
        call=next(c for c in parent['calls'] if c['label']==label)
        if (call.get('kind')!='gradient' or not np.array_equal(call['delta'],parent['final_delta']) or
            not np.array_equal(terminal['delta'],parent['final_delta'])):
            raise ValueError('terminal gradient is not at the report final accepted state')
        statefile=root/call['bundle_initial_state']; basecsv=root/call['bundle_csv']
        state=split.read_literal_vector(statefile)
        if sha(basecsv)!=call['output_sha256'] or inverse.digest(state)!=call['state_sha256']:
            raise ValueError('accepted terminal state or baseline CSV digest mismatch')
        bmeta,barrays,bscalars,btext=inverse.read_payload(basecsv)
        driver.validate_trial(bmeta,barrays,bscalars,btext,expected_initial=state,
                              center_arrays=None,gradient=True)
        if not np.array_equal(barrays.get('initial_state'),state): raise ValueError('baseline CSV initial state mismatch')
        if not np.isclose(bscalars['objective_physical_w'],terminal['objective'],rtol=1e-12,atol=1e-10):
            raise ValueError('baseline CSV objective differs from terminal gradient receipt')
        inputs=root/'inputs'; obsfile=inputs/'observations.txt'
        fixture=ROOT/'tools/fixtures/pr282-exact-returned-state.zip'
        if sha(fixture)!=driver.FIXTURE_SHA: raise ValueError('fixed-state fixture archive SHA mismatch')
        with zipfile.ZipFile(fixture) as z:
            manifest=json.loads(z.read('manifest.json'))
            for name in ('initial_state.txt','observations.txt','descriptor_fine.csv','canonical_basis.npy'):
                raw=z.read(name);record=manifest['files'][name]
                if hashlib.sha256(raw).hexdigest()!=record['sha256'] or sha(inputs/name)!=record['sha256']:
                    raise ValueError(f'accepted artifact fixed input differs from fixture: {name}')
        if (sha(obsfile)!=parent['input_sha256']['observations'] or
            sha(inputs/'initial_state.txt')!=parent['input_sha256']['initial_state']):
            raise ValueError('accepted artifact input hashes differ from its report')
        _,descriptor,_,_=inverse.read_payload(inputs/'descriptor_fine.csv')
        base=split.read_literal_vector(inputs/'initial_state.txt')
        basis=np.load(inputs/'canonical_basis.npy',allow_pickle=False)
        if (basis.shape!=(8480,4) or basis.dtype!=np.float64 or not np.isfinite(basis).all() or
            inverse.digest(basis)!=parent['canonical_basis_sha256'] or
            inverse.digest(basis)!=manifest['canonical_basis']['array_sha256']):
            raise ValueError('accepted artifact canonical Bcan pin mismatch')
        expected_state=base+basis@np.asarray(terminal['delta'],dtype=np.float64)
        if not np.array_equal(state,expected_state): raise ValueError('accepted state does not match the pinned local chart')
        for key in split.CONTEXT_ARRAYS:
            if not np.array_equal(barrays[key],descriptor[key]): raise ValueError(f'accepted state context mismatch: {key}')
        obs=np.loadtxt(obsfile,skiprows=2)
        if obs.shape!=(105,5): raise ValueError('fixed observation input shape mismatch')
        for t,col in ((150,3),(300,4)):
            if not np.array_equal(barrays[f'observed_{t}'],obs[:,col]): raise ValueError(f'baseline observations differ: t={t}')
        if sha(exe)!=parent['executable_sha256'] or parent['short_replay_gate']['executable_sha256']!=sha(exe):
            raise ValueError('comparison executable differs from accepted-run executable')
        gatefile=root/parent['short_replay_gate']['bundle_csv']
        if sha(gatefile)!=parent['short_replay_gate']['csv_sha256']:
            raise ValueError('accepted-run short replay gate CSV digest mismatch')
        expected_sources=parent['source_sha256']; current_sources=driver.source_fingerprints()
        mismatch={k:(expected_sources.get(k),v) for k,v in current_sources.items() if expected_sources.get(k)!=v}
        if mismatch: raise ValueError(f'accepted-run source fingerprints differ: {mismatch}')
        current_sources[str(Path(__file__).resolve().relative_to(ROOT))]=sha(Path(__file__).resolve())
        command=[str(exe),*map(str,GRID),str(out/'h0625.csv'),'--physical-wave-inverse',str(statefile),str(obsfile),
                 '480','0.625','--newton-tol','1e-14','--krylov-tol','1e-12','--bounded-tape-forward-only']
        usage=out/'h0625.time.txt';timed=['/usr/bin/time','-v','-o',str(usage),*command]
        receipt.update({'status':'running','native_calls_started':True,
          'source_git_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
          'accepted_source_git_head':parent['source_git_head'],
          'source_sha256':current_sources,'accepted_zip_sha256':a.accepted_sha256,'accepted_report_sha256':sha(root/'report.json'),
          'accepted_state_delta':parent['final_delta'],'accepted_state_file_sha256':sha(statefile),
          'accepted_state_array_sha256':inverse.digest(state),'accepted_csv_sha256':sha(basecsv),
          'fixed_input_sha256':{'literal_initial_state':sha(inputs/'initial_state.txt'),
            'observations':sha(obsfile),'descriptor_fine':sha(inputs/'descriptor_fine.csv'),
            'canonical_basis_file':sha(inputs/'canonical_basis.npy')},
          'canonical_basis_array_sha256':inverse.digest(basis),
          'executable_sha256':sha(exe),
          'short_replay_gate_sha256':sha(gatefile),
          'baseline_objective_h03125':float(bscalars['objective_physical_w']),'baseline_csv':str(basecsv),
          'comparison':{'steps':480,'dt':0.625,'seconds':300,'sigma':SIGMA},'command':command,'timed_command':timed,
          'output_csv':str(out/'h0625.csv'),'stdout_log':str(out/'h0625.stdout.log'),
          'stderr_log':str(out/'h0625.stderr.log'),'time_report':str(usage),
          'active_call':{'label':'h0625','kind':'forward_only','state_sha256':sha(statefile)}})
        save(report_path,receipt);print('native start h0625',flush=True);start=time.monotonic()
        result=subprocess.run(timed,capture_output=True,text=True);duration=time.monotonic()-start
        print(f'native end h0625 rc={result.returncode} elapsed_s={duration:.3f}',flush=True)
        (out/'h0625.stdout.log').write_text(result.stdout);(out/'h0625.stderr.log').write_text(result.stderr)
        receipt.update({'active_call':None,'return_code':result.returncode,'seconds':duration,
          'process_elapsed_wall':driver.parse_time_value(usage.read_text() if usage.exists() else '', 'Elapsed (wall clock) time (h:mm:ss or m:ss)'),
          'peak_rss_kib':driver.parse_time_value(usage.read_text() if usage.exists() else '', 'Maximum resident set size (kbytes)'),
          'output_sha256':sha(out/'h0625.csv') if (out/'h0625.csv').exists() else None})
        save(report_path,receipt)
        if result.returncode: raise RuntimeError(f'native comparison failed rc={result.returncode}; see logs')
        meta,arrays,scalars,text=inverse.read_payload(out/'h0625.csv')
        expected={'trajectory_steps':480,'trajectory_dt_fp32':0.625,'trajectory_seconds':300,
          'observation_count_per_time':105,'observation_time_150_seconds':150,'observation_time_300_seconds':300,
          'physical_observation_sigma_m_s':SIGMA,'forward_only':1,'pullback_requested':0,
          'retains_tape':1,'tape_window_steps':1,'newton_tol_config':float(np.float32(1e-14)),
          'krylov_tol_config':float(np.float32(1e-12)),'nk_adaptive_tol_config':1,
          'ewt_rtol_config':float(np.float32(1e-6)),'internal_fp64':1,'internal_fp64_state_carry':1,
          'admissibility_checked_every_step':1}
        bad={k:(meta.get(k),v) for k,v in expected.items() if meta.get(k)!=v}
        if bad: raise ValueError(f'h=.625 native mode metadata mismatch: {bad}')
        inverse.check_fine_solver_tolerances(meta,newton=1e-14,krylov=1e-12,where='h=.625 comparison')
        if (text.get('execution_mode')!='single_step_tape_handoff' or
            text.get('checkpoint_precision')!='single_step_retained_fp64_handoff' or
            text.get('objective_normalization')!='0.5_sum_over_times_and_points_of_residual_over_sigma_squared' or
            text.get('physical_observation_domain_guard')!='passed' or
            text.get('vertical_height_monotonic_guard')!='passed'):
            raise ValueError('h=.625 execution or physical guards failed')
        if not np.array_equal(arrays.get('initial_state'),state): raise ValueError('h=.625 changed the accepted initial state')
        if not all(np.isfinite(x).all() for x in arrays.values()) or not all(np.isfinite(x) for x in scalars.values()):
            raise ValueError('h=.625 output contains NaN/Inf')
        for key in split.CONTEXT_ARRAYS:
            if not np.array_equal(arrays.get(key),barrays[key]): raise ValueError(f'h=.625 context changed: {key}')
        metrics={};total_delta=[];j625=0.0;stable_delta_j=np.longdouble(0.0)
        for t in TIMES:
            p0=barrays[f'predicted_{t}'];p1=arrays[f'predicted_{t}'];obs0=barrays[f'observed_{t}'];obs1=arrays[f'observed_{t}']
            column=3 if t==150 else 4
            if (not np.array_equal(obs0,obs1) or not np.array_equal(obs0,obs[:,column]) or
                not np.array_equal(barrays[f'bracket_index_{t}'],arrays[f'bracket_index_{t}'])):
                raise ValueError(f'h=.625 observation or bracket changed at t={t}')
            diff=p1-p0;r0=(p0-obs0).astype(np.longdouble);dr=diff.astype(np.longdouble);sig=np.longdouble(SIGMA)
            j0=np.longdouble(0.5)*np.sum((r0/sig)**2);cross=np.sum(r0*dr)/(sig*sig);quad=np.longdouble(0.5)*np.sum((dr/sig)**2)
            jt=np.longdouble(0.5)*np.sum(((p1-obs1).astype(np.longdouble)/sig)**2);j625+=float(jt);total_delta.extend(diff.tolist())
            decomp_error=float((jt-j0)-(cross+quad))
            if abs(decomp_error)>1e-10*max(1.0,abs(float(jt-j0))): raise ValueError(f'h=.625 residual cost decomposition failed at t={t}')
            stable_delta_j+=cross+quad
            rms=float(np.sqrt(np.mean(diff.astype(np.longdouble)**2)));metrics[str(t)]={'J_h03125':float(j0),'J_h0625':float(jt),
              'delta_J_direct':float(jt-j0),'delta_J_cross':float(cross),'delta_J_quadratic':float(quad),
              'delta_J_decomposition_error':decomp_error,
              'prediction_difference_max_abs':float(np.max(np.abs(diff))),
              'prediction_difference_rms':rms,'prediction_difference_rms_over_sigma':rms/SIGMA,
              'diagnostic_rms_le_0_1_sigma':bool(rms/SIGMA<=0.1)}
        if not np.isclose(j625,scalars['objective_physical_w'],rtol=2e-12,atol=1e-10): raise ValueError('h=.625 reported cost fails residual recomputation')
        d=np.asarray(total_delta,dtype=np.float64);aggregate=float(np.linalg.norm(d/SIGMA));aggregate_rms=aggregate/np.sqrt(d.size)
        direct_delta_j=float(j625-bscalars['objective_physical_w'])
        receipt.update({'status':'completed','objective_h0625':float(scalars['objective_physical_w']),'per_time':metrics,
          'delta_objective_h0625_minus_h03125':float(stable_delta_j),
          'delta_objective_direct_total_difference':direct_delta_j,
          'delta_objective_decomposition_error':float(np.longdouble(direct_delta_j)-stable_delta_j),
          'normalized_aggregate_prediction_delta_l2':aggregate,'normalized_aggregate_prediction_delta_rms':aggregate_rms,
          'aggregate_rms_le_0_1_sigma_diagnostic':bool(aggregate_rms<=0.1),
          'diagnostic_only':'0.1 sigma RMS is a predeclared comparison diagnostic, not an accuracy bound, Eg, optimizer, or optimum certificate.'})
        save(report_path,receipt);return 0
    except Exception as e:
        receipt.update({'status':'failed','failure_type':type(e).__name__,'failure':str(e)})
        save(report_path,receipt);raise

if __name__=='__main__': raise SystemExit(main())
