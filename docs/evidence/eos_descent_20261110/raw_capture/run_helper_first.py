#!/usr/bin/env python3
import hashlib,json,pathlib,subprocess,sys
E=pathlib.Path(__file__).resolve().parent;W=pathlib.Path('/Users/yhlee/SDIRK3-review-worktrees/eos-descent-20261110');F=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/link-origin-o1-20261102');S=W/'external/libtorch_wrf/sdirk3'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
if len(sys.argv)!=2:sys.exit('usage: run_helper_first.py TARGET_HEAD')
target=sys.argv[1];assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip()==target and not subprocess.check_output(['git','status','--porcelain','--untracked-files=all'],cwd=W,text=True).strip()
a=json.loads((F/'build_receipt.json').read_text())['newton_argv'];flags=[];skip=False
for x in a[1:]:
 if skip:skip=False;continue
 if x=='-o':skip=True;continue
 if x in ('-c','-MMD','-MP','-DWRF_SDIRK3_LINEAR_DELAY_AUDIT') or x.endswith('/wrf_sdirk3_newton_solver.cpp'):continue
 flags.append(x.replace('/private/tmp/link-origin-o1-20261102/external/libtorch_wrf/sdirk3',str(S)))
assert '-DNDEBUG' not in flags
src=S/'tests/test_accepted_step_probe.cpp';paths=[src,S/'wrf_sdirk3_accepted_step_probe.h',S/'wrf_sdirk3_terminal_jvp_probe.h'];pins={str(p):sha(p) for p in paths};torch='/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/lib';B=E/'helper_build';B.mkdir(exist_ok=False);exe=B/'test_accepted_step_probe';cmd=[a[0],*flags,str(src),'-L'+torch,'-ltorch','-ltorch_cpu','-lc10','-Wl,-rpath,'+torch,'-o',str(exe)]
p=subprocess.run(cmd,text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(E/'helper_build.log').write_text(p.stdout);result={'source_commit':target,'source_hashes':pins,'compile_argv':cmd,'compile_returncode':p.returncode,'compiler_version':subprocess.check_output([a[0],'--version'],text=True).strip(),'torch_library_sha256':{str(pathlib.Path(torch)/x):sha(pathlib.Path(torch)/x) for x in ('libtorch.dylib','libtorch_cpu.dylib','libc10.dylib')}}
if p.returncode==0:
 q=subprocess.run([str(exe)],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(E/'helper_run.log').write_text(q.stdout);result.update(executable_sha256=sha(exe),run_returncode=q.returncode)
assert all(sha(p)==h for p,h in pins.items()) and subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip()==target
(E/'helper_test_receipt.json').write_text(json.dumps(result,indent=2)+'\n')
assert p.returncode==0 and result['run_returncode']==0
