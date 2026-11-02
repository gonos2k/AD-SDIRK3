#!/usr/bin/env python3
"""Link once after frozen build receipt exists. No model run.
Usage: link_single_archive.py TARGET_HEAD BUILD_RECEIPT_PATH BUILD_RECEIPT_SHA256
Build receipt must have source_commit, source_hashes, archive_sha256.
"""
import hashlib,json,pathlib,re,subprocess,sys
E=pathlib.Path(__file__).resolve().parent;W=pathlib.Path('/private/tmp/link-origin-o1-20261102')
P=pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-residual-consistency-20261102');B=E/'native_build'
def sha(p):return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()
def out(a,**kw):return subprocess.check_output(list(map(str,a)),text=True,stderr=subprocess.STDOUT,**kw).strip()
def main():
 if len(sys.argv)!=4:raise SystemExit(__doc__)
 target,receipt_path,receipt_hash=sys.argv[1:];receipt_path=pathlib.Path(receipt_path);assert sha(receipt_path)==receipt_hash
 r=json.loads(receipt_path.read_text());assert r['source_commit']==target
 assert out(['git','rev-parse','HEAD'],cwd=W)==target and not out(['git','status','--porcelain','--untracked-files=all'],cwd=W)
 for p,h in r['source_hashes'].items():assert sha(W/p)==h,p
 archive=B/'libwrf_sdirk3_libtorch.a';assert sha(archive)==r['archive_sha256']
 prior_path=P/'newton_build_receipt.json';prior=json.loads(prior_path.read_text());cmd=prior['link_argv'][:]
 oldlib='-lwrf_sdirk3_libtorch';assert cmd.count(oldlib)==1;idx=cmd.index(oldlib);oldsearch=cmd[idx-1];assert oldsearch.startswith('-L') and oldsearch.endswith('/native_build');del cmd[idx-1:idx+1]
 oldarchive=str(P/'native_build/libwrf_sdirk3_libtorch.a');assert cmd.count(oldarchive)==1;cmd[cmd.index(oldarchive)]=str(archive)
 exe=B/'wrf.exe';mapfile=E/'wrf_link.map';cmd[cmd.index('-o')+1]=str(exe)
 assert sum('libwrf_sdirk3_libtorch' in a for a in cmd)==1 and not exe.exists() and not mapfile.exists() and not (E/'link_receipt.json').exists()
 cmd.extend(['-Wl,-why_load','-Wl,-map,'+str(mapfile)])
 ld=out(['xcrun','--find','ld']);ld_version=out([ld,'-v']);driver_version=out([cmd[0],'--version'])
 # Save the driver's own linker invocation to reveal the selected linker, not merely xcrun's default.
 probe=subprocess.run([cmd[0],'-###',*cmd[1:]],cwd=prior['link_cwd'],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,check=True).stdout
 assert ('\"'+ld+'\"') in probe, 'actual driver linker differs from xcrun ld'
 (E/'link_driver_probe.txt').write_text(probe)
 with (E/'link.stdout.log').open('x') as stdout,(E/'link.stderr.log').open('x') as stderr:subprocess.run(cmd,cwd=prior['link_cwd'],stdout=stdout,stderr=stderr,check=True)
 assert sha(receipt_path)==receipt_hash and sha(archive)==r['archive_sha256'] and out(['git','rev-parse','HEAD'],cwd=W)==target
 for p,h in r['source_hashes'].items():assert sha(W/p)==h,p
 lines=mapfile.read_text(errors='replace').splitlines();member_lines=[line for line in lines if re.search(r'libwrf_sdirk3_libtorch\.a(?:\[\d+\])?\(',line)]
 assert member_lines,'link map did not identify archive members'
 archive_re=re.escape(str(archive))+r'(?:\[\d+\])?\('
 assert all(re.search(archive_re,line) for line in member_lines),member_lines
 for member in ('wrf_sdirk3_newton_solver.o','wrf_sdirk3_tile_unified_impl.o'):
  assert any(re.search(archive_re+re.escape(member)+r'\)',line) for line in member_lines),member
 owners={}
 for line in member_lines:
  match=re.match(r'^\s*\[\s*(\d+)\]\s+(.+)$',line)
  assert match, line
  owners[match.group(1)]=match.group(2)
 candidates=[]
 for line in lines:
  if 'solve_stage_impl' not in line and 'solveImplicitStage' not in line: continue
  match=re.match(r'^\s*0x[0-9a-fA-F]+\s+0x[0-9a-fA-F]+\s+\[\s*(\d+)\]\s+(\S+)',line)
  if match: candidates.append((line,match.group(1),match.group(2)))
 assert candidates,'link map missing target method symbols'
 demangled=subprocess.run(['/usr/bin/c++filt'],input='\n'.join(x[2] for x in candidates)+'\n',text=True,stdout=subprocess.PIPE,check=True).stdout.splitlines()
 assert len(demangled)==len(candidates)
 symbols=[]
 for (line,owner,mangled),name in zip(candidates,demangled):
  if name.startswith('wrf::sdirk3::WRFNewtonKrylovSolver::Impl::solve_stage_impl('): expected='wrf_sdirk3_newton_solver.o'
  elif name.startswith('TileSDIRK3UnifiedSolver::solveImplicitStage('): expected='wrf_sdirk3_tile_unified_impl.o'
  else: continue
  assert re.fullmatch(archive_re+re.escape(expected)+r'\)',owners.get(owner,'')),(name,owner,owners.get(owner))
  symbols.append({'map_line':line,'file_id':owner,'object':owners[owner],'demangled':name})
 assert any(x['demangled'].startswith('wrf::sdirk3::WRFNewtonKrylovSolver::Impl::solve_stage_impl(') for x in symbols),symbols
 assert any(x['demangled'].startswith('TileSDIRK3UnifiedSolver::solveImplicitStage(') for x in symbols),symbols
 why=(E/'link.stdout.log').read_text(errors='replace')+'\n'+(E/'link.stderr.log').read_text(errors='replace')
 why_lines=[line for line in why.splitlines() if 'libwrf_sdirk3_libtorch' in line]
 assert all(str(archive) in line for line in why_lines),why_lines
 rec={'source_commit':target,'build_receipt_path':str(receipt_path),'build_receipt_sha256':receipt_hash,'prior_link_receipt_sha256':sha(prior_path),'removed_pair':[oldsearch,oldlib],'archive_path':str(archive),'archive_sha256':sha(archive),'link_argv':cmd,'link_cwd':prior['link_cwd'],'driver_version':driver_version,'xcrun_ld_path':ld,'xcrun_ld_sha256':sha(ld),'xcrun_ld_version':ld_version,'driver_link_probe_sha256':sha(E/'link_driver_probe.txt'),'map_sha256':sha(mapfile),'map_archive_member_lines':member_lines,'method_symbol_origins':symbols,'why_load_archive_lines':why_lines,'stdout_sha256':sha(E/'link.stdout.log'),'stderr_sha256':sha(E/'link.stderr.log'),'executable_sha256':sha(exe),'model_run':False}
 with (E/'link_receipt.json').open('x') as f:json.dump(rec,f,indent=2);f.write('\n')
if __name__=='__main__':main()
