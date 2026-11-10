from pathlib import Path
import hashlib,json,datetime,subprocess,time
E=Path(__file__).resolve().parent
W=Path('/Users/yhlee/SDIRK3-review-worktrees/alb-error-propagation-20261110')
D=W/'docs/evidence/eos_descent_20261110'
R=Path('/Users/yhlee/AD-SDIRK3-review-evidence/eos-descent-20261110/run/eos_raw')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=[E/x for x in ('alb_mpfr.cpp','alb_mpfr','evaluate_alb.py','reconstruct_alb_inputs.py','test_alb_mpfr.py','alb_preanalysis_test_receipt.json','green_alb_input_feasibility.json','red_alb_input_audit.json','red_alb_checker_preflight.json','red_alb_tube_plan.json','red_alb_tube_preflight.json','widen_alb_tube.py','test_widen_alb_tube.py','reconstructed/theta_base.npy','reconstructed/reconstruction.json')]
files += [D/'run/accepted_step_probe.json',D/'eos_domain_analysis.json',W/'external/libtorch_wrf/sdirk3/wrf_hydrostatic_pressure.cpp',W/'external/libtorch_wrf/sdirk3/wrf_hydrostatic_pressure.h',W/'external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp',W/'dyn_em/module_implicit_sdirk3.F',Path(__file__).resolve()]
files+=sorted(R.glob('*.npy'));assert len(list(R.glob('*.npy')))==16
assert not(E/'alb_analysis.json').exists() and not(E/'alb_execution.json').exists()
reg={'status':'registered','timestamp_local':datetime.datetime.now().astimezone().isoformat(),'base_source':'2c1dcce1d82560297b3259a5e7687dd52b2e38c2','scope':'one offline all-cell upstream-alb/composed EOS analysis and exact-real tube widening','native_runs':0,'maximum_offline_attempts':1,'retry':False,'timeout_seconds':300,'failure_policy':'preserve failure and leave unresolved; no threshold changes or retry','conditions':['fixed captured rounded other EOS arguments and exact stored constants','theta input identity remains FNV-qualified, pressure direct byte match','MPFR256 directed rounding trusted','all target domains positive; reject crossing zero','full RHS/scaling/norm/exact merit not certified'],'immutable_inputs':{str(p):sha(p)for p in files}}
with(E/'alb_registration.json').open('x')as f:json.dump(reg,f,indent=2);f.write('\n')
cmd=['python3',str(E/'evaluate_alb.py'),str(D/'run/accepted_step_probe.json'),str(R),str(E/'reconstructed/theta_base.npy'),str(E/'reconstructed/reconstruction.json'),str(E/'alb_analysis.json')]
t=time.monotonic();result={'command':cmd,'registration_sha256':sha(E/'alb_registration.json')}
try:
 p=subprocess.run(cmd,capture_output=True,text=True,timeout=300);result.update(returncode=p.returncode,stdout=p.stdout,stderr=p.stderr,status='completed'if p.returncode==0 else'failed')
except subprocess.TimeoutExpired as exc:result.update(status='timeout',stdout=str(exc.stdout),stderr=str(exc.stderr))
result['elapsed_seconds']=time.monotonic()-t
result['immutable_inputs_unchanged']=all(sha(Path(p))==h for p,h in reg['immutable_inputs'].items())
with(E/'alb_execution.json').open('x')as f:json.dump(result,f,indent=2);f.write('\n')
print(json.dumps(result))
if result['status']!='completed' or not result['immutable_inputs_unchanged']:raise SystemExit(1)
