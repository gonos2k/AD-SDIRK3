#!/usr/bin/env python3
"""Compare actual paired V-only direction against source-derived native mu action."""
from pathlib import Path
import sys,json,hashlib
import numpy as np
root=Path(__file__).resolve().parent
sys.path.insert(0,str(root.parent))
from analyze_a5_offline import records,norm
paths=[root/'runs'/k/'pressure_a5_direction.bin' for k in ('control','candidate')]
x,y=map(records,paths)
for k in ('U_eval','v_scaled','S_diag','eos_mu','mu_base','c1h','c2h','rdnw','msftx','msfty','msfvx','constants'):
 assert np.array_equal(x[k],y[k]),k
ny,nz,nx=x['eos_t'].shape;m=ny-1;n=nx-1
sizes=[ny*nz*(nx+1),(ny+1)*nz*nx,ny*(nz+1)*nx,ny*(nz+1)*nx,ny*nz*nx,ny*nx];p=np.cumsum([0]+sizes)
S=x['S_diag'].ravel();v=S*x['v_scaled'].ravel();z0=S*x['z_scaled'].ravel();z1=S*y['z_scaled'].ravel();A0=S*x['Az_scaled'].ravel();A1=S*y['Az_scaled'].ravel();delta=z1-z0
assert all(np.array_equal(delta[p[k]:p[k+1]],np.zeros(sizes[k])) for k in (0,2,3,4,5))
dv=delta[p[1]:p[2]].reshape(ny+1,nz,nx)
assert np.all(dv[0,:,:n]==0) and np.all(dv[m,:,:n]==0) and np.array_equal(dv[:m+1,:,n],dv[:m+1,:,0]) and np.array_equal(dv[m+1,:,:n],-dv[m-1,:,:n])
mu=x['eos_mu'][:m,:n]+x['mu_base'][:m,:n];muv=np.concatenate((mu[:1],.5*(mu[:-1]+mu[1:]),mu[-1:]),axis=0)
H=x['c1h'][:nz][None,:,None]*muv[:,None,:]+x['c2h'][:nz][None,:,None]
flux=H*dv[:m+1,:,:n]/x['msfvx'][:m+1,None,:n]
dnw=-1.0/x['rdnw'][:nz];rdy=x['constants'][6];a=x['constants'][-1]
dm=(x['msftx'][:m,None,:n]*dnw[None,:,None]*(rdy*(flux[1:]-flux[:-1]))).sum(axis=1)
dm*=x['msfty'][:m,:n];dm=np.concatenate((dm,dm[:,:1]),axis=1);dm=np.concatenate((dm,dm[m-1:m]),axis=0)
e0=(v-A0)[p[5]:p[6]].reshape(ny,nx);e1=(v-A1)[p[5]:p[6]].reshape(ny,nx);pred=e0+a*dm;diff=e1-pred
parts={'physical_core':(slice(0,m),slice(0,n)),'x_alias':(slice(0,m),slice(n,n+1)),'y_alias':(slice(m,m+1),slice(0,n+1)),'all_packed':(slice(None),slice(None))}
result={'scope':'actual same-stage saved control and candidate first vectors, source-derived isolated mu prediction; no extra RHS/JVP; not a full residual prediction','source':'wrf_sdirk3_ww_cp.h diagnose_wrf_mass_flux; tile_unified_impl.cpp wrf_mass_flux, mu_tend, projectStateBoundaries','sha256':{str(q.relative_to(root)):hashlib.sha256(q.read_bytes()).hexdigest() for q in paths},'candidate_only_changes_V':True,'native_V_projection_exact':True,'a':float(a),'rdy':float(rdy),'deltaV_l2':norm(dv),'checks':{}}
for name,idx in parts.items():
 d=diff[idx];truth=e1[idx];pr=pred[idx];result['checks'][name]={'count':d.size,'actual_l2':norm(truth),'pred_l2':norm(pr),'difference_l2':norm(d),'difference_max_abs':float(np.max(np.abs(d))),'relative_l2':norm(d)/norm(truth) if norm(truth) else None}
print(json.dumps(result,indent=2,allow_nan=False))
