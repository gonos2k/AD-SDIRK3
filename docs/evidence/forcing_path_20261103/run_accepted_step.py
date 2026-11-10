#!/usr/bin/env python3
"""Single terminal h600 operator-gap diagnostic; not a forecast regression or performance gate.
Usage: run_accepted_step.py TARGET_HEAD REGISTRATION_JSON REGISTRATION_SHA256
Registration must contain status=registered, source_commit=TARGET_HEAD, maximum_native_attempts=1,
retry=false, immutable_inputs mapping of absolute paths to sha256, including this script and test receipt.
"""
import hashlib,json,math,os,pathlib,re,shutil,signal,subprocess,sys,time
from collections import Counter
E=pathlib.Path(__file__).resolve().parent; W=pathlib.Path('/private/tmp/forcing-path-20261103')
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
 assert target=='bc0f66461efddded7ffd181265d44f7e6caa7add'==receipt['source_commit']
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
 assert '-DWRF_SDIRK3_ACCEPTED_STEP_PROBE' in receipt['newton_argv']
 return link
def clock_triplet():
 m1=time.monotonic(); wall=time.time(); m2=time.monotonic(); return {'monotonic_before':m1,'wall':wall,'monotonic_after':m2}
def finite(x): return isinstance(x,(int,float)) and not isinstance(x,bool) and math.isfinite(x)
def validate_probe(d):
 errors=[]
 def require(c,name):
  if not c: errors.append(name)
 if not isinstance(d,dict): return ['missing_probe']
 require(d.get('status')=='completed' and d.get('forecast_complete') is False and d.get('context_scope')=='observed_subset','scope')
 require(d.get('pair_equality')=='numeric_finite' and d.get('mask_scope')=='identity' and d.get('profile')=='mode3_single_tile_fp64_ref_update_off_reuse_off','native_contract')
 require(all(type(d.get(k)) is int and d[k]==v for k,v in {'stage':3,'newton':0,'timestep':0,'solve_ordinal':3,'mu_dtype':7,'mub_dtype':7,'torch_threads':1,'torch_interop_threads':1,'receipt_rhs_delta':0,'receipt_jvp_delta':0,'receipt_preconditioner_delta':0}.items()),'profile_counters')
 require(d.get('receipt_fingerprints_unchanged') is True and d.get('outcome') in ('accepted_trust','accepted_recovery'),'accepted_outcome')
 require(d.get('N')==1080491 and type(d.get('mu_count')) is int and d['mu_count']>0,'packed_dimensions')
 for k in ('scaled_l2_before','scaled_l2_after','native_scaled_rms_before','native_scaled_rms_after','delta_K_l2','delta_K_scaled_l2','mu_min','mu_absmax','mub_min','mub_absmax','B_mu_absmax','K_before_mu_absmax','K_after_mu_absmax','outer_update_alpha','dt','gamma','a','gmres_effective_relative','gmres_raw_relative'):
  require(finite(d.get(k)),k)
 require(d.get('dt')==600 and finite(d.get('a')) and d['a']>0,'coefficient')
 for k in ('S_inv_hash','base_K_hash','base_R_hash','post_K_hash','post_R_hash','reference_hash'):
  require(isinstance(d.get(k),str) and d[k].isdigit(),'hash_'+k)
 require(all(type(d.get(k)) is bool for k in ('direction_modified','trust_attempted','recovery_attempted','grad_mode')),'outcome_metadata')
 require(isinstance(d.get('base_context_hashes'),list) and len(d['base_context_hashes'])>0,'context')
 return errors

def main():
 if len(sys.argv)!=4: sys.exit('usage: run_accepted_step.py TARGET_HEAD REGISTRATION_JSON REGISTRATION_SHA256')
 target,regname,regsha=sys.argv[1:];reg=pathlib.Path(regname)
 assert sha(reg)==regsha; policy=json.loads(reg.read_text()); assert policy['status']=='registered' and policy['source_commit']==target and policy['maximum_native_attempts']==1 and policy['retry'] is False and policy['performance_eligible'] is False
 for required in (__file__,BUILD,LINK,E/'wrf_link.map',E/'helper_test_receipt.json',E/'red_stage3_source_review.json',E/'mass_tube_bound.py',E/'red_mass_floor_point_audit.json',E/'red_projection_forcing_audit.json'):
  assert str(pathlib.Path(required).resolve()) in policy['immutable_inputs']
 for p,h in policy['immutable_inputs'].items(): assert sha(p)==h,p
 receipt=json.loads(BUILD.read_text()); assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain','--untracked-files=all'],cwd=W)
 link=verify_build_link(receipt,target); helper=json.loads((E/'helper_test_receipt.json').read_text()); assert helper['source_commit']==target and receipt['helper_test_receipt_sha256']==sha(E/'helper_test_receipt.json') and helper['compile_returncode']==helper['run_returncode']==0
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
 attempt={'target_head':target,'one_attempt':True,'retry':False,'performance_eligible':False,'registration_sha256':regsha,'build_receipt_sha256':sha(BUILD),'link_receipt_sha256':sha(LINK),'executable_sha256':sha(EXE),'inputs':INPUTS,'disk_free_start':free,'command':cmd,'effective_environment':env,'otool_loads':loads(EXE),'build_version':versions(EXE),'profile':'terminal stage 3 Newton 0 after accepted update; raw principal; no forcing certification'}
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
 markers=[line for line in text.splitlines() if '[ACCEPTED_STEP] status=' in line]
 if rc!=86 or markers!=['[ACCEPTED_STEP] status=completed forecast_complete=false']:errors.append('terminal_exit_or_marker')
 steps=len(re.findall(r'Timing for main:',read('rsl.out.0000')));stages=Counter()
 for name in streams: stages|=Counter(int(x) for x in re.findall(r'\[Newton\] CONVERGED stage=(\d+)',read(name)))
 if steps!=0 or any(stage!=2 for stage in stages) or 'SUCCESS COMPLETE WRF' in text:errors.append('unexpected_forecast_progress')
 probe=None
 try:
  probe=json.loads((run/'accepted_step_probe.json').read_text(),parse_constant=lambda s:(_ for _ in ()).throw(ValueError(s)))
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
 result={'status':'diagnostic_completed' if not errors else 'inconclusive','errors':errors,'return_code':rc,'model_completed':False,'forecast_complete':False,'performance_eligible':False,'exact_forcing_certified':False,'scope':'observed Stage 3 accepted pair in raw-principal profile; no exact forcing or whole-Jacobian claim','steps':steps,'logged_converged_stages':sorted(stages.elements()),'stage_3_completed':False,'mass_tube_bound':'pending offline conditional analysis','markers':markers,'initial_state_comparison':initial,'rank_peak_rss_bytes':peak,'disk_free_end':free_end,'clock_triplets':{'start':start,'end':end},'diagnostic':probe,'artifacts':{p.name:{'sha256':sha(p),'bytes':p.stat().st_size} for p in sorted(run.iterdir()) if p.is_file() and p.name!='result.json'}}
 with (run/'result.json').open('x') as f:json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
 print(json.dumps({'status':result['status'],'errors':errors,'return_code':rc}));return bool(errors)
if __name__=='__main__':sys.exit(main())
