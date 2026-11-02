#!/usr/bin/env python3
"""Single h=600 numerical regression attempt; comparisons are separate, never a performance gate.
Usage: run_native_regression.py TARGET_HEAD REGISTRATION_JSON REGISTRATION_SHA256
Registration must contain status=registered, source_commit=TARGET_HEAD, maximum_native_attempts=1,
retry=false, immutable_inputs mapping of absolute paths to sha256, including this script and test receipt.
"""
import hashlib,json,os,pathlib,re,shutil,signal,subprocess,sys,time
from collections import Counter
E=pathlib.Path(__file__).resolve().parent; W=pathlib.Path('/private/tmp/newton-residual-consistency-20261102')
P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101'); OLD=P/'runs/control'
N=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_build')
T=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102/native_terminal')
B=E/'native_build'; BUILD=E/'newton_build_receipt.json'; EXE=B/'wrf.exe'; FLOOR=5*1024**3; CAP=4*1024**3
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
def clock_triplet():
 m1=time.monotonic(); wall=time.time(); m2=time.monotonic(); return {'monotonic_before':m1,'wall':wall,'monotonic_after':m2}
def main():
 if len(sys.argv)!=4: sys.exit(__doc__)
 target,regname,regsha=sys.argv[1:]; reg=pathlib.Path(regname)
 assert sha(reg)==regsha; policy=json.loads(reg.read_text()); assert policy['status']=='registered' and policy['source_commit']==target and policy['maximum_native_attempts']==1 and policy['retry'] is False
 assert str(pathlib.Path(__file__).resolve()) in policy['immutable_inputs']
 for required in (W/TRUST,W/'external/libtorch_wrf/sdirk3/tests/test_newton_returned_residual.cpp',E/'trust_halo_test_receipt.json'):
  assert str(required) in policy['immutable_inputs'],required
 for p,h in policy['immutable_inputs'].items(): assert sha(p)==h,p
 receipt=json.loads(BUILD.read_text()); prior=json.loads((T/'build_receipt.json').read_text())
 assert out(['git','rev-parse','HEAD'],cwd=W)==target
 dirty=out(['git','status','--porcelain','--untracked-files=all'],cwd=W).splitlines()
 assert all(line[3:]==TRUST for line in dirty),dirty
 assert subprocess.run(['git','merge-base','--is-ancestor',receipt['source_commit'],target],cwd=W).returncode==0
 changed=out(['git','diff','--name-only',receipt['source_commit'],target],cwd=W).splitlines()
 assert set(changed)<= {TRUST},changed
 for p,h in receipt['source_hashes'].items():
  if p!=TRUST: assert sha(W/p)==h,p
 assert sha(EXE)==receipt['executable_sha256'] and sha(B/'libwrf_sdirk3_libtorch.a')==receipt['archive_sha256']
 for p,h in INPUTS.items(): assert sha(OLD/p)==h,p
 proof=N/'complete_provenance.json'; sdk=P/'current_sdk_provenance.json'
 assert sha(proof)==prior['fortran_provenance_sha256'] and sha(sdk)==prior['current_sdk_provenance_sha256']
 pr=json.loads(proof.read_text())
 for p,h in pr['object_module_archive_sha256'].items(): assert sha(N/'source'/p)==h,p
 for p,h in pr['dylib_sha256'].items():
  if h!='macOS shared cache': assert sha(p)==h,p
 stack=json.loads(sdk.read_text())
 assert stack['SDK_version']=='27.2' and out(['xcrun','--show-sdk-path'])==stack['SDK_path'] and out(['xcrun','--show-sdk-version'])==stack['SDK_version']
 assert sha('/usr/bin/g++')==stack['compiler_driver_sha256'] and sha(stack['compiler_resolved_path'])==stack['compiler_resolved_sha256'] and out(['/usr/bin/g++','--version'])==stack['compiler_version'] and out(['sw_vers'])==stack['OS']
 assert loads(EXE)==loads(P/'native_control/wrf.exe') and versions(EXE)==versions(P/'native_control/wrf.exe')
 free=shutil.disk_usage(E).free; assert free>=FLOOR
 run=E/'native_regression'; run.mkdir() # exclusive: no retries even after failure
 for p,h in INPUTS.items(): shutil.copyfile(OLD/p,run/p); assert sha(run/p)==h
 cmd=['/opt/homebrew/bin/mpirun','--timeout','360','--report-state-on-timeout','-np','1','--bind-to','none','/usr/bin/time','-l','-o',str(run/'rank_rusage.log'),str(EXE)]
 env={k:os.environ[k] for k in ('HOME','USER','LOGNAME','TMPDIR','LANG','LC_ALL') if k in os.environ}; env['PATH']='/opt/homebrew/bin:/usr/bin:/bin:/usr/sbin:/sbin'; env.update({'OMP_NUM_THREADS':'1','OPENBLAS_NUM_THREADS':'1','VECLIB_MAXIMUM_THREADS':'1','MKL_NUM_THREADS':'1','WRF_SDIRK3_STAGE_OPERAND_DIAG':'0','WRF_SDIRK3_EW_ETA_INITIAL':'0.1','WRF_SDIRK3_GMRES_ESTIMATE_PROBE':'0'})
 attempt={'one_attempt':True,'retry':False,'performance_eligible':False,'target_head':target,'registration_sha256':regsha,'build_receipt_sha256':sha(BUILD),'executable_sha256':sha(EXE),'archive_sha256':sha(B/'libwrf_sdirk3_libtorch.a'),'fortran_proof_sha256':sha(proof),'sdk_proof_sha256':sha(sdk),'otool_loads':loads(EXE),'build_version':versions(EXE),'inputs':INPUTS,'disk_free_start':free,'command':cmd,'effective_environment':env,'start_epoch':time.time()}
 with (run/'attempt.json').open('x') as f: json.dump(attempt,f,indent=2); f.write('\n'); f.flush(); os.fsync(f.fileno())
 start=clock_triplet(); rc=None; errors=[]
 try:
  with (run/'launcher.stdout.log').open('xb') as o,(run/'launcher.stderr.log').open('xb') as e:
   proc=subprocess.Popen(cmd,cwd=run,env=env,stdout=o,stderr=e,start_new_session=True)
   try: rc=proc.wait(timeout=390)
   except subprocess.TimeoutExpired:
    errors.append('launcher_timeout'); os.killpg(proc.pid,signal.SIGTERM)
    try: rc=proc.wait(timeout=5)
    except subprocess.TimeoutExpired: os.killpg(proc.pid,signal.SIGKILL); rc=proc.wait()
 except Exception as exc: errors.append('launcher_exception: '+repr(exc))
 end=clock_triplet(); elapsed=(end['monotonic_before']+end['monotonic_after']-start['monotonic_before']-start['monotonic_after'])/2; wall_elapsed=end['wall']-start['wall']
 def read(n):
  p=run/n; return p.read_text(errors='replace') if p.exists() else ''
 text='\n'.join(read(n) for n in ('rsl.out.0000','rsl.error.0000','launcher.stdout.log','launcher.stderr.log'))
 steps=len(re.findall(r'Timing for main:',read('rsl.out.0000')))
 stage_counts=Counter()
 for name in ('rsl.out.0000','rsl.error.0000','launcher.stdout.log','launcher.stderr.log'):
  stage_counts |= Counter(int(x) for x in re.findall(r'\[Newton\] CONVERGED stage=(\d+)',read(name)))
 stages=sorted(stage_counts.elements()); success='SUCCESS COMPLETE WRF' in text
 rss=re.findall(r'^\s*(\d+)\s+maximum resident set size\s*$',read('rank_rusage.log'),re.M); peak=int(rss[0]) if len(rss)==1 else None
 if rc!=0: errors.append('mpi_return_code')
 if not success: errors.append('missing_wrf_success')
 if steps!=1: errors.append('step_count')
 if stages!=[2,3,4]: errors.append('converged_stage_count')
 if peak is None or peak>CAP: errors.append('rank_rss_missing_or_exceeded')
 endfree=shutil.disk_usage(E).free
 if endfree<FLOOR: errors.append('postrun_disk_floor')
 try:
  assert sha(reg)==regsha and sha(EXE)==receipt['executable_sha256'] and sha(BUILD)==attempt['build_receipt_sha256'] and sha(B/'libwrf_sdirk3_libtorch.a')==receipt['archive_sha256']
  for p,h in INPUTS.items(): assert sha(OLD/p)==h and sha(run/p)==h
  for p,h in policy['immutable_inputs'].items(): assert sha(p)==h
  for p,h in receipt['source_hashes'].items():
   if p!=TRUST: assert sha(W/p)==h
 except Exception as exc: errors.append('postrun_identity_failure: '+repr(exc))
 result={'status':'pending_numerical_comparisons' if not errors else 'inconclusive','errors':errors,'performance_eligible':False,'return_code':rc,'normal_forecast_success':success,'steps':steps,'converged_stages':stages,'rank_peak_rss_bytes':peak,'disk_free_end':endfree,'monotonic_elapsed_seconds':elapsed,'wall_elapsed_seconds':wall_elapsed,'clock_gap_seconds':wall_elapsed-elapsed,'clock_triplets':{'start':start,'end':end},'pending':['wrapper count 3/0','finite fields','true Newton scaled RMS <= 1e-9','174 numeric endpoint arrays bitwise versus existing control','same-setup archived RK3 comparison'],'artifacts':{p.name:{'sha256':sha(p),'bytes':p.stat().st_size} for p in sorted(run.iterdir()) if p.is_file() and p.name!='result.json'}}
 with (run/'result.json').open('x') as f: json.dump(result,f,indent=2,allow_nan=False); f.write('\n')
 print(json.dumps({'status':result['status'],'errors':errors,'return_code':rc})); return bool(errors)
if __name__=='__main__': sys.exit(main())
