#!/usr/bin/env python3
"""Compile and run both tests against frozen archive. Usage: run_tests.py EXPECTED_NEW_TEST_SHA256"""
import hashlib,json,pathlib,subprocess,sys
E=pathlib.Path(__file__).resolve().parent
W=pathlib.Path('/private/tmp/newton-residual-consistency-20261102')
S=W/'external/libtorch_wrf/sdirk3'
B=E/'native_build'
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
if len(sys.argv)!=3 or any(len(x)!=64 for x in sys.argv[1:]): sys.exit('usage: run_tests.py EXPECTED_NEW_TEST_SHA EXPECTED_TRUST_TEST_SHA')
r=json.loads((E/'newton_build_receipt.json').read_text())
if sha(B/'libwrf_sdirk3_libtorch.a')!=r['archive_sha256']: sys.exit('archive changed')
for p,h in r['source_hashes'].items():
 if p!='external/libtorch_wrf/sdirk3/test_trust_model_contract.cpp' and sha(W/p)!=h:
  sys.exit('core source changed after build: '+p)
tests=[S/'test_trust_model_contract.cpp',S/'tests/test_newton_returned_residual.cpp']
if sha(tests[1])!=sys.argv[1] or sha(tests[0])!=sys.argv[2]: sys.exit('test differs from explicit frozen hashes')
a=r['newton_argv']; flags=[]; skip=False
for x in a[1:]:
 if skip: skip=False; continue
 if x=='-o': skip=True; continue
 if x in ('-c','-MMD','-MP') or x.endswith('/wrf_sdirk3_newton_solver.cpp'): continue
 flags.append(x)
torch='/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/lib'
mpi='/opt/homebrew/opt/open-mpi/lib'
results=[]
for t in tests:
 exe=B/t.stem
 cmd=[a[0],*flags,str(t),str(B/'libwrf_sdirk3_libtorch.a'),'-Wl,-dead_strip','-L'+torch,'-ltorch','-ltorch_cpu','-lc10','-Wl,-rpath,'+torch,'-L'+mpi,'-lmpi','-Wl,-rpath,'+mpi,'-o',str(exe)]
 print('RUN',cmd,flush=True); p=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT); (E/(t.stem+'_build.log')).write_text(p.stdout)
 result={'source':str(t),'source_sha256':sha(t),'argv':cmd,'compile_returncode':p.returncode}
 if p.returncode==0:
  result['executable_sha256']=sha(exe); q=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT); (E/(t.stem+'_run.log')).write_text(q.stdout); result['run_returncode']=q.returncode
 results.append(result); (E/'newton_test_receipt.json').write_text(json.dumps({'archive_sha256':r['archive_sha256'],'results':results},indent=2)+'\n')
 if p.returncode or result['run_returncode']: print((E/(t.stem+('_build.log' if p.returncode else '_run.log'))).read_text()); sys.exit(1)
print('Both tests passed')
