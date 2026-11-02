#!/usr/bin/env python3
"""After clean source freeze: build_early.py TARGET_COMMIT. Never runs the model."""
import hashlib,json,pathlib,shutil,subprocess,sys
E=pathlib.Path(__file__).resolve().parent; W=pathlib.Path('/private/tmp/early-residual-status-20261102')
P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-residual-consistency-20261102')
T=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102/native_terminal')
BASE='696def3b4719fad24f2c2040d1fbe7ae5bcf1824'; C='external/libtorch_wrf/sdirk3/'
r=json.loads((T/'build_receipt.json').read_text()); prev=json.loads((P/'newton_build_receipt.json').read_text())
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a,**kw): return subprocess.check_output(list(map(str,a)),text=True,**kw).strip()
def run(a,**kw): print('RUN',a,flush=True); subprocess.run(a,check=True,**kw)
if len(sys.argv)!=2: sys.exit(__doc__)
target=sys.argv[1]
assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain','--untracked-files=all'],cwd=W)
assert subprocess.run(['git','merge-base','--is-ancestor',BASE,target],cwd=W).returncode==0
changed=out(['git','diff','--name-only',BASE,target],cwd=W).splitlines()
allowed={C+x for x in ('wrf_sdirk3_newton_solver.cpp','wrf_sdirk3_newton_solver.h','wrf_sdirk3_tile_unified_impl.cpp','wrf_sdirk3_first_failure.h','test_trust_model_contract.cpp','tests/test_newton_early_residual.cpp','tests/test_first_failure_classification.cpp')}
assert set(changed)<=allowed,changed
archive=P/'native_build/libwrf_sdirk3_libtorch.a'; assert sha(archive)==prev['archive_sha256']; assert sha(T/'build_receipt.json')==prev['baseline_receipt_sha256']
base_drift={C+'wrf_sdirk3_linear_delay_audit.h',C+'wrf_sdirk3_terminal_jvp_probe.h'}
for p,h in prev['source_hashes'].items():
 if p in base_drift:
  assert sha(W/p)==hashlib.sha256(subprocess.check_output(['git','show',BASE+':'+p],cwd=W)).hexdigest(),p
 elif p not in allowed and p!=C+'test_trust_model_contract.cpp': assert sha(W/p)==h,p
assert C+'test_trust_model_contract.cpp' in changed
feature=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-a4-20261031/a4_build/B/wrf_sdirk3_pressure_reference_expected.h'); assert sha(feature)==prev['feature_header_sha256']
members=[x for x in out(['ar','-t',archive]).splitlines() if not x.startswith('__.SYMDEF')]; assert members==r['archive_members'] and len(members)==31
baseline=T/'libwrf_sdirk3_libtorch.a'; assert sha(baseline)==r['archive_sha256']
for m in members:
 got=subprocess.check_output(['ar','-p',str(archive),m]); old=subprocess.check_output(['ar','-p',str(baseline),m]); assert (hashlib.sha256(got).hexdigest()==prev['newton_object_sha256'] if m=='wrf_sdirk3_newton_solver.o' else got==old),m
hits=[]; dep_hashes={}
for entry in r['source_objects']:
 dep=pathlib.Path(entry['object']).with_suffix('.d'); assert dep.is_file(),dep; dep_hashes[str(dep)]=sha(dep)
 if 'wrf_sdirk3_first_failure.h' in dep.read_text(): hits.append(entry['source'])
expected=['wrf_sdirk3_tile_unified_interface.cpp','wrf_sdirk3_interface_zerocopy.cpp','wrf_sdirk3_tile_unified.cpp','wrf_sdirk3_tile_unified_impl.cpp','wrf_sdirk3_newton_solver.cpp','wrf_sdirk3_base_state_zerocopy.cpp','wrf_sdirk3_unified_preconditioner.cpp','wrf_sdirk3_cache_utils.cpp','wrf_sdirk3_pressure_stage_probe.cpp','wrf_sdirk3_native_pressure_reference.cpp']
assert hits==expected,hits
for hdr in base_drift | {C+'wrf_sdirk3_newton_solver.h'}:
 consumers=[x['source'] for x in r['source_objects'] if pathlib.Path(hdr).name in pathlib.Path(x['object']).with_suffix('.d').read_text()]
 assert set(consumers)<=set(hits),(hdr,consumers)
B=E/'native_build'; B.mkdir(exist_ok=False); compiled=[]
tracked=out(['git','ls-files',C],cwd=W).splitlines(); pins={p:sha(W/p) for p in tracked if (W/p).is_file()}
for source in hits:
 entry=next(x for x in r['source_objects'] if x['source']==source)
 a=prev['newton_argv'][:] if source=='wrf_sdirk3_newton_solver.cpp' else entry['argv'][:]
 # Replace any recorded historical source-directory include/source with current worktree.
 for i,arg in enumerate(a):
  marker='/external/libtorch_wrf/sdirk3'
  if marker in arg:
   prefix,suffix=arg.split(marker,1); a[i]=('-I' if prefix.startswith('-I') else '')+str(W/C)+suffix
 a[a.index('-o')+1]=str(B/pathlib.Path(source).with_suffix('.o').name)
 with (B/(source+'.log')).open('w') as log: run(a,cwd=W,stdout=log,stderr=subprocess.STDOUT)
 compiled.append({'source':source,'source_sha256':sha(W/C/source),'argv':a,'object_sha256':sha(a[a.index('-o')+1]),'depfile_sha256':sha(pathlib.Path(a[a.index('-o')+1]).with_suffix('.d'))})
assert all(sha(W/p)==h for p,h in pins.items()),'sources changed during build'
new=B/'libwrf_sdirk3_libtorch.a';shutil.copyfile(archive,new); run(['ar','-r',new,*[B/pathlib.Path(s).with_suffix('.o').name for s in hits]]);run(['ranlib',new])
receipt={'source_commit':target,'base_commit':BASE,'changed_files':changed,'source_hashes':pins,'prior_receipt_sha256':sha(P/'newton_build_receipt.json'),'baseline_archive_sha256':sha(archive),'baseline_terminal_receipt_sha256':sha(T/'build_receipt.json'),'feature_header_sha256':sha(feature),'baseline_depfile_hashes':dep_hashes,'affected_sources':hits,'compiled':compiled,'archive_members':members,'archive_sha256':sha(new),'model_run':False,'note':'research-feature archive; native link/run deferred'}
(E/'early_build_receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
