import pathlib,hashlib,subprocess,time,json
p=pathlib.Path('/private/tmp/pressure-coupled-inverse-20261101');out=pathlib.Path(__file__).resolve().parent;s=p/'external/libtorch_wrf/sdirk3/tests/test_coupled_pressure_inverse.cpp';h=p/'external/libtorch_wrf/sdirk3/wrf_sdirk3_coupled_pressure_inverse.h';exe=out/'test_coupled_pressure_packed';t='/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch';sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();cmd=['/usr/bin/g++','-std=c++17','-O3','-DMACOS','-DDMPARALLEL','-DUSE_SDIRK3','-D_GLIBCXX_USE_CXX11_ABI=0','-I'+t+'/include','-I'+t+'/include/torch/csrc/api/include','-I/opt/homebrew/include',str(s),'/Users/yhlee/AD-SDIRK3-review-evidence/pressure-a5-20261101/vrow_pair_draft/build_control/libwrf_sdirk3_libtorch.a','-L/opt/homebrew/opt/open-mpi/lib','-lmpi','-L/opt/homebrew/opt/gcc/lib/gcc/current','-lgfortran','-lquadmath','-L'+t+'/lib','-ltorch','-ltorch_cpu','-lc10','-Wl,-rpath,'+t+'/lib','-o',str(exe)];archive=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-a5-20261101/vrow_pair_draft/build_control/libwrf_sdirk3_libtorch.a');assert sha(archive)=='9aa8992f33e535e5a11f8d37df019c9de024c1773122caea985732d3241b0ec7'
r={'archive_sha256':sha(archive),'source_sha256':sha(s),'header_sha256':sha(h),'build_cmd':cmd,'model_run':False}
with (out/'packed_build.log').open('w') as f:
 try:q=subprocess.run(cmd,cwd=p,stdout=f,stderr=subprocess.STDOUT,timeout=180);r['build_returncode']=q.returncode
 except subprocess.TimeoutExpired:r['build_timeout']=True
if r.get('build_returncode')==0:
 r['binary_sha256']=sha(exe);r['run_cmd']=[str(exe)];start=time.monotonic()
 with (out/'packed_run.log').open('w') as f:
  try:q=subprocess.run([str(exe)],cwd=p,stdout=f,stderr=subprocess.STDOUT,timeout=180);r['run_returncode']=q.returncode
  except subprocess.TimeoutExpired:r['run_timeout']=True
 r['run_seconds']=time.monotonic()-start;r['run_log_sha256']=sha(out/'packed_run.log')
r['passed']=r.get('build_returncode')==0 and r.get('run_returncode')==0
(out/'packed_receipt.json').write_text(json.dumps(r,indent=2)+'\n');print(json.dumps(r,indent=2))
