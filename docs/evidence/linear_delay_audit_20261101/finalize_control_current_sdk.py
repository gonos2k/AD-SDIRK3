#!/usr/bin/env python3
"""Proposed read-only verification and receipt creation; never compiles, links or runs WRF."""
import hashlib,json,pathlib,shutil,subprocess,sys
E=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101');W=pathlib.Path('/private/tmp/linear-delay-audit-20261101');C=W/'external/libtorch_wrf/sdirk3';A=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-v-direct-20261101/native_control');N=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_build');B=E/'native_control';TARGET='5dc7fc7c8c064f71dc2a272099852f4a529bb73f';REG='b6f25d7a602c82c9e0930ef27a40e8bba462257af0e5b50bcaadff6fcdcabfcf'
def sha(p):
 h=hashlib.sha256()
 with open(p,'rb') as f:
  for b in iter(lambda:f.read(4*1024*1024),b''):h.update(b)
 return h.hexdigest()
def out(a,**k):return subprocess.check_output(list(map(str,a)),text=True,**k).strip()
if len(sys.argv)!=3:raise SystemExit('requires reviewed binding addendum path and SHA256')
add=pathlib.Path(sys.argv[1]);assert sha(add)==sys.argv[2];policy=json.loads(add.read_text());assert policy['status']=='approved_for_control_receipt' and policy['registration_sha256']==REG and policy['finalizer_sha256']==sha(__file__) and policy['current_sdk_provenance_sha256']==sha(E/'current_sdk_provenance.json')
reg=E/'registration.json';assert sha(reg)==REG
for p,h in json.loads(reg.read_text())['immutable_inputs'].items():assert sha(p)==h,p
assert out(['git','rev-parse','HEAD'],cwd=W)==TARGET and not out(['git','status','--porcelain'],cwd=W)
prior=json.loads((A/'build_receipt.json').read_text());assert sha(A/'wrf.exe')==prior['executable_sha256'] and sha(A/'libwrf_sdirk3_libtorch.a')==prior['archive_sha256'];proof=json.loads((N/'complete_provenance.json').read_text());assert sha(N/'complete_provenance.json')==prior['fortran_provenance_sha256']
for p,h in proof['object_module_archive_sha256'].items():assert sha(N/'source'/p)==h,p
for p,h in proof['dylib_sha256'].items():
 if h!='macOS shared cache':assert sha(p)==h,p
for p,h in prior['header_sha256'].items():assert sha(W/p)==h,p
assert sha(C/'wrf_sdirk3_linear_delay_audit.h')=='5d1866fc9296154b9522f391254c7f7c60f0b9c8fa1a848c52a6b5e3a8ee44bb'
old=prior['source_objects'];records=[]
for x in old:
 n=x['source'];obj=pathlib.Path(x['object'])
 if n in ('wrf_sdirk3_newton_solver.cpp','wrf_sdirk3_tile_unified_impl.cpp'):
  obj=B/(pathlib.Path(n).stem+'.o'); argv=x['argv'].copy();oldroot=str(pathlib.Path(next(a for a in argv if a.endswith('/'+n))).parent);argv=[a.replace(oldroot,str(C)) for a in argv];argv[-1]=str(obj);argv.insert(1,'-DWRF_SDIRK3_LINEAR_DELAY_AUDIT');y={'source':n,'source_sha256':sha(C/n),'object':str(obj),'object_sha256':sha(obj),'argv':argv,'depfile_sha256':sha(obj.with_suffix('.d'))}
 else:
  assert sha(C/n)==x['source_sha256'] and sha(obj)==x['object_sha256'];y=x
 assert hashlib.sha256(subprocess.check_output(['ar','-p',str(B/'libwrf_sdirk3_libtorch.a'),obj.name])).hexdigest()==sha(obj);records.append(y)
members=[x for x in out(['ar','-t',B/'libwrf_sdirk3_libtorch.a']).splitlines() if not x.startswith('__.SYMDEF')];assert len(members)==31 and members==[pathlib.Path(x['object']).name for x in records]
p=json.loads((E/'current_sdk_provenance.json').read_text());assert sha(B/'wrf.exe')==p['control_executable_sha256'] and sha(B/'libwrf_sdirk3_libtorch.a')==p['control_archive_sha256']; assert out(['otool','-L',B/'wrf.exe']).splitlines()[1:]==(B/'otool.txt').read_text().splitlines()[1:]
a=(A/'otool.txt').read_text().splitlines()[1:];b=(B/'otool.txt').read_text().splitlines()[1:];diff=[{'old':x.strip(),'new':y.strip()} for x,y in zip(a,b) if x!=y];assert len(a)==len(b) and diff==policy['allowlisted_dylib_differences'] and diff==p['dylib_differences_only']
link=prior['link_argv'].copy();link[2]=str(B/'wrf.exe');link[link.index('-L'+str(A))]='-L'+str(B);link[link.index(str(A/'libwrf_sdirk3_libtorch.a'))]=str(B/'libwrf_sdirk3_libtorch.a')
r={'header_sha256':{str(x.relative_to(W)):sha(x) for x in sorted(C.rglob('*')) if x.is_file() and x.suffix in ('.h','.hpp') and 'graphify-out' not in x.parts},'expected_preflight_digest_fnv64':prior['expected_preflight_digest_fnv64'],'expected_header_sha256':prior['expected_header_sha256'],'source_commit':TARGET,'registration_sha256':REG,'binding_addendum_sha256':sha(add),'source_objects':records,'archive_members':members,'archive_sha256':sha(B/'libwrf_sdirk3_libtorch.a'),'link_argv':link,'link_cwd':prior['link_cwd'],'executable_sha256':sha(B/'wrf.exe'),'fortran_provenance_sha256':sha(N/'complete_provenance.json'),'observer_test_receipt_sha256':sha(E/'tests_v7/receipt.json'),'postbuild_free_bytes':shutil.disk_usage(E).free,'model_run':False,'qualification':'current SDK provenance addendum; no numerical parity established'}
f=B/'build_receipt.json';assert not f.exists();f.write_text(json.dumps(r,indent=2)+'\n');print(sha(f))
