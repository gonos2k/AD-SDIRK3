#!/usr/bin/env python3
"""Offline saved-control validation; never launches a model or alters prior results."""
import hashlib,json,math,pathlib,re,struct,sys,importlib.util
import numpy as np
import benchmark_wrf_accuracy_cost as helper
from base_screen_reference import all_numeric_bitwise
from run_a4 import positive,record,compare_linear,jvp_summary
from run_a5 import read_records
R=pathlib.Path(__file__).resolve().parent;PAYLOAD_CAP=192*1024**2
spec=importlib.util.spec_from_file_location('observer_checks',R/'observer_checks.draft.py');observer=importlib.util.module_from_spec(spec);spec.loader.exec_module(observer)
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
def main():
 if len(sys.argv)!=3:raise SystemExit('usage: validate_saved_case.py EXPECTED_PINS_SHA EXPECTED_RESULT_SHA')
 d=R/'runs/control';ip=R/'control_intent.json';pp=R/'control_pins.json';op=d/'result.json';intent=json.loads(ip.read_text());original=json.loads(op.read_text());assert sha(pp)==sys.argv[1] and sha(op)==sys.argv[2]
 for p,h in json.loads(pp.read_text()).items():assert sha(pathlib.Path(p))==h,p
 assert original['status']=='failed_or_incomplete' and original['decision']=='INCONCLUSIVE' and original['model_completed'] is True and original['return_code']==0 and original['failure_class']=='linear_validity' and original['failure_reason']=="name 'positive' is not defined"
 assert original['source_commit']==intent['source_commit'] and original['executable_sha256']==sha(pathlib.Path(intent['executable']['path']))
 assert original['postrun_disk_floor_met'] is True and original['rank_peak_rss_bytes'] is not None and original['rank_peak_rss_bytes']<=4*1024**3 and abs(original['clock_gap_seconds'])<1
 attempt=json.loads((d/'attempt.json').read_text());assert attempt['disk_free_start']>=5*1024**3 and attempt['one_attempt'] and attempt['no_retry']
 read=lambda x:(d/x).read_text(errors='replace') if (d/x).exists() else '';out=read('rsl.out.0000');text='\n'.join(read(x) for x in ('rsl.out.0000','rsl.error.0000','launcher.stdout.log','launcher.stderr.log'))
 rows=[tuple(map(int,x)) for x in re.findall(r'Tile\s+1:\s+Steps=\s*(\d+),\s+Failed=\s*(\d+)',out)];stages={int(x) for x in re.findall(r'\[Newton\] CONVERGED stage=(\d+)',text)}
 assert len(re.findall(r'Timing for main:',out))==1 and 'SUCCESS COMPLETE WRF' in text and stages=={2,3,4} and rows and rows[-1]==(3,0) and all(f==0 and 0<=n<=3 for n,f in rows) and not re.search(r'final_update_aborted=[1-9]|outcome=HARD_STAGE_ABORT|\[Newton\] (?:FAILED|NOT CONVERGED)|FATAL CALLED',text,re.I)
 residuals={int(k):float(v) for k,v in re.findall(r'\[Newton\] CONVERGED stage=(\d+)[^\n]*?_rms=([0-9.eE+-]+)',text)};assert set(residuals)=={2,3,4} and all(math.isfinite(v) and 0<=v<1e-9 for v in residuals.values())
 cfg=re.findall(r'\[CONFIG\] reuse_linearization = ([01])\b',text);assert cfg and all(x=='0' for x in cfg) and '[CONFIG] internal_fp64 = true' in text and '[CONFIG] internal_fp64_state_carry = true' in text
 for stamp in ('0001-01-01_00:00:00','0001-01-01_00:10:00'):assert (d/('wrfout_d01_'+stamp)).is_file() and (d/('wrfoutReady_d01_'+stamp)).is_file()
 contract=intent['analysis_contract'];exe=intent['executable']['path'];row={'label':'control','path':str(d),'method':'ARK324','h_seconds':600,'input_sha256':sha(d/'wrfinput_d01'),'executable_sha256':sha(pathlib.Path(exe)),'executable_path':exe,'namelist_sha256':sha(d/'namelist.input')};loaded=helper._load_run(row,R,contract);output={k:loaded[k] for k in ('grid','physical_minima_over_all_frames','physical_minimum_total_pressure_Pa','timestamps','step_logs')}
 ref=helper.load_initial_state(R/'reference',contract['expected_grid'],dry_case_no_qv=True,use_theta_m=1,source='wrfout');cur=helper.load_initial_state(d,contract['expected_grid'],dry_case_no_qv=True,use_theta_m=1,source='wrfout');t0=helper.compare_initial_states({'rk3':ref,'control':cur},require_input_hash=True)
 pre=record(text,'PRESSURE_PREFLIGHT');linear=record(text,'PRESSURE_LINEAR');assert linear['flavor']=='reference_p0';diagnostic_check_linear(pre,dict(linear),'candidate');assert pre['block_scale_requested']==0 and linear['stopping_metric_name']=='unweighted_scaled_L2' and pre.get('torch_intra_op_threads')==1 and pre.get('torch_inter_op_threads')==1
 assert not any(x in text for x in ('[PRECOND REFINE EVENT] disable on defect ratio','LOCKING to identity for remainder of GMRES solve','WARN-ONLY mode: keeping preconditioner output (fallback '));telemetry=[line for line in text.splitlines() if '[SOLVER TELEMETRY]' in line];fd=[re.findall(r'\bjvp_fd_calls=(\d+)',line) for line in telemetry];assert len(telemetry)==6 and all(len(x)==1 for x in fd)
 a4=record(text,'PRESSURE_A4');jvp=jvp_summary(text);assert a4.get('eligible') is True and a4.get('timer_overlap') is False and a4.get('nested_scope') is False
 capture=record(text,'PRESSURE_A5_CAPTURE');decomposition=record(text,'PRESSURE_A5_DECOMPOSITION');legacy=record(text,'PRESSURE_A5');assert capture.get('status')=='captured' and capture.get('missing')==[] and legacy.get('status')=='observed' and legacy.get('ad_pointwise_ok') is True and all(legacy.get(k)==0 for k in ('primal_dpy_max','primal_level_mass_max','primal_velocity_mass_max','primal_alpha_v_max','primal_ratio_max','primal_alpha_max','primal_pressure_max'));payload=validate_payload(d,'control')
 old=json.loads(pathlib.Path(intent['archived_arm_result']['path']).read_text());compare_linear(old['preflight'],old['linear'],pre,linear);old_direction=read_records(pathlib.Path(intent['archived_arm_direction']['path']));new_direction=read_records(d/'pressure_a5_direction.bin')
 for k in ('v_scaled','z_scaled','Az_scaled','S_diag'):
  a=old_direction[k];b=new_direction[k];assert a.shape==b.shape and a.dtype==b.dtype and a.tobytes()==b.tobytes(),k
 observation=observer.validate(text,d,[d/'pressure_a5_direction.bin',d/'pressure_a5_actions.bin',d/'pressure_a4_first_pair.bin']);parity=all_numeric_bitwise(intent['archived_arm_endpoint']['path'],d/'wrfout_d01_0001-01-01_00:10:00');assert parity['passed']
 assert sha(op)==sys.argv[2] and sha(pp)==sys.argv[1]
 artifact_names=('result.json','attempt.json','rsl.out.0000','rsl.error.0000','launcher.stdout.log','launcher.stderr.log','rank_rusage.log','pressure_a5_direction.bin','pressure_a5_actions.bin','pressure_a4_first_pair.bin',*observer.NAMES,'wrfout_d01_0001-01-01_00:00:00','wrfout_d01_0001-01-01_00:10:00','wrfoutReady_d01_0001-01-01_00:00:00','wrfoutReady_d01_0001-01-01_00:10:00')
 result={'status':'PASSED','scope':'offline completion of the same registered control gates; original INCONCLUSIVE result unchanged','source_commit':intent['source_commit'],'original_result_sha256':sha(op),'control_pins_sha256':sha(pp),'sdk_addendum_sha256':sha(R/'current_sdk_binding_addendum.json'),'model_run':False,'stage_residual_scaled_rms':residuals,'output_check':output,'t0_gate':t0,'preflight':pre,'linear':linear,'telemetry_fd_calls':[int(x[0]) for x in fd],'a4':a4,'existing_jvp_summaries':jvp,'payload':payload,'observer':observation,'first_point_v_z_Az_S_bytes_equal':True,'archived_arm_endpoint_parity':parity,'original_artifact_sha256':{n:sha(d/n) for n in artifact_names}}
 dest=R/'control_postvalidation.json';assert not dest.exists();dest.write_text(json.dumps(result,indent=2)+'\n');print(sha(dest))
if __name__=='__main__':main()
