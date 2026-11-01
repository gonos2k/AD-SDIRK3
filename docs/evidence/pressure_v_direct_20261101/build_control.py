#!/usr/bin/env python3
"""Draft 31-unit Direct-V control: verified six components, 24 old units, new Newton; no model."""
import hashlib,json,os,pathlib,shutil,subprocess,sys,time
P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-v-direct-20261101');E=P/'native_control';W=pathlib.Path('/private/tmp/pressure-v-direct-20261101');A=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-coupled-inverse-20261101/native_control');Q=P/'component_refresh';N=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_build');C='external/libtorch_wrf/sdirk3';base='e05ab44a01886ce493f39cc928134793db56615c';oldW='/private/tmp/pressure-coupled-inverse-20261101';os.environ['PATH']='/opt/homebrew/bin:/usr/bin:/bin'
if len(sys.argv)!=4 or len(sys.argv[1])!=40 or len(sys.argv[3])!=64:raise SystemExit('Requires FROZEN_SHA REGISTERED_PROTOCOL_PATH PROTOCOL_SHA; no build attempted')
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
prior=json.loads((A/'build_receipt.json').read_text());component=json.loads((Q/'receipt.json').read_text());assert prior['source_commit']==base and component['passed'] and component['source_header_test_hashes_unchanged'] and sha(A/'wrf.exe')==prior['executable_sha256'] and sha(A/'libwrf_sdirk3_libtorch.a')==prior['archive_sha256']
pv=json.loads((N/'complete_provenance.json').read_text())
for p,h in pv['object_module_archive_sha256'].items():assert sha(N/'source'/p)==h,p
for p,h in pv['dylib_sha256'].items():
 if h!='macOS shared cache':assert sha(p)==h,p
changed=out(['git','diff','--name-only',base,target],cwd=W).splitlines();assert not [p for p in changed if p.startswith(('dyn_em/','Registry/','frame/','share/','phys/','main/','arch/'))]
expected={C+'/'+x for x in ('wrf_sdirk3_newton_solver.cpp','wrf_sdirk3_core_sources.txt','wrf_sdirk3_reference_pressure.cpp','wrf_sdirk3_reference_pressure.h','wrf_sdirk3_reference_pressure_model.cpp','wrf_sdirk3_reference_pressure_model.h','wrf_sdirk3_native_pressure_reference.h','wrf_sdirk3_v_primary_probe.cpp','wrf_sdirk3_v_primary_probe.h','wrf_sdirk3_pressure_v_direct.cpp','wrf_sdirk3_pressure_v_direct.h')};actual={p for p in changed if p.startswith(C+'/') and not p.startswith(C+'/tests/') and p!=C+'/CMakeLists.txt'};assert actual==expected,(sorted(actual),sorted(expected))
oldrecords=prior['source_objects'];assert len(oldrecords)==30;old={x['source']:x for x in oldrecords};fresh={x['source']:x for x in component['reused_five']};fresh[component['fresh_direct']['source']]=component['fresh_direct'];assert len(fresh)==6
manifest=[x.strip() for x in (W/C/'wrf_sdirk3_core_sources.txt').read_text().splitlines() if x.strip() and not x.lstrip().startswith('#')];assert len(manifest)==31 and len(set(manifest))==31 and set(manifest)==set(old)|{'wrf_sdirk3_pressure_v_direct.cpp'}
snapshot=json.loads((Q/'input_hashes.json').read_text());assert sha(Q/'input_hashes.json')==component['input_hashes_sha256'];changed_headers={'wrf_sdirk3_reference_pressure.h','wrf_sdirk3_reference_pressure_model.h','wrf_sdirk3_v_primary_probe.h','wrf_sdirk3_native_pressure_reference.h','wrf_sdirk3_pressure_v_direct.h'}
for n,x in fresh.items():
 assert sha(W/C/n)==x['source_sha256'] and sha(x['object'])==x['object_sha256'];assert all(f in x['argv'] for f in ('-DWRF_SDIRK3_PRESSURE_STAGE_PROBE','-DWRF_SDIRK3_PRESSURE_A4','-DWRF_SDIRK3_PRESSURE_A5'))
for path,h in snapshot.items():
 if pathlib.Path(path).suffix in ('.h','.hpp'):assert sha(W/path)==h,path
for n,x in old.items():
 if n=='wrf_sdirk3_newton_solver.cpp' or n in fresh:continue
 p=pathlib.Path(x['object']);dep=p.with_suffix('.d');assert dep.is_file() and not any(h in dep.read_text(errors='replace') for h in changed_headers) and sha(W/C/n)==x['source_sha256'] and sha(p)==x['object_sha256'] and hashlib.sha256(subprocess.check_output(['ar','-p',str(A/'libwrf_sdirk3_libtorch.a'),p.name])).hexdigest()==x['object_sha256']
assert len([n for n in old if n!='wrf_sdirk3_newton_solver.cpp' and n not in fresh])==24
E.mkdir();n=old['wrf_sdirk3_newton_solver.cpp'];obj=E/'wrf_sdirk3_newton_solver.o';cmd=[x.replace(oldW,str(W)) for x in n['argv']];cmd[-1]=str(obj);assert '-DWRF_SDIRK3_PRESSURE_V_DIRECT' not in cmd and '-DWRF_SDIRK3_PRESSURE_COUPLED_INVERSE' not in cmd and '-DWRF_SDIRK3_PRESSURE_A5_VROW' not in cmd
with (E/'newton_compile.log').open('w') as f:subprocess.run(cmd,cwd=W/C,stdout=f,stderr=subprocess.STDOUT,check=True)
assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain'],cwd=W)
for path,h in snapshot.items():
 if pathlib.Path(path).suffix in ('.h','.hpp'):assert sha(W/path)==h,path
new={'source':'wrf_sdirk3_newton_solver.cpp','source_sha256':sha(W/C/'wrf_sdirk3_newton_solver.cpp'),'object':str(obj),'object_sha256':sha(obj),'argv':cmd,'depfile_sha256':sha(obj.with_suffix('.d'))};records=[new if n=='wrf_sdirk3_newton_solver.cpp' else fresh[n] if n in fresh else old[n] for n in manifest];assert all(sha(W/C/x['source'])==x['source_sha256'] for x in records)
archive=E/'libwrf_sdirk3_libtorch.a';subprocess.run(['ar','rcs',str(archive),*[x['object'] for x in records]],check=True);subprocess.run(['ranlib',str(archive)],check=True);members=[x for x in out(['ar','-t',archive]).splitlines() if not x.startswith('__.SYMDEF')];assert members==[pathlib.Path(x['object']).name for x in records]
for x in records:assert hashlib.sha256(subprocess.check_output(['ar','-p',str(archive),pathlib.Path(x['object']).name])).hexdigest()==sha(x['object'])
link=prior['link_argv'].copy();assert link[2]==str(A/'wrf.exe');link[2]=str(E/'wrf.exe');oldL='-L'+str(A);assert link.count(oldL)==1;link[link.index(oldL)]='-L'+str(E);oldarc=str(A/'libwrf_sdirk3_libtorch.a');assert link.count(oldarc)==1;link[link.index(oldarc)]=str(archive)
with (E/'link.log').open('w') as f:subprocess.run(link,cwd=prior['link_cwd'],stdout=f,stderr=subprocess.STDOUT,check=True)
otool=subprocess.check_output(['otool','-L',str(E/'wrf.exe')],text=True);(E/'otool.txt').write_text(otool);assert otool.splitlines()[1:]==(A/'otool.txt').read_text().splitlines()[1:]
r={'source_commit':target,'registration_sha256':regsha,'expected_preflight_digest_fnv64':prior['expected_preflight_digest_fnv64'],'expected_header_sha256':prior['expected_header_sha256'],'component_receipt_sha256':sha(Q/'receipt.json'),'component_combined_receipt_sha256':sha(P/'component_combined_receipt.json'),'prior_native_receipt_sha256':sha(A/'build_receipt.json'),'fortran_provenance_sha256':sha(N/'complete_provenance.json'),'source_objects':records,'header_sha256':{str(p.relative_to(W)):sha(p) for p in sorted((W/C).rglob('*')) if p.is_file() and p.suffix in ('.h','.hpp') and 'graphify-out' not in p.parts},'fresh_newton':new,'component_six_sha256':{n:x['object_sha256'] for n,x in fresh.items()},'reused_24_sha256':{n:x['object_sha256'] for n,x in old.items() if n!='wrf_sdirk3_newton_solver.cpp' and n not in fresh},'archive_members':members,'archive_sha256':sha(archive),'link_argv':link,'link_cwd':prior['link_cwd'],'executable_sha256':sha(E/'wrf.exe'),'postbuild_free_bytes':shutil.disk_usage(E).free,'model_run':False,'time':time.strftime('%Y-%m-%dT%H:%M:%S%z')};(E/'build_receipt.json').write_text(json.dumps(r,indent=2)+'\n');print('control executable SHA256',r['executable_sha256'])
