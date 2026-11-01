from pathlib import Path
import json,hashlib,struct,re
import numpy as np

def fnv(b):
 h=14695981039346656037
 for v in b:h=((h^v)*1099511628211)&0xffffffffffffffff
 return h

def main():
 e=Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102');plan=json.loads((e/'prior_point_comparison_plan.json').read_text());d=json.loads((e/'run/terminal_jvp_probe.json').read_text());names={'linear_delay_u_eval.npy':'u_hash','linear_delay_first_tangent.npy':'v_hash','linear_delay_first_jvp.npy':'tangent_hash'};out=[]
 for name,meta in plan['files'].items():
  p=Path(name);assert p.stat().st_size==meta['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==meta['sha256'];a=np.load(p,allow_pickle=False);assert a.ndim==1 and a.dtype==np.dtype('<f8') and np.isfinite(a).all()
  if p.name in names:
   old=str(fnv(a.tobytes()));new=d[names[p.name]]
  else:
   old=format(fnv(struct.pack('<Biqqq',1,7,1,a.size,a.size)+a.tobytes()),'x');new=re.search(r';ref=([0-9a-f]+);',d['original']['context']).group(1)
  out.append({'prior_file':name,'prior_sha256':meta['sha256'],'shape':list(a.shape),'fingerprint_prior':old,'fingerprint_terminal':new,'fingerprints_match':old==new})
 r={'status':'fingerprints_consistent' if all(x['fingerprints_match'] for x in out) else 'fingerprints_differ','comparison_plan_sha256':hashlib.sha256((e/'prior_point_comparison_plan.json').read_bytes()).hexdigest(),'terminal_json_sha256':hashlib.sha256((e/'run/terminal_jvp_probe.json').read_bytes()).hexdigest(),'arrays':out,'scope':'A9 captured U/reference/v/Jv vs new selected sample. Hash agreement is a bounded fingerprint, not independent direct byte equality without new arrays; within-run C++ memcmp is separate. No endpoint or continuation claim.'};(e/'prior_point_comparison.json').write_text(json.dumps(r,indent=2)+'\n');print(r['status'])
if __name__=='__main__':main()
