import numpy as np,xarray as xr,struct,json,hashlib,datetime
from pathlib import Path
E=Path('/Users/yhlee/AD-SDIRK3-review-evidence/forcing-path-20261103');P=Path('/Users/yhlee/AD-SDIRK3-review-evidence/operator-gap-20261103');N=Path('/Users/yhlee/AD-SDIRK3-review-evidence/operator-gap-20261103_scope_fix');r=N/'run/wrfout_d01_0001-01-01_00:00:00';ds=xr.open_dataset(r,decode_times=False,mask_and_scale=False)
def h(a,id):
 a=np.ascontiguousarray(a,dtype='<f8' if id==7 else '<f4');b=struct.pack('<Biq',1,id,a.ndim)+b''.join(struct.pack('<q',i) for i in a.shape)+struct.pack('<q',a.size)+a.tobytes();v=14695981039346656037
 for x in b:v=((v^x)*1099511628211)&((1<<64)-1)
 return f'{v:016x}'
def pad(a):
 a=np.asarray(a);q=np.empty((81,a.shape[1],41),dtype=np.float64);q[:80,:,:40]=a;q[:80,:,40]=q[:80,:,0];q[80]=q[79];return q
phb=pad(ds.PHB.values[0].transpose(1,0,2));c1=np.asarray(ds.C1H.values[0]);c2=np.asarray(ds.C2H.values[0]);assert h(phb,7)=='56aa110cd24e8725' and h(c1,6)=='e581f25d019bd1eb' and h(c2,6)=='30613798fac7f3eb'
u=np.load(P/'run/linear_delay_u_eval.npy');u64hash=hashlib.sha256((P/'run/linear_delay_u_eval.npy').read_bytes()).hexdigest();assert u64hash=='36066354aab639372f4eee8f4a47bde6f8047f39557062c6402c80b7dfb50e86';ph=u[648761:864626].reshape(81,65,41);t=u[864626:1077170].reshape(81,64,41);mu=u[1077170:].reshape(81,41)
def Q(x):
 y=np.empty_like(x);y[:80,:,:40]=x[:80,:,:40];y[:80,:,40]=y[:80,:,0];y[80]=y[79];return y
ph=Q(ph);t=Q(t);mu=Q(mu[:,None,:])[:,0,:];mb=np.empty((81,41));mb[:80,:40]=ds.MUB.values[0];mb[:80,40]=mb[:80,0];mb[80]=mb[79];assert h(mb,7)=='87f1b77d980beefe';minus=-np.inf;down=lambda x:np.nextafter(x,minus)
# Outward bounds on exact-real quantities formed from stored binary values.
dbase=down(phb[:,1:,:]-phb[:,:-1,:]);dpert=down(ph[:,1:,:]-ph[:,:-1,:]);dphi=down(dbase+dpert);theta=down(t+300.0);mass=down(mu+mb);assert np.all(c1>=0);den=down(down(c1.astype(np.float64)[None,:,None]*mass[:,None,:])+c2.astype(np.float64)[None,:,None]);out={'scope':'selected point only, no model run; binary input reconstructions checked against recorded hashes','time':datetime.datetime.now().astimezone().isoformat(),'ueval_sha256':u64hash,'ph_base_fnv64':h(phb,7),'c1h_fnv64':h(c1,6),'c2h_fnv64':h(c2,6),'mu_base_fnv64':h(mb,7),'conservative_exact_real_lower_bounds':{'theta_plus_300':float(theta.min()),'total_geopotential_layer_difference':float(dphi.min()),'hybrid_mass_c1_times_muts_plus_c2':float(den.min())},'method':'IEEE binary64 nearest operations widened down one representable number after each subtraction/addition/multiplication; c1 nonnegative; exact-real lower bounds assuming standard rounding, finite inputs and gradual underflow','limits':['Selected point only, no direction-neighborhood or full input context proof.','Does not directly evaluate EOS inverse density alt or certify EOS power argument, FP operation error, FD error, or forcing residual.','Reconstruction of base and coefficients uses initialized wrfout and verified FNV hashes; U_eval uses separately verified byte SHA.']};(E/'green_eos_point_margins.json').write_text(json.dumps(out,indent=2)+'\n');print(out['conservative_exact_real_lower_bounds'])
