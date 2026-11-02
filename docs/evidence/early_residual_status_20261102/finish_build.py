#!/usr/bin/env python3
"""Complete archive after test-only follow-up, using successful frozen production compilations."""
import ast,hashlib,json,pathlib,shutil,subprocess
E=pathlib.Path(__file__).resolve().parent;W=pathlib.Path('/private/tmp/early-residual-status-20261102');B=E/'native_build';P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-residual-consistency-20261102');T=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102/native_terminal');C='external/libtorch_wrf/sdirk3/';old='e7fc08a1bb19732ae551fd35413fb246b2598b84';new='d8024e52'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a,**kw):return subprocess.check_output(list(map(str,a)),text=True,**kw).strip()
head=out(['git','rev-parse','HEAD'],cwd=W);assert subprocess.run(['git','merge-base','--is-ancestor',new,head],cwd=W).returncode==0 and not out(['git','status','--porcelain','--untracked-files=all'],cwd=W)
assert out(['git','diff','--name-only',old,head],cwd=W)==C+'tests/test_newton_early_residual.cpp'
log=(E/'build_driver.log').read_text(); assert 'AssertionError: sources changed during build' in log, 'unexpected compile result; inspect retained log'
commands=[ast.literal_eval(line[4:]) for line in log.splitlines() if line.startswith('RUN ')];assert len(commands)==10
tracked=out(['git','ls-files',C],cwd=W).splitlines();pins={p:sha(W/p) for p in tracked if (W/p).is_file()}
for p,h in pins.items():
 if p!=C+'tests/test_newton_early_residual.cpp':assert hashlib.sha256(subprocess.check_output(['git','show',old+':'+p],cwd=W)).hexdigest()==h,p
compiled=[]
for a in commands:
 obj=pathlib.Path(a[a.index('-o')+1]);src=next(pathlib.Path(x) for x in a if x.endswith('.cpp')); assert obj.is_file() and obj.with_suffix('.d').is_file(); assert (B/(src.name+'.log')).is_file()
 compiled.append({'source':src.name,'source_sha256':sha(src),'argv':a,'object_sha256':sha(obj),'depfile_sha256':sha(obj.with_suffix('.d'))})
assert len({x['source'] for x in compiled})==10
r=json.loads((T/'build_receipt.json').read_text());prev=json.loads((P/'newton_build_receipt.json').read_text());baseline=P/'native_build/libwrf_sdirk3_libtorch.a';assert sha(baseline)==prev['archive_sha256']
archive=B/'libwrf_sdirk3_libtorch.a';assert not archive.exists();shutil.copyfile(baseline,archive)
subprocess.run(['ar','-r',str(archive),*[x['argv'][x['argv'].index('-o')+1] for x in compiled]],check=True);subprocess.run(['ranlib',str(archive)],check=True)
assert all(sha(W/p)==h for p,h in pins.items())
receipt={'source_commit':head,'compiled_source_commit':old,'only_followup_change':C+'tests/test_newton_early_residual.cpp','source_hashes':pins,'prior_receipt_sha256':sha(P/'newton_build_receipt.json'),'baseline_archive_sha256':sha(baseline),'baseline_terminal_receipt_sha256':sha(T/'build_receipt.json'),'feature_header_sha256':prev['feature_header_sha256'],'baseline_depfile_hashes':{str(pathlib.Path(x['object']).with_suffix('.d')):sha(pathlib.Path(x['object']).with_suffix('.d')) for x in r['source_objects']},'affected_sources':[x['source'] for x in compiled],'compiled':compiled,'archive_members':r['archive_members'],'archive_sha256':sha(archive),'model_run':False,'note':'research-feature archive; source-only test correction after production compilation; native link/run deferred'}
(E/'early_build_receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
