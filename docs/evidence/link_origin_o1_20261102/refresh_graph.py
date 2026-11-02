from pathlib import Path
import hashlib,json,subprocess
from graphify.extract import extract
def main():
    E=Path(__file__).resolve().parent;W=Path('/private/tmp/link-origin-o1-20261102');before=json.loads((E/'graph_before_pins.json').read_text());files=[W/p for p in before['files']];r=extract(files,cache_root=E/'graphify-cache')
    (E/'graph_after.json').write_text(json.dumps(r,indent=2)+'\n');p={'base':subprocess.check_output(['git','rev-parse','HEAD'],cwd=W,text=True).strip(),'files':{str(f.relative_to(W)):hashlib.sha256(f.read_bytes()).hexdigest()for f in files},'nodes':len(r['nodes']),'edges':len(r['edges']),'semantic_llm_tokens':0,'limits':'bounded AST39; callback/TLS/Fortran gaps. Darwin make expansion separately source-audited, not AST-proven.'};(E/'graph_after_pins.json').write_text(json.dumps(p,indent=2)+'\n');print(len(files),p['nodes'],p['edges'])

if __name__=="__main__": main()
