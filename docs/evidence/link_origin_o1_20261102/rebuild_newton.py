#!/usr/bin/env python3
"""After freeze, recompile Newton only. Usage: rebuild_newton.py TARGET_HEAD"""
import hashlib,json,pathlib,shutil,subprocess,sys
E=pathlib.Path(__file__).resolve().parent;W=pathlib.Path('/private/tmp/link-origin-o1-20261102');P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/early-residual-status-20261102');C='external/libtorch_wrf/sdirk3/';BASE='eca8f6fc'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a):return subprocess.check_output(a,cwd=W,text=True).strip()
if len(sys.argv)!=2:raise SystemExit(__doc__)
target=sys.argv[1];assert out(['git','rev-parse','HEAD'])==target and not out(['git','status','--porcelain','--untracked-files=all']);assert set(out(['git','diff','--name-only',BASE,target]).splitlines())=={C+'wrf_sdirk3_newton_solver.cpp','arch/configure.defaults'}
rp=P/'early_build_receipt.json';r=json.loads(rp.read_text());archive=P/'native_build/libwrf_sdirk3_libtorch.a';assert sha(archive)==r['archive_sha256']
for p,h in r['source_hashes'].items():
 if p!=C+'wrf_sdirk3_newton_solver.cpp':assert sha(W/p)==h,p
entry=next(x for x in r['compiled'] if x['source']=='wrf_sdirk3_newton_solver.cpp');a=entry['argv'][:]
a=[x.replace('/private/tmp/early-residual-status-20261102/external/libtorch_wrf/sdirk3',str(W/C)) for x in a]
B=E/'native_build';B.mkdir(exist_ok=False);a[a.index('-o')+1]=str(B/'wrf_sdirk3_newton_solver.o');before=sha(W/C/'wrf_sdirk3_newton_solver.cpp')
with (E/'newton_compile.log').open('x') as f:subprocess.run(a,cwd=W,stdout=f,stderr=subprocess.STDOUT,check=True)
assert sha(W/C/'wrf_sdirk3_newton_solver.cpp')==before and out(['git','rev-parse','HEAD'])==target
new=B/'libwrf_sdirk3_libtorch.a';shutil.copyfile(archive,new);subprocess.run(['ar','-r',str(new),a[a.index('-o')+1]],check=True);subprocess.run(['ranlib',str(new)],check=True)
members=[x for x in subprocess.check_output(['ar','-t',str(new)],text=True).splitlines() if not x.startswith('__.SYMDEF')];assert members==r['archive_members']
for m in members:
 data=subprocess.check_output(['ar','-p',str(new),m])
 if m=='wrf_sdirk3_newton_solver.o':assert hashlib.sha256(data).hexdigest()==sha(B/m)
 else:assert data==subprocess.check_output(['ar','-p',str(archive),m]),m
pins={p:sha(W/p) for p in r['source_hashes']};record={'source_commit':target,'base_commit':out(['git','rev-parse',BASE]),'prior_build_receipt_sha256':sha(rp),'prior_archive_sha256':sha(archive),'source_hashes':pins,'newton_argv':a,'newton_source_sha256':before,'newton_object_sha256':sha(a[a.index('-o')+1]),'newton_depfile_sha256':sha(B/'wrf_sdirk3_newton_solver.d'),'archive_sha256':sha(new),'archive_members':r['archive_members'],'model_run':False}
(E/'build_receipt.json').write_text(json.dumps(record,indent=2)+'\n')
