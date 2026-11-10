#!/usr/bin/env python3
"""Single terminal h600 operator-gap diagnostic; not a forecast regression or performance gate.
Usage: run_w_runtime.py TARGET_HEAD REGISTRATION_JSON REGISTRATION_SHA256
Registration must contain status=registered, source_commit=TARGET_HEAD, maximum_native_attempts=1,
retry=false, immutable_inputs mapping of absolute paths to sha256, including this script and test receipt.
"""
import hashlib,json,math,os,pathlib,re,shutil,signal,struct,subprocess,sys,time
from collections import Counter
E=pathlib.Path(__file__).resolve().parent; W=pathlib.Path('/Users/yhlee/SDIRK3-review-worktrees/w-runtime-contract-20261110')
P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101'); OLD=P/'runs/control'
N=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_build')
T=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102/native_terminal')
C=E; B=C/'native_build'; BUILD=C/'build_receipt.json'; LINK=C/'link_receipt.json'; EXE=B/'wrf.exe'; FLOOR=5*1024**3; CAP=4*1024**3
INPUTS={'wrfinput_d01':'c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912','namelist.input':'68c2b63dc98de0a1d313919b8c6a57fd383a5bbf8508d4076dae0db15d4589cf'}
OLD_PACKET=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/eos-descent-20261110/run/accepted_step_probe.json')
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
 assert target=='ddc9475fdb0e5cf6ecdb8d32dee6524c9ff5602b'==receipt['source_commit']
 for p,h in receipt['source_hashes'].items(): assert sha(W/p)==h,p
 compiled={pathlib.Path(x['source']).name:x for x in receipt['compiled']}
 assert len(compiled)==len(receipt['compiled']) and 'wrf_sdirk3_newton_solver.cpp' in compiled and 'wrf_sdirk3_tile_unified_impl.cpp' in compiled
 for name,entry in compiled.items():
  obj=pathlib.Path(entry['argv'][entry['argv'].index('-o')+1])
  assert sha(obj)==entry['object_sha256'] and sha(obj.with_suffix('.d'))==entry['depfile_sha256'],name
 assert sha(W/'external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp')==receipt['source_hashes']['external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp']
 link=json.loads(LINK.read_text()); archive=B/'libwrf_sdirk3_libtorch.a'
 assert link['source_commit']==target and link['build_receipt_sha256']==sha(BUILD)
 assert link['archive_path']==str(archive) and sha(archive)==receipt['archive_sha256']==link['archive_sha256']
 assert sha(EXE)==link['executable_sha256'] and sha(C/'wrf_link.map')==link['map_sha256']
 assert sha(C/'link.stdout.log')==link['stdout_sha256'] and sha(C/'link.stderr.log')==link['stderr_sha256']
 assert sha(C/'link_driver_probe.txt')==link['driver_link_probe_sha256']
 assert sha(link['xcrun_ld_path'])==link['xcrun_ld_sha256'] and out(['xcrun','--find','ld'])==link['xcrun_ld_path']
 assert out([link['xcrun_ld_path'],'-v'],stderr=subprocess.STDOUT)==link['xcrun_ld_version'] and out([link['link_argv'][0],'--version'])==link['driver_version']
 assert sum('libwrf_sdirk3_libtorch' in x for x in link['link_argv'])==1 and '-lwrf_sdirk3_libtorch' not in link['link_argv']
 maptext=(C/'wrf_link.map').read_text(errors='strict').splitlines()
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
  source=member.removesuffix('.o')+'.cpp'
  assert source in compiled and hashlib.sha256(obj).hexdigest()==compiled[source]['object_sha256']
 assert '-DWRF_SDIRK3_ACCEPTED_STEP_PROBE' in compiled['wrf_sdirk3_newton_solver.cpp']['argv']
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
 w=d.get('w_runtime')
 if not isinstance(w,dict):return errors+['missing_w_runtime']
 require(w.get('scope')=='actual_W_consumers_and_cloned_metric' and w.get('fixed_coefficients_equal') is True,'w_scope')
 endpoints=[]
 for label in ('base','accepted'):
  x=w.get(label)
  if not isinstance(x,dict):errors.append('w_'+label);continue
  vals=x.get('rdn_values');count=x.get('rdn_count');end=x.get('rdn_used_end_exclusive')
  valid=type(count) is int and type(end) is int and isinstance(vals,list) and len(vals)==count and 1<end<=count and x.get('rdn_used_begin')==1 and x.get('rdn_source_dtype')==6 and all(finite(v) for v in vals)
  require(valid,'w_rdn_metadata')
  if valid:
   try:
    raw=b''.join(struct.pack('<f',v) for v in vals)
    require(all(struct.unpack('<f',raw[4*i:4*i+4])[0]==v for i,v in enumerate(vals)) and all(v>0 for v in vals[1:end]),'w_rdn_values')
    h=14695981039346656037
    for byte in raw:h=((h^byte)*1099511628211)&0xffffffffffffffff
    require(str(h)==x.get('rdn_fnv64_f32'),'w_rdn_hash')
   except (ValueError,OverflowError,struct.error):errors.append('w_rdn_conversion')
  for key in ('gravity_f32','top_rdnw_f32'):
   v=x.get(key)
   try:require(finite(v) and v>0 and struct.unpack('<f',struct.pack('<f',v))[0]==v,'w_'+key)
   except (ValueError,OverflowError,struct.error):errors.append('w_'+key)
  require(x.get('canonical_horizontal') is True and x.get('cq1_all_one') is True,'w_mapping')
  for key in ('msfty','level_mass_w','velocity_mass_w','alpha_w'):
   low=x.get(key+'_min');high=x.get(key+'_max');require(finite(low) and finite(high) and 0<low<=high,'w_'+key)
  endpoints.append(x)
 if len(endpoints)==2:
  require(all(endpoints[0].get(k)==endpoints[1].get(k) for k in ('rdn_source_dtype','rdn_count','rdn_used_begin','rdn_used_end_exclusive','rdn_fnv64_f32','rdn_values','gravity_f32','top_rdnw_f32','msfty_min','msfty_max')),'w_fixed_coefficients')
 metric=w.get('metric')
 require(isinstance(metric,dict) and metric.get('block')=='rw' and metric.get('start')==432896 and metric.get('count')==215865 and metric.get('dtype')==7 and metric.get('full_S_inv_fnv64')==d.get('S_inv_hash') and finite(metric.get('min')) and finite(metric.get('max')) and 0<metric['min']<=metric['max'],'w_metric')
 require(isinstance(d.get('trial_input_hash'),str) and d['trial_input_hash'].isdigit(),'trial_input_hash')
 eos=d.get('eos_scalars')
 if not isinstance(eos,dict): return errors+['missing_eos_scalars']
 n=eos.get('levels');require(type(n) is int and n>0,'eos_levels')
 if type(n) is not int or n<=0:return errors
 require(eos.get('scope')=='same_call_EOS_endpoint_outputs_no_error_bound','eos_scope')
 for key in ('c1h','c2h','rdnw'):
  vals=eos.get(key);require(eos.get(key+'_dtype') in (6,7) and isinstance(eos.get(key+'_hash'),str) and eos[key+'_hash'].isdigit() and isinstance(vals,list) and len(vals)>=n and all(finite(x) for x in vals),'eos_coeff_'+key)
 require(isinstance(eos.get('rdnw'),list) and len(eos['rdnw'])>=n and all(finite(x) and x>0 for x in eos['rdnw'][:n]),'rdnw_positive')
 for key in ('rd','cv','cp','p0','t0','B_ph_absmax','K0_ph_absmax','K1_ph_absmax','B_t_absmax','K0_t_absmax','K1_t_absmax'):require(finite(eos.get(key)),'eos_'+key)
 for name in ('base','accepted'):
  ep=eos.get(name)
  if not isinstance(ep,dict):errors.append('eos_endpoint_'+name);continue
  for key in ('theta_pert','dphi_pert_computed','alb','alt','p_pert'):
   rows=ep.get(key)
   require(isinstance(rows,list) and len(rows)==n and all(isinstance(x,dict) and all(finite(x.get(k)) for k in ('min','max','absmax')) and x['min']<=x['max'] and x['absmax']>=max(abs(x['min']),abs(x['max'])) for x in rows),'eos_'+name+'_'+key)
 return errors

def validate_raw(run,d):
 import numpy as np
 names={'base_ph','base_t','base_mu','base_alt','base_p_pert','accepted_ph','accepted_t','accepted_mu','accepted_alt','accepted_p_pert','mu_base','alb','p_base','c1h','c2h','rdnw'}
 raw=d.get('raw_eos');assert isinstance(raw,dict) and raw.get('scope')=='rounded_EOS_subroutine_inputs_and_outputs_only' and raw.get('format')=='flat_npy_little_endian_float64' and raw.get('fnv_scope')=='flat_data_bytes'
 files=raw.get('files');assert isinstance(files,list) and len(files)==16 and {f.get('name') for f in files}==names
 actual={p.name for p in (run/'eos_raw').iterdir()};assert actual=={name+'.npy' for name in names}
 eos=d['eos_scalars']
 for row in files:
  name=row['name'];assert row['path']=='eos_raw/'+name+'.npy' and row['source_dtype'] in (6,7)
  shape=row['shape'];assert isinstance(shape,list) and shape and all(type(x) is int and x>0 for x in shape)
  count=math.prod(shape);assert row['elements']==count and row['data_bytes']==count*8
  arr=np.load(run/row['path'],allow_pickle=False);assert arr.ndim==1 and arr.shape==(count,) and arr.dtype==np.dtype('<f8') and arr.nbytes==row['data_bytes'] and np.isfinite(arr).all()
  h=14695981039346656037
  for byte in arr.tobytes(order='C'): h=((h^byte)*1099511628211)&0xffffffffffffffff
  assert str(h)==row['fnv64']
  if name in ('c1h','c2h','rdnw'):
   assert row['source_dtype']==eos[name+'_dtype'] and arr.tolist()==eos[name]
   if name=='rdnw':assert (arr[:eos['levels']]>0).all()
 return {'status':'passed','file_count':len(files),'scope':'flat array bytes, shapes, source dtype metadata and FNV checked; not full EOS correctness'}

def main():
 if len(sys.argv)!=4: sys.exit('usage: run_w_runtime.py TARGET_HEAD REGISTRATION_JSON REGISTRATION_SHA256')
 target,regname,regsha=sys.argv[1:];reg=pathlib.Path(regname)
 assert sha(reg)==regsha; policy=json.loads(reg.read_text()); assert policy['status']=='registered' and policy['source_commit']==target and policy['maximum_native_attempts']==1 and policy['retry'] is False and policy['performance_eligible'] is False
 for required in (__file__,BUILD,LINK,C/'wrf_link.map',C/'helper_test_receipt.json',E/'red_w_source_review.json',E/'red_w_source_fix_review.json',E/'red_w_analysis_preflight.json',E/'analyze_w_runtime.py',E/'test_analyze_w_runtime.py',OLD_PACKET):
  assert str(pathlib.Path(required).resolve()) in policy['immutable_inputs']
 for p,h in policy['immutable_inputs'].items(): assert sha(p)==h,p
 receipt=json.loads(BUILD.read_text()); assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain','--untracked-files=all'],cwd=W)
 link=verify_build_link(receipt,target); helper=json.loads((C/'helper_test_receipt.json').read_text()); assert helper['source_commit']==target and helper['build_receipt_sha256']==sha(BUILD) and helper['archive_sha256']==receipt['archive_sha256'] and helper['compile_returncode']==helper['run_returncode']==0
 assert helper['source_sha256']==sha(W/'external/libtorch_wrf/sdirk3/tests/test_accepted_step_probe.cpp')
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
 probe=None;raw_validation=None
 try:
  probe=json.loads((run/'accepted_step_probe.json').read_text(),parse_constant=lambda s:(_ for _ in ()).throw(ValueError(s)))
  errors.extend(validate_probe(probe))
  raw_validation=validate_raw(run,probe)
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
 old=json.loads(OLD_PACKET.read_text());pair_keys=('base_K_hash','base_R_hash','post_K_hash','post_R_hash','S_inv_hash','reference_hash','native_scaled_rms_before','native_scaled_rms_after','N')
 pair_match=isinstance(probe,dict) and all(k in old and type(probe.get(k)) is type(old[k]) and probe[k]==old[k] for k in pair_keys)
 if not pair_match:errors.append('original_pair_target_unmatched')
 result={'original_pair_target':'matched' if pair_match else 'unmatched','original_pair_sha256':sha(OLD_PACKET),'status':'diagnostic_completed' if not errors else 'inconclusive','errors':errors,'return_code':rc,'model_completed':False,'forecast_complete':False,'performance_eligible':False,'exact_forcing_certified':False,'scope':'observed Stage 3 accepted pair in raw-principal profile; no exact forcing or whole-Jacobian claim','steps':steps,'logged_converged_stages':sorted(stages.elements()),'stage_3_completed':False,'w_runtime_scope':'observed coefficients and actual scaling, not cost or forecast validation','markers':markers,'initial_state_comparison':initial,'rank_peak_rss_bytes':peak,'disk_free_end':free_end,'clock_triplets':{'start':start,'end':end},'diagnostic':probe,'raw_eos_validation':raw_validation,'artifacts':{str(p.relative_to(run)):{'sha256':sha(p),'bytes':p.stat().st_size} for p in sorted(run.rglob('*')) if p.is_file() and p!=run/'result.json'}}
 with (run/'result.json').open('x') as f:json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
 print(json.dumps({'status':result['status'],'errors':errors,'return_code':rc}));return bool(errors)
if __name__=='__main__':sys.exit(main())
