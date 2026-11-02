from pathlib import Path
import json,hashlib,subprocess,sys
from graphify.extract import extract

def main():
 w=Path('/private/tmp/early-residual-status-20261102');e=Path('/Users/yhlee/AD-SDIRK3-review-evidence/early-residual-status-20261102');o=Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-residual-consistency-20261102/graph_after_pins.json');p=json.loads(o.read_text());names=set(p['files']);names.add('external/libtorch_wrf/sdirk3/tests/test_first_failure_classification.cpp');names.add('external/libtorch_wrf/sdirk3/tests/test_newton_early_residual.cpp');files=[w/n for n in sorted(names)];r=extract(files,cache_root=e/'graphify-cache');assert r['nodes'];phase=sys.argv[1] if len(sys.argv)>1 else 'before';(e/f'graph_{phase}.json').write_text(json.dumps(r,indent=2)+'\n');(e/f'graph_{phase}_pins.json').write_text(json.dumps({'base':subprocess.check_output(['git','rev-parse','HEAD'],cwd=w,text=True).strip(),'files':{str(f.relative_to(w)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},'nodes':len(r['nodes']),'edges':len(r['edges']),'semantic_llm_tokens':0,'limits':'bounded AST source/callers; TLS,callback,Fortran gaps, source audited, not numerical proof'},indent=2)+'\n');print(phase,len(files),len(r['nodes']),len(r['edges']))
if __name__=='__main__':main()
