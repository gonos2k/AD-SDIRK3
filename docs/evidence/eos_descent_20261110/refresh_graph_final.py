from pathlib import Path
import hashlib,json,subprocess
from graphify.extract import extract
from graphify.cache import load_cached

def main():
 E=Path(__file__).resolve().parent;W=Path('/Users/yhlee/SDIRK3-review-worktrees/eos-descent-20261110');previous='354748d60248d2b407ef18b646b618e28cbf445a';head='05aac9acdbbe01302589f8f7fce1603795dd721c';tile='external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp'
 def out(a):return subprocess.check_output(a,cwd=W,text=True).strip()
 assert out(['git','rev-parse','HEAD'])==head and not out(['git','status','--porcelain','--untracked-files=all']);assert out(['git','diff','--name-only',previous,head])==tile
 prior=json.loads((E/'graph_after_pins.json').read_text());assert prior['base']==previous;files=[W/n for n in sorted(prior['files'])];assert len(files)==43
 for f in files:
  name=str(f.relative_to(W));h=hashlib.sha256(f.read_bytes()).hexdigest()
  if name!=tile:assert h==prior['files'][name],name
 cache=E/'graphify-cache';hits=[str(f.relative_to(W)) for f in files if load_cached(f,cache) is not None];assert len(hits)==42 and tile not in hits,(len(hits),tile in hits)
 r=extract(files,cache_root=cache);serialized=json.dumps(r,indent=2);assert '/private/tmp/forcing-path-20261103' not in serialized and 'private_tmp_forcing_path_20261103' not in serialized
 (E/'graph_final.json').write_text(serialized+'\n');pins={'base':head,'native_source_commit':previous,'worktree':str(W),'files':{str(f.relative_to(W)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},'nodes':len(r['nodes']),'edges':len(r['edges']),'semantic_llm_tokens':0,'cache_reused_count':len(hits),'freshly_extracted':[tile],'prior_pins_sha256':hashlib.sha256((E/'graph_after_pins.json').read_bytes()).hexdigest(),'limits':'bounded AST only; Fortran, preprocessor branches, callbacks and mutable cache semantics require source review; graph is not numerical proof'};(E/'graph_final_pins.json').write_text(json.dumps(pins,indent=2)+'\n')
 wanted=['.solve_stage_impl()','solveImplicitStage()','wrf_sdirk3_accepted_step_probe.h','test_accepted_step_probe.cpp'];entries=[]
 for name in wanted:
  for n in r['nodes']:
   if n.get('label')==name:entries.append({'label':name,'source_file':n.get('source_file'),'incident_edges':sum(e.get('source')==n['id'] or e.get('target')==n['id'] for e in r['edges'])})
 nav={'source_commit':head,'native_source_commit':previous,'native_executable_uses_source_before_comment_only_tile_change':True,'graph_sha256':hashlib.sha256((E/'graph_final.json').read_bytes()).hexdigest(),'pins_sha256':hashlib.sha256((E/'graph_final_pins.json').read_bytes()).hexdigest(),'navigation_nodes':entries,'gaps':['43 selected C++ sources/headers only; Fortran callers and other files are outside this graph.','Preprocessor conditions, runtime dispatch, callback state, and mutable caches require source-level verification.','References and calls are navigation aids and do not prove numerical correctness or native data provenance.']};(E/'graph_final_navigation.json').write_text(json.dumps(nav,indent=2)+'\n');print(json.dumps({'files':len(files),'nodes':pins['nodes'],'edges':pins['edges'],'cache_reused':len(hits),'freshly_extracted':tile}))
if __name__=='__main__':main()
