from pathlib import Path
import json,hashlib,subprocess
from graphify.extract import extract

def main():
 E=Path(__file__).resolve().parent;W=Path('/private/tmp/operator-gap-20261103');p=json.loads((E/'graph_before_pins.json').read_text());names=set(p['files'])|{'external/libtorch_wrf/sdirk3/wrf_sdirk3_operator_gap_probe.h','external/libtorch_wrf/sdirk3/tests/test_operator_gap_probe.cpp'};files=[W/n for n in sorted(names)];r=extract(files,cache_root=E/'graphify-cache');assert len(r['nodes'])>800
 (E/'graph_after.json').write_text(json.dumps(r,indent=2)+'\n');pins={'base':subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip(),'files':{str(f.relative_to(W)):hashlib.sha256(f.read_bytes()).hexdigest() for f in files},'nodes':len(r['nodes']),'edges':len(r['edges']),'semantic_llm_tokens':0,'limits':'bounded AST only, callback/TLS/Fortran gaps; source/diagnostic evidence separate, not proof.'};(E/'graph_after_pins.json').write_text(json.dumps(pins,indent=2)+'\n');print(len(files),pins['nodes'],pins['edges'])
if __name__=='__main__':main()
