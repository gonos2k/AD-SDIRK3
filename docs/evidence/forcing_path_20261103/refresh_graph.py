from pathlib import Path
import json,hashlib,subprocess
from graphify.extract import extract

def main():
 E=Path(__file__).resolve().parent;W=Path('/private/tmp/forcing-path-20261103');p=json.loads((E/'graph_before_pins.json').read_text());names=set(p['files'])|{'external/libtorch_wrf/sdirk3/wrf_sdirk3_accepted_step_probe.h','external/libtorch_wrf/sdirk3/tests/test_accepted_step_probe.cpp'};files=[W/n for n in sorted(names)];r=extract(files,cache_root=E/'graphify-cache');assert len(r['nodes'])>800
 (E/'graph_after.json').write_text(json.dumps(r,indent=2)+'\n');m={'base':subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip(),'files':{str(f.relative_to(W)):hashlib.sha256(f.read_bytes()).hexdigest()for f in files},'nodes':len(r['nodes']),'edges':len(r['edges']),'semantic_llm_tokens':0,'limits':'bounded AST only; Fortran authority/callback/cache paths manually source-audited; not numerical proof'};(E/'graph_after_pins.json').write_text(json.dumps(m,indent=2)+'\n');print(len(files),m['nodes'],m['edges'])
if __name__=='__main__':main()
