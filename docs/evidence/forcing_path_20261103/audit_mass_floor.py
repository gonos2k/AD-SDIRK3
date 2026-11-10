import hashlib,json,struct,sys
from fractions import Fraction
from pathlib import Path
import numpy as np
import xarray as xr
old=Path('/Users/yhlee/AD-SDIRK3-review-evidence/operator-gap-20261103/run')
new=Path('/Users/yhlee/AD-SDIRK3-review-evidence/operator-gap-20261103_scope_fix')
out=Path('/Users/yhlee/AD-SDIRK3-review-evidence/forcing-path-20261103/red_mass_floor_point_audit.json')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def audit_hash(a):
 a=np.ascontiguousarray(a,dtype='<f8'); b=struct.pack('<Biq',1,7,a.ndim)+b''.join(struct.pack('<q',x) for x in a.shape)+struct.pack('<q',a.size)+a.tobytes(); h=14695981039346656037
 for c in b:h=((h^c)*1099511628211)&((1<<64)-1)
 return f'{h:016x}'
reuse=json.loads((new/'point_reuse_audit.json').read_text()); assert reuse['all_two_match'] and not reuse['other_inputs_not_cross_compared']==False
u=np.load(old/'linear_delay_u_eval.npy',allow_pickle=False); ref=np.load(old/'linear_delay_u_ref_first_jvp.npy',allow_pickle=False); assert u.dtype==ref.dtype==np.dtype('float64') and u.shape==ref.shape==(1080491,)
log=next(json.loads(l.split('[LINEAR_DELAY] ',1)[1]) for l in (old/'rsl.out.0000').read_text().splitlines() if '[LINEAR_DELAY] ' in l); context=log['selected_context']; entry=next(x for x in context['source_handles'] if x['name']=='mu_base'); assert entry['unchanged'] and entry['meta']=={'dtype_id':7,'shape':[81,41]}
assert audit_hash(u)==context['sample_u_eval_fnv64'] and audit_hash(ref)==context['sample_reference_fnv64']
fp=new/'run/wrfout_d01_0001-01-01_00:00:00'; ds=xr.open_dataset(fp,decode_times=False,mask_and_scale=False); core=np.asarray(ds.MUB.values[0]); assert core.shape==(80,40) and core.dtype==np.float32 and np.isfinite(core).all(); mub=np.empty((81,41),dtype=np.float64);mub[:80,:40]=core.astype(np.float64);mub[:80,40]=mub[:80,0];mub[80,:]=mub[79,:]
actual=audit_hash(mub); expected=entry['fnv64']; assert actual==expected=='87f1b77d980beefe',f'MUB mismatch: {actual} != {expected}'
def result(v):
 x=v[1077170:1080491].reshape(81,41); q=np.empty_like(x);q[:80,:40]=x[:80,:40];q[:80,40]=q[:80,0];q[80,:]=q[79,:]
 assert np.isfinite(q).all(); pairs=[Fraction.from_float(float(a))+Fraction.from_float(float(b)) for a,b in zip(q.ravel(),mub.ravel())]; mn=min(pairs);idx=pairs.index(mn); fl=q+mub
 return {'minimum_exact':{'numerator':str(mn.numerator),'denominator':str(mn.denominator),'decimal':float(mn)},'minimum_index_j_i':[idx//41,idx%41],'exact_nonpositive_count':sum(x<=0 for x in pairs),'ieee_binary64_sum_min':float(fl.min()),'ieee_binary64_nonpositive_count':int((fl<=0).sum()),'all_positive':mn>0}
res={'time_scope':'archived observed selected point; no new model','ueval_path':str(old/'linear_delay_u_eval.npy'),'ueval_sha256':sha(old/'linear_delay_u_eval.npy'),'reference_path':str(old/'linear_delay_u_ref_first_jvp.npy'),'reference_sha256':sha(old/'linear_delay_u_ref_first_jvp.npy'),'run_log_sha256':sha(old/'rsl.out.0000'),'point_reuse_audit_sha256':sha(new/'point_reuse_audit.json'),'netcdf':str(fp),'netcdf_sha256':sha(fp),'mub_shape':[81,41],'mub_audit_hash_expected':expected,'mub_audit_hash_actual':actual,'mub_reconstruction':'WRF MUB float32 physical [80,40], append periodic X first column and symmetric Y last physical row; promote exactly to binary64. Constant field means digest does not independently discriminate alias rules.','layout':'packed mu offset 1077170, size 3321, [81,41]; Q uses physical core, periodic first-column X and last-row Y','rounding':'Exact rational binary inputs plus separately evaluated binary64 addition; assumes IEEE 754 round-to-nearest ties-to-even, gradual underflow and matching implementation.','ueval':result(u),'reference':result(ref),'limits':['Corrected attempt shares U and reference byte hashes with these saved arrays only; no complete-context identity.','No p/direction bound or nearby-point claim; no new model, build, or runtime probe.']};out.write_text(json.dumps(res,indent=2)+'\n');print(json.dumps(res,indent=2))
