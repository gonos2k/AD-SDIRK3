#!/usr/bin/env python3
"""Compile/run operator-gap helper test after frozen diagnostic archive is built."""
import hashlib,json,pathlib,subprocess
E=pathlib.Path(__file__).resolve().parent;W=pathlib.Path('/private/tmp/operator-gap-20261103');B=E/'native_build';S=W/'external/libtorch_wrf/sdirk3'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
r=json.loads((E/'build_receipt.json').read_text());assert sha(B/'libwrf_sdirk3_libtorch.a')==r['archive_sha256'];assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip()==r['source_commit']
for p,h in r['source_hashes'].items():assert sha(W/p)==h,p
src=S/'tests/test_operator_gap_probe.cpp';assert src.is_file() and str(src.relative_to(W)) in r['source_hashes'];a=r['newton_argv'];flags=[];skip=False
for x in a[1:]:
 if skip:skip=False;continue
 if x=='-o':skip=True;continue
 if x in ('-c','-MMD','-MP','-DWRF_SDIRK3_OPERATOR_GAP_PROBE','-DWRF_SDIRK3_LINEAR_DELAY_AUDIT') or x.endswith('/wrf_sdirk3_newton_solver.cpp'):continue
 flags.append(x)
torch='/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/lib';exe=B/'test_operator_gap_probe';assert not exe.exists()
cmd=[a[0],*flags,str(src),'-L'+torch,'-ltorch','-ltorch_cpu','-lc10','-Wl,-rpath,'+torch,'-o',str(exe)]
p=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(E/'helper_build.log').write_text(p.stdout);result={'source_sha256':sha(src),'archive_sha256':r['archive_sha256'],'build_receipt_sha256':sha(E/'build_receipt.json'),'compile_argv':cmd,'compile_returncode':p.returncode,'compiler_version':subprocess.check_output([a[0],'--version'],text=True).strip(),'torch_library_sha256':{str(pathlib.Path(torch)/x):sha(pathlib.Path(torch)/x) for x in ('libtorch.dylib','libtorch_cpu.dylib','libc10.dylib')}}
if p.returncode==0:
 q=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(E/'helper_run.log').write_text(q.stdout);result.update(executable_sha256=sha(exe),run_returncode=q.returncode)
(E/'helper_test_receipt.json').write_text(json.dumps(result,indent=2)+'\n')
assert p.returncode==0 and result['run_returncode']==0
