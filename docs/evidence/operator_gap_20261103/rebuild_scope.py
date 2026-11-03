#!/usr/bin/env python3
import hashlib,json,pathlib,shutil,subprocess
E=pathlib.Path(__file__).resolve().parent;P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/operator-gap-20261103');W=pathlib.Path('/private/tmp/operator-gap-20261103');C='external/libtorch_wrf/sdirk3/';OLD='85df46805c6129b2ab0c0d86b90f91ef30f8ce0f';HEAD='acc393912b38c6d83537649a7e75ea485c462db1'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a):return subprocess.check_output(a,cwd=W,text=True).strip()
assert out(['git','rev-parse','HEAD'])==HEAD and not out(['git','status','--porcelain','--untracked-files=all']);assert out(['git','diff','--name-only',OLD,HEAD])==C+'wrf_sdirk3_newton_solver.cpp'
prior=P/'build_receipt.json';r=json.loads(prior.read_text());assert r['source_commit']==OLD;archive=P/'native_build/libwrf_sdirk3_libtorch.a';assert sha(archive)==r['archive_sha256']
for p,h in r['source_hashes'].items():
 if p!=C+'wrf_sdirk3_newton_solver.cpp':assert sha(W/p)==h,p
helper=P/'helper_test_receipt.json';hr=json.loads(helper.read_text());assert hr['source_commit']==OLD and hr['compile_returncode']==hr['run_returncode']==0
for p,h in hr['source_hashes'].items():assert sha(p)==h,p
reuse={'status':'PASS','test_source_commit':OLD,'current_source_commit':HEAD,'helper_receipt_path':str(helper),'helper_receipt_sha256':sha(helper),'unchanged_source_hashes':hr['source_hashes'],'rerun':False};(E/'helper_evidence_reuse.json').write_text(json.dumps(reuse,indent=2)+'\n')
B=E/'native_build';B.mkdir(exist_ok=False);a=r['newton_argv'][:];a[a.index('-o')+1]=str(B/'wrf_sdirk3_newton_solver.o');before=sha(W/C/'wrf_sdirk3_newton_solver.cpp')
with (E/'newton_compile.log').open('x') as log:subprocess.run(a,cwd=W,stdout=log,stderr=subprocess.STDOUT,check=True)
assert out(['git','rev-parse','HEAD'])==HEAD and sha(W/C/'wrf_sdirk3_newton_solver.cpp')==before
new=B/'libwrf_sdirk3_libtorch.a';shutil.copyfile(archive,new);assert new.stat().st_ino!=archive.stat().st_ino;subprocess.run(['ar','-r',str(new),str(B/'wrf_sdirk3_newton_solver.o')],check=True);subprocess.run(['ranlib',str(new)],check=True)
members=[x for x in subprocess.check_output(['ar','-t',str(new)],text=True).splitlines() if not x.startswith('__.SYMDEF')];assert members==r['archive_members'] and len(members)==31
for m in members:
 data=subprocess.check_output(['ar','-p',str(new),m]);assert (hashlib.sha256(data).hexdigest()==sha(B/m) if m=='wrf_sdirk3_newton_solver.o' else data==subprocess.check_output(['ar','-p',str(archive),m])),m
pins={p:sha(W/p) for p in r['source_hashes']};record={'source_commit':HEAD,'prior_source_commit':OLD,'source_hashes':pins,'prior_build_receipt_sha256':sha(prior),'prior_archive_sha256':sha(archive),'helper_evidence_reuse_sha256':sha(E/'helper_evidence_reuse.json'),'newton_argv':a,'newton_object_sha256':sha(B/'wrf_sdirk3_newton_solver.o'),'newton_depfile_sha256':sha(B/'wrf_sdirk3_newton_solver.d'),'archive_members':members,'archive_sha256':sha(new),'model_run':False}
(E/'build_receipt.json').write_text(json.dumps(record,indent=2)+'\n')
