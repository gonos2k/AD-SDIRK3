#!/usr/bin/env python3
"""Draft direct-V paired case runner; registration/source freeze required."""
import hashlib,json,math,os,pathlib,re,shutil,struct,subprocess,sys,time,importlib.util
import numpy as np
import benchmark_wrf_accuracy_cost as helper
from base_screen_reference import all_numeric_bitwise
from run_a4 import record,check_linear,compare_linear,jvp_summary,owned_comparison
from run_a5 import read_records
from pair_checks import evaluate
from direct_checks import assess as assess_direct
R=pathlib.Path(__file__).resolve().parent
_spec=importlib.util.spec_from_file_location('observer_checks',R/'observer_checks.draft.py');observer=importlib.util.module_from_spec(_spec);_spec.loader.exec_module(observer)
FLOOR=5*1024**3;CAP=4*1024**3;PAYLOAD_CAP=192*1024**2

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()

def diagnostic_check_linear(pre,lin,case):
 if pre.get('status')!='ok' or pre.get('identity_scope')!='bounded_captured_inputs_not_all_transitive_state' or not re.fullmatch('[0-9a-f]{1,16}',pre.get('digest_fnv64','')):raise ValueError('invalid preflight status/identity/digest')
 for k in ('state_fnv64','F_fnv64','K_fnv64','b_fnv64','x0_fnv64','S_fnv64','Sinv_fnv64','mask_fnv64','eos_fnv64','geometry_fnv64','raw_grid_fnv64','raw_column_0_0_fnv64','raw_full_fnv64'):
  if not re.fullmatch('[0-9a-f]{1,16}',pre.get(k,'')):raise ValueError('missing preflight fingerprint '+k)
 for k in ('restart','max_restarts'):
  if not isinstance(pre.get(k),int) or pre[k]<=0 or pre[k]!=lin.get('restart_budget' if k=='restart' else k):raise ValueError('invalid/inconsistent budget '+k)
 if pre.get('tolerance')!=lin.get('tolerance_applied'):raise ValueError('preflight and linear tolerance differ')
 tol=positive(lin.get('tolerance_applied'),'tolerance_applied')
 registered_tol=float(np.float32(1e-7))
 if tol!=registered_tol:raise ValueError(f'applied tolerance {tol!r} differs from registered FP32 1e-7 {registered_tol!r}')
 rho=lin.get('stopping_relative_norm')
 if not isinstance(rho,(int,float)) or not math.isfinite(rho) or not 0<=rho<tol:raise ValueError('actual stopping relative norm does not meet tolerance')
 if lin.get('stopping_metric_name') not in ('unweighted_scaled_L2','block_D_weighted_scaled_L2') or lin.get('residual_provenance')!='saved_native_checked_raw_not_independent':raise ValueError('unsupported stopping metric/residual provenance')
 for k in ('preflight_ms','linear_with_native_truecheck_ms','interval_from_newton_entry_ms','saved_native_scaled_residual_norm','saved_native_physical_residual_norm','scaled_rhs_norm','physical_rhs_norm'):positive(lin.get(k),k)
 setup=lin.get('setup_ms')
 if case=='candidate':positive(setup,'setup_ms')
 elif not isinstance(setup,(int,float)) or not math.isfinite(setup) or setup<0:raise ValueError('invalid baseline setup time')
 if lin.get('flavor')!=('native_raw' if case=='baseline' else 'reference_p0') or lin.get('success') is not True:raise ValueError('linear solve not eligible')
 if lin.get('guard_events')!=0 or not isinstance(lin.get('preconditioner_calls'),int) or lin['preconditioner_calls']<=0:raise ValueError('guard/fallback/action check failed')
 if case=='candidate' and (not isinstance(lin.get('reference_calls'),int) or lin['reference_calls']<=0):raise ValueError('reference preconditioner was not called')

def validate_payload(d,case):
 direction=d/'pressure_a5_direction.bin';actions=d/'pressure_a5_actions.bin';pair=d/'pressure_a4_first_pair.bin'
 if not all(x.is_file() for x in (direction,actions,pair)):raise ValueError('missing early direction, actions or A4 pair')
 size=sum(x.stat().st_size for x in (direction,actions,pair))
 if size>PAYLOAD_CAP:raise ValueError('combined three-file raw artifact footprint exceeds 256MiB')
 x=read_records(direction);y=read_records(actions);needed={'U_eval','P0z_physical','v_scaled','z_scaled','Az_scaled','S_diag','eos_t','flags'}
 if not needed<=set(x) or not {'Qz_physical','V_Jphi','V_Jp','V_Jalpha','V_Jfwad','V_P0_difference'}<=set(y):raise ValueError('missing required A5 typed records')
 if 'base_z_scaled' in x:raise ValueError('Direct-V does not capture base_z_scaled')
 flags=x['flags'].reshape(-1)
 if len(flags)!=8 or list(flags[:4])!=[1,0,0,1] or flags[5]!=1 or flags[6]!=1:raise ValueError('unsupported dry-native context')
 raw=pair.read_bytes()
 if len(raw)<16 or raw[:8]!=b'SDIRKA41':raise ValueError('invalid A4 pair header')
 n=struct.unpack_from('<Q',raw,8)[0]
 if not n or len(raw)!=16+16*n:raise ValueError('invalid A4 pair length')
 S=x['S_diag'].reshape(-1);v=x['v_scaled'].reshape(-1);z=x['z_scaled'].reshape(-1);az=x['Az_scaled'].reshape(-1)
 if any(t.dtype!=np.dtype('<f8') or t.size!=n for t in (S,v,z,az)) or not np.all(S>0):raise ValueError('invalid packed scaled triplet')
 if (S*v).astype('<f8',copy=False).tobytes()!=raw[16:16+8*n] or (S*az).astype('<f8',copy=False).tobytes()!=raw[16+8*n:]:raise ValueError('captured scaled V/Az does not match original physical pair bytes')
 return {'direction_path':str(direction),'direction_sha256':sha(direction),'actions_path':str(actions),'actions_sha256':sha(actions),'A4_pair_path':str(pair),'A4_pair_sha256':sha(pair),'combined_bytes':size,'V_and_Az_reconstruction_bitwise':True,'base_z_present':'base_z_scaled' in x}

def apply_endpoint_gate(assessment, comparison):
 # Preserve numerical and work evidence when the endpoint gate fails.
 passed=all(v['violations']==0 for v in comparison.values())
 result=dict(assessment);result['endpoint_8eps_passed']=passed
 if not passed:result['decision']='NO-GO'
 return result

def main():
 if len(sys.argv)!=4 or sys.argv[1]!='--go' or sys.argv[2] not in ('control','candidate'):raise SystemExit('Requires --go control|candidate PIN_SHA256; draft protocol cannot launch')
 case=sys.argv[2];pin=R/(case+'_pins.json')
 if sha(pin)!=sys.argv[3]:raise SystemExit('pin manifest mismatch')
 pins=json.loads(pin.read_text());intent=json.loads((R/(case+'_intent.json')).read_text());reg=pathlib.Path(intent['registration']['path'])
 if sha(reg)!=intent['registration']['sha256'] or json.loads(reg.read_text()).get('status')!='registered':raise SystemExit('final registered protocol required')
 for p,h in pins.items():
  if sha(p)!=h:raise SystemExit('pin mismatch '+p)
 prior=None
 if case=='candidate':
  prior_path=pathlib.Path(intent['control_result']['path']);prior=json.loads(prior_path.read_text())
  if prior.get('status')!='completed' or prior.get('decision')!='CONTROL_COMPLETED' or not prior.get('model_completed') or prior.get('preflight',{}).get('digest_fnv64')!=intent['expected_preflight_digest_fnv64']:raise SystemExit('valid control required')
 free=shutil.disk_usage(R).free
 if free<FLOOR:raise SystemExit('insufficient prelaunch disk')
 power=subprocess.run(['/usr/sbin/ioreg','-r','-k','AppleClamshellState','-d','4'],capture_output=True,text=True,check=True,timeout=5).stdout
 if '"AppleClamshellState" = No' not in power:raise SystemExit('clamshell not confirmed open')
 d=R/'runs'/case;exe=intent['executable']['path'];tf=d/'rank_rusage.log';cmd=['/opt/homebrew/bin/mpirun','--timeout','360','--report-state-on-timeout','-np','1','--bind-to','none','/usr/bin/time','-l','-o',str(tf),exe]
 if any((d/x).exists() for x in ('attempt.json','launcher.stdout.log','launcher.stderr.log','rank_rusage.log','rsl.out.0000','rsl.error.0000','result.json','pressure_a5_direction.bin','pressure_a5_actions.bin','pressure_a4_first_pair.bin',*observer.NAMES)) or list(d.glob('wrfout_d01*')):raise SystemExit('attempt or output already exists')
 with (d/'attempt.json').open('x') as f:json.dump({'one_attempt':True,'no_retry':True,'start_epoch':time.time(),'disk_free_start':free,'clamshell_open':True},f,indent=2)
 env={k:v for k,v in os.environ.items() if not k.startswith(('WRF_SDIRK3_','WRF_PARITY_'))};env.update({'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','VECLIB_MAXIMUM_THREADS':'1','MKL_NUM_THREADS':'1','WRF_SDIRK3_STAGE_OPERAND_DIAG':'0','WRF_SDIRK3_EW_ETA_INITIAL':'0.1','WRF_SDIRK3_GMRES_ESTIMATE_PROBE':'0'})
 start=time.monotonic();wall=time.time();result={'case':case,'status':'failed_or_incomplete','decision':'INCONCLUSIVE','model_completed':False,'failure_class':None,'failure_reason':None,'executable_sha256':sha(exe),'source_commit':intent['source_commit'],'rss_scope':'postrun rank maxrss; excludes launcher','timing_scope':'instrumented diagnostic only; no performance gate, Gate B or forecast claim'};rc=None
 try:
  with (d/'launcher.stdout.log').open('xb') as out,(d/'launcher.stderr.log').open('xb') as err:
   caf=subprocess.Popen(['/usr/bin/caffeinate','-i','-w',str(os.getpid())])
   try:p=subprocess.Popen(cmd,cwd=d,env=env,stdout=out,stderr=err,start_new_session=True);rc=p.wait()
   finally:caf.terminate();caf.wait(timeout=5)
  result.update(return_code=rc,command=cmd,monotonic_elapsed_seconds=time.monotonic()-start,wall_elapsed_seconds=time.time()-wall,environment_overrides={k:env[k] for k in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','VECLIB_MAXIMUM_THREADS','MKL_NUM_THREADS','WRF_SDIRK3_STAGE_OPERAND_DIAG','WRF_SDIRK3_EW_ETA_INITIAL','WRF_SDIRK3_GMRES_ESTIMATE_PROBE')});result['clock_gap_seconds']=result['wall_elapsed_seconds']-result['monotonic_elapsed_seconds'];read=lambda x:(d/x).read_text(errors='replace') if (d/x).exists() else '';out=read('rsl.out.0000');text='\n'.join(read(x) for x in ('rsl.out.0000','rsl.error.0000','launcher.stdout.log','launcher.stderr.log'));phase='process_failure'
  result['available_artifacts']={x:{'bytes':(d/x).stat().st_size,'sha256':sha(d/x)} for x in ('pressure_a5_direction.bin','pressure_a5_actions.bin','pressure_a4_first_pair.bin',*observer.NAMES) if (d/x).is_file()};result['raw_artifact_combined_bytes']=sum(result['available_artifacts'].get(x,{}).get('bytes',0) for x in ('pressure_a5_direction.bin','pressure_a5_actions.bin','pressure_a4_first_pair.bin'));result['raw_artifact_cap_met']=result['raw_artifact_combined_bytes']<=PAYLOAD_CAP
  result['linear_delay_raw_lines']=[line for line in text.splitlines() if '[LINEAR_DELAY]' in line]
  try:result['linear_delay']=observer.validate(text,d,[d/'pressure_a5_direction.bin',d/'pressure_a5_actions.bin',d/'pressure_a4_first_pair.bin'])
  except Exception as e:result['observer_error']=str(e)
  result['steps']=len(re.findall(r'Timing for main:',out));result['stage_residuals']=re.findall(r'\[Newton\] CONVERGED[^\n]*',text);rows=[tuple(map(int,x)) for x in re.findall(r'Tile\s+1:\s+Steps=\s*(\d+),\s+Failed=\s*(\d+)',out)];result['wrapper_rows']=rows;rss=re.findall(r'^\s*(\d+)\s+maximum resident set size\s*$',read('rank_rusage.log'),re.M);result['rank_peak_rss_bytes']=int(rss[0]) if len(rss)==1 else None
  if rc!=0:phase='timeout' if rc==241 else 'process_failure';raise ValueError(f'mpirun exited {rc}')
  phase='missing_completion';stages={int(x) for x in re.findall(r'\[Newton\] CONVERGED stage=(\d+)',text)}
  if result['steps']!=1 or 'SUCCESS COMPLETE WRF' not in text or stages!={2,3,4} or not rows or rows[-1]!=(3,0) or any(f!=0 or not 0<=n<=3 for n,f in rows) or re.search(r'final_update_aborted=[1-9]|outcome=HARD_STAGE_ABORT|\[Newton\] (?:FAILED|NOT CONVERGED)|FATAL CALLED',text,re.I):raise ValueError('step/stages/wrapper/success invalid')
  residuals={int(k):float(v) for k,v in re.findall(r'\[Newton\] CONVERGED stage=(\d+)[^\n]*?_rms=([0-9.eE+-]+)',text)};result['stage_residual_scaled_rms']=residuals
  if set(residuals)!={2,3,4} or any(not math.isfinite(v) or not 0<=v<1e-9 for v in residuals.values()):raise ValueError('stage residual gate failed')
  result['model_completed']=True
  phase='configuration_mismatch';cfg=re.findall(r'\[CONFIG\] reuse_linearization = ([01])\b',text)
  if not cfg or any(x!='0' for x in cfg) or '[CONFIG] internal_fp64 = true' not in text or '[CONFIG] internal_fp64_state_carry = true' not in text:raise ValueError('OFF/FP64 carry config mismatch')
  phase='resource_postrun_RSS'
  if result['rank_peak_rss_bytes'] is None or result['rank_peak_rss_bytes']>CAP:raise ValueError('rank RSS missing or above 4GiB')
  if not result['raw_artifact_cap_met']:raise ValueError('combined payload exceeds 256MiB')
  phase='timing_ineligible'
  if abs(result['clock_gap_seconds'])>=1:raise ValueError('wall/monotonic gap >=1 second')
  phase='missing_artifact'
  for stamp in ('0001-01-01_00:00:00','0001-01-01_00:10:00'):
   if not (d/('wrfout_d01_'+stamp)).is_file() or not (d/('wrfoutReady_d01_'+stamp)).is_file():raise ValueError('history/ready missing '+stamp)
  contract=intent['analysis_contract'];row={'label':case,'path':str(d),'method':'ARK324','h_seconds':600,'input_sha256':sha(d/'wrfinput_d01'),'executable_sha256':sha(exe),'executable_path':exe,'namelist_sha256':sha(d/'namelist.input')};phase='numeric_admissibility';loaded=helper._load_run(row,R,contract);result['output_check']={k:loaded[k] for k in ('grid','physical_minima_over_all_frames','physical_minimum_total_pressure_Pa','timestamps','step_logs')}
  phase='initial_state';ref=helper.load_initial_state(R/'reference',contract['expected_grid'],dry_case_no_qv=True,use_theta_m=1,source='wrfout');cur=helper.load_initial_state(d,contract['expected_grid'],dry_case_no_qv=True,use_theta_m=1,source='wrfout');result['t0_gate']=helper.compare_initial_states({'rk3':ref,case:cur},require_input_hash=True)
  phase='linear_validity';result['preflight']=record(text,'PRESSURE_PREFLIGHT');result['linear']=record(text,'PRESSURE_LINEAR');observed=dict(result['linear']);expected_flavor='reference_p0' if case=='control' else 'reference_direct_p1'
  if observed.get('flavor')!=expected_flavor:raise ValueError('unexpected first-point flavor')
  observed['flavor']='reference_p0';diagnostic_check_linear(result['preflight'],observed,'candidate')
  if result['preflight']['block_scale_requested']!=0 or result['linear']['stopping_metric_name']!='unweighted_scaled_L2':raise ValueError('IdentityS/block0 required')
  if result['preflight'].get('torch_intra_op_threads')!=1 or result['preflight'].get('torch_inter_op_threads')!=1:raise ValueError('actual Torch intra/inter-op threads must both equal one')
  result['torch_threads_actual']={'intra_op':1,'inter_op':1}
  if '[PRECOND REFINE EVENT] disable on defect ratio' in text or 'LOCKING to identity for remainder of GMRES solve' in text or 'WARN-ONLY mode: keeping preconditioner output (fallback ' in text:raise ValueError('guard or refinement fallback')
  telemetry=[line for line in text.splitlines() if '[SOLVER TELEMETRY]' in line];fd=[re.findall(r'\bjvp_fd_calls=(\d+)',line) for line in telemetry]
  if len(telemetry)!=6 or any(len(x)!=1 for x in fd):raise ValueError('missing FD telemetry')
  result['telemetry_fd_calls']=[int(x[0]) for x in fd]
  result['a4']=record(text,'PRESSURE_A4');result['existing_jvp_summaries']=jvp_summary(text)
  if result['a4'].get('eligible') is not True or result['a4'].get('timer_overlap') is not False or result['a4'].get('nested_scope') is not False:raise ValueError('A4 first-point timing/capture invalid')
  result['capture']=record(text,'PRESSURE_A5_CAPTURE');result['legacy_decomposition']=record(text,'PRESSURE_A5_DECOMPOSITION');result['legacy_A5']=record(text,'PRESSURE_A5')
  if result['capture'].get('status')!='captured' or result['capture'].get('missing')!=[] or result['legacy_A5'].get('status')!='observed':raise ValueError('native A5 capture/helper incomplete')
  primal_fields=('primal_dpy_max','primal_level_mass_max','primal_velocity_mass_max','primal_alpha_v_max','primal_ratio_max','primal_alpha_max','primal_pressure_max')
  if result['legacy_A5'].get('ad_pointwise_ok') is not True or any(result['legacy_A5'].get(k)!=0 for k in primal_fields):raise ValueError('source primal or analytic-vs-FWAD check failed')
  result['legacy_A5_scope']='primal and AD checks required; do not gate corrected z using P0 inverse or FP64 finite-difference eligibility'
  result['payload']=validate_payload(d,case)

  phase='first_point_identity';old=json.loads(pathlib.Path(intent['archived_arm_result']['path']).read_text());compare_linear(old['preflight'],old['linear'],result['preflight'],result['linear'])
  old_direction=read_records(pathlib.Path(intent['archived_arm_direction']['path']));new_direction=read_records(d/'pressure_a5_direction.bin')
  for key in ('v_scaled','z_scaled','Az_scaled','S_diag'):
   a=old_direction[key];b=new_direction[key]
   if a.shape!=b.shape or a.dtype!=b.dtype or a.tobytes()!=b.tobytes():raise ValueError('first-point arm mismatch '+key)
  result['first_point_arm_v_z_Az_S_byte_equal']=True
  phase='observer_coverage'
  if 'linear_delay' not in result:raise ValueError(result.get('observer_error','observer evidence missing'))
  phase='endpoint_parity'
  result['archived_arm_endpoint_parity']=all_numeric_bitwise(intent['archived_arm_endpoint']['path'],d/'wrfout_d01_0001-01-01_00:10:00')
  if not result['archived_arm_endpoint_parity']['passed']:raise ValueError('arm endpoint differs from archived A8')
  result['decision']='CONTROL_COMPLETED' if case=='control' else 'DIAGNOSTIC_COMPLETED'
  result['gate_B_authorized']=False
  result['executable_path']=exe;result['status']='completed'
 except Exception as e:result['failure_class']=locals().get('phase','process_failure');result['failure_reason']=str(e)
 finally:
  result['disk_free_end']=shutil.disk_usage(R).free;result['postrun_disk_floor_met']=result['disk_free_end']>=FLOOR
  if not result['postrun_disk_floor_met']:
   result['status']='failed_or_incomplete';result['decision']='INCONCLUSIVE';result['secondary_resource_postrun_disk_failure']=True
   if not result.get('failure_class'):
    result['failure_class']='resource_postrun_disk';result['failure_reason']='postrun free disk below 5GiB'
  (d/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:result.get(k) for k in ('case','status','decision','return_code','failure_class','failure_reason')},indent=2));return 0 if result['status']=='completed' and (case=='control' or result['decision']=='DIAGNOSTIC_COMPLETED') else 2
if __name__=='__main__':raise SystemExit(main())
