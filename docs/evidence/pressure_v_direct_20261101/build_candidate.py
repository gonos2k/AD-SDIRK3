#!/usr/bin/env python3
"""Draft V-row candidate Newton-only build, after frozen registered control build; never runs WRF."""
import hashlib,json,os,pathlib,shutil,subprocess,sys,time
W=pathlib.Path('/private/tmp/pressure-v-direct-20261101');E=pathlib.Path(__file__).resolve().parent/'native_candidate';O=E.parent/'native_control';C='external/libtorch_wrf/sdirk3';os.environ['PATH']='/opt/homebrew/bin:/usr/bin:/bin'
if len(sys.argv)!=4 or len(sys.argv[1])!=40 or len(sys.argv[3])!=64:raise SystemExit('Requires FROZEN_SHA REGISTERED_PROTOCOL_PATH PROTOCOL_SHA; no build attempted')
target=sys.argv[1];reg=pathlib.Path(sys.argv[2]).resolve();expected_reg=sys.argv[3]
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for x in iter(lambda:f.read(4*1024*1024),b''):h.update(x)
 return h.hexdigest()
def out(a,**kw):return subprocess.check_output(list(map(str,a)),text=True,**kw).strip()
assert reg.is_file();assert not E.exists() and reg.is_file() and sha(reg)==expected_reg and shutil.disk_usage(E.parent).free>=5*1024**3
registration=json.loads(reg.read_text());assert registration['status']=='registered' and registration['source_commit']==target and registration['order']==['control','candidate'] and registration['maximum_native_attempts']==2 and registration['retry'] is False
for p,h in registration['immutable_inputs'].items():assert sha(p)==h,p
assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain'],cwd=W)
base=json.loads((O/'build_receipt.json').read_text());assert base['source_commit']==target and base['registration_sha256']==expected_reg and sha(O/'wrf.exe')==base['executable_sha256'] and sha(O/'libwrf_sdirk3_libtorch.a')==base['archive_sha256']
headers={str(p.relative_to(W)):sha(p) for p in sorted((W/C).rglob('*')) if p.is_file() and p.suffix in ('.h','.hpp') and 'graphify-out' not in p.parts};assert headers==base['header_sha256'];records=base['source_objects'];assert len(records)==31 and len({x['source'] for x in records})==31
assert 'WRF_SDIRK3_PRESSURE_V_DIRECT' not in (W/C/'wrf_sdirk3_v_primary_probe.cpp').read_text() and 'WRF_SDIRK3_PRESSURE_V_DIRECT' not in (W/C/'wrf_sdirk3_v_primary_probe.h').read_text()
for x in records:
 assert sha(W/C/x['source'])==x['source_sha256'] and sha(x['object'])==x['object_sha256'] and hashlib.sha256(subprocess.check_output(['ar','-p',str(O/'libwrf_sdirk3_libtorch.a'),pathlib.Path(x['object']).name])).hexdigest()==x['object_sha256']
E.mkdir();newton=next(x for x in records if x['source']=='wrf_sdirk3_newton_solver.cpp');obj=E/'wrf_sdirk3_newton_solver.o';cmd=newton['argv'].copy();assert '-DWRF_SDIRK3_PRESSURE_V_DIRECT' not in cmd and '-DWRF_SDIRK3_PRESSURE_A5_VROW' not in cmd and '-DWRF_SDIRK3_PRESSURE_COUPLED_INVERSE' not in cmd and '-DWRF_SDIRK3_PRESSURE_REFERENCE_STAGE' in cmd;cmd.insert(1,'-DWRF_SDIRK3_PRESSURE_V_DIRECT');cmd[-1]=str(obj)
with (E/'newton_compile.log').open('w') as f:subprocess.run(cmd,cwd=W/C,stdout=f,stderr=subprocess.STDOUT,check=True)
assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain'],cwd=W) and all(sha(W/C/x['source'])==x['source_sha256'] for x in records)
assert headers=={str(p.relative_to(W)):sha(p) for p in sorted((W/C).rglob('*')) if p.is_file() and p.suffix in ('.h','.hpp') and 'graphify-out' not in p.parts}
objects=[str(obj) if x['source']=='wrf_sdirk3_newton_solver.cpp' else x['object'] for x in records];archive=E/'libwrf_sdirk3_libtorch.a';subprocess.run(['ar','rcs',str(archive),*objects],check=True);subprocess.run(['ranlib',str(archive)],check=True);members=[x for x in out(['ar','-t',archive]).splitlines() if not x.startswith('__.SYMDEF')];assert members==base['archive_members']
for p in objects:assert hashlib.sha256(subprocess.check_output(['ar','-p',str(archive),pathlib.Path(p).name])).hexdigest()==sha(p)
link=base['link_argv'].copy();assert link[2]==str(O/'wrf.exe');link[2]=str(E/'wrf.exe');old='-L'+str(O);assert link.count(old)==1;link[link.index(old)]='-L'+str(E);old=str(O/'libwrf_sdirk3_libtorch.a');assert link.count(old)==1;link[link.index(old)]=str(archive)
with (E/'link.log').open('w') as f:subprocess.run(link,cwd=base['link_cwd'],stdout=f,stderr=subprocess.STDOUT,check=True)
otool=subprocess.check_output(['otool','-L',str(E/'wrf.exe')],text=True);(E/'otool.txt').write_text(otool);assert otool.splitlines()[1:]==(O/'otool.txt').read_text().splitlines()[1:]
r={'source_commit':target,'registration_sha256':expected_reg,'control_build_receipt_sha256':sha(O/'build_receipt.json'),'candidate_newton_source_sha256':sha(W/C/newton['source']),'candidate_newton_object_sha256':sha(obj),'candidate_newton_compile_argv':cmd,'reused_30_objects_sha256':{x['source']:x['object_sha256'] for x in records if x['source']!='wrf_sdirk3_newton_solver.cpp'},'header_sha256':headers,'expected_preflight_digest_fnv64':base['expected_preflight_digest_fnv64'],'expected_header_sha256':base['expected_header_sha256'],'archive_members':members,'archive_sha256':sha(archive),'link_argv':link,'link_cwd':base['link_cwd'],'executable_sha256':sha(E/'wrf.exe'),'postbuild_free_bytes':shutil.disk_usage(E).free,'model_run':False,'time':time.strftime('%Y-%m-%dT%H:%M:%S%z')};(E/'build_receipt.json').write_text(json.dumps(r,indent=2)+'\n');print('candidate executable SHA256',r['executable_sha256'])
