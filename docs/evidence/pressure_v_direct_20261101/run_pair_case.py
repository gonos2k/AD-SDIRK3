#!/usr/bin/env python3
"""Draft direct-V paired case runner; registration/source freeze required."""
import hashlib,json,math,os,pathlib,re,shutil,struct,subprocess,sys,time
import numpy as np
import benchmark_wrf_accuracy_cost as helper
from base_screen_reference import all_numeric_bitwise
from run_a4 import record,check_linear,compare_linear,jvp_summary,owned_comparison
from run_a5 import read_records
from pair_checks import evaluate
from direct_checks import assess as assess_direct
R=pathlib.Path(__file__).resolve().parent;FLOOR=5*1024**3;CAP=4*1024**3;PAYLOAD_CAP=256*1024**2

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()

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
 if any((d/x).exists() for x in ('attempt.json','launcher.stdout.log','launcher.stderr.log','rank_rusage.log','rsl.out.0000','rsl.error.0000','result.json','pressure_a5_direction.bin','pressure_a5_actions.bin','pressure_a4_first_pair.bin')) or list(d.glob('wrfout_d01*')):raise SystemExit('attempt or output already exists')
 with (d/'attempt.json').open('x') as f:json.dump({'one_attempt':True,'no_retry':True,'start_epoch':time.time(),'disk_free_start':free,'clamshell_open':True},f,indent=2)
 env={k:v for k,v in os.environ.items() if not k.startswith(('WRF_SDIRK3_','WRF_PARITY_'))};env.update({'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','VECLIB_MAXIMUM_THREADS':'1','MKL_NUM_THREADS':'1','WRF_SDIRK3_STAGE_OPERAND_DIAG':'0','WRF_SDIRK3_EW_ETA_INITIAL':'0.1','WRF_SDIRK3_GMRES_ESTIMATE_PROBE':'0'})
 start=time.monotonic();wall=time.time();result={'case':case,'status':'failed_or_incomplete','decision':'INCONCLUSIVE','model_completed':False,'failure_class':None,'failure_reason':None,'executable_sha256':sha(exe),'source_commit':intent['source_commit'],'rss_scope':'postrun rank maxrss; excludes launcher','timing_scope':'single instrumented feasibility pair only; no Gate B or forecast claim'};rc=None
 try:
  with (d/'launcher.stdout.log').open('xb') as out,(d/'launcher.stderr.log').open('xb') as err:
   caf=subprocess.Popen(['/usr/bin/caffeinate','-i','-w',str(os.getpid())])
   try:p=subprocess.Popen(cmd,cwd=d,env=env,stdout=out,stderr=err,start_new_session=True);rc=p.wait()
   finally:caf.terminate();caf.wait(timeout=5)
  result.update(return_code=rc,command=cmd,monotonic_elapsed_seconds=time.monotonic()-start,wall_elapsed_seconds=time.time()-wall,environment_overrides={k:env[k] for k in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','VECLIB_MAXIMUM_THREADS','MKL_NUM_THREADS','WRF_SDIRK3_STAGE_OPERAND_DIAG','WRF_SDIRK3_EW_ETA_INITIAL','WRF_SDIRK3_GMRES_ESTIMATE_PROBE')});result['clock_gap_seconds']=result['wall_elapsed_seconds']-result['monotonic_elapsed_seconds'];read=lambda x:(d/x).read_text(errors='replace') if (d/x).exists() else '';out=read('rsl.out.0000');text='\n'.join(read(x) for x in ('rsl.out.0000','rsl.error.0000','launcher.stdout.log','launcher.stderr.log'));phase='process_failure'
  result['available_artifacts']={x:{'bytes':(d/x).stat().st_size,'sha256':sha(d/x)} for x in ('pressure_a5_direction.bin','pressure_a5_actions.bin','pressure_a4_first_pair.bin') if (d/x).is_file()};result['raw_artifact_combined_bytes']=sum(x['bytes'] for x in result['available_artifacts'].values());result['raw_artifact_cap_met']=result['raw_artifact_combined_bytes']<=PAYLOAD_CAP
  result['steps']=len(re.findall(r'Timing for main:',out));result['stage_residuals']=re.findall(r'\[Newton\] CONVERGED[^\n]*',text);rows=[tuple(map(int,x)) for x in re.findall(r'Tile\s+1:\s+Steps=\s*(\d+),\s+Failed=\s*(\d+)',out)];result['wrapper_rows']=rows;rss=re.findall(r'^\s*(\d+)\s+maximum resident set size\s*$',read('rank_rusage.log'),re.M);result['rank_peak_rss_bytes']=int(rss[0]) if len(rss)==1 else None
  direct_lines=[line for line in text.splitlines() if '[PRESSURE_V_DIRECT]' in line]
  if case=='candidate':
   if direct_lines:
    try:result['direct']=record(text,'PRESSURE_V_DIRECT');result['direct_check']=assess_direct(result['direct'])
    except Exception as e:result['direct_parse_error']=str(e)
   direct_check=result.get('direct_check',{})
   if direct_check.get('classification')=='NO-GO':
    phase='preflight_identity';early=record(text,'PRESSURE_PREFLIGHT')
    if early.get('status')!='ok' or early.get('digest_fnv64')!='7bdd17f3a694726e' or early.get('torch_intra_op_threads')!=1 or early.get('torch_inter_op_threads')!=1:raise ValueError('direct budget failure lacks registered preflight/thread identity')
    if any(early.get(k)!=prior['preflight'].get(k) for k in set(early)|set(prior['preflight']) if k!='preflight_ms'):raise ValueError('direct budget failure preflight differs from control')
    result['preflight']=early;result['torch_threads_actual']={'intra_op':1,'inter_op':1}
    if result['rank_peak_rss_bytes'] is None or result['rank_peak_rss_bytes']>CAP or not result['raw_artifact_cap_met']:phase='resource_postrun_RSS';raise ValueError('budget outcome lacks passing postrun resource evidence')
    if abs(result['clock_gap_seconds'])>=1:phase='timing_ineligible';raise ValueError('budget outcome wall/monotonic gap >=1 second')
    phase=direct_check.get('failure_class','direct_gate_failed');result['decision']='NO-GO';raise ValueError(direct_check['reason'])
   if direct_check.get('failure_class') in ('resource_allocation','direct_evaluation_failed'):
    phase=direct_check['failure_class'];result['decision']='INCONCLUSIVE';raise ValueError(direct_check['reason'])
  if rc!=0:phase='timeout' if rc==241 else 'process_failure';raise ValueError(f'mpirun exited {rc}')
  phase='missing_completion';stages={int(x) for x in re.findall(r'\[Newton\] CONVERGED stage=(\d+)',text)}
  if result['steps']!=1 or 'SUCCESS COMPLETE WRF' not in text or stages!={2,3,4} or not rows or rows[-1]!=(3,0) or any(f!=0 or not 0<=n<=3 for n,f in rows) or re.search(r'final_update_aborted=[1-9]|outcome=HARD_STAGE_ABORT|\[Newton\] (?:FAILED|NOT CONVERGED)|FATAL CALLED',text,re.I):raise ValueError('step/stages/wrapper/success invalid')
  residuals={int(k):float(v) for k,v in re.findall(r'\[Newton\] CONVERGED stage=(\d+)[^\n]*?_rms=([0-9.eE+-]+)',text)};result['stage_residual_scaled_rms']=residuals
  if set(residuals)!={2,3,4} or any(not math.isfinite(v) or not 0<=v<1e-9 for v in residuals.values()):raise ValueError('stage residual gate failed')
  result['model_completed']=True
  if case=='candidate' and result.get('direct_check',{}).get('classification')!='VALID':
   phase='direct_receipt_inconclusive';raise ValueError(result.get('direct_parse_error') or result.get('direct_check',{}).get('reason','direct receipt missing'))
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
  observed['flavor']='reference_p0';check_linear(result['preflight'],observed,'candidate')
  if result['preflight']['block_scale_requested']!=0 or result['linear']['stopping_metric_name']!='unweighted_scaled_L2':raise ValueError('IdentityS/block0 required')
  if result['preflight'].get('torch_intra_op_threads')!=1 or result['preflight'].get('torch_inter_op_threads')!=1:raise ValueError('actual Torch intra/inter-op threads must both equal one')
  result['torch_threads_actual']={'intra_op':1,'inter_op':1}
  if '[PRECOND REFINE EVENT] disable on defect ratio' in text or 'LOCKING to identity for remainder of GMRES solve' in text or 'WARN-ONLY mode: keeping preconditioner output (fallback ' in text:raise ValueError('guard or refinement fallback')
  telemetry=[line for line in text.splitlines() if '[SOLVER TELEMETRY]' in line];fd=[re.findall(r'\bjvp_fd_calls=(\d+)',line) for line in telemetry]
  if len(telemetry)!=6 or any(len(x)!=1 or x[0]!='0' for x in fd):raise ValueError('missing telemetry or forward-AD fallback')
  result['a4']=record(text,'PRESSURE_A4');result['existing_jvp_summaries']=jvp_summary(text)
  if result['a4'].get('eligible') is not True or result['a4'].get('timer_overlap') is not False or result['a4'].get('nested_scope') is not False:raise ValueError('A4 first-point timing/capture invalid')
  result['capture']=record(text,'PRESSURE_A5_CAPTURE');result['legacy_decomposition']=record(text,'PRESSURE_A5_DECOMPOSITION');result['legacy_A5']=record(text,'PRESSURE_A5')
  if result['capture'].get('status')!='captured' or result['capture'].get('missing')!=[] or result['legacy_A5'].get('status')!='observed':raise ValueError('native A5 capture/helper incomplete')
  primal_fields=('primal_dpy_max','primal_level_mass_max','primal_velocity_mass_max','primal_alpha_v_max','primal_ratio_max','primal_alpha_max','primal_pressure_max')
  if result['legacy_A5'].get('ad_pointwise_ok') is not True or any(result['legacy_A5'].get(k)!=0 for k in primal_fields):raise ValueError('source primal or analytic-vs-FWAD check failed')
  result['legacy_A5_scope']='primal and AD checks required; do not gate corrected z using P0 inverse or FP64 finite-difference eligibility'
  result['payload']=validate_payload(d,case)

  phase='endpoint_parity'
  if case=='control':
   old05=json.loads(pathlib.Path(intent['old05_result']['path']).read_text());oldpre={k:v for k,v in old05['preflight'].items() if k not in ('torch_intra_op_threads','torch_inter_op_threads')};newpre={k:v for k,v in result['preflight'].items() if k not in ('torch_intra_op_threads','torch_inter_op_threads')};compare_linear(oldpre,old05['linear'],newpre,result['linear'])
   current=read_records(d/'pressure_a5_direction.bin');oldpayload=read_records(pathlib.Path(intent['old05_direction']['path']))
   for key in ('v_scaled','z_scaled','Az_scaled','S_diag'):
    a=current[key];b=oldpayload[key]
    if a.shape!=b.shape or a.dtype!=b.dtype or a.tobytes()!=b.tobytes():raise ValueError('control differs from old05 '+key)
   v=current['v_scaled'].reshape(-1);S=current['S_diag'].reshape(-1);Pz=current['P0z_physical'].reshape(-1);norm=float(np.linalg.norm(v));inverse=float(np.linalg.norm(v-Pz/S)/norm) if norm>0 else float('inf')
   if not math.isfinite(inverse) or inverse>1e-8:raise ValueError('control base P0 inverse reconstruction failed')
   result['control_P0_inverse_scaled_relative']=inverse;result['control_old05_scaled_v_z_Az_S_bytes_equal']=True
   result['old05_pair_bytes_equal']= (d/'pressure_a4_first_pair.bin').read_bytes()==pathlib.Path(intent['old05_pair']['path']).read_bytes()
   if not result['old05_pair_bytes_equal']:raise ValueError('control V/Az pair differs from old05')
   result['old05_endpoint_parity']=all_numeric_bitwise(intent['old05_endpoint']['path'],d/'wrfout_d01_0001-01-01_00:10:00')
   if not result['old05_endpoint_parity']['passed']:raise ValueError('control endpoint differs from old05')
   result['decision']='CONTROL_COMPLETED'
  else:
   compare_linear(prior['preflight'],prior['linear'],result['preflight'],result['linear']);bd=R/'runs/control';brow={'label':'control','path':str(bd),'method':'ARK324','h_seconds':600,'input_sha256':sha(bd/'wrfinput_d01'),'executable_sha256':prior['executable_sha256'],'executable_path':prior['executable_path'],'namelist_sha256':sha(bd/'namelist.input')};bloaded=helper._load_run(brow,R,contract);result['owned_field_comparison']=owned_comparison(bloaded['frames']['0001-01-01_00:10:00'],loaded['frames']['0001-01-01_00:10:00'])
   phase='paired_quality_and_work';assessment=evaluate(prior,result,bd/'pressure_a5_direction.bin',d/'pressure_a5_direction.bin');result['pair_assessment']=apply_endpoint_gate(assessment,result['owned_field_comparison']);result['endpoint_8eps_passed']=result['pair_assessment']['endpoint_8eps_passed'];result['decision']=result['pair_assessment']['decision']
   result['twofold_goal_met_separate']=assessment['work']['twofold_goal_met_separate'];result['gate_B_authorized']=False;result['old_A4_outcome']='NO-GO unchanged'
  result['executable_path']=exe;result['status']='completed'
 except Exception as e:result['failure_class']=locals().get('phase','process_failure');result['failure_reason']=str(e)
 finally:
  result['disk_free_end']=shutil.disk_usage(R).free;result['postrun_disk_floor_met']=result['disk_free_end']>=FLOOR
  if not result['postrun_disk_floor_met']:
   result['status']='failed_or_incomplete';result['decision']='INCONCLUSIVE';result['failure_class']='resource_postrun_disk';result['failure_reason']='postrun free disk below 5GiB'
  (d/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:result.get(k) for k in ('case','status','decision','return_code','failure_class','failure_reason')},indent=2));return 0 if result['status']=='completed' and (case=='control' or result['decision']=='SCREEN_POSITIVE') else 2
if __name__=='__main__':raise SystemExit(main())
