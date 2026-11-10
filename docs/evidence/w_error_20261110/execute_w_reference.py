from pathlib import Path
import json,hashlib,datetime,subprocess,sys,time,signal,os
E=Path(__file__).resolve().parent;sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
files=[Path(__file__).resolve()]+[E/n for n in ('w_full_mpfr.cpp','w_full_mpfr','run_w_full_mpfr.py','test_w_full_mpfr.py','prepare_w_full_manifest.py','test_prepare_w_full_manifest.py','norm_exact.py','test_norm_exact.py','red_w_full_reference_preflight.json','red_w_normalizer_preflight.json','red_norm_exact_preflight.json','w_full_preanalysis_test_receipt.json','w_normalizer_test_receipt.json','native_registration.json','run/accepted_step_probe.json','run/result.json')]+list((E/'normalized_case').glob('*'))
reg={'status':'registered','timestamp_local':datetime.datetime.now().astimezone().isoformat(),'scope':'direct complete W residual evaluation error at two fixed B/a/C/K inputs, and stored-input norm gap separately','maximum_reference_evaluations':1,'timeout_seconds':900,'native_runs_in_this_block':0,'criteria':['all captured profile and association checks pass','MPFR real-domain denominators and EOS positive, unsupported branch refuses','all W entries/aliases compared; no threshold tuning or root residual-decrease pass inferred'],'input_sha256':{str(p):sha(p)for p in files}}
with(E/'reference_registration.json').open('x')as f:json.dump(reg,f,indent=2);f.write('\n')
assert not(E/'full_w_error.json').exists()
cmd=['/usr/bin/time','-l','-o',str(E/'reference_rusage.log'),sys.executable,str(E/'run_w_full_mpfr.py'),str(E/'normalized_case/manifest.json'),str(E/'normalized_case'),str(E/'full_w_error.json')]
begin=time.monotonic()
with(E/'reference.stdout.log').open('xb')as out,(E/'reference.stderr.log').open('xb')as err:
 p=subprocess.Popen(cmd,stdout=out,stderr=err,start_new_session=True)
 try:rc=p.wait(timeout=900);status='completed'if rc==0 else'failed'
 except subprocess.TimeoutExpired:
  status='timeout';os.killpg(p.pid,signal.SIGTERM)
  try:rc=p.wait(timeout=5)
  except subprocess.TimeoutExpired:os.killpg(p.pid,signal.SIGKILL);rc=p.wait()
res={'status':status,'returncode':rc,'elapsed_seconds':time.monotonic()-begin,'command':cmd,'registration_sha256':sha(E/'reference_registration.json'),'immutable_inputs_unchanged':all(sha(Path(p))==h for p,h in reg['input_sha256'].items())}
with(E/'reference_execution.json').open('x')as f:json.dump(res,f,indent=2);f.write('\n')
print(json.dumps(res));raise SystemExit(0 if status=='completed'and res['immutable_inputs_unchanged']else 1)
