#!/usr/bin/env python3
"""Two-attempt OFF/ON one-step screen; run only after reviewed pins and --go."""
import hashlib,json,os,pathlib,re,shutil,subprocess,sys,time
import numpy as np
from netCDF4 import Dataset
import benchmark_wrf_accuracy_cost as helper
R=pathlib.Path(__file__).resolve().parent
CAP=4*1024**3

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()

def all_numeric_bitwise(a,b):
 with Dataset(a) as left,Dataset(b) as right:
  left.set_auto_maskandscale(False);right.set_auto_maskandscale(False)
  la={k for k,v in left.variables.items() if np.issubdtype(v.dtype,np.number)};rb={k for k,v in right.variables.items() if np.issubdtype(v.dtype,np.number)}
  if la!=rb:raise ValueError(f'numeric variable set mismatch: left-only={sorted(la-rb)}, right-only={sorted(rb-la)}')
  mismatch=[]
  for k in sorted(la):
   lv=left.variables[k];rv=right.variables[k];x=np.asarray(lv[:]);y=np.asarray(rv[:])
   if lv.dimensions!=rv.dimensions or x.shape!=y.shape or x.dtype!=y.dtype or x.tobytes()!=y.tobytes():mismatch.append(k)
  return {'numeric_variables_compared':len(la),'bitwise_mismatch_variables':mismatch,'passed':not mismatch}

def on_fields(off,on):
 result={};limit_factor=8*np.finfo(np.float32).eps
 for k in ('U','V','W','PH','T','MU'):
  a=off[k];b=on[k]
  if a.shape!=b.shape or not np.isfinite(a).all() or not np.isfinite(b).all():raise ValueError(f'{k} owned field has mismatched shape or non-finite values')
  delta=np.abs(b-a);limit=limit_factor*np.maximum(np.maximum(np.abs(a),np.abs(b)),1.0)
  result[k]={'owned_points':a.size,'max_abs_difference':float(delta.max()),'max_limit_fraction':float((delta/limit).max()),'violations':int(np.count_nonzero(delta>limit))}
 return result

def reuse_report(text):
 lines=re.findall(r'^\[JVP REUSE\] ([^\n]+)',text,re.M);records=[]
 for line in lines:
  d=dict(re.findall(r'(\w+)=([^\s]+)',line));required={'status','preparation_ms','krylov_ms','reference_calls','preparation_calls','validation_apply_calls','prepared_apply_calls'}
  if not required.issubset(d):raise ValueError('incomplete JVP reuse record')
  for k in ('preparation_ms','krylov_ms'):
   d[k]=float(d[k])
   if not np.isfinite(d[k]) or d[k]<0:raise ValueError(f'invalid reuse timer {k}: {d[k]}')
  for k in ('reference_calls','preparation_calls','validation_apply_calls','prepared_apply_calls'):d[k]=int(d[k])
  records.append(d)
 active=len(re.findall(r'\[Newton\] GMRES:',text));eligible=bool(records) and active>0 and len(records)>=active and all(x['status']=='enabled' and x['reference_calls']==1 and x['preparation_calls']==1 and x['validation_apply_calls']==1 and x['prepared_apply_calls']>0 for x in records)
 return {'records':records,'logged_active_linear_solves':active,'every_logged_reuse_enabled_and_validated':eligible,'preparation_ms_total':sum(x['preparation_ms'] for x in records),'krylov_ms_total':sum(x['krylov_ms'] for x in records),'reference_calls_total':sum(x['reference_calls'] for x in records),'preparation_calls_total':sum(x['preparation_calls'] for x in records),'validation_apply_calls_total':sum(x['validation_apply_calls'] for x in records),'prepared_apply_calls_total':sum(x['prepared_apply_calls'] for x in records)}

def main():
 if len(sys.argv)!=3 or sys.argv[1]!='--go':raise SystemExit('Requires --go PIN_SHA256; no native launch attempted')
 if sha(R/'pins.json')!=sys.argv[2]:raise SystemExit('Pin manifest mismatch')
 pins=json.loads((R/'pins.json').read_text());intent=json.loads((R/'intent.json').read_text())
 for p,h in pins.items():
  if sha(p)!=h:raise SystemExit('Pin mismatch: '+p)
 disk=shutil.disk_usage(R).free
 if disk<5*1024**3:raise SystemExit('Insufficient prelaunch disk')
 power=subprocess.run(['/usr/sbin/ioreg','-r','-k','AppleClamshellState','-d','4'],capture_output=True,text=True,check=True,timeout=5).stdout
 if '"AppleClamshellState" = No' not in power:raise SystemExit('Clamshell not confirmed open')
 with (R/'launch_receipt.json').open('x') as f:json.dump({'attempt_limit':2,'no_retry':True,'start_epoch':time.time(),'disk_free_start':disk,'clamshell_open':True,'clamshell_ioreg':power},f,indent=2)
 env={k:v for k,v in os.environ.items() if not k.startswith(('WRF_SDIRK3_','WRF_PARITY_'))};env.update({'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','VECLIB_MAXIMUM_THREADS':'1','MKL_NUM_THREADS':'1','WRF_SDIRK3_STAGE_OPERAND_DIAG':'0','WRF_SDIRK3_EW_ETA_INITIAL':'0.1','WRF_SDIRK3_GMRES_ESTIMATE_PROBE':'0'})
 exe=intent['executable']['path'];contract=intent['analysis_contract'];outputs=[];off_frames=None
 try:
  for case in ('OFF','ON'):
   d=R/'runs'/case;timefile=d/'rank_rusage.log';cmd=['/opt/homebrew/bin/mpirun','--timeout','360','--report-state-on-timeout','-np','1','--bind-to','none','/usr/bin/time','-l','-o',str(timefile),exe]
   if any((d/n).exists() for n in ('launcher.stdout.log','launcher.stderr.log','rank_rusage.log','rsl.out.0000','rsl.error.0000','result.json')) or list(d.glob('wrfout_d01*')):raise RuntimeError('output collision: '+str(d))
   print('start '+case,flush=True);start=time.monotonic();wall=time.time()
   with (d/'launcher.stdout.log').open('xb') as out,(d/'launcher.stderr.log').open('xb') as err:
    caf=subprocess.Popen(['/usr/bin/caffeinate','-i','-w',str(os.getpid())])
    try:
     p=subprocess.Popen(cmd,cwd=d,env=env,stdout=out,stderr=err,start_new_session=True);rc=p.wait()
    finally:caf.terminate();caf.wait(timeout=5)
   res={'case':case,'return_code':rc,'monotonic_elapsed_seconds':time.monotonic()-start,'wall_elapsed_seconds':time.time()-wall,'command':cmd,'environment_overrides':{k:env[k] for k in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','VECLIB_MAXIMUM_THREADS','MKL_NUM_THREADS','WRF_SDIRK3_STAGE_OPERAND_DIAG','WRF_SDIRK3_EW_ETA_INITIAL','WRF_SDIRK3_GMRES_ESTIMATE_PROBE')},'thread_count_actual':'not independently measured','rss_scope':'one rank process maxrss from /usr/bin/time -l, postrun; excludes launcher','cost_diagnostic_only':True,'status':'failed_or_incomplete'}
   res['clock_discrepancy_seconds']=res['wall_elapsed_seconds']-res['monotonic_elapsed_seconds']
   res['timing_eligible']=abs(res['clock_discrepancy_seconds'])<5
   res['model_completed']=False;res['failure_class']=None;res['failure_reason']=None
   res['timing_class']='eligible' if res['timing_eligible'] else 'timing_ineligible'
   phase='process_failure'
   try:
    out=(d/'rsl.out.0000').read_text(errors='replace') if (d/'rsl.out.0000').exists() else ''
    err=(d/'rsl.error.0000').read_text(errors='replace') if (d/'rsl.error.0000').exists() else ''
    text=out+'\n'+err
    res['steps']=len(re.findall(r'Timing for main:',out));res['stage_residuals']=re.findall(r'\[Newton\] CONVERGED[^\n]*',text)
    rows=[tuple(map(int,x)) for x in re.findall(r'Tile\s+1:\s+Steps=\s*(\d+),\s+Failed=\s*(\d+)',out)];res['wrapper_rows']=rows
    res['rank_peak_rss_bytes']=None
    if timefile.exists():
     rss=re.findall(r'^\s*(\d+)\s+maximum resident set size\s*$',timefile.read_text(),re.M)
     if len(rss)==1:res['rank_peak_rss_bytes']=int(rss[0])
    if case=='ON':
     try:res['reuse_work']=reuse_report(text)
     except Exception as e:res['reuse_work_error']=repr(e)
    if rc!=0:
     phase='timeout' if rc==241 else 'process_failure'
     raise ValueError(f'mpirun exited with status {rc}')
    phase='missing_completion'
    if res['steps']!=1 or 'SUCCESS COMPLETE WRF' not in text:raise ValueError('missing one-step timing or WRF success marker')
    stages={int(x) for x in re.findall(r'\[Newton\] CONVERGED stage=(\d+)',text)}
    if stages!={2,3,4}:raise ValueError(f'expected converged stages 2,3,4; got {sorted(stages)}')
    if not rows or rows[-1]!=(3,0) or any(f!=0 or not 0<=n<=3 for n,f in rows):raise ValueError(f'Fortran wrapper completion/counters invalid: {rows}')
    if re.search(r'final_update_aborted=[1-9]|outcome=HARD_STAGE_ABORT|\[Newton\] (?:FAILED|NOT CONVERGED)|FATAL CALLED',text,re.I):raise ValueError('solver failure/abort marker present')
    res['model_completed']=True
    phase='configuration_mismatch'
    config=re.findall(r'\[CONFIG\] reuse_linearization = ([01])\b',text);res['reuse_configuration_records']=config
    if not config or any(x!=('0' if case=='OFF' else '1') for x in config):raise ValueError(f'reuse configuration records do not match {case}: {config}')
    if '[CONFIG] internal_fp64 = true' not in text or '[CONFIG] internal_fp64_state_carry = true' not in text:raise ValueError('FP64 or carried-state effective configuration missing')
    phase='resource_postrun_RSS'
    if res['rank_peak_rss_bytes'] is None:raise ValueError('rank maximum RSS is missing or ambiguous')
    if res['rank_peak_rss_bytes']>CAP:raise ValueError(f"rank maximum RSS {res['rank_peak_rss_bytes']} exceeds {CAP}")
    phase='missing_artifact'
    for stamp in ('0001-01-01_00:00:00','0001-01-01_00:10:00'):
     if not (d/('wrfout_d01_'+stamp)).is_file() or not (d/('wrfoutReady_d01_'+stamp)).is_file():raise ValueError('history or ready marker missing: '+stamp)
    row={'label':case,'path':str(d),'method':'ARK324','h_seconds':600,'input_sha256':sha(d/'wrfinput_d01'),'executable_sha256':sha(exe),'executable_path':exe,'namelist_sha256':sha(d/'namelist.input')}
    phase='numeric_admissibility';loaded=helper._load_run(row,R,contract)
    res['output_check']={k:loaded[k] for k in ('grid','physical_minima_over_all_frames','physical_minimum_total_pressure_Pa','timestamps','step_logs')}
    phase='initial_state'
    ref=helper.load_initial_state(R/'reference',contract['expected_grid'],dry_case_no_qv=True,use_theta_m=1,source='wrfout');cur=helper.load_initial_state(d,contract['expected_grid'],dry_case_no_qv=True,use_theta_m=1,source='wrfout');res['t0_gate']=helper.compare_initial_states({'rk3':ref,case:cur},require_input_hash=True)
    end=loaded['frames']['0001-01-01_00:10:00'];phase='parity_acceptance'
    if case=='OFF':
     res['archived_off_regression']=all_numeric_bitwise(intent['archived_mode3_endpoint']['path'],d/'wrfout_d01_0001-01-01_00:10:00')
     if not res['archived_off_regression']['passed']:raise ValueError('OFF archived numeric arrays differ bitwise')
     off_frames=end
    else:
     res['on_owned_field_comparison']=on_fields(off_frames,end)
     if any(v['violations'] for v in res['on_owned_field_comparison'].values()):raise ValueError('ON owned-field tolerance exceeded')
     phase='reuse_guard_fallback'
     if 'reuse_work_error' in res:raise ValueError('cannot parse reuse work: '+res['reuse_work_error'])
     if not res['reuse_work']['every_logged_reuse_enabled_and_validated']:raise ValueError('reuse not enabled and validated for every logged solve')
    res['status']='completed'
   except Exception as e:
    res['failure_class']=phase;res['failure_reason']=str(e);res['validation_error']=repr(e)
   (d/'result.json').write_text(json.dumps(res,indent=2)+'\n');outputs.append(res)
   if res['status']!='completed':break
 finally:
  (R/'summary.json').write_text(json.dumps({'attempts':len(outputs),'case_statuses':[x['status'] for x in outputs],'disk_free_end':shutil.disk_usage(R).free,'results':outputs},indent=2)+'\n')
 return 0 if len(outputs)==2 and all(x['status']=='completed' for x in outputs) else 2
if __name__=='__main__':raise SystemExit(main())
