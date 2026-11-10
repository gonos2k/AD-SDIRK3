#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess,sys
E=pathlib.Path(__file__).resolve().parent; N=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_build');P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101');T=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102/native_terminal');F=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/forcing-path-20261103')
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a):return subprocess.check_output(a,text=True,stderr=subprocess.STDOUT).strip()
prior=json.loads((T/'build_receipt.json').read_text());proof=N/'complete_provenance.json';sdk=P/'current_sdk_provenance.json'; pr=json.loads(proof.read_text());st=json.loads(sdk.read_text());bad=[]
def check(name,fn):
 try:
  if not fn():bad.append(name)
 except Exception as e:bad.append(name+': '+repr(e))
check('fortran_proof_hash',lambda:sha(proof)==prior['fortran_provenance_sha256']);check('sdk_proof_hash',lambda:sha(sdk)==prior['current_sdk_provenance_sha256'])
for p,h in pr['object_module_archive_sha256'].items():check('fortran:'+p,lambda p=p,h=h:sha(N/'source'/p)==h)
for p,h in pr['dylib_sha256'].items():
 if h!='macOS shared cache':check('dylib:'+p,lambda p=p,h=h:sha(p)==h)
for name,fn in [('sdk_path',lambda:out(['xcrun','--show-sdk-path'])==st['SDK_path']),('sdk_version',lambda:out(['xcrun','--show-sdk-version'])==st['SDK_version']),('driver_hash',lambda:sha('/usr/bin/g++')==st['compiler_driver_sha256']),('resolved_compiler_hash',lambda:sha(st['compiler_resolved_path'])==st['compiler_resolved_sha256']),('compiler_version',lambda:out(['/usr/bin/g++','--version'])==st['compiler_version']),('os',lambda:out(['sw_vers'])==st['OS'])]:check(name,fn)
b=json.loads((F/'build_receipt.json').read_text());l=json.loads((F/'link_receipt.json').read_text());check('archive',lambda:sha(F/'native_build/libwrf_sdirk3_libtorch.a')==b['archive_sha256']);check('exe',lambda:sha(F/'native_build/wrf.exe')==l['executable_sha256']);check('driver_linker',lambda:sha(l['xcrun_ld_path'])==l['xcrun_ld_sha256'] and out([l['xcrun_ld_path'],'-v'])==l['xcrun_ld_version']);check('feature_header',lambda:sha('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-a4-20261031/a4_build/B/wrf_sdirk3_pressure_reference_expected.h')=='91acfb9ec88905e7e5f762bc84211ec9ae57a78c375771aa36995fbd0c64b845')
result={'status':'PASS' if not bad else 'FAIL','failures':bad,'fortran_proof_sha256':sha(proof),'sdk_proof_sha256':sha(sdk),'fortran_object_count':len(pr['object_module_archive_sha256']),'dylib_count':len(pr['dylib_sha256']),'sdk_version_current':out(['xcrun','--show-sdk-version']),'baseline_build_receipt_sha256':sha(F/'build_receipt.json'),'baseline_link_receipt_sha256':sha(F/'link_receipt.json'),'no_compilation_or_model_run':True}
(E/'stack_feasibility.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2));sys.exit(bool(bad))
