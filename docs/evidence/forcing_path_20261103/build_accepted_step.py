#!/usr/bin/env python3
import hashlib,json,pathlib,shutil,subprocess
E=pathlib.Path(__file__).resolve().parent;P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/operator-gap-20261103_scope_fix');O=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/operator-gap-20261103');W=pathlib.Path('/private/tmp/forcing-path-20261103');C='external/libtorch_wrf/sdirk3/';BASE='3a82d601';HEAD='bc0f66461efddded7ffd181265d44f7e6caa7add'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a):return subprocess.check_output(a,cwd=W,text=True).strip()
assert out(['git','rev-parse','HEAD'])==HEAD and not out(['git','status','--porcelain','--untracked-files=all']);assert subprocess.run(['git','merge-base','--is-ancestor',BASE,HEAD],cwd=W).returncode==0
prior=P/'build_receipt.json';r=json.loads(prior.read_text());o=json.loads((O/'build_receipt.json').read_text());archive=P/'native_build/libwrf_sdirk3_libtorch.a';assert sha(archive)==r['archive_sha256'];assert r['prior_build_receipt_sha256']==sha(O/'build_receipt.json')
newton=C+'wrf_sdirk3_newton_solver.cpp';oldheader=C+'wrf_sdirk3_terminal_jvp_probe.h';helper=C+'wrf_sdirk3_accepted_step_probe.h'
for p,h in r['source_hashes'].items():
 if p not in (newton,oldheader):assert sha(W/p)==h,p
old=subprocess.check_output(['git','show',r['source_commit']+':'+oldheader],cwd=W,text=True);assert (W/oldheader).read_text().replace(' || defined(WRF_SDIRK3_ACCEPTED_STEP_PROBE)','')==old
users=[p.name for p in (W/C).glob('*') if p.suffix in ('.cpp','.h') and p.is_file() and p.name!=pathlib.Path(helper).name and pathlib.Path(helper).name in p.read_text()];assert users==['wrf_sdirk3_newton_solver.cpp'],users
provenance=o['dependency_provenance'];provenance['wrf_sdirk3_newton_solver.cpp']={'depfile':str(P/'native_build/wrf_sdirk3_newton_solver.d'),'sha256':r['newton_depfile_sha256'],'terminal_header':True}
assert len(provenance)==31 and [s for s,x in provenance.items() if x['terminal_header']]==['wrf_sdirk3_newton_solver.cpp']
for x in provenance.values():assert sha(x['depfile'])==x['sha256']
hr=json.loads((E/'helper_test_receipt.json').read_text());assert hr['source_commit']==HEAD and hr['compile_returncode']==hr['run_returncode']==0
for p,h in hr['source_hashes'].items():assert sha(p)==h
B=E/'native_build';B.mkdir(exist_ok=False);a=r['newton_argv'][:];a=[x.replace('/private/tmp/operator-gap-20261103/external/libtorch_wrf/sdirk3',str(W/C)) for x in a];a.remove('-DWRF_SDIRK3_OPERATOR_GAP_PROBE');a.insert(1,'-DWRF_SDIRK3_ACCEPTED_STEP_PROBE');a[a.index('-o')+1]=str(B/'wrf_sdirk3_newton_solver.o')
for macro in ('WRF_SDIRK3_LINEAR_DELAY_AUDIT','WRF_SDIRK3_PRESSURE_STAGE_PROBE','WRF_SDIRK3_PRESSURE_REFERENCE_STAGE','WRF_SDIRK3_PRESSURE_A4','WRF_SDIRK3_PRESSURE_A5','WRF_SDIRK3_ACCEPTED_STEP_PROBE'):assert '-D'+macro in a
for macro in ('WRF_SDIRK3_TERMINAL_JVP_PROBE','WRF_SDIRK3_MATCHED_PRESSURE_PROBE','WRF_SDIRK3_PRESSURE_V_DIRECT','WRF_SDIRK3_OPERATOR_GAP_PROBE'):assert '-D'+macro not in a
tracked=out(['git','ls-files',C]).splitlines();pins={p:sha(W/p) for p in tracked if (W/p).is_file()}
with (E/'newton_compile.log').open('x') as log:subprocess.run(a,cwd=W,stdout=log,stderr=subprocess.STDOUT,check=True)
assert out(['git','rev-parse','HEAD'])==HEAD and all(sha(W/p)==h for p,h in pins.items())
new=B/'libwrf_sdirk3_libtorch.a';shutil.copyfile(archive,new);assert new.stat().st_ino!=archive.stat().st_ino;subprocess.run(['ar','-r',str(new),str(B/'wrf_sdirk3_newton_solver.o')],check=True);subprocess.run(['ranlib',str(new)],check=True)
members=[x for x in subprocess.check_output(['ar','-t',str(new)],text=True).splitlines() if not x.startswith('__.SYMDEF')];assert members==r['archive_members'] and len(members)==31
for m in members:
 data=subprocess.check_output(['ar','-p',str(new),m]);assert (hashlib.sha256(data).hexdigest()==sha(B/m) if m=='wrf_sdirk3_newton_solver.o' else data==subprocess.check_output(['ar','-p',str(archive),m])),m
record={'source_commit':HEAD,'source_hashes':pins,'prior_build_receipt_sha256':sha(prior),'prior_archive_sha256':sha(archive),'helper_test_receipt_sha256':sha(E/'helper_test_receipt.json'),'dependency_provenance':provenance,'newton_argv':a,'newton_object_sha256':sha(B/'wrf_sdirk3_newton_solver.o'),'newton_depfile_sha256':sha(B/'wrf_sdirk3_newton_solver.d'),'archive_members':members,'archive_sha256':sha(new),'profile':'accepted step terminal stage3 diagnostic','model_run':False}
(E/'build_receipt.json').write_text(json.dumps(record,indent=2)+'\n')
