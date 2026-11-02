#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess
E=pathlib.Path(__file__).resolve().parent;W=pathlib.Path('/private/tmp/early-residual-status-20261102');B=E/'native_build';C='external/libtorch_wrf/sdirk3/';old='2625b228';new='c7a282e3032b6f760d9d8154ee9536ab3688a95b'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a):return subprocess.check_output(a,cwd=W,text=True).strip()
r=json.loads((E/'attempt1/early_build_receipt.json').read_text());assert out(['git','rev-parse','HEAD'])==new and not out(['git','status','--porcelain','--untracked-files=all'])
assert set(out(['git','diff','--name-only',old,new]).splitlines())=={C+'wrf_sdirk3_newton_solver.cpp',C+'tests/test_newton_early_residual.cpp'}
archive=B/'libwrf_sdirk3_libtorch.a';assert sha(archive)==r['archive_sha256']
for p,h in r['source_hashes'].items():
 if p not in {C+'wrf_sdirk3_newton_solver.cpp',C+'tests/test_newton_early_residual.cpp'}:assert sha(W/p)==h,p
for x in r['compiled']:
 obj=B/pathlib.Path(x['source']).with_suffix('.o').name
 assert sha(obj)==x['object_sha256'],obj
entry=next(x for x in r['compiled'] if x['source']=='wrf_sdirk3_newton_solver.cpp');a=entry['argv'][:];obj=B/'wrf_sdirk3_newton_solver.o';tmp=E/'newton_recompiled';tmp.mkdir(exist_ok=False);a[a.index('-o')+1]=str(tmp/obj.name)
with (E/'newton_recompile.log').open('w') as f:subprocess.run(a,cwd=W,stdout=f,stderr=subprocess.STDOUT,check=True)
assert out(['git','rev-parse','HEAD'])==new and not out(['git','status','--porcelain','--untracked-files=all'])
subprocess.run(['ar','-r',str(archive),str(tmp/obj.name)],check=True);subprocess.run(['ranlib',str(archive)],check=True)
entry.update({'source_sha256':sha(W/C/'wrf_sdirk3_newton_solver.cpp'),'argv':a,'object_sha256':sha(tmp/obj.name),'depfile_sha256':sha(tmp/'wrf_sdirk3_newton_solver.d'),'compiled_source_commit':new})
for x in r['compiled']:
 if x is not entry:x['compiled_source_commit']=r['compiled_source_commit']
r.update({'source_commit':new,'compiled_source_commit':new,'previous_build_receipt_sha256':sha(E/'attempt1/early_build_receipt.json'),'previous_archive_sha256':r['archive_sha256'],'archive_sha256':sha(archive),'source_hashes':{p:sha(W/p) for p in r['source_hashes']},'note':'Newton recompiled at c7; nine affected object members reused from e7 production build; 21 unchanged baseline members; native link/run deferred'})
(E/'early_build_receipt.json').write_text(json.dumps(r,indent=2)+'\n')
