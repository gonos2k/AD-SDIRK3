#!/usr/bin/env python3
"""Single terminal h600 operator-gap diagnostic; not a forecast regression or performance gate.
Usage: run_operator_gap.py TARGET_HEAD REGISTRATION_JSON REGISTRATION_SHA256
Registration must contain status=registered, source_commit=TARGET_HEAD, maximum_native_attempts=1,
retry=false, immutable_inputs mapping of absolute paths to sha256, including this script and test receipt.
"""
import hashlib,json,math,os,pathlib,re,shutil,signal,subprocess,sys,time
from collections import Counter
E=pathlib.Path(__file__).resolve().parent; OE=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/operator-gap-20261103'); W=pathlib.Path('/private/tmp/operator-gap-20261103')
P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101'); OLD=P/'runs/control'
N=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_build')
T=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102/native_terminal')
B=E/'native_build'; BUILD=E/'build_receipt.json'; LINK=E/'link_receipt.json'; EXE=B/'wrf.exe'; FLOOR=5*1024**3; CAP=4*1024**3
INPUTS={'wrfinput_d01':'c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912','namelist.input':'68c2b63dc98de0a1d313919b8c6a57fd383a5bbf8508d4076dae0db15d4589cf'}
TRUST='external/libtorch_wrf/sdirk3/test_trust_model_contract.cpp'
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(4194304),b''): h.update(b)
 return h.hexdigest()
def out(a,**kw): return subprocess.check_output(list(map(str,a)),text=True,**kw).strip()
def versions(p):
 lines=out(['otool','-l',p]).splitlines(); i=next(i for i,x in enumerate(lines) if x.strip()=='cmd LC_BUILD_VERSION'); return lines[i:i+6]
def loads(p): return out(['otool','-L',p]).splitlines()[1:]
def verify_build_link(receipt,target):
 assert target=='acc393912b38c6d83537649a7e75ea485c462db1'==receipt['source_commit']
 for p,h in receipt['source_hashes'].items(): assert sha(W/p)==h,p
 obj=pathlib.Path(receipt['newton_argv'][receipt['newton_argv'].index('-o')+1])
 assert sha(obj)==receipt['newton_object_sha256'] and sha(obj.with_suffix('.d'))==receipt['newton_depfile_sha256']
 assert sha(W/'external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp')==receipt['source_hashes']['external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp']
 link=json.loads(LINK.read_text()); archive=B/'libwrf_sdirk3_libtorch.a'
 assert link['source_commit']==target and link['build_receipt_sha256']==sha(BUILD)
 assert link['archive_path']==str(archive) and sha(archive)==receipt['archive_sha256']==link['archive_sha256']
 assert sha(EXE)==link['executable_sha256'] and sha(E/'wrf_link.map')==link['map_sha256']
 assert sha(E/'link.stdout.log')==link['stdout_sha256'] and sha(E/'link.stderr.log')==link['stderr_sha256']
 assert sha(E/'link_driver_probe.txt')==link['driver_link_probe_sha256']
 assert sha(link['xcrun_ld_path'])==link['xcrun_ld_sha256'] and out(['xcrun','--find','ld'])==link['xcrun_ld_path']
 assert out([link['xcrun_ld_path'],'-v'],stderr=subprocess.STDOUT)==link['xcrun_ld_version'] and out([link['link_argv'][0],'--version'])==link['driver_version']
 assert sum('libwrf_sdirk3_libtorch' in x for x in link['link_argv'])==1 and '-lwrf_sdirk3_libtorch' not in link['link_argv']
 maptext=(E/'wrf_link.map').read_text(errors='strict').splitlines()
 for needle,member in [('wrf::sdirk3::WRFNewtonKrylovSolver::Impl::solve_stage_impl(', 'wrf_sdirk3_newton_solver.o'), ('TileSDIRK3UnifiedSolver::solveImplicitStage(', 'wrf_sdirk3_tile_unified_impl.o')]:
  rows=[]
  for x in link['method_symbol_origins']:
   match=re.fullmatch(r'\s*0x[0-9a-fA-F]+\s+0x[0-9a-fA-F]+\s+\[\s*(\d+)\]\s+(_{1,2}ZN\S+)\s*',x['map_line'])
   if x['demangled'].startswith(needle) and match and '.' not in match.group(2): rows.append((x,match))
  assert len(rows)==1,needle
  row,match=rows[0];expected=str(archive)+'('+member+')'
  assert row['object']==expected and row['map_line'] in maptext and match.group(1)==row['file_id']
  owner=re.compile(r'^\s*\[\s*'+re.escape(row['file_id'])+r'\s*\]\s+'+re.escape(expected)+r'\s*$')
  assert any(owner.fullmatch(line) for line in maptext) and any(owner.fullmatch(line) for line in link['map_archive_member_lines'])
  obj=subprocess.check_output(['ar','-p',str(archive),member])
  if member=='wrf_sdirk3_newton_solver.o': assert hashlib.sha256(obj).hexdigest()==receipt['newton_object_sha256']
 assert '-DWRF_SDIRK3_OPERATOR_GAP_PROBE' in receipt['newton_argv']
 return link
def clock_triplet():
 m1=time.monotonic(); wall=time.time(); m2=time.monotonic(); return {'monotonic_before':m1,'wall':wall,'monotonic_after':m2}
def finite(x): return isinstance(x,(int,float)) and not isinstance(x,bool) and math.isfinite(x)
def validate_probe(d):
 errors=[]
 def require(c,name):
  if not c: errors.append(name)
 if not isinstance(d,dict): return ['missing_probe']
 require(d.get('status')=='completed' and d.get('forecast_complete') is False and d.get('context_scope')=='observed_fixed_inputs_only' and d.get('exact_forcing_certified') is False,'scope')
 require(all(type(d.get(k)) is int and d[k]==v for k,v in {'stage':2,'newton':0,'timestep':0,'solve_ordinal':1,'probe_ad_calls':3,'probe_fd_calls':0,'probe_fallbacks':0,'probe_direct_rhs_calls':22,'probe_total_rhs_calls':25}.items()),'probe_counters')
 require(d.get('probe_rhs_mode_order')=='full,implicit,explicit' and d.get('probe_rhs_modes')==[0,25,0],'probe_rhs_modes')
 for k in ('projection_idempotence_l2','boundary_Q_l2','used_p_Q_error_l2','q_b_scaled_cosine','p_q_scaled_cosine','p_b_scaled_cosine','base_G_scaled_l2','gmres_tolerance','gmres_raw_relative','direction_relative'):
  require(finite(d.get(k)),k)
 handles=d.get('fixed_input_handles')
 require(isinstance(handles,list) and len(handles)>=12 and all(isinstance(x,dict) and isinstance(x.get('name'),str) and isinstance(x.get('defined'),bool) and isinstance(x.get('fnv64'),str) and x['fnv64'].isdigit() for x in handles),'fixed_input_handles')
 if isinstance(handles,list):
  h={x.get('name'):x for x in handles if isinstance(x,dict)}
  require(all(h.get(k,{}).get('defined') is True for k in ('U_stage','U_eval','K','b','x0','S','Sinv','p_base','th_base','mu_base','ph_base')),'required_context')
 require(d.get('mask_scope')=='all_packed_no_halo_mask' and isinstance(d.get('direction_modified'),bool),'mask_and_direction')
 dirs=d.get('directions')
 if not isinstance(dirs,list) or len(dirs)!=3: return errors+['direction_count']
 require([x.get('name') if isinstance(x,dict) else None for x in dirs]==['used_p','Q_seed','boundary_complement'],'direction_order')
 for i,direction in enumerate(dirs):
  if not isinstance(direction,dict): errors.append('direction_invalid');continue
  require(isinstance(direction.get('direction_fnv64'),str) and direction['direction_fnv64'].isdigit(),'direction_hash')
  require(all(finite(direction.get(k)) for k in ('scaled_norm','Q_direction_l2','Ahat_scaled_l2','linear_residual_over_base_G')),'direction_finite')
  samples=direction.get('samples')
  if not isinstance(samples,list) or len(samples)!=3: errors.append('sample_count');continue
  require([x.get('epsilon') if isinstance(x,dict) else None for x in samples]==[1e-2,1e-3,1e-4],'epsilon_order')
  for j,x in enumerate(samples):
   if not isinstance(x,dict): errors.append('sample_invalid');continue
   require(all(finite(x.get(k)) for k in ('gap_scaled_l2','gap_scaled_relative','gap_over_base_G','gap_masked_scaled_l2','gap_masked_scaled_relative','delta_G_plus_scaled_l2','delta_G_minus_scaled_l2','k_lost_components','state_lost_components','k_lost_scaled_norm','state_lost_scaled_norm')),'sample_finite')
   require(x.get('fd_adjacent_scaled_l2') is None if j==0 else finite(x.get('fd_adjacent_scaled_l2')),'adjacent_fd')
 return errors

def main():
 if len(sys.argv)!=4: sys.exit('usage: run_operator_gap.py TARGET_HEAD REGISTRATION_JSON REGISTRATION_SHA256')
 target,regname,regsha=sys.argv[1:];reg=pathlib.Path(regname)
 assert sha(reg)==regsha; policy=json.loads(reg.read_text()); assert policy['status']=='registered' and policy['source_commit']==target and policy['maximum_native_attempts']==1 and policy['retry'] is False and policy['performance_eligible'] is False
 for required in (__file__,BUILD,LINK,E/'wrf_link.map',OE/'helper_test_receipt.json',OE/'red_o2_fd_context_audit.json',OE/'red_o2_path_audit.json',OE/'red_probe_source_review.json',OE/'red_o3_unsupported_audit.json',E/'prior_attempt_scope_failure.json',E/'helper_evidence_reuse.json'):
  assert str(pathlib.Path(required).resolve()) in policy['immutable_inputs']
 for p,h in policy['immutable_inputs'].items(): assert sha(p)==h,p
 receipt=json.loads(BUILD.read_text()); assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain','--untracked-files=all'],cwd=W)
 link=verify_build_link(receipt,target); helper=json.loads((OE/'helper_test_receipt.json').read_text()); assert helper['source_commit']=='85df46805c6129b2ab0c0d86b90f91ef30f8ce0f' and helper['compile_returncode']==helper['run_returncode']==0
 reuse=json.loads((E/'helper_evidence_reuse.json').read_text()); assert receipt['helper_evidence_reuse_sha256']==sha(E/'helper_evidence_reuse.json')
 assert reuse['status']=='PASS' and reuse['current_source_commit']==target and reuse['test_source_commit']==helper['source_commit'] and reuse['rerun'] is False
 assert reuse['helper_receipt_path']==str(OE/'helper_test_receipt.json') and reuse['helper_receipt_sha256']==sha(OE/'helper_test_receipt.json') and reuse['unchanged_source_hashes']==helper['source_hashes']
 for p,h in helper['source_hashes'].items(): assert sha(p)==h
 for p,h in INPUTS.items(): assert sha(OLD/p)==h
 prior=json.loads((T/'build_receipt.json').read_text()); proof=N/'complete_provenance.json';sdk=P/'current_sdk_provenance.json'
 assert sha(proof)==prior['fortran_provenance_sha256'] and sha(sdk)==prior['current_sdk_provenance_sha256']
 pr=json.loads(proof.read_text())
 for p,h in pr['object_module_archive_sha256'].items(): assert sha(N/'source'/p)==h
 for p,h in pr['dylib_sha256'].items():
  if h!='macOS shared cache': assert sha(p)==h
 stack=json.loads(sdk.read_text());assert out(['xcrun','--show-sdk-path'])==stack['SDK_path'] and out(['xcrun','--show-sdk-version'])==stack['SDK_version']
 assert sha('/usr/bin/g++')==stack['compiler_driver_sha256'] and sha(stack['compiler_resolved_path'])==stack['compiler_resolved_sha256'] and out(['/usr/bin/g++','--version'])==stack['compiler_version'] and out(['sw_vers'])==stack['OS']
 assert sorted(loads(EXE))==sorted(loads(P/'native_control/wrf.exe')) and versions(EXE)==versions(P/'native_control/wrf.exe')
 free=shutil.disk_usage(E).free;assert free>=FLOOR
 run=E/'run';run.mkdir()
 for p,h in INPUTS.items(): shutil.copyfile(OLD/p,run/p);assert sha(run/p)==h
 cmd=['/opt/homebrew/bin/mpirun','--timeout','360','--report-state-on-timeout','-np','1','--bind-to','none','/usr/bin/time','-l','-o',str(run/'rank_rusage.log'),str(EXE)]
 env={k:os.environ[k] for k in ('HOME','USER','LOGNAME','TMPDIR','LANG','LC_ALL') if k in os.environ};env['PATH']='/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin';env.update({'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','VECLIB_MAXIMUM_THREADS':'1','MKL_NUM_THREADS':'1','WRF_SDIRK3_STAGE_OPERAND_DIAG':'0','WRF_SDIRK3_EW_ETA_INITIAL':'0.1','WRF_SDIRK3_GMRES_ESTIMATE_PROBE':'0'})
 attempt={'target_head':target,'one_attempt':True,'retry':False,'performance_eligible':False,'registration_sha256':regsha,'build_receipt_sha256':sha(BUILD),'link_receipt_sha256':sha(LINK),'executable_sha256':sha(EXE),'inputs':INPUTS,'disk_free_start':free,'command':cmd,'effective_environment':env,'otool_loads':loads(EXE),'build_version':versions(EXE),'profile':'terminal stage 2 Newton 0 after actual FGMRES direction; raw principal; no forcing certification'}
 with (run/'attempt.json').open('x') as f: json.dump(attempt,f,indent=2);f.write('\n');f.flush();os.fsync(f.fileno())
 errors=[];rc=None;start=clock_triplet()
 try:
  with (run/'launcher.stdout.log').open('xb') as stdout,(run/'launcher.stderr.log').open('xb') as stderr:
   proc=subprocess.Popen(cmd,cwd=run,env=env,stdout=stdout,stderr=stderr,start_new_session=True)
   try:rc=proc.wait(timeout=390)
   except subprocess.TimeoutExpired:
    errors.append('launcher_timeout');os.killpg(proc.pid,signal.SIGTERM)
    try:rc=proc.wait(timeout=5)
    except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);rc=proc.wait()
 except Exception as exc: errors.append('launcher_exception: '+repr(exc))
 end=clock_triplet()
 def read(n):
  p=run/n;return p.read_text(errors='replace') if p.exists() else ''
 streams=('rsl.out.0000','rsl.error.0000','launcher.stdout.log','launcher.stderr.log');text='\n'.join(read(n) for n in streams)
 markers=[line for line in text.splitlines() if '[OPERATOR_GAP] status=' in line]
 if rc!=86 or markers!=['[OPERATOR_GAP] status=completed forecast_complete=false']:errors.append('terminal_exit_or_marker')
 steps=len(re.findall(r'Timing for main:',read('rsl.out.0000')));stages=Counter()
 for name in streams: stages|=Counter(int(x) for x in re.findall(r'\[Newton\] CONVERGED stage=(\d+)',read(name)))
 if steps!=0 or sum(stages.values())!=0 or 'SUCCESS COMPLETE WRF' in text:errors.append('unexpected_forecast_progress')
 probe=None
 try:
  probe=json.loads((run/'operator_gap_probe.json').read_text(),parse_constant=lambda s:(_ for _ in ()).throw(ValueError(s)))
  errors.extend(validate_probe(probe))
 except Exception as exc:errors.append('probe_invalid: '+repr(exc))
 rss=re.findall(r'^\s*(\d+)\s+maximum resident set size\s*$',read('rank_rusage.log'),re.M);peak=int(rss[0]) if len(rss)==1 else None
 if peak is None or peak>CAP:errors.append('rank_rss_missing_or_exceeded')
 free_end=shutil.disk_usage(E).free
 if free_end<FLOOR:errors.append('postrun_disk_floor')
 try:
  assert sha(reg)==regsha and sha(BUILD)==attempt['build_receipt_sha256'] and sha(LINK)==attempt['link_receipt_sha256'] and sha(EXE)==attempt['executable_sha256']
  for p,h in INPUTS.items():assert sha(OLD/p)==h and sha(run/p)==h
  for p,h in policy['immutable_inputs'].items():assert sha(p)==h
  assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain','--untracked-files=all'],cwd=W)
  verify_build_link(receipt,target)
 except Exception as exc:errors.append('postrun_identity: '+repr(exc))
 initial=None
 try:
  sys.path.insert(0,str(P));import benchmark_wrf_accuracy_cost as initial_helper
  rk3=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/noise-cost-coupling-20261031/modal/runs/baseline_rk3')
  states={label:initial_helper.load_initial_state(path,{'nx':40,'ny':80,'nz':64},dry_case_no_qv=True,use_theta_m=1,source='wrfout') for label,path in [('new_terminal',run),('archived_A9_old_binary',OLD),('archived_RK3',rk3)]}
  initial=initial_helper.compare_initial_states(states,require_input_hash=False);assert initial['status']=='passed' and len(initial['fields_checked'])==21
 except Exception as exc:errors.append('initial_state_comparison: '+repr(exc))
 result={'status':'diagnostic_completed' if not errors else 'inconclusive','errors':errors,'return_code':rc,'model_completed':False,'forecast_complete':False,'performance_eligible':False,'exact_forcing_certified':False,'scope':'directional comparison at observed input subset after a raw-principal solve; no complete fixed-operator, exact Jacobian or forcing claim','steps':steps,'completed_implicit_stages':sum(stages.values()),'markers':markers,'initial_state_comparison':initial,'rank_peak_rss_bytes':peak,'disk_free_end':free_end,'clock_triplets':{'start':start,'end':end},'diagnostic':probe,'artifacts':{p.name:{'sha256':sha(p),'bytes':p.stat().st_size} for p in sorted(run.iterdir()) if p.is_file() and p.name!='result.json'}}
 with (run/'result.json').open('x') as f:json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
 print(json.dumps({'status':result['status'],'errors':errors,'return_code':rc}));return bool(errors)
if __name__=='__main__':sys.exit(main())
