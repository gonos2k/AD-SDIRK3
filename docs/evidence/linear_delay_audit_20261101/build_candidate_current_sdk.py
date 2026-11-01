#!/usr/bin/env python3
"""DRAFT. Build control, or candidate after control gate; never runs WRF."""
import hashlib,json,pathlib,shutil,subprocess,sys
W=pathlib.Path('/private/tmp/linear-delay-audit-20261101'); E=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101'); A=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-v-direct-20261101/native_control'); N=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_build'); C=W/'external/libtorch_wrf/sdirk3'; TARGET='5dc7fc7c8c064f71dc2a272099852f4a529bb73f'; BASE='baacbb4bd76f5e9ec93bfa13baf2e47877c29178'
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()
def output(a,**k):return subprocess.check_output(list(map(str,a)),text=True,**k).strip()
if len(sys.argv)!=4 or sys.argv[1] not in ('control','candidate'):raise SystemExit('usage: build_pair control|candidate registered_protocol_path sha256')
case=sys.argv[1]; assert case=='candidate'; reg=pathlib.Path(sys.argv[2]); assert sha(reg)==sys.argv[3]; policy=json.loads(reg.read_text()); assert policy['status']=='registered' and policy['source_commit']==TARGET and policy['order']==['control','candidate'] and policy['maximum_native_attempts']==2 and not policy['retry']
for p,h in policy['immutable_inputs'].items():assert sha(p)==h,p
assert output(['git','rev-parse','HEAD'],cwd=W)==TARGET and not output(['git','status','--porcelain'],cwd=W)
changed=output(['git','diff','--name-only',BASE,TARGET],cwd=W).splitlines(); prod=[p for p in changed if p.startswith(('dyn_em/','Registry/','frame/','share/','phys/','main/','arch/','external/libtorch_wrf/sdirk3/')) and '/tests/' not in p and not p.endswith('CMakeLists.txt')]; assert set(prod)=={'external/libtorch_wrf/sdirk3/'+x for x in ['wrf_sdirk3_newton_solver.cpp','wrf_sdirk3_tile_unified_impl.cpp','wrf_sdirk3_linear_delay_audit.h']},prod
prior=json.loads((A/'build_receipt.json').read_text()); assert prior['source_commit']==BASE and sha(A/'wrf.exe')==prior['executable_sha256'] and sha(A/'libwrf_sdirk3_libtorch.a')==prior['archive_sha256']; old=prior['source_objects']; assert len(old)==31
proof=json.loads((N/'complete_provenance.json').read_text()); assert sha(N/'complete_provenance.json')==prior['fortran_provenance_sha256']
for p,h in proof['object_module_archive_sha256'].items():assert sha(N/'source'/p)==h,p
for p,h in proof['dylib_sha256'].items():
 if h!='macOS shared cache':assert sha(p)==h,p
header='wrf_sdirk3_linear_delay_audit.h'; assert sha(C/header)=='5d1866fc9296154b9522f391254c7f7c60f0b9c8fa1a848c52a6b5e3a8ee44bb'; assert sha(E/'tests_v7/receipt.json')==policy['observer_test_receipt_sha256'] and json.loads((E/'tests_v7/receipt.json').read_text())['passed']
assert [p.name for p in C.glob('*.cpp') if ('#include "'+header+'"') in p.read_text()]==[p.name for p in C.glob('*.cpp') if p.name in ('wrf_sdirk3_newton_solver.cpp','wrf_sdirk3_tile_unified_impl.cpp')]
for p,h in prior['header_sha256'].items():assert sha(W/p)==h,p
skip={'wrf_sdirk3_newton_solver.cpp','wrf_sdirk3_tile_unified_impl.cpp'}
for x in old:
 if x['source'] in skip:continue
 obj=pathlib.Path(x['object']); assert sha(C/x['source'])==x['source_sha256'] and sha(obj)==x['object_sha256']; assert hashlib.sha256(subprocess.check_output(['ar','-p',str(A/'libwrf_sdirk3_libtorch.a'),obj.name])).hexdigest()==x['object_sha256']; assert header not in obj.with_suffix('.d').read_text(errors='replace')
stack=json.loads((E/'current_sdk_provenance.json').read_text()); assert output(['xcrun','--show-sdk-path'])==stack['SDK_path'] and output(['xcrun','--show-sdk-version'])==stack['SDK_version'] and sha('/usr/bin/g++')==stack['compiler_driver_sha256'] and sha(stack['compiler_resolved_path'])==stack['compiler_resolved_sha256'] and output(['/usr/bin/g++','--version'])==stack['compiler_version'] and output(['sw_vers'])==stack['OS']
out=E/('native_'+case); assert not out.exists() and shutil.disk_usage(E).free>=5*1024**3
if case=='candidate':
 gate=E/'candidate_build_control_gate.json'; g=json.loads(gate.read_text()); assert g['eligible'] is True and sha(pathlib.Path(g['control_result_path']))==g['control_result_sha256']; control=json.loads((E/'native_control/build_receipt.json').read_text()); assert control['source_commit']==TARGET and control['registration_sha256']==sys.argv[3]
out.mkdir(); records=[]
for x in old:
 n=x['source']
 if n not in skip:records.append(x);continue
 if case=='candidate' and n=='wrf_sdirk3_tile_unified_impl.cpp':
  y=next(y for y in control['source_objects'] if y['source']==n); assert sha(y['object'])==y['object_sha256']; records.append(y);continue
 obj=out/(pathlib.Path(n).stem+'.o'); argv=x['argv'].copy(); oldsrc=str(pathlib.Path(argv[-3]).parent) if False else None
 # Replace only the recorded source/include root, and explicit output.
 oldroot=str(pathlib.Path(next(a for a in argv if a.endswith('/'+n))).parent)
 argv=[a.replace(oldroot,str(C)) for a in argv]; argv[-1]=str(obj); argv.insert(1,'-DWRF_SDIRK3_LINEAR_DELAY_AUDIT')
 if case=='candidate':argv.insert(1,'-DWRF_SDIRK3_PRESSURE_V_DIRECT')
 with (out/(pathlib.Path(n).stem+'.log')).open('w') as f:subprocess.run(argv,cwd=C,stdout=f,stderr=subprocess.STDOUT,check=True)
 records.append({'source':n,'source_sha256':sha(C/n),'object':str(obj),'object_sha256':sha(obj),'argv':argv,'depfile_sha256':sha(obj.with_suffix('.d'))})
assert len(records)==31 and len({x['source'] for x in records})==31
archive=out/'libwrf_sdirk3_libtorch.a'; subprocess.run(['ar','rcs',str(archive),*[x['object'] for x in records]],check=True);subprocess.run(['ranlib',str(archive)],check=True)
members=[x for x in output(['ar','-t',archive]).splitlines() if not x.startswith('__.SYMDEF')]; assert members==[pathlib.Path(x['object']).name for x in records]
for x in records:assert hashlib.sha256(subprocess.check_output(['ar','-p',str(archive),pathlib.Path(x['object']).name])).hexdigest()==x['object_sha256']
link=prior['link_argv'].copy();link[2]=str(out/'wrf.exe'); link[link.index('-L'+str(A))]='-L'+str(out);link[link.index(str(A/'libwrf_sdirk3_libtorch.a'))]=str(archive)
with (out/'link.log').open('w') as f:subprocess.run(link,cwd=prior['link_cwd'],stdout=f,stderr=subprocess.STDOUT,check=True)
otool=output(['otool','-L',out/'wrf.exe']);(out/'otool.txt').write_text(otool+'\n'); assert otool.splitlines()[1:]==(E/'native_control/otool.txt').read_text().splitlines()[1:]
def build_version(p):
 lines=output(['otool','-l',p]).splitlines();i=next(i for i,x in enumerate(lines) if x.strip()=='cmd LC_BUILD_VERSION');return lines[i:i+6]
assert build_version(out/'wrf.exe')==build_version(E/'native_control/wrf.exe')
assert output(['xcrun','--show-sdk-path'])==stack['SDK_path'] and output(['xcrun','--show-sdk-version'])==stack['SDK_version'] and sha('/usr/bin/g++')==stack['compiler_driver_sha256'] and sha(stack['compiler_resolved_path'])==stack['compiler_resolved_sha256'] and output(['/usr/bin/g++','--version'])==stack['compiler_version'] and output(['sw_vers'])==stack['OS']
assert output(['git','rev-parse','HEAD'],cwd=W)==TARGET and not output(['git','status','--porcelain'],cwd=W)
receipt={'header_sha256':{str(x.relative_to(W)):sha(x) for x in sorted(C.rglob('*')) if x.is_file() and x.suffix in ('.h','.hpp') and 'graphify-out' not in x.parts},'expected_preflight_digest_fnv64':prior['expected_preflight_digest_fnv64'],'expected_header_sha256':prior['expected_header_sha256'],'source_commit':TARGET,'registration_sha256':sys.argv[3],'source_objects':records,'archive_members':members,'archive_sha256':sha(archive),'link_argv':link,'link_cwd':prior['link_cwd'],'executable_sha256':sha(out/'wrf.exe'),'fortran_provenance_sha256':sha(N/'complete_provenance.json'),'observer_test_receipt_sha256':sha(E/'tests_v7/receipt.json'),'current_sdk_provenance_sha256':sha(E/'current_sdk_provenance.json'),'current_sdk_binding_addendum_sha256':sha(E/'current_sdk_binding_addendum.json'),'postbuild_free_bytes':shutil.disk_usage(E).free,'model_run':False}
if case=='candidate':receipt.update(control_receipt_sha256=sha(E/'native_control/build_receipt.json'),control_gate_sha256=sha(gate))
(out/'build_receipt.json').write_text(json.dumps(receipt,indent=2)+'\n');print(receipt['executable_sha256'])
