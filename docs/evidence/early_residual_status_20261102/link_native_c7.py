#!/usr/bin/env python3
"""Link once from verified archive and prior link arguments; never run WRF."""
import hashlib, json, pathlib, subprocess, sys
E=pathlib.Path(__file__).resolve().parent
W=pathlib.Path('/private/tmp/early-residual-status-20261102')
P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-residual-consistency-20261102')
B=E/'native_build'
from verify_rebuilt_archive import verify
def sha(p): return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a,**kw): return subprocess.check_output(list(map(str,a)),text=True,**kw).strip()
def main():
 if len(sys.argv)!=2: raise SystemExit('usage: link_native.py TARGET_HEAD')
 target=sys.argv[1]; build=E/'early_build_receipt.json'; r=json.loads(build.read_text()); prior_path=P/'newton_build_receipt.json'; prior=json.loads(prior_path.read_text())
 verify(r,target)
 assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain','--untracked-files=all'],cwd=W)
 assert sha(prior_path)==r['prior_receipt_sha256'] and sha(B/'libwrf_sdirk3_libtorch.a')==r['archive_sha256']
 verify(r,target)
 cmd=prior['link_argv'][:]; old_archive=str(P/'native_build/libwrf_sdirk3_libtorch.a'); assert cmd.count(old_archive)==1
 cmd[cmd.index(old_archive)]=str(B/'libwrf_sdirk3_libtorch.a'); cmd[cmd.index('-o')+1]=str(B/'wrf.exe')
 assert not (B/'wrf.exe').exists() and not (E/'native_link_receipt.json').exists()
 subprocess.run(cmd,cwd=prior['link_cwd'],check=True)
 assert sha(build)==sha_before and sha(B/'libwrf_sdirk3_libtorch.a')==r['archive_sha256']
 verify(r,target)
 rec={'source_commit':target,'compiled_source_commit':r['compiled_source_commit'],'build_receipt_sha256':sha(build),'prior_link_receipt_sha256':sha(prior_path),'archive_sha256':sha(B/'libwrf_sdirk3_libtorch.a'),'link_argv':cmd,'link_cwd':prior['link_cwd'],'executable_sha256':sha(B/'wrf.exe'),'model_run':False}
 with (E/'native_link_receipt.json').open('x') as f: json.dump(rec,f,indent=2); f.write('\n')
if __name__=='__main__':
 sha_before=sha(E/'early_build_receipt.json'); main()
