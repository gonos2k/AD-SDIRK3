"""Independent scalar audit of the final compact native packet; no model calls."""
import hashlib, json, math
from pathlib import Path
import numpy as np
E=Path(__file__).resolve().parent
P=E/'packet/native_residual_objective.npz'
with np.load(P,allow_pickle=False) as z:a={k:z[k] for k in z.files}
dot=lambda x,y:math.fsum(float(u)*float(v) for u,v in zip(x,y))
h=np.array([[dot(a['q_mid_f32'],m),dot(a['q_terminal_f32'],n)] for m,n in zip(a['mid_states_f64'],a['terminal_states_f64'])])
y=a['y_f64'];s=a['sigma_f64'];cost=.5*((h-y)/s)**2
alpha=(h[0]-y)/(s*s);v=a['direction_f32'].astype('f8');g=a['lambda_out_f32'][-1].astype('f8');old=a['old_terminal_g0_f32'].astype('f8')
ads={'full':dot(v,g),'midpoint':dot(v,g-alpha[1]*old),'terminal_reference':float(alpha[1])*dot(v,old)}
values={'full':cost.sum(axis=1),'midpoint':cost[:,0],'terminal_reference':cost[:,1]}
rows=[]
for term,vals in values.items():
 for ip,im in [(2,3),(4,5)]:
  ep,em=map(float,a['eta_actual_f32'][[ip,im]]);fd=float((vals[ip]-vals[im])/(ep-em));ad=ads[term]
  rows.append({'component':term,'eta':ep,'FD':fd,'AD':ad,'relative_error':abs(fd-ad)/max(abs(fd),abs(ad))})
r={'schema':'root_native_residual_independent_scalar_audit_v1','npz_sha256':hashlib.sha256(P.read_bytes()).hexdigest(),'h':h.tolist(),'cost_components':cost.tolist(),'directional_derivatives':ads,'rows':rows,'all_six_relative_errors_below_one_percent':all(x['relative_error']<=.01 for x in rows),'scope':'Independent scalar/cotangent audit; raw ledger proof remains in collector receipts. Midpoint is the derived composite increment, not independent midpoint-only VJP.'}
out=E/'root_actual_scalar_audit.json';out.write_text(json.dumps(r,indent=2,allow_nan=False)+'\n');print(json.dumps(r,indent=2))
