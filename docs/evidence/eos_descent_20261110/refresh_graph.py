from pathlib import Path
import hashlib,json,subprocess
from graphify.extract import extract
from graphify.cache import cache_dir,file_hash

def main():
 E=Path(__file__).resolve().parent;W=Path('/Users/yhlee/SDIRK3-review-worktrees/eos-descent-20261110');OLD=Path('/private/tmp/forcing-path-20261103');O=Path('/Users/yhlee/AD-SDIRK3-review-evidence/forcing-path-20261103/graphify-cache');p=json.loads((E/'graph_before_pins.json').read_text());head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip();assert head=='354748d60248d2b407ef18b646b618e28cbf445a';assert not subprocess.check_output(['git','status','--porcelain','--untracked-files=all'],cwd=W,text=True).strip()
 files=[W/n for n in sorted(p['files'])];assert len(files)==43
 for name,h in p['files'].items():assert hashlib.sha256(subprocess.check_output(['git','show',p['base']+':'+name],cwd=W)).hexdigest()==h,name
 cache=E/'graphify-cache';dest=cache_dir(cache,'ast');dest.mkdir(parents=True,exist_ok=True);src=cache_dir(O,'ast');reused=[];missing=[]
 for f in files:
  name=str(f.relative_to(W));data=f.read_bytes()
  if hashlib.sha256(data).hexdigest()!=p['files'][name]:continue
  # Prior graphify cache root lay outside its worktree, so key used old absolute path.
  h=hashlib.sha256(data+b'\x00'+str(OLD/name).lower().encode()).hexdigest();q=src/(h+'.json')
  if not q.exists():missing.append(name);continue
  text=q.read_text();text=text.replace(str(OLD),str(W)).replace('private_tmp_forcing_path_20261103','users_yhlee_sdirk3_review_worktrees_eos_descent_20261110')
  (dest/(file_hash(f,cache)+'.json')).write_text(text);reused.append(name)
 r=extract(files,cache_root=cache);assert len(r['nodes'])>800
 serialized=json.dumps(r,indent=2);assert str(OLD) not in serialized and 'private_tmp_forcing_path_20261103' not in serialized
 (E/'graph_after.json').write_text(serialized+'\n');m={'base':head,'worktree':str(W),'files':{str(f.relative_to(W)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},'nodes':len(r['nodes']),'edges':len(r['edges']),'semantic_llm_tokens':0,'cache_reused_count':len(reused),'cache_reused_sources':reused,'cache_misses':missing,'limits':'bounded AST only; Fortran, preprocessor branches, callbacks and cache mutation require source-level verification; extracted edges are not numerical proof','pending':'refresh after any later Tile comment patch'};(E/'graph_after_pins.json').write_text(json.dumps(m,indent=2)+'\n');print(json.dumps({'files':len(files),'nodes':m['nodes'],'edges':m['edges'],'reused':len(reused),'cache_misses':len(missing)}))
if __name__=='__main__':main()
