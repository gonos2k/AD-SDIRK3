import pathlib,json,hashlib,subprocess,time,datetime
p=pathlib.Path(__file__).resolve().parent
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
r=p/'registration.json';reg=json.loads(r.read_text());assert sha(r)=='bd9e1a88c7dbfdd13b4b8cfd32e9ae40a56c1b397fd7b12616220c28cd57d8d9'
for s,item in reg['input_files'].items():
 f=pathlib.Path(s);assert f.stat().st_size==item['bytes'] and sha(f)==item['sha256']
assert not (p/'result.json').exists() and not (p/'run.log').exists()
base=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-a5-20261101/serializer_fix_validation/native_case/run')
cmd=[str(p/'fd_mpfr'),str(base/'pressure_a5_direction.bin'),str(base/'pressure_a5_actions.bin')]
receipt={'registration_sha256':sha(r),'source_sha256':sha(p/'fd_mpfr.cpp'),'binary_sha256':sha(p/'fd_mpfr'),'cmd':cmd,'start_local':datetime.datetime.now().astimezone().isoformat(),'model_run':False}
start=time.monotonic()
with (p/'run.log').open('x') as log:
 proc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,cwd=p)
receipt.update(returncode=proc.returncode,seconds=time.monotonic()-start,log_sha256=sha(p/'run.log'),end_local=datetime.datetime.now().astimezone().isoformat())
(p/'result.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps(receipt,indent=2))
