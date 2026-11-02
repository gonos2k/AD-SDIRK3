#!/usr/bin/env python3
"""Compile and run frozen focused tests against the rebuilt research-feature archive."""
import hashlib,json,pathlib,subprocess,sys
E=pathlib.Path(__file__).resolve().parent;W=pathlib.Path('/private/tmp/early-residual-status-20261102');S=W/'external/libtorch_wrf/sdirk3';B=E/'native_build'
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
r=json.loads((E/'early_build_receipt.json').read_text());assert sha(B/'libwrf_sdirk3_libtorch.a')==r['archive_sha256']
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip()==r['source_commit']
for p,h in r['source_hashes'].items(): assert sha(W/p)==h,p
a=next(x['argv'] for x in r['compiled'] if x['source']=='wrf_sdirk3_newton_solver.cpp');flags=[];skip=False
for x in a[1:]:
 if skip:skip=False;continue
 if x=='-o':skip=True;continue
 if x in ('-c','-MMD','-MP') or x.endswith('/wrf_sdirk3_newton_solver.cpp'):continue
 flags.append(x)
torch='/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/lib';mpi='/opt/homebrew/opt/open-mpi/lib'
compiler_version=subprocess.check_output([a[0],'--version'],text=True).strip()
libs={str(pathlib.Path(torch)/name):sha(pathlib.Path(torch)/name) for name in ('libtorch.dylib','libtorch_cpu.dylib','libc10.dylib')}
results=[]
for src in (S/'test_trust_model_contract.cpp',S/'tests/test_first_failure_classification.cpp',S/'tests/test_newton_early_residual.cpp'):
 exe=B/src.stem;cmd=[a[0],*flags,str(src),str(B/'libwrf_sdirk3_libtorch.a'),'-Wl,-dead_strip','-L'+torch,'-ltorch','-ltorch_cpu','-lc10','-Wl,-rpath,'+torch,'-L'+mpi,'-lmpi','-Wl,-rpath,'+mpi,'-o',str(exe)]
 print('COMPILING',src.name,flush=True);p=subprocess.run(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);(E/(src.stem+'_build.log')).write_text(p.stdout)
 item={'source':str(src),'source_sha256':sha(src),'compile_argv':cmd,'compile_returncode':p.returncode}
 if p.returncode==0:
  item['executable_sha256']=sha(exe);q=subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True);(E/(src.stem+'_run.log')).write_text(q.stdout);item['run_returncode']=q.returncode
 results.append(item);(E/'focused_test_receipt.json').write_text(json.dumps({'archive_sha256':r['archive_sha256'],'build_receipt_sha256':sha(E/'early_build_receipt.json'),'compiler_version':compiler_version,'torch_library_sha256':libs,'results':results},indent=2)+'\n')
 if p.returncode or item['run_returncode']:
  print('FAILED',src.name);sys.exit(1)
 print('PASS',src.name,flush=True)
