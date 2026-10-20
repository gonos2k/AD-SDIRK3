#!/usr/bin/env python3
"""Recompute PR311 public-field screens from bundled NumPy arrays only."""
from __future__ import annotations
import csv
import hashlib
import json
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent
PROTOCOL=HERE/'root_final_protocol.json'
MANIFEST=HERE/'manifest.json'
OLD_NPZ=HERE.parent/'pr310_strict_reference_240s'/'published_fields_and_W.npz'
H3_NPZ=HERE/'h3_published_physical_fields.npz'
H15_NPZ=HERE/'archived_h15_published_physical_fields.npz'
TIMES=(60,120,180,240)
FIELDS=('U','V','W','PH','T','MU')
ROOT_PROTOCOL_SHA='7a3055189a01233837a67e8c7e7044ad34b165a42218c5f8160076657a9f83e0'

def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1<<20),b''): h.update(b)
 return h.hexdigest()

def stat(x):
 x=np.asarray(x,dtype=np.float64).ravel()
 if not np.isfinite(x).all(): raise ValueError('nonfinite data')
 return {'count':int(x.size),'rms':float(np.sqrt(np.mean(x*x))),
         'max_abs':float(np.max(np.abs(x))),'signed_mean':float(np.mean(x)),
         'l2_norm':float(np.linalg.norm(x))}

def cosine(a,b):
 na=float(np.linalg.norm(a));nb=float(np.linalg.norm(b))
 if na==0.0 or nb==0.0:return None,'null: one or both difference vectors have zero L2 norm'
 return float(np.dot(a.ravel(),b.ravel())/(na*nb)),'ok'

def main():
 if sha(PROTOCOL)!=ROOT_PROTOCOL_SHA:raise ValueError('active root protocol hash mismatch')
 p=json.loads(PROTOCOL.read_text());m=json.loads(MANIFEST.read_text())
 old_ref=HERE/m['external_reference']['file']
 if old_ref.resolve()!=OLD_NPZ.resolve() or not old_ref.is_file() or sha(old_ref)!=m['external_reference']['sha256']:
  raise ValueError('external old PR310 reference path/hash mismatch')
 for key,path in [('h3_npz',H3_NPZ),('archived_h15_npz',H15_NPZ)]:
  if not path.is_file() or sha(path)!=m['files'][key]['sha256']:raise ValueError(f'{key} hash mismatch')
 if sha(OLD_NPZ)!=p['existing_data']['npz_sha256']:raise ValueError('old PR310 NPZ does not match root protocol')
 owners=p['official_screen']['owner_counts'];limits=p['official_screen']['epsilon_over_3'];full=p['conditional_archived_candidate']['full_epsilon_limits']
 ref_rows=[];candidate_rows=[];shape_rows=[]
 with np.load(OLD_NPZ,allow_pickle=False) as old,np.load(H3_NPZ,allow_pickle=False) as h3,np.load(H15_NPZ,allow_pickle=False) as h15:
  for t in TIMES:
   for field in FIELDS:
    c=np.asarray(old[f'{field}_t{t}_coarse'],dtype=np.float64)
    mid=np.asarray(old[f'{field}_t{t}_fine'],dtype=np.float64)
    new=np.asarray(h3[f'{field}_t{t}'],dtype=np.float64)
    cand=np.asarray(h15[f'{field}_t{t}'],dtype=np.float64)
    n=owners[field]
    if any(x.size!=n for x in (c,mid,new,cand)) or not(c.shape==mid.shape==new.shape==cand.shape):
     raise ValueError(f'{field}@{t} owner shape mismatch')
    d1=mid-c;d2=new-mid
    s1=stat(d1);s2=stat(d2)
    q,qs=cosine(d1,d2)
    ratio=s2['rms']/s1['rms'] if s1['rms']>0 else None
    shape_rows.append({'time_seconds':t,'field':field,'units':p['official_screen']['transforms'][field],
      'owner_count':n,'D1_middle_minus_coarse':s1,'D2_h3_minus_middle':s2,
      'D2_over_D1_RMS_ratio':ratio,'cosine_D1_D2':q,'cosine_status':qs})
    lim=float(limits[field]);ref_rows.append({'time_seconds':t,'field':field,'units':p['official_screen']['transforms'][field],
      'owner_count':n,'h3_minus_middle':s2,'epsilon_over_3_limit':lim,'ratio':s2['rms']/lim,'pass':s2['rms']<=lim})
    cd=cand-new;cs=stat(cd);flim=float(full[field])
    candidate_rows.append({'time_seconds':t,'field':field,'units':p['official_screen']['transforms'][field],
      'owner_count':n,'h15_minus_h3':cs,'full_epsilon_limit':flim,'ratio':cs['rms']/flim,'pass':cs['rms']<=flim})
    # Difference vectors are intentionally not serialized: every row is
    # reproducible from the three checkpoint NPZ sources and protocol.
 refpass=all(x['pass'] for x in ref_rows)
 if len(ref_rows)!=24 or not refpass:raise ValueError('reference screen must be 24/24 before candidate evaluation')
 def write_csv(name,rows,fields):
  path=HERE/name
  with path.open('w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(rows)
  return path
 rflat=[{'time_seconds':x['time_seconds'],'field':x['field'],'units':x['units'],'owner_count':x['owner_count'],
   'rms':x['h3_minus_middle']['rms'],'max_abs':x['h3_minus_middle']['max_abs'],'epsilon_over_3_limit':x['epsilon_over_3_limit'],'ratio':x['ratio'],'pass':x['pass']} for x in ref_rows]
 cflat=[{'time_seconds':x['time_seconds'],'field':x['field'],'units':x['units'],'owner_count':x['owner_count'],
   'rms':x['h15_minus_h3']['rms'],'max_abs':x['h15_minus_h3']['max_abs'],'full_epsilon_limit':x['full_epsilon_limit'],'ratio':x['ratio'],'pass':x['pass']} for x in candidate_rows]
 sflat=[{'time_seconds':x['time_seconds'],'field':x['field'],'units':x['units'],'D1_rms':x['D1_middle_minus_coarse']['rms'],'D2_rms':x['D2_h3_minus_middle']['rms'],'D2_over_D1_RMS_ratio':x['D2_over_D1_RMS_ratio'],'cosine_D1_D2':x['cosine_D1_D2'],'cosine_status':x['cosine_status']} for x in shape_rows]
 files=[write_csv('h3_vs_middle_reference_screen.csv',rflat,list(rflat[0])),write_csv('archived_h15_full_epsilon_screen.csv',cflat,list(cflat[0])),write_csv('three_level_d1_d2_shape.csv',sflat,list(sflat[0]))]
 result={'schema':'pr311_three_level_portable_recomputation_v1','root_protocol_sha256':ROOT_PROTOCOL_SHA,
  'old_npz_sha256':sha(OLD_NPZ),'h3_npz_sha256':sha(H3_NPZ),'h15_npz_sha256':sha(H15_NPZ),
  'official_reference_screen':{'row_count':len(ref_rows),'pass_count':sum(x['pass'] for x in ref_rows),'all_24_pass':refpass,'rows':ref_rows},
  'conditional_archived_h15_screen':{'evaluated':True,'row_count':len(candidate_rows),'pass_count':sum(x['pass'] for x in candidate_rows),'all_24_pass':all(x['pass'] for x in candidate_rows),'rows':candidate_rows},
  'three_level_D1_D2_shape':shape_rows,'outputs':{x.name:sha(x) for x in files},
  'method':'NumPy-only recomputation from public-owner FP64 arrays. No NetCDF reader, model, source, or optimized helper is needed.'}
 summary=HERE/'portable_recomputed_results.json';summary.write_text(json.dumps(result,indent=2,sort_keys=True)+'\n')
 print(json.dumps({'summary':summary.name,'h3_reference_pass_count':result['official_reference_screen']['pass_count'],'h15_candidate_pass_count':result['conditional_archived_h15_screen']['pass_count'],'summary_sha256':sha(summary)},indent=2))
if __name__=='__main__':main()
