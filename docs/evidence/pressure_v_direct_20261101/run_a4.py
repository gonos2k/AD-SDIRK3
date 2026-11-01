#!/usr/bin/env python3
"""Draft A4 per-case runner; do not launch until frozen registration and review."""
import hashlib,json,math,os,pathlib,re,shutil,struct,subprocess,sys,time
import numpy as np
import benchmark_wrf_accuracy_cost as helper
from base_screen_reference import all_numeric_bitwise
R=pathlib.Path(__file__).resolve().parent;CAP=4*1024**3;FLOOR=5*1024**3

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()
def record(text,label):
 lines=[x for x in text.splitlines() if f'[{label}]' in x]
 if len(lines)!=1:raise ValueError(f'{label} expected one record, got {len(lines)}')
 match=re.search(r'\{.*\}\s*$',lines[0])
 if not match:raise ValueError(f'{label} malformed record')
 def reject(x):raise ValueError('nonfinite JSON token '+x)
 return json.loads(match.group(),parse_constant=reject)
def positive(x,name):
 if not isinstance(x,(int,float)) or isinstance(x,bool) or not math.isfinite(x) or x<=0:raise ValueError(f'{name} must be finite positive')
 return x
def check_linear(pre,lin,case):
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
 if lin.get('flavor')!=('native_raw' if case=='baseline' else 'reference_p0') or lin.get('success') is not True or lin.get('comparison_eligible') is not True:raise ValueError('linear solve not eligible')
 if lin.get('guard_events')!=0 or lin.get('jvp_fd_fallbacks')!=0 or not isinstance(lin.get('preconditioner_calls'),int) or lin['preconditioner_calls']<=0:raise ValueError('guard/fallback/action check failed')
 if case=='candidate' and (not isinstance(lin.get('reference_calls'),int) or lin['reference_calls']<=0):raise ValueError('reference preconditioner was not called')
def compare_linear(base_pre,base_lin,pre,lin):
 for k in set(base_pre)|set(pre):
  if k!='preflight_ms' and base_pre.get(k)!=pre.get(k):raise ValueError('preflight identity mismatch: '+k)
 for k in ('stopping_metric','stopping_metric_name','restart_budget','max_restarts','tolerance_applied','mask_active','refinement_passes','apinv_probe','jvp_probe'):
  if base_lin.get(k)!=lin.get(k):raise ValueError('linear metric/budget mismatch: '+k)
def owned_comparison(a,b):
 result={};eps=8*np.finfo(np.float32).eps
 for k in ('U','V','W','PH','T','MU'):
  x=a[k];y=b[k]
  if x.shape!=y.shape or not np.isfinite(x).all() or not np.isfinite(y).all():raise ValueError('invalid owned field '+k)
  diff=y-x;limit=eps*np.maximum(np.maximum(np.abs(x),np.abs(y)),1.);result[k]={'unit':{'U':'m s-1','V':'m s-1','W':'m s-1','PH':'m2 s-2','T':'K','MU':'Pa'}[k],'owned_points':x.size,'max_abs':float(np.max(np.abs(diff))),'rms':float(np.sqrt(np.mean(diff*diff))),'violations':int(np.count_nonzero(np.abs(diff)>limit))}
 return result


def jvp_summary(text):
 rows=re.findall(r'\[Newton\] JVP calls:\s*(\d+),\s*total JVP time:\s*([0-9.eE+-]+)\s*ms',text)
 if len(rows)!=6:raise ValueError(f'expected six existing Newton JVP summaries; got {len(rows)}')
 records=[{'calls':int(count),'time_ms':float(ms)} for count,ms in rows]
 if any(r['calls']<=0 or not math.isfinite(r['time_ms']) or r['time_ms']<0 for r in records):raise ValueError('invalid JVP summary')
 return {'first_linear_solve':records[0],'whole_step_records':records,'whole_step_calls_sum':sum(r['calls'] for r in records),'whole_step_jvp_time_ms_sum':sum(r['time_ms'] for r in records),'scope':'existing JVP summaries: first-solve JVP is nested in first-point scaled A; whole-step sum spans six solves. Never add either to A time; JVP counts need not equal A calls'}

def pair_info(d,obs):
 if obs.get('eligible') is not True or obs.get('pair_written') is not True or obs.get('capture')!='fgmres_restart0_arnoldi0_raw_A_M_v_before_D_and_orthogonalization' or obs.get('pair_format')!='SDIRKA41,u64le_numel,physical_v_f64le,physical_w_f64le' or obs.get('physical_transform')!='S_times_scaled_krylov_vectors' or obs.get('block_scope')!='full_packed_block_not_owned_core':raise ValueError('A4 capture scope/eligibility mismatch')
 if obs.get('pair_file')!='pressure_a4_first_pair.bin' or obs.get('capture_attempts')!=1 or obs.get('capture_count')!=1 or obs.get('capture_failed') is not False or obs.get('timer_overlap') is not False or obs.get('nested_scope') is not False:raise ValueError('A4 capture count or timer overlap invalid')
 path=d/'pressure_a4_first_pair.bin';data=path.read_bytes()
 if len(data)<16 or data[:8]!=b'SDIRKA41':raise ValueError('A4 pair header invalid')
 n=struct.unpack('<Q',data[8:16])[0]
 if n==0 or len(data)!=16+16*n:raise ValueError('A4 pair size mismatch')
 v=data[16:16+8*n];w=data[16+8*n:];vv=np.frombuffer(v,dtype='<f8');ww=np.frombuffer(w,dtype='<f8')
 if not np.isfinite(vv).all() or not np.isfinite(ww).all():raise ValueError('A4 pair has nonfinite data')
 blocks=obs.get('blocks')
 if not isinstance(blocks,list) or len(blocks)!=6 or sum(x.get('size',0) for x in blocks)!=n:raise ValueError('A4 packed block inventory invalid')
 return {'path':str(path),'file_sha256':sha(path),'numel':n,'v_sha256':hashlib.sha256(v).hexdigest(),'w_sha256':hashlib.sha256(w).hexdigest()}

def a4_checks(obs,lin,pre):
 if lin['stopping_metric_name']!='unweighted_scaled_L2' or pre.get('block_scale_requested')!=0:raise ValueError('A4 requires IdentityS and zero block scaling')
 if obs.get('S_fnv64')!=pre.get('S_fnv64'):raise ValueError('A4 scaling fingerprint mismatch')
 for name in ('full_scaled_A_calls','wrapped_M_calls','physical_M_calls'):
  if not isinstance(obs.get(name),int) or obs[name]<=0:raise ValueError('invalid A4 call count '+name)
 for name in ('full_scaled_A_ms','wrapped_M_ms','physical_M_ms_nested','capture_clone_ms_included'):
  positive(obs.get(name),name)
 remainder=obs.get('linear_remainder_ms')
 if not isinstance(remainder,(int,float)) or not math.isfinite(remainder) or remainder<0:raise ValueError('negative/nonfinite linear remainder')
 if obs['physical_M_ms_nested']>obs['wrapped_M_ms']:raise ValueError('physical M exceeds enclosing wrapped M')
 total=obs['full_scaled_A_ms']+obs['wrapped_M_ms']+remainder
 if not math.isclose(total,lin['linear_with_native_truecheck_ms'],rel_tol=1e-12,abs_tol=1e-8):raise ValueError('A4 nonnested timer accounting mismatch')
 fraction=obs['capture_clone_ms_included']/positive(lin['interval_from_newton_entry_ms'],'interval_from_newton_entry_ms')
 if fraction>0.01:raise ValueError('A4 snapshot clone exceeds 1% of first-point interval')
 return fraction

def main():
 if len(sys.argv)!=4 or sys.argv[1]!='--go' or sys.argv[2] not in ('A0','B0','B1','A1'):raise SystemExit('Requires --go A0|B0|B1|A1 PIN_SHA256; no model attempted')
 label=sys.argv[2];order=['A0','B0','B1','A1'];index=order.index(label);flavor='baseline' if label.startswith('A') else 'candidate';pinfile=R/(label+'_pins.json')
 if sha(pinfile)!=sys.argv[3]:raise SystemExit('pin manifest mismatch')
 pins=json.loads(pinfile.read_text());intent=json.loads((R/(label+'_intent.json')).read_text())
 reg=pathlib.Path(intent['registration']['path'])
 if sha(reg)!=intent['registration']['sha256'] or json.loads(reg.read_text()).get('status')!='registered':raise SystemExit('final registered A4 protocol required; draft is not runnable')
 for p,h in pins.items():
  if sha(p)!=h:raise SystemExit('pin mismatch '+p)
 previous=[]
 for name in order[:index]:
  p=R/'runs'/name/'result.json'
  if not p.is_file():raise SystemExit('previous phase missing: '+name)
  r=json.loads(p.read_text())
  if r.get('status')!='completed' or not r.get('a4_eligible'):raise SystemExit('previous phase ineligible: '+name)
  previous.append(r)
 for name in order[index+1:]:
  if (R/'runs'/name/'attempt.json').exists():raise SystemExit('later phase already attempted: '+name)
 if label in ('A1','B1'):
  first=previous[0] if flavor=='baseline' else previous[1]
  if intent['executable']['sha256']!=first['executable_sha256']:raise SystemExit('same-flavor executable changed')
 if label.startswith('B'):
  first=previous[0]
  if intent['expected_preflight_digest_fnv64']!=first['preflight']['digest_fnv64']:raise SystemExit('candidate expected-header digest is not from eligible A0')
  header=pathlib.Path(intent['expected_header']['path']);build=json.loads(pathlib.Path(intent['build_receipt']['path']).read_text())
  if sha(header)!=intent['expected_header']['sha256'] or build.get('expected_header_sha256')!=sha(header) or build.get('expected_digest_fnv64')!=first['preflight']['digest_fnv64'] or build.get('executable_sha256')!=intent['executable']['sha256']:raise SystemExit('candidate header or executable not bound to eligible A0')
  found=re.findall(r'kExpectedPreflightDigest\s*=\s*0x([0-9a-fA-F]+)ULL',header.read_text())
  if len(found)!=1 or int(found[0],16)!=int(first['preflight']['digest_fnv64'],16):raise SystemExit('candidate header contents do not match A0')
 free=shutil.disk_usage(R).free
 if free<FLOOR:raise SystemExit('insufficient prelaunch disk')
 power=subprocess.run(['/usr/sbin/ioreg','-r','-k','AppleClamshellState','-d','4'],capture_output=True,text=True,check=True,timeout=5).stdout
 if '"AppleClamshellState" = No' not in power:raise SystemExit('clamshell not confirmed open')
 d=R/'runs'/label;exe=intent['executable']['path'];tf=d/'rank_rusage.log'
 input_hash=sha(d/'wrfinput_d01');namelist_hash=sha(d/'namelist.input')
 if input_hash!=intent['input_sha256'] or namelist_hash!=intent['namelist_sha256']:raise SystemExit('case input or namelist pin mismatch')
 if previous and (input_hash!=previous[0]['input_sha256'] or namelist_hash!=previous[0]['namelist_sha256'] or intent['source_commit']!=previous[0]['source_commit']):raise SystemExit('case input, namelist or source differs from A0')
 cmd=['/opt/homebrew/bin/mpirun','--timeout','360','--report-state-on-timeout','-np','1','--bind-to','none','/usr/bin/time','-l','-o',str(tf),exe]
 if any((d/x).exists() for x in ('attempt.json','launcher.stdout.log','launcher.stderr.log','rank_rusage.log','rsl.out.0000','rsl.error.0000','result.json','pressure_a4_first_pair.bin')) or list(d.glob('wrfout_d01*')):raise SystemExit('attempt or output already exists')
 with (d/'attempt.json').open('x') as f:json.dump({'case':label,'one_attempt':True,'no_retry':True,'start_epoch':time.time(),'disk_free_start':free,'clamshell_open':True},f,indent=2)
 env={k:v for k,v in os.environ.items() if not k.startswith(('WRF_SDIRK3_','WRF_PARITY_'))};env.update({'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','VECLIB_MAXIMUM_THREADS':'1','MKL_NUM_THREADS':'1','WRF_SDIRK3_STAGE_OPERAND_DIAG':'0','WRF_SDIRK3_EW_ETA_INITIAL':'0.1','WRF_SDIRK3_GMRES_ESTIMATE_PROBE':'0'})
 start=time.monotonic();wall=time.time();result={'case':label,'source_commit':intent['source_commit'],'input_sha256':input_hash,'namelist_sha256':namelist_hash,'status':'failed_or_incomplete','a4_eligible':False,'model_completed':False,'failure_class':None,'failure_reason':None,'executable_sha256':sha(exe),'thread_count_actual':'not independently measured','rss_scope':'postrun rank maxrss; excludes launcher','cost_scope':'instrumented first Newton point only'};rc=None
 try:
  with (d/'launcher.stdout.log').open('xb') as out,(d/'launcher.stderr.log').open('xb') as err:
   caf=subprocess.Popen(['/usr/bin/caffeinate','-i','-w',str(os.getpid())])
   try:p=subprocess.Popen(cmd,cwd=d,env=env,stdout=out,stderr=err,start_new_session=True);rc=p.wait()
   finally:caf.terminate();caf.wait(timeout=5)
  result.update(return_code=rc,command=cmd,monotonic_elapsed_seconds=time.monotonic()-start,wall_elapsed_seconds=time.time()-wall,environment_overrides={k:env[k] for k in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','VECLIB_MAXIMUM_THREADS','MKL_NUM_THREADS','WRF_SDIRK3_STAGE_OPERAND_DIAG','WRF_SDIRK3_EW_ETA_INITIAL','WRF_SDIRK3_GMRES_ESTIMATE_PROBE')});result['clock_gap_seconds']=result['wall_elapsed_seconds']-result['monotonic_elapsed_seconds'];read=lambda x:(d/x).read_text(errors='replace') if (d/x).exists() else '';out=read('rsl.out.0000');text='\n'.join(read(x) for x in ('rsl.out.0000','rsl.error.0000','launcher.stdout.log','launcher.stderr.log'));phase='process_failure'
  result['steps']=len(re.findall(r'Timing for main:',out));result['stage_residuals']=re.findall(r'\[Newton\] CONVERGED[^\n]*',text);rows=[tuple(map(int,x)) for x in re.findall(r'Tile\s+1:\s+Steps=\s*(\d+),\s+Failed=\s*(\d+)',out)];result['wrapper_rows']=rows
  rss=re.findall(r'^\s*(\d+)\s+maximum resident set size\s*$',read('rank_rusage.log'),re.M);result['rank_peak_rss_bytes']=int(rss[0]) if len(rss)==1 else None
  if rc!=0:phase='timeout' if rc==241 else 'process_failure';raise ValueError(f'mpirun exited {rc}')
  phase='missing_completion';stages={int(x) for x in re.findall(r'\[Newton\] CONVERGED stage=(\d+)',text)}
  if result['steps']!=1 or 'SUCCESS COMPLETE WRF' not in text or stages!={2,3,4} or not rows or rows[-1]!=(3,0) or any(f!=0 or not 0<=n<=3 for n,f in rows) or re.search(r'final_update_aborted=[1-9]|outcome=HARD_STAGE_ABORT|\[Newton\] (?:FAILED|NOT CONVERGED)|FATAL CALLED',text,re.I):raise ValueError('step, stages, wrapper or success marker invalid')
  residuals={int(st):float(val) for st,val in re.findall(r'\[Newton\] CONVERGED stage=(\d+)[^\n]*?_rms=([0-9.eE+-]+)',text)};result['stage_residual_scaled_rms']=residuals
  if set(residuals)!={2,3,4} or any(not math.isfinite(v) or not 0<=v<1e-9 for v in residuals.values()):raise ValueError('stage residuals exceed registered threshold')
  result['model_completed']=True;phase='configuration_mismatch';cfg=re.findall(r'\[CONFIG\] reuse_linearization = ([01])\b',text)
  if not cfg or any(x!='0' for x in cfg) or '[CONFIG] internal_fp64 = true' not in text or '[CONFIG] internal_fp64_state_carry = true' not in text:raise ValueError('OFF/FP64 carry config mismatch')
  phase='resource_postrun_RSS'
  if result['rank_peak_rss_bytes'] is None or result['rank_peak_rss_bytes']>CAP:raise ValueError('rank RSS missing or over 4 GiB')
  phase='timing_ineligible'
  if abs(result['clock_gap_seconds'])>=1:raise ValueError('wall/monotonic gap >=1 second')
  phase='missing_artifact'
  for stamp in ('0001-01-01_00:00:00','0001-01-01_00:10:00'):
   if not (d/('wrfout_d01_'+stamp)).is_file() or not (d/('wrfoutReady_d01_'+stamp)).is_file():raise ValueError('history/ready missing '+stamp)
  row={'label':label,'path':str(d),'method':'ARK324','h_seconds':600,'input_sha256':sha(d/'wrfinput_d01'),'executable_sha256':sha(exe),'executable_path':exe,'namelist_sha256':sha(d/'namelist.input')};contract=intent['analysis_contract'];phase='numeric_admissibility';loaded=helper._load_run(row,R,contract);result['output_check']={k:loaded[k] for k in ('grid','physical_minima_over_all_frames','physical_minimum_total_pressure_Pa','timestamps','step_logs')}
  phase='initial_state';ref=helper.load_initial_state(R/'reference',contract['expected_grid'],dry_case_no_qv=True,use_theta_m=1,source='wrfout');cur=helper.load_initial_state(d,contract['expected_grid'],dry_case_no_qv=True,use_theta_m=1,source='wrfout');result['t0_gate']=helper.compare_initial_states({'rk3':ref,label:cur},require_input_hash=True)
  phase='linear_eligibility';result['preflight']=record(text,'PRESSURE_PREFLIGHT');result['linear']=record(text,'PRESSURE_LINEAR');check_linear(result['preflight'],result['linear'],flavor)
  if '[PRECOND REFINE EVENT] disable on defect ratio' in text:raise ValueError('preconditioner defect disable event')
  if previous:compare_linear(previous[0]['preflight'],previous[0]['linear'],result['preflight'],result['linear'])
  phase='A4_observation';result['a4']=record(text,'PRESSURE_A4');result['capture_fraction']=a4_checks(result['a4'],result['linear'],result['preflight']);result['pair']=pair_info(d,result['a4']);result['existing_jvp_summaries']=jvp_summary(text)
  if previous:
   first=previous[0]['pair'];other=pathlib.Path(first['path']).read_bytes();new=pathlib.Path(result['pair']['path']).read_bytes();n=first['numel']
   if result['pair']['numel']!=n or other[16:16+8*n]!=new[16:16+8*n]:raise ValueError('V0 bytes differ from A0')
  phase='parity_acceptance'
  if flavor=='baseline':
   result['off_parity']=all_numeric_bitwise(intent['be661_off_endpoint']['path'],d/'wrfout_d01_0001-01-01_00:10:00')
   if not result['off_parity']['passed']:raise ValueError('A differs from archived OFF numeric arrays')
  else:
   a0=previous[0];bd=R/'runs/A0';brow={'label':'A0','path':str(bd),'method':'ARK324','h_seconds':600,'input_sha256':sha(bd/'wrfinput_d01'),'executable_sha256':a0['executable_sha256'],'executable_path':a0['executable_path'],'namelist_sha256':sha(bd/'namelist.input')};bloaded=helper._load_run(brow,R,contract);result['owned_field_comparison']=owned_comparison(bloaded['frames']['0001-01-01_00:10:00'],loaded['frames']['0001-01-01_00:10:00'])
   if any(v['violations'] for v in result['owned_field_comparison'].values()):raise ValueError('B owned fields exceed registered tolerance')
  if label in ('B1','A1'):
   earlier=previous[1] if label=='B1' else previous[0];result['same_flavor_pair_file_byte_equal']=pathlib.Path(earlier['pair']['path']).read_bytes()==pathlib.Path(result['pair']['path']).read_bytes()
  result['executable_path']=exe;result['a4_eligible']=True;result['status']='completed'
 except Exception as e:result['failure_class']=locals().get('phase','process_failure');result['failure_reason']=str(e)
 finally:
  result['disk_free_end']=shutil.disk_usage(R).free;result['postrun_disk_floor_met']=result['disk_free_end']>=FLOOR
  if not result['postrun_disk_floor_met'] and result['status']=='completed':result['status']='failed_or_incomplete';result['a4_eligible']=False;result['failure_class']='resource_postrun_disk';result['failure_reason']='postrun free disk below 5 GiB'
  if label=='A1' and result['status']=='completed':
   ratios=[previous[0]['linear']['interval_from_newton_entry_ms']/previous[1]['linear']['interval_from_newton_entry_ms'],result['linear']['interval_from_newton_entry_ms']/previous[2]['linear']['interval_from_newton_entry_ms']]
   median=sum(ratios)/2;passed=all(x>=2 for x in ratios) and median>=2
   result['block_decision']={'mirror_A0_B0_and_A1_B1_ratios':ratios,'median_ratio':median,'threshold_each_and_median':2,'outcome':'next_step_may_be_considered' if passed else 'NO-GO','scope':'instrumented first point only; next limited step may be considered scientifically; no automatic Gate B'}
  (d/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:result.get(k) for k in ('case','status','return_code','failure_class','failure_reason')},indent=2));return 0 if result['status']=='completed' else 2
if __name__=='__main__':raise SystemExit(main())
