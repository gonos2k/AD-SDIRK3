#!/usr/bin/env python3
"""After frozen source and helper pass: rebuild_raw.py TARGET_HEAD."""
import hashlib,json,pathlib,shutil,subprocess,sys
E=pathlib.Path(__file__).resolve().parent;P=E.parent;W=pathlib.Path('/Users/yhlee/SDIRK3-review-worktrees/eos-descent-20261110');C='external/libtorch_wrf/sdirk3/';OLD='1502def5f222ecfea75670c3c79d663435a2cede'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a):return subprocess.check_output(a,cwd=W,text=True).strip()
if len(sys.argv)!=2:raise SystemExit(__doc__)
target=sys.argv[1];assert out(['git','rev-parse','HEAD'])==target and not out(['git','status','--porcelain','--untracked-files=all']);assert subprocess.run(['git','merge-base','--is-ancestor',OLD,target],cwd=W).returncode==0
newton=C+'wrf_sdirk3_newton_solver.cpp';header=C+'wrf_sdirk3_accepted_step_probe.h';test=C+'tests/test_accepted_step_probe.cpp';changed=out(['git','diff','--name-only',OLD,target]).splitlines();assert set(changed)<={newton,header,test},changed
prior=P/'build1502/build_receipt.json';r=json.loads(prior.read_text());assert r['source_commit']==OLD;archive=P/'native_build/libwrf_sdirk3_libtorch.a';assert sha(archive)==r['archive_sha256']
for p,h in r['source_hashes'].items():
 if p not in {newton,header,test}:assert sha(W/p)==h,p
users=[p.name for p in (W/C).glob('*') if p.suffix in ('.cpp','.h') and p.is_file() and p.name!=pathlib.Path(header).name and pathlib.Path(header).name in p.read_text()];assert users==['wrf_sdirk3_newton_solver.cpp'],users
prov=r['dependency_provenance'];prov['wrf_sdirk3_newton_solver.cpp']={'depfile':str(P/'native_build/wrf_sdirk3_newton_solver.d'),'sha256':r['newton_depfile_sha256']};assert len(prov)==31
for x in prov.values():assert sha(x['depfile'])==x['sha256']
assert [s for s,x in prov.items() if pathlib.Path(header).name in pathlib.Path(x['depfile']).read_text()]==['wrf_sdirk3_newton_solver.cpp']
hr=json.loads((E/'helper_test_receipt.json').read_text());assert hr['source_commit']==target and hr['compile_returncode']==hr['run_returncode']==0
for p,h in hr['source_hashes'].items():assert sha(p)==h
B=E/'native_build';B.mkdir(exist_ok=False);a=r['newton_argv'][:];a[a.index('-o')+1]=str(B/'wrf_sdirk3_newton_solver.o')
assert '-DWRF_SDIRK3_ACCEPTED_STEP_PROBE' in a
for macro in ('WRF_SDIRK3_TERMINAL_JVP_PROBE','WRF_SDIRK3_MATCHED_PRESSURE_PROBE','WRF_SDIRK3_OPERATOR_GAP_PROBE','WRF_SDIRK3_PRESSURE_V_DIRECT'):assert '-D'+macro not in a
tracked=out(['git','ls-files',C]).splitlines();pins={p:sha(W/p) for p in tracked if (W/p).is_file()}
with (E/'newton_compile.log').open('x') as log:subprocess.run(a,cwd=W,stdout=log,stderr=subprocess.STDOUT,check=True)
assert out(['git','rev-parse','HEAD'])==target and all(sha(W/p)==h for p,h in pins.items())
new=B/'libwrf_sdirk3_libtorch.a';shutil.copyfile(archive,new);assert new.stat().st_ino!=archive.stat().st_ino;subprocess.run(['ar','-r',str(new),str(B/'wrf_sdirk3_newton_solver.o')],check=True);subprocess.run(['ranlib',str(new)],check=True)
members=[x for x in subprocess.check_output(['ar','-t',str(new)],text=True).splitlines() if not x.startswith('__.SYMDEF')];assert members==r['archive_members'] and len(members)==31
for m in members:
 data=subprocess.check_output(['ar','-p',str(new),m]);assert (hashlib.sha256(data).hexdigest()==sha(B/m) if m=='wrf_sdirk3_newton_solver.o' else data==subprocess.check_output(['ar','-p',str(archive),m])),m
assert sha(archive)==r['archive_sha256']
record={'source_commit':target,'prior_source_commit':OLD,'source_hashes':pins,'prior_build_receipt_sha256':sha(prior),'prior_archive_sha256':sha(archive),'helper_test_receipt_sha256':sha(E/'helper_test_receipt.json'),'dependency_provenance':prov,'newton_argv':a,'newton_object_sha256':sha(B/'wrf_sdirk3_newton_solver.o'),'newton_depfile_sha256':sha(B/'wrf_sdirk3_newton_solver.d'),'archive_members':members,'archive_sha256':sha(new),'profile':'accepted step EOS raw terminal stage3 diagnostic','model_run':False}
(E/'build_receipt.json').write_text(json.dumps(record,indent=2)+'\n')
