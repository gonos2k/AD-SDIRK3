#!/usr/bin/env python3
"""Draft Newton-only control rebuild. Requires reviewed frozen source/protocol; no model."""
import hashlib,json,os,pathlib,shutil,subprocess,sys,time
W=pathlib.Path('/private/tmp/pressure-coupled-inverse-20261101');P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-coupled-inverse-20261101');E=P/'native_control';O=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-a5-20261101/vrow_pair_draft/build_control');N=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_build');C='external/libtorch_wrf/sdirk3';base='112f3449b177128bfd71d645cead81606a5708c7';oldW='/private/tmp/pressure-a5-20261101';os.environ['PATH']='/opt/homebrew/bin:/usr/bin:/bin'
if len(sys.argv)!=4 or len(sys.argv[1])!=40 or len(sys.argv[3])!=64:raise SystemExit('Requires FROZEN_SHA REVIEWED_REGISTERED_PROTOCOL_PATH PROTOCOL_SHA; no build attempted')
target=sys.argv[1];reg=pathlib.Path(sys.argv[2]).resolve();regsha=sys.argv[3]
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()
def out(a,**kw):return subprocess.check_output(list(map(str,a)),text=True,**kw).strip()
assert not E.exists() and reg.is_file() and sha(reg)==regsha and shutil.disk_usage(P).free>=5*1024**3
policy=json.loads(reg.read_text());assert policy['status']=='registered' and policy['source_commit']==target and policy['order']==['control','candidate'] and policy['maximum_native_attempts']==2 and policy['retry'] is False
for p,h in policy['immutable_inputs'].items():assert sha(p)==h,p
assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain'],cwd=W)
pv=json.loads((N/'complete_provenance.json').read_text())
for p,h in pv['object_module_archive_sha256'].items():assert sha(N/'source'/p)==h,p
for p,h in pv['dylib_sha256'].items():
 if h!='macOS shared cache':assert sha(p)==h,p
prior=json.loads((O/'build_receipt.json').read_text());assert prior['source_commit']==base and sha(O/'wrf.exe')==prior['executable_sha256'] and sha(O/'libwrf_sdirk3_libtorch.a')==prior['archive_sha256']
expected_header=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-a4-20261031/a4_build/B/wrf_sdirk3_pressure_reference_expected.h');assert sha(expected_header)==prior['expected_header_sha256'] and prior['expected_preflight_digest_fnv64']=='7bdd17f3a694726e'
changed=out(['git','diff','--name-only',base,target],cwd=W).splitlines();production=[p for p in changed if p.startswith(C+'/') and not p.startswith(C+'/tests/') and p!=C+'/CMakeLists.txt'];expected={C+'/wrf_sdirk3_newton_solver.cpp',C+'/wrf_sdirk3_coupled_pressure_inverse.h'};assert set(production)==expected,production;assert not [p for p in changed if p.startswith(('dyn_em/','Registry/','frame/','share/','phys/','main/','arch/'))]
headers={str(p.relative_to(W)):sha(p) for p in sorted((W/C).rglob('*')) if p.is_file() and p.suffix in ('.h','.hpp') and 'graphify-out' not in p.parts};assert set(headers)-set(prior['header_sha256'])=={C+'/wrf_sdirk3_coupled_pressure_inverse.h'} and not(set(prior['header_sha256'])-set(headers));assert all(headers[k]==v for k,v in prior['header_sha256'].items())
records=prior['source_objects'];names=[x.strip() for x in (W/C/'wrf_sdirk3_core_sources.txt').read_text().splitlines() if x.strip() and not x.lstrip().startswith('#')];assert len(records)==len(names)==30 and [x['source'] for x in records]==names
for x in records:
 obj=pathlib.Path(x['object']);assert sha(obj)==x['object_sha256'] and hashlib.sha256(subprocess.check_output(['ar','-p',str(O/'libwrf_sdirk3_libtorch.a'),obj.name])).hexdigest()==x['object_sha256']
 if x['source']!='wrf_sdirk3_newton_solver.cpp':
  dep=obj.with_suffix('.d');assert sha(W/C/x['source'])==x['source_sha256'] and dep.is_file() and 'wrf_sdirk3_coupled_pressure_inverse.h' not in dep.read_text(errors='replace') and 'WRF_SDIRK3_PRESSURE_COUPLED_INVERSE' not in (W/C/x['source']).read_text(errors='replace')
E.mkdir();n=next(x for x in records if x['source']=='wrf_sdirk3_newton_solver.cpp');obj=E/'wrf_sdirk3_newton_solver.o';cmd=[x.replace(oldW,str(W)) for x in n['argv']];cmd[-1]=str(obj);assert '-DWRF_SDIRK3_PRESSURE_COUPLED_INVERSE' not in cmd and '-DWRF_SDIRK3_PRESSURE_A5_VROW' not in cmd
with (E/'newton_compile.log').open('w') as f:subprocess.run(cmd,cwd=W/C,stdout=f,stderr=subprocess.STDOUT,check=True)
assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain'],cwd=W);assert headers=={str(p.relative_to(W)):sha(p) for p in sorted((W/C).rglob('*')) if p.is_file() and p.suffix in ('.h','.hpp') and 'graphify-out' not in p.parts}
for x in records:
 if x['source']!='wrf_sdirk3_newton_solver.cpp':assert sha(W/C/x['source'])==x['source_sha256']
selected=[str(obj) if x['source']=='wrf_sdirk3_newton_solver.cpp' else x['object'] for x in records];archive=E/'libwrf_sdirk3_libtorch.a';subprocess.run(['ar','rcs',str(archive),*selected],check=True);subprocess.run(['ranlib',str(archive)],check=True);members=[x for x in out(['ar','-t',archive]).splitlines() if not x.startswith('__.SYMDEF')];assert members==prior['archive_members']
for path in selected:assert hashlib.sha256(subprocess.check_output(['ar','-p',str(archive),pathlib.Path(path).name])).hexdigest()==sha(path)
link=prior['link_argv'].copy();assert link[2]==str(O/'wrf.exe');link[2]=str(E/'wrf.exe');old='-L'+str(O);assert link.count(old)==1;link[link.index(old)]='-L'+str(E);old=str(O/'libwrf_sdirk3_libtorch.a');assert link.count(old)==1;link[link.index(old)]=str(archive)
with (E/'link.log').open('w') as f:subprocess.run(link,cwd=prior['link_cwd'],stdout=f,stderr=subprocess.STDOUT,check=True)
otool=subprocess.check_output(['otool','-L',str(E/'wrf.exe')],text=True);(E/'otool.txt').write_text(otool);assert otool.splitlines()[1:]==(O/'otool.txt').read_text().splitlines()[1:]
new={'source':'wrf_sdirk3_newton_solver.cpp','source_sha256':sha(W/C/n['source']),'object':str(obj),'object_sha256':sha(obj),'argv':cmd,'depfile_sha256':sha(obj.with_suffix('.d'))};chosen=[new if x['source']==new['source'] else x for x in records];receipt={'source_commit':target,'prior_commit':base,'registration_path':str(reg),'registration_sha256':regsha,'prior_build_receipt_sha256':sha(O/'build_receipt.json'),'clean_fortran_provenance_sha256':sha(N/'complete_provenance.json'),'source_objects':chosen,'header_sha256':headers,'newton_compile_argv':cmd,'reused_29_objects_sha256':{x['source']:x['object_sha256'] for x in records if x['source']!='wrf_sdirk3_newton_solver.cpp'},'expected_preflight_digest_fnv64':prior['expected_preflight_digest_fnv64'],'expected_header_sha256':prior['expected_header_sha256'],'archive_members':members,'archive_sha256':sha(archive),'link_argv':link,'link_cwd':prior['link_cwd'],'executable_sha256':sha(E/'wrf.exe'),'postbuild_free_bytes':shutil.disk_usage(E).free,'model_run':False,'time':time.strftime('%Y-%m-%dT%H:%M:%S%z')};(E/'build_receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print('control executable SHA256',receipt['executable_sha256'])
