#!/usr/bin/env python3
"""One sealed B-only A5 numerical diagnostic; no retries or timing claim."""
import hashlib,json,math,os,pathlib,re,shutil,struct,subprocess,sys,time
import numpy as np
import benchmark_wrf_accuracy_cost as helper
from base_screen_reference import all_numeric_bitwise
from run_a4 import record,check_linear,compare_linear
R=pathlib.Path(__file__).resolve().parent;FLOOR=5*1024**3;CAP=4*1024**3;PAYLOAD_CAP=256*1024**2

def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()

def read_records(path):
 data=path.read_bytes()
 if len(data)<16 or data[:8]!=b'SDIRKA53':raise ValueError('invalid SDIRKA53 header: '+str(path))
 count=struct.unpack_from('<Q',data,8)[0]
 if count>128:raise ValueError('excess SDIRKA53 records')
 pos=16;records={}
 for _ in range(count):
  if pos+80>len(data):raise ValueError('truncated SDIRKA53 record')
  name=data[pos:pos+32].split(b'\0',1)[0].decode('ascii');item,ndim,*rest=struct.unpack_from('<II4qQ',data,pos+32);dims=rest[:4];nbytes=rest[4];pos+=80
  if not name or name in records or item not in (4,8) or ndim>4 or any(x<0 for x in dims[:ndim]) or any(x!=0 for x in dims[ndim:]):raise ValueError('invalid SDIRKA53 metadata')
  count_elements=math.prod(dims[:ndim])
  if nbytes!=item*count_elements or pos+nbytes>len(data):raise ValueError('invalid SDIRKA53 record size')
  x=np.frombuffer(data,dtype='<f4' if item==4 else '<f8',count=count_elements,offset=pos).reshape(tuple(dims[:ndim]))
  if not np.isfinite(x).all():raise ValueError('nonfinite payload record '+name)
  records[name]=x;pos+=nbytes
 if pos!=len(data):raise ValueError('extra SDIRKA53 data')
 return records

def capture_and_identity(d,intent,prior):
 raw=d/'pressure_a5_direction.bin';actions=d/'pressure_a5_actions.bin';a4=d/'pressure_a4_first_pair.bin';old=pathlib.Path(intent['A4_B0_pair']['path'])
 if not raw.is_file() or not actions.is_file():raise ValueError('missing early direction or evaluated actions fixture')
 if not a4.is_file():raise ValueError('missing transient A4 supporting pair')
 total=raw.stat().st_size+actions.stat().st_size+a4.stat().st_size
 if total>PAYLOAD_CAP:raise ValueError('combined A5 fixture and A4 supporting pair exceed 256 MiB')
 x=read_records(raw);y=read_records(actions)
 expected={'U_eval','P0z_physical','v_scaled','z_scaled','Az_scaled','S_diag','eos_ph','eos_t','eos_mu','mu_base','alb','p_base','c1h','c2h','rdnw','alt','p_pert','c1f','c2f','rdn','msftx','msfty','msfux','msfuy','msfvx','msfvy','ground_phi','cqv_at_v','level_mass_v','velocity_mass_v','alpha_v','msf_ratio_v','dpy_primary','profile_q','profile_etheta','profile_emu','profile_gz_edge','profile_alpha','profile_beta','profile_H','profile_nu','profile_dnw','profile_p_ref','profile_alpha_ref','profile_theta_ref','constants','flags'}
 if set(x)!=expected or set(y)!={'Qz_physical','V_Jphi','V_Jp','V_Jalpha','V_Jfwad','V_P0_difference'}:raise ValueError('A5 typed payload record manifest mismatch')
 flags=x['flags'].reshape(-1)
 if len(flags)!=8 or list(flags[:4])!=[1,0,0,1] or flags[5]!=1 or flags[6]!=1:raise ValueError('dry captured native primary flags invalid')
 if not a4.is_file() or a4.read_bytes()!=old.read_bytes():raise ValueError('physical V0/Az first-pair bytes differ from historical B0')
 pair=a4.read_bytes()
 if len(pair)<16 or pair[:8]!=b'SDIRKA41':raise ValueError('invalid current A4 pair')
 n=struct.unpack_from('<Q',pair,8)[0]
 if len(pair)!=16+16*n:raise ValueError('invalid A4 pair length')
 S=x['S_diag'].reshape(-1);v=x['v_scaled'].reshape(-1);z=x['z_scaled'].reshape(-1);az=x['Az_scaled'].reshape(-1)
 if any(t.dtype!=np.dtype('<f8') or t.size!=n for t in (S,v,z,az)):raise ValueError('scaled triplet/S must be matching FP64 vectors')
 if not np.all(S>0):raise ValueError('scaling vector must be strictly positive')
 if (S*v).astype('<f8',copy=False).tobytes()!=pair[16:16+8*n] or (S*az).astype('<f8',copy=False).tobytes()!=pair[16+8*n:]:raise ValueError('A5 scaled triplet does not reproduce A4 physical V0/Az bytes')
 pz=x['P0z_physical'].reshape(-1)
 if pz.dtype!=np.dtype('<f8') or pz.size!=n:raise ValueError('P0 forward action must be matching FP64 vector')
 vp=S*v;zp=S*z;ap=S*az;qs=v-pz/S;vs_norm=float(np.linalg.norm(v));inverse=float(np.linalg.norm(qs)/vs_norm) if vs_norm>0 else float('inf')
 if not math.isfinite(inverse) or inverse>1e-8:raise ValueError('independent scaled inverse reconstruction exceeds 1e-8')
 physical_closure=(vp-ap)-(vp-pz)-(pz-ap);scaled_pz=pz/S;scaled_closure=(v-az)-(v-scaled_pz)-(scaled_pz-az);blocks=prior['a4']['blocks'];independent_blocks=[]
 if len(blocks)!=6:raise ValueError('historical packed block layout missing')
 for b in blocks:
  sl=slice(b['start'],b['start']+b['size']);scale=max(float(np.max(np.abs(q[sl]))) for q in (vp,pz,ap));sscale=max(float(np.max(np.abs(q[sl]))) for q in (v,scaled_pz,az));err=float(np.max(np.abs(physical_closure[sl])));serr=float(np.max(np.abs(scaled_closure[sl])));budget=128*np.finfo(np.float64).eps*scale;sbudget=128*np.finfo(np.float64).eps*sscale
  if err>budget or serr>sbudget:raise ValueError('independent packed closure exceeds bound: '+b['name'])
  independent_blocks.append({'name':b['name'],'closure_max':err,'closure_budget':budget,'scaled_closure_max':serr,'scaled_closure_budget':sbudget})
 ny,nz,nx=x['eos_t'].shape;vb=blocks[1];shape=(ny+1,nz,nx)
 if vb['name']!='rv' or math.prod(shape)!=vb['size']:raise ValueError('historical V block shape mismatch')
 core=lambda q:q[1:ny-1,:,0:nx-1]
 rv=(pz-ap)[vb['start']:vb['start']+vb['size']].reshape(shape);p0diff=(zp-pz)[vb['start']:vb['start']+vb['size']].reshape(shape)
 if y['V_P0_difference'].shape!=shape or not np.array_equal(y['V_P0_difference'],p0diff):raise ValueError('saved V P0 difference inconsistent')
 fw=y['V_Jfwad'];phi=y['V_Jphi'];press=y['V_Jp'];alpha=y['V_Jalpha']
 if any(q.shape!=shape for q in (fw,phi,press,alpha)):raise ValueError('saved directional V shape mismatch')
 aderr=np.abs(core((phi+press+alpha)-fw));adbound=1e-12+1e-9*np.abs(core(fw));ad_ok=bool(np.all(aderr<=adbound));rvcore=core(rv);vcore=core(vp[vb['start']:vb['start']+vb['size']].reshape(shape));a=float(x['constants'].reshape(-1)[-1]);tv=core(a*fw-p0diff);rn=float(np.linalg.norm(rvcore));vn=float(np.linalg.norm(vcore));signal=vn>0 and rn>0 and rn/vn>=1e-8;score=float((np.linalg.norm(rvcore-tv)/rn)**2) if signal else None
 if not ad_ok:raise ValueError('independent saved-action AD pointwise check failed')
 independent={'inverse_scaled_relative':inverse,'block_closures':independent_blocks,'saved_action_AD_pointwise_ok':ad_ok,'signal_ratio':rn/vn if vn>0 else None,'signal_eligible':signal,'unit_coefficient_squared_ratio':score,'FD_scope':'source-reported pointwise flags; FD arrays not serialized'}
 return {'direction_path':str(raw),'direction_sha256':sha(raw),'direction_bytes':raw.stat().st_size,'actions_path':str(actions),'actions_sha256':sha(actions),'actions_bytes':actions.stat().st_size,'three_file_combined_bytes':total,'A4_transient_pair_path':str(a4),'A4_transient_pair_sha256':sha(a4),'A4_pair_bytes_match_B0':True,'A5_v_and_Az_match_B0_bytes':True,'z_scope':'newly captured; no historical Z0 comparator','direction_record_names':sorted(x),'action_record_names':sorted(y),'independent_checks':independent}

def interpret_a5(obs,decomp):
 if obs.get('status')!='observed' or obs.get('cost_eligible') is not False:raise ValueError('A5 helper did not produce observed cost-ineligible record')
 if obs.get('artifact')!='pressure_a5_direction.bin' or obs.get('actions_artifact')!='pressure_a5_actions.bin' or not str(obs.get('artifact_format','')).startswith('SDIRKA53:'):raise ValueError('A5 output manifest mismatch')
 if obs.get('fd_steps_seconds')!=[.01,.001,.0001] or obs.get('gate_threshold_squared_ratio')!=.5 or obs.get('minimum_signal_ratio')!=1e-8 or obs.get('inverse_scaled_limit')!=1e-8:raise ValueError('A5 thresholds differ from registration')
 for k in ('inverse_scaled_relative','rV_norm','vV_norm','unit_squared_ratio'):
  if not isinstance(obs.get(k),(int,float)) or isinstance(obs[k],bool) or not math.isfinite(obs[k]):raise ValueError('missing/nonfinite A5 value '+k)
 if obs['base_state_branch'] is not True or obs['use_wrf_mass_flux'] is not True:raise ValueError('unsupported dry native primary branch')
 blocks=decomp.get('blocks')
 if not isinstance(blocks,list) or len(blocks)!=6:raise ValueError('missing packed closure blocks')
 closure=True
 for b in blocks:
  for k in ('closure_max','closure_budget','scaled_closure_max','scaled_closure_budget'):
   if not isinstance(b.get(k),(int,float)) or not math.isfinite(b[k]) or b[k]<0:raise ValueError('invalid closure '+k)
  closure=closure and b['closure_max']<=b['closure_budget'] and b['scaled_closure_max']<=b['scaled_closure_budget']
 fds=obs.get('fd_pointwise_ok');signal=obs['vV_norm']>0 and obs['rV_norm']>0 and obs['rV_norm']/obs['vV_norm']>=1e-8;checks={'inverse':0<=obs['inverse_scaled_relative']<=1e-8 and obs.get('inverse_scaled_ok') is True,'closure':closure and decomp.get('packed_closure_ok') is True and obs.get('packed_closure_ok') is True,'AD':obs.get('ad_pointwise_ok') is True,'FD':isinstance(fds,list) and len(fds)==3 and all(type(x) is bool for x in fds) and sum(fds)>=2,'signal':signal}
 if not all(checks.values()) or obs.get('eligible') is not True:return {'outcome':'INCONCLUSIVE','checks':checks,'cost_eligible':False,'V_row_correction_authorized':False}
 score=obs['unit_squared_ratio']
 if score<0:raise ValueError('negative unit-coefficient squared ratio')
 expected=score<=.5
 if obs.get('gate_pass') is not expected:raise ValueError('A5 gate flag contradicts registered score threshold')
 return {'outcome':'ATTRIBUTION_THRESHOLD_MET' if expected else 'ATTRIBUTION_GATE_FAILED_STOP','checks':checks,'unit_coefficient_squared_ratio':score,'cost_eligible':False,'V_row_correction_authorized':False}

def main():
 if len(sys.argv)!=3 or sys.argv[1]!='--go':raise SystemExit('Requires --go PIN_SHA256; no model launch attempted')
 pin=R/'pins.json'
 if sha(pin)!=sys.argv[2]:raise SystemExit('pin manifest mismatch')
 pins=json.loads(pin.read_text());intent=json.loads((R/'intent.json').read_text())
 for p,h in pins.items():
  if sha(p)!=h:raise SystemExit('pin mismatch '+p)
 free=shutil.disk_usage(R).free
 if free<FLOOR:raise SystemExit('insufficient prelaunch disk')
 power=subprocess.run(['/usr/sbin/ioreg','-r','-k','AppleClamshellState','-d','4'],capture_output=True,text=True,check=True,timeout=5).stdout
 if '"AppleClamshellState" = No' not in power:raise SystemExit('clamshell not confirmed open')
 d=R/'run';exe=intent['executable']['path'];tf=d/'rank_rusage.log';cmd=['/opt/homebrew/bin/mpirun','--timeout','360','--report-state-on-timeout','-np','1','--bind-to','none','/usr/bin/time','-l','-o',str(tf),exe]
 if any((d/x).exists() for x in ('attempt.json','launcher.stdout.log','launcher.stderr.log','rank_rusage.log','rsl.out.0000','rsl.error.0000','result.json','pressure_a5_direction.bin','pressure_a5_actions.bin','pressure_a4_first_pair.bin')) or list(d.glob('wrfout_d01*')):raise SystemExit('attempt or output already exists')
 with (d/'attempt.json').open('x') as f:json.dump({'one_attempt':True,'no_retry':True,'start_epoch':time.time(),'disk_free_start':free,'clamshell_open':True},f,indent=2)
 env={k:v for k,v in os.environ.items() if not k.startswith(('WRF_SDIRK3_','WRF_PARITY_'))};env.update({'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','VECLIB_MAXIMUM_THREADS':'1','MKL_NUM_THREADS':'1','WRF_SDIRK3_STAGE_OPERAND_DIAG':'0','WRF_SDIRK3_EW_ETA_INITIAL':'0.1','WRF_SDIRK3_GMRES_ESTIMATE_PROBE':'0'})
 start=time.monotonic();wall=time.time();result={'status':'failed_or_incomplete','diagnostic_outcome':'INCONCLUSIVE','cost_eligible':False,'gate_b_authorized':False,'V_row_correction_authorized':False,'model_completed':False,'failure_class':None,'failure_reason':None,'rss_scope':'postrun rank maxrss; excludes launcher','thread_count_actual':'not independently measured'};rc=None
 try:
  with (d/'launcher.stdout.log').open('xb') as out,(d/'launcher.stderr.log').open('xb') as err:
   caf=subprocess.Popen(['/usr/bin/caffeinate','-i','-w',str(os.getpid())])
   try:p=subprocess.Popen(cmd,cwd=d,env=env,stdout=out,stderr=err,start_new_session=True);rc=p.wait()
   finally:caf.terminate();caf.wait(timeout=5)
  result.update(return_code=rc,command=cmd,monotonic_elapsed_seconds=time.monotonic()-start,wall_elapsed_seconds=time.time()-wall,environment_overrides={k:env[k] for k in ('OMP_NUM_THREADS','OPENBLAS_NUM_THREADS','VECLIB_MAXIMUM_THREADS','MKL_NUM_THREADS','WRF_SDIRK3_STAGE_OPERAND_DIAG','WRF_SDIRK3_EW_ETA_INITIAL','WRF_SDIRK3_GMRES_ESTIMATE_PROBE')})
  read=lambda x:(d/x).read_text(errors='replace') if (d/x).exists() else '';out=read('rsl.out.0000');text='\n'.join(read(x) for x in ('rsl.out.0000','rsl.error.0000','launcher.stdout.log','launcher.stderr.log'));phase='process_failure'
  result['available_artifacts']={x:{'bytes':(d/x).stat().st_size,'sha256':sha(d/x)} for x in ('pressure_a5_direction.bin','pressure_a5_actions.bin','pressure_a4_first_pair.bin') if (d/x).is_file()}
  result['raw_artifact_combined_bytes']=sum(v['bytes'] for v in result['available_artifacts'].values())
  result['raw_artifact_cap_met']=result['raw_artifact_combined_bytes']<=PAYLOAD_CAP
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
  if result['rank_peak_rss_bytes'] is None or result['rank_peak_rss_bytes']>CAP:raise ValueError('rank RSS missing or above 4 GiB')
  phase='resource_postrun_payload'
  if not result['raw_artifact_cap_met']:raise ValueError('combined three-file raw artifact footprint exceeds 256 MiB')
  phase='missing_artifact'
  for stamp in ('0001-01-01_00:00:00','0001-01-01_00:10:00'):
   if not (d/('wrfout_d01_'+stamp)).is_file() or not (d/('wrfoutReady_d01_'+stamp)).is_file():raise ValueError('history/ready missing '+stamp)
  row={'label':'A5','path':str(d),'method':'ARK324','h_seconds':600,'input_sha256':sha(d/'wrfinput_d01'),'executable_sha256':sha(exe),'executable_path':exe,'namelist_sha256':sha(d/'namelist.input')};contract=intent['analysis_contract'];phase='numeric_admissibility';loaded=helper._load_run(row,R,contract);result['output_check']={k:loaded[k] for k in ('grid','physical_minima_over_all_frames','physical_minimum_total_pressure_Pa','timestamps','step_logs')}
  phase='initial_state';ref=helper.load_initial_state(R/'reference',contract['expected_grid'],dry_case_no_qv=True,use_theta_m=1,source='wrfout');cur=helper.load_initial_state(d,contract['expected_grid'],dry_case_no_qv=True,use_theta_m=1,source='wrfout');result['t0_gate']=helper.compare_initial_states({'rk3':ref,'A5':cur},require_input_hash=True)
  phase='linear_numerical_validity';result['preflight']=record(text,'PRESSURE_PREFLIGHT');result['linear']=record(text,'PRESSURE_LINEAR');check_linear(result['preflight'],result['linear'],'candidate');prior=json.loads(pathlib.Path(intent['A4_B0_result']['path']).read_text());compare_linear(prior['preflight'],prior['linear'],result['preflight'],result['linear'])
  if '[PRECOND REFINE EVENT] disable on defect ratio' in text or 'LOCKING to identity for remainder of GMRES solve' in text or 'WARN-ONLY mode: keeping preconditioner output (fallback ' in text:raise ValueError('preconditioner guard or defect-disable event')
  telemetry=[line for line in text.splitlines() if '[SOLVER TELEMETRY]' in line]
  fd_counts=[re.findall(r'\bjvp_fd_calls=(\d+)',line) for line in telemetry]
  result['whole_step_jvp_fd_calls']=[int(x[0]) for x in fd_counts if len(x)==1]
  if len(telemetry)!=6 or len(result['whole_step_jvp_fd_calls'])!=6 or any(result['whole_step_jvp_fd_calls']):raise ValueError('missing telemetry or nonzero forward-AD to FD fallback')
  phase='endpoint_parity';result['B0_endpoint_parity']=all_numeric_bitwise(intent['A4_B0_endpoint']['path'],d/'wrfout_d01_0001-01-01_00:10:00')
  if not result['B0_endpoint_parity']['passed']:raise ValueError('A5 changes B0 numeric endpoint arrays')
  phase='A5_context_or_helper';result['capture']=record(text,'PRESSURE_A5_CAPTURE');result['decomposition']=record(text,'PRESSURE_A5_DECOMPOSITION');result['A5']=record(text,'PRESSURE_A5')
  if result['capture'].get('status')!='captured' or result['capture'].get('path')!='pressure_a5_direction.bin' or result['capture'].get('missing')!=[]:raise ValueError('missing/failed A5 raw capture context')
  result['payload']=capture_and_identity(d,intent,prior);result['attribution']=interpret_a5(result['A5'],result['decomposition']);independent=result['payload']['independent_checks'];reported=result['A5'];
  if [x.get('name') for x in result['decomposition'].get('blocks',[])]!=[x['name'] for x in prior['a4']['blocks']]:raise ValueError('reported closure block order differs from historical packed layout')
  if not math.isclose(independent['inverse_scaled_relative'],reported['inverse_scaled_relative'],rel_tol=1e-9,abs_tol=1e-12):raise ValueError('independent inverse reconstruction disagrees with helper')
  if independent['signal_eligible'] and (not math.isclose(independent['unit_coefficient_squared_ratio'],reported['unit_squared_ratio'],rel_tol=1e-10,abs_tol=1e-12)):raise ValueError('independent unit-coefficient score disagrees with A5 report')
  if independent['signal_eligible']!=result['attribution']['checks']['signal']:raise ValueError('independent signal gate disagrees')
  result['diagnostic_outcome']=result['attribution']['outcome'];result['status']='completed' if result['diagnostic_outcome']!='INCONCLUSIVE' else 'inconclusive'
 except Exception as e:result['failure_class']=locals().get('phase','process_failure');result['failure_reason']=str(e)
 finally:
  result['disk_free_end']=shutil.disk_usage(R).free;result['postrun_disk_floor_met']=result['disk_free_end']>=FLOOR
  if not result['postrun_disk_floor_met'] and result['status']=='completed':result['status']='failed_or_incomplete';result['diagnostic_outcome']='INCONCLUSIVE';result['failure_class']='resource_postrun_disk';result['failure_reason']='postrun free disk below 5 GiB'
  (d/'result.json').write_text(json.dumps(result,indent=2)+'\n')
 print(json.dumps({k:result.get(k) for k in ('status','diagnostic_outcome','return_code','failure_class','failure_reason')},indent=2));return 0 if result['status']=='completed' else 2
if __name__=='__main__':raise SystemExit(main())
