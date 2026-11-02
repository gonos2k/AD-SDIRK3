#!/usr/bin/env python3
"""Build research-configuration archive and native WRF from pinned prior receipt.
Usage: python3 build_newton.py EXPECTED_NEWTON_SHA256
Only run after source freeze. Does not run WRF.
"""
import hashlib, json, pathlib, shutil, subprocess, sys
E=pathlib.Path(__file__).resolve().parent
W=pathlib.Path('/private/tmp/newton-residual-consistency-20261102')
OLD=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102/native_terminal')
R=json.loads((OLD/'build_receipt.json').read_text())
NEWTON='external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp'
TRUST='external/libtorch_wrf/sdirk3/test_trust_model_contract.cpp'
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def run(a, **kw): print('RUN',a,flush=True); subprocess.run(a,check=True,**kw)
if len(sys.argv)!=2 or len(sys.argv[1])!=64: sys.exit(__doc__)
if sha(W/NEWTON)!=sys.argv[1]: sys.exit('Newton hash does not match frozen argument')
for p,h in R['source_hashes'].items():
 if p not in (NEWTON,TRUST) and (not (W/p).is_file() or sha(W/p)!=h): sys.exit('source/header mismatch: '+p)
feature_header=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-a4-20261031/a4_build/B/wrf_sdirk3_pressure_reference_expected.h')
if sha(feature_header)!='91acfb9ec88905e7e5f762bc84211ec9ae57a78c375771aa36995fbd0c64b845': sys.exit('feature header changed')
archive=OLD/'libwrf_sdirk3_libtorch.a'
if sha(archive)!=R['archive_sha256']: sys.exit('baseline archive hash mismatch')
members=[m for m in subprocess.check_output(['ar','-t',str(archive)],text=True).splitlines() if not m.startswith('__.SYMDEF')]
if members!=R['archive_members'] or len(members)!=31 or len(set(members))!=31: sys.exit('archive inventory mismatch')
for entry in R['source_objects']:
 member=pathlib.Path(entry['source']).with_suffix('.o').name
 data=subprocess.check_output(['ar','-p',str(archive),member])
 if hashlib.sha256(data).hexdigest()!=entry['object_sha256']: sys.exit('archive member mismatch: '+member)
B=E/'native_build'; B.mkdir(exist_ok=True)
a=next(x['argv'] for x in R['source_objects'] if x['source']==pathlib.Path(NEWTON).name)
a=[x for x in a if x!='-DWRF_SDIRK3_TERMINAL_JVP_PROBE']
a=[x.replace('/private/tmp/terminal-jvp-replay-20261102/external/libtorch_wrf/sdirk3',str(W/'external/libtorch_wrf/sdirk3')) for x in a]
a[a.index('-o')+1]=str(B/'wrf_sdirk3_newton_solver.o')
run(a,cwd=W)
if sha(W/NEWTON)!=sys.argv[1]: sys.exit('source changed during compile')
out=B/'libwrf_sdirk3_libtorch.a'; shutil.copyfile(archive,out)
run(['ar','-r',str(out),str(B/'wrf_sdirk3_newton_solver.o')]); run(['ranlib',str(out)])
link=R['link_argv'][:]; link[link.index('-o')+1]=str(B/'wrf.exe')
link=[x.replace(str(OLD),str(B)) for x in link]
run(link,cwd=R['link_cwd'])
receipt={'feature_header_sha256':sha(feature_header),'baseline_receipt_sha256':sha(OLD/'build_receipt.json'),'baseline_archive_sha256':sha(archive),'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip(),'source_hashes':{p:sha(W/p) for p in R['source_hashes']},'newton_argv':a,'newton_object_sha256':sha(B/'wrf_sdirk3_newton_solver.o'),'newton_depfile_sha256':sha(B/'wrf_sdirk3_newton_solver.d'),'archive_members':members,'archive_sha256':sha(out),'link_argv':link,'link_cwd':R['link_cwd'],'executable_sha256':sha(B/'wrf.exe'),'model_run':False}
(E/'newton_build_receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
