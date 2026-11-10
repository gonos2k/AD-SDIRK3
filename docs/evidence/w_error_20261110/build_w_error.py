#!/usr/bin/env python3
"""After source freeze: build_w_error.py TARGET_HEAD. No model run."""
import hashlib,json,pathlib,shutil,subprocess,sys
E=pathlib.Path(__file__).resolve().parent;W=pathlib.Path('/Users/yhlee/SDIRK3-review-worktrees/w-error-envelope-20261110');P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/w-runtime-contract-20261110');C='external/libtorch_wrf/sdirk3/';BASE='3d4f7857f7683a42f355fee20550d2df04a1b322'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a):return subprocess.check_output(a,cwd=W,text=True).strip()
if len(sys.argv)!=2:raise SystemExit(__doc__)
target=sys.argv[1];assert out(['git','rev-parse','HEAD'])==target and not out(['git','status','--porcelain','--untracked-files=all']);assert subprocess.run(['git','merge-base','--is-ancestor',BASE,target],cwd=W).returncode==0
stack=E/'stack_feasibility.json';assert json.loads(stack.read_text())['status']=='PASS';prior=P/'build_receipt.json';r=json.loads(prior.read_text());archive=P/'native_build/libwrf_sdirk3_libtorch.a';assert sha(archive)==r['archive_sha256']
allowed={C+x for x in ('wrf_sdirk3_pressure_stage_probe.h','wrf_sdirk3_pressure_stage_probe.cpp','wrf_sdirk3_tile_unified_impl.cpp','wrf_sdirk3_newton_solver.cpp','wrf_sdirk3_accepted_step_probe.h','tests/test_accepted_step_probe.cpp')}
changed=out(['git','diff','--name-only',BASE,target]).splitlines();assert all(not p.startswith(C) or p in allowed for p in changed),changed
for p,h in r['source_hashes'].items():
 if p not in allowed:assert sha(W/p)==h,p
expected={x['source'] for x in r['compiled']};assert expected=={'wrf_sdirk3_tile_unified_impl.cpp','wrf_sdirk3_newton_solver.cpp','wrf_sdirk3_pressure_stage_probe.cpp','wrf_sdirk3_native_pressure_reference.cpp','wrf_sdirk3_v_primary_probe.cpp'}
prov=r['dependency_provenance'];assert len(prov)==31
for x in r['compiled']:
 obj=pathlib.Path(x['argv'][x['argv'].index('-o')+1]);prov[x['source']]={'depfile':str(obj.with_suffix('.d')),'sha256':x['depfile_sha256']}
for x in prov.values():assert sha(x['depfile'])==x['sha256']
pressure=[s for s,x in prov.items() if 'wrf_sdirk3_pressure_stage_probe.h' in pathlib.Path(x['depfile']).read_text()];assert set(pressure)==expected,pressure
for entry in r['compiled']:
 member=pathlib.Path(entry['source']).with_suffix('.o').name;assert hashlib.sha256(subprocess.check_output(['ar','-p',str(archive),member])).hexdigest()==entry['object_sha256'],member
members=[x for x in subprocess.check_output(['ar','-t',str(archive)],text=True).splitlines() if not x.startswith('__.SYMDEF')];assert members==r['archive_members'] and len(members)==31
B=E/'native_build';B.mkdir(exist_ok=False);tracked=out(['git','ls-files',C]).splitlines();pins={p:sha(W/p) for p in tracked if (W/p).is_file()};compiled=[]
for source in sorted(expected):
 entry=next(x for x in r['compiled'] if x['source']==source);a=entry['argv'][:]
 for i,arg in enumerate(a):
  marker='/external/libtorch_wrf/sdirk3'
  if marker in arg:
   prefix,suffix=arg.split(marker,1);a[i]=('-I' if prefix.startswith('-I') else '')+str(W/C)+suffix
 if source in ('wrf_sdirk3_tile_unified_impl.cpp','wrf_sdirk3_newton_solver.cpp'):assert '-DWRF_SDIRK3_ACCEPTED_STEP_PROBE' in a
 for macro in ('WRF_SDIRK3_PRESSURE_STAGE_PROBE','WRF_SDIRK3_PRESSURE_A5'):assert '-D'+macro in a,(source,macro)
 for macro in ('WRF_SDIRK3_TERMINAL_JVP_PROBE','WRF_SDIRK3_MATCHED_PRESSURE_PROBE','WRF_SDIRK3_OPERATOR_GAP_PROBE','WRF_SDIRK3_PRESSURE_V_DIRECT'):assert '-D'+macro not in a,(source,macro)
 obj=B/pathlib.Path(source).with_suffix('.o').name;a[a.index('-o')+1]=str(obj);print('COMPILING',source,flush=True)
 with (E/(source+'.log')).open('x') as log:subprocess.run(a,cwd=W,stdout=log,stderr=subprocess.STDOUT,check=True)
 compiled.append({'source':source,'source_sha256':sha(W/C/source),'argv':a,'object_sha256':sha(obj),'depfile_sha256':sha(obj.with_suffix('.d'))})
assert out(['git','rev-parse','HEAD'])==target and all(sha(W/p)==h for p,h in pins.items())
new=B/'libwrf_sdirk3_libtorch.a';shutil.copyfile(archive,new);assert new.stat().st_ino!=archive.stat().st_ino;subprocess.run(['ar','-r',str(new),*[str(B/pathlib.Path(s).with_suffix('.o').name) for s in sorted(expected)]],check=True);subprocess.run(['ranlib',str(new)],check=True)
assert [x for x in subprocess.check_output(['ar','-t',str(new)],text=True).splitlines() if not x.startswith('__.SYMDEF')]==members
for m in members:
 data=subprocess.check_output(['ar','-p',str(new),m]);src=pathlib.Path(m).with_suffix('.cpp').name;assert (hashlib.sha256(data).hexdigest()==sha(B/m) if src in expected else data==subprocess.check_output(['ar','-p',str(archive),m])),m
assert sha(archive)==r['archive_sha256']
rec={'source_commit':target,'base_commit':BASE,'source_hashes':pins,'stack_feasibility_sha256':sha(stack),'prior_build_receipt_sha256':sha(prior),'prior_archive_sha256':sha(archive),'dependency_provenance':prov,'compiled':compiled,'archive_members':members,'archive_sha256':sha(new),'profile':'accepted-step W error capture, research only','model_run':False};(E/'build_receipt.json').write_text(json.dumps(rec,indent=2)+'\n')
