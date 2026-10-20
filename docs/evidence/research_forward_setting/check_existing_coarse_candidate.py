#!/usr/bin/env python3
"""Direct full-epsilon comparison of pinned h=.9375 and h=.234375 public fields."""
from __future__ import annotations
import csv
import hashlib
import json
from pathlib import Path
import numpy as np

HERE=Path(__file__).resolve().parent
OLD_NPZ=HERE.parent/'pr310_strict_reference_240s'/'published_fields_and_W.npz'
H3_NPZ=HERE.parent/'three_level_w_reference'/'h3_published_physical_fields.npz'
PROTOCOL=HERE.parent/'three_level_w_reference'/'root_final_protocol.json'
COARSE_RECEIPT=HERE.parent/'pr310_strict_reference_240s'/'coarse_run_receipt.json'
MIDDLE_RECEIPT=HERE.parent/'pr310_strict_reference_240s'/'fine_run_receipt.json'
H3_RECEIPT=HERE.parent/'three_level_w_reference'/'new_run_receipt.json'
EXPECTED={'protocol':'7a3055189a01233837a67e8c7e7044ad34b165a42218c5f8160076657a9f83e0',
 'old_npz':'4fe480ad79bc9957488f8b02498fde4bb9a45bff7f10d246bd1826490778070e',
 'h3_npz':'28f4fee625a6fedfb27ffdb376586df6cd6d5c8e5f2ca6c023f37bfec7ad0029',
 'coarse_receipt':'7577abe80774eac04418563d37332379189383016ae241a650e97ad110945257',
 'middle_receipt':'7fc31f45206dd9dbbf16f214b7b9456fc07202acceac9bf4e3ff28857c2adfc8',
 'h3_receipt':'ef6fb87deba873141306d78fe23a834b3533519750e46a3babf5c9d378194e95'}
FIELDS=('U','V','W','PH','T','MU');TIMES=(60,120,180,240)

def sha(path):
 h=hashlib.sha256()
 with Path(path).open('rb') as f:
  for block in iter(lambda:f.read(1<<20),b''):h.update(block)
 return h.hexdigest()

def stat(x):
 x=np.asarray(x,dtype=np.float64).ravel()
 if not np.isfinite(x).all():raise ValueError('nonfinite comparison array')
 return {'count':int(x.size),'rms':float(np.sqrt(np.mean(x*x))),
         'max_abs':float(np.max(np.abs(x))),'signed_mean':float(np.mean(x)),
         'l2_norm':float(np.linalg.norm(x))}

def run_metadata(path, expected_sha, role, expected_h):
 if not path.is_file() or sha(path)!=expected_sha:raise ValueError(f'{role} receipt hash mismatch')
 d=json.loads(path.read_text())
 case=d.get('case',{}); inp=d.get('inputs',d.get('input',{}));prod=d.get('producer',{})
 h=case.get('dt_s',case.get('time_step_seconds',case.get('time_step')))
 dur=case.get('duration_s',case.get('run_seconds',case.get('duration_seconds')))
 if float(h)!=expected_h or float(dur)!=240.:raise ValueError(f'{role} receipt dt/horizon mismatch: {h}/{dur}')
 input_hash=inp.get('wrfinput_sha256',inp.get('sha256',inp.get('expected_e71_sha256')))
 exe_hash=prod.get('executable_sha256',d.get('executable_sha256'))
 if input_hash!='e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9':raise ValueError(f'{role} input is not the pinned e71 state')
 if exe_hash!='5bf90cd95e6d5ac4a5a76f73ec407d9ee6e91c1b6919488df20c9df09a2a7769':raise ValueError(f'{role} executable differs from the pinned strict-reference executable')
 return {'path':str(path),'sha256':sha(path),'status':d.get('status'),'dt_seconds':float(h),
  'duration_seconds':float(dur),'input_sha256':input_hash,'executable_sha256':exe_hash,
  'namelist_sha256':inp.get('namelist_sha256',d.get('namelist_sha256')),
  'newton_tolerance':case.get('newton_tol')}

def main():
 for key,path in [('protocol',PROTOCOL),('old_npz',OLD_NPZ),('h3_npz',H3_NPZ),
                  ('coarse_receipt',COARSE_RECEIPT),('middle_receipt',MIDDLE_RECEIPT),('h3_receipt',H3_RECEIPT)]:
  if not path.is_file() or sha(path)!=EXPECTED[key]:raise ValueError(f'{key} pinned input missing/hash mismatch')
 protocol=json.loads(PROTOCOL.read_text())
 counts=protocol['official_screen']['owner_counts'];units=protocol['official_screen']['transforms']
 full=protocol['conditional_archived_candidate']['full_epsilon_limits']
 if protocol['schema']!='three_level_w_reference_root_final_v1':raise ValueError('unexpected root protocol')
 runs={'h09375_tau1e9':run_metadata(COARSE_RECEIPT,EXPECTED['coarse_receipt'],'coarse',.9375),
       'h046875_middle':run_metadata(MIDDLE_RECEIPT,EXPECTED['middle_receipt'],'middle',.46875),
       'h0234375_h3':run_metadata(H3_RECEIPT,EXPECTED['h3_receipt'],'h3',.234375)}
 rows=[]
 with np.load(OLD_NPZ,allow_pickle=False) as old,np.load(H3_NPZ,allow_pickle=False) as h3:
  for t in TIMES:
   for f in FIELDS:
    coarse=np.asarray(old[f'{f}_t{t}_coarse'])
    middle=np.asarray(old[f'{f}_t{t}_fine'])
    fine=np.asarray(h3[f'{f}_t{t}'])
    expected_count=int(counts[f])
    if any(x.dtype!=np.float64 for x in (coarse,middle,fine)):
     raise ValueError(f'{f}@{t} arrays must already be FP32-promoted FP64')
    if not(coarse.shape==middle.shape==fine.shape) or coarse.size!=expected_count:
     raise ValueError(f'{f}@{t} physical-owner shape/count mismatch')
    if not(np.isfinite(coarse).all() and np.isfinite(middle).all() and np.isfinite(fine).all()):
     raise ValueError(f'{f}@{t} nonfinite input')
    d1=middle-coarse;d2=fine-middle;direct=fine-coarse
    s1=stat(d1);s2=stat(d2);sd=stat(direct)
    eps=float(full[f]);triangle=s1['rms']+s2['rms']
    if s1['l2_norm']>0 and s2['l2_norm']>0:
     cosine=float(np.dot(d1.ravel(),d2.ravel())/(s1['l2_norm']*s2['l2_norm']));cosine_status='ok'
    else: cosine=None;cosine_status='null: one or both D1/D2 vectors have zero L2 norm'
    rows.append({'time_seconds':t,'field':f,'units':units[f],'owner_count':expected_count,
      'direct_h3_minus_h09375':sd,'full_epsilon_limit':eps,'direct_ratio':sd['rms']/eps,
      'direct_pass':sd['rms']<=eps,'D1_h046875_minus_h09375':s1,'D2_h0234375_minus_h046875':s2,
      'triangle_bound_rms':triangle,'triangle_bound_ratio':triangle/eps,
      'triangle_inequality_satisfied':bool(sd['rms']<=triangle+16*np.finfo(np.float64).eps*max(s1['rms'],s2['rms'])),
      'cosine_D1_D2':cosine,'cosine_status':cosine_status})
 if len(rows)!=24:raise AssertionError('expected 24 field/checkpoint rows')
 out=HERE/'direct_h09375_vs_h0234375_full_epsilon.json'
 summary={'schema':'research_forward_setting_direct_candidate_check_v1','protocol_path':'../three_level_w_reference/root_final_protocol.json',
   'protocol_sha256':sha(PROTOCOL),'old_checkpoint_npz_path':'../pr310_strict_reference_240s/published_fields_and_W.npz','old_checkpoint_npz_sha256':sha(OLD_NPZ),
   'h3_checkpoint_npz_path':'../three_level_w_reference/h3_published_physical_fields.npz','h3_checkpoint_npz_sha256':sha(H3_NPZ),
   'runs':runs,'definition':'D_direct = h=.234375 - h=.9375; D1=h=.46875 - h=.9375; D2=h=.234375 - h=.46875. All arrays are already transformed physical-owner FP64 values.',
   'rows':rows,'row_count':len(rows),'pass_count':sum(x['direct_pass'] for x in rows),'all_24_pass':all(x['direct_pass'] for x in rows),
   'maximum_triangle_bound_ratio':max(x['triangle_bound_ratio'] for x in rows),
   'all_triangle_inequalities_satisfied':all(x['triangle_inequality_satisfied'] for x in rows),
   'interpretation_limits':['This directly tests the archived h=.9375 strict candidate against h=.234375 with full original epsilon values.',
     'The triangle bound is reported independently of the direct screen and cannot replace it.',
     'D1/D2 cosine is descriptive only. No order, true-error, or general acceptance claim.']}
 out.write_text(json.dumps(summary,indent=2,sort_keys=True)+'\n')
 csvpath=HERE/'direct_h09375_vs_h0234375_full_epsilon.csv'
 cols=('time_seconds','field','units','owner_count','direct_rms','direct_max_abs','full_epsilon_limit','direct_ratio','direct_pass',
       'D1_rms','D2_rms','triangle_bound_rms','triangle_bound_ratio','triangle_inequality_satisfied','cosine_D1_D2','cosine_status')
 with csvpath.open('w',newline='') as f:
  w=csv.DictWriter(f,fieldnames=cols);w.writeheader()
  for x in rows:w.writerow({'time_seconds':x['time_seconds'],'field':x['field'],'units':x['units'],'owner_count':x['owner_count'],
   'direct_rms':x['direct_h3_minus_h09375']['rms'],'direct_max_abs':x['direct_h3_minus_h09375']['max_abs'],'full_epsilon_limit':x['full_epsilon_limit'],
   'direct_ratio':x['direct_ratio'],'direct_pass':x['direct_pass'],'D1_rms':x['D1_h046875_minus_h09375']['rms'],'D2_rms':x['D2_h0234375_minus_h046875']['rms'],
   'triangle_bound_rms':x['triangle_bound_rms'],'triangle_bound_ratio':x['triangle_bound_ratio'],'triangle_inequality_satisfied':x['triangle_inequality_satisfied'],
   'cosine_D1_D2':x['cosine_D1_D2'],'cosine_status':x['cosine_status']})
 print(json.dumps({'json':out.name,'json_sha256':sha(out),'csv':csvpath.name,'csv_sha256':sha(csvpath),
  'rows':len(rows),'pass_count':summary['pass_count'],'all_24_pass':summary['all_24_pass'],
  'maximum_triangle_bound_ratio':summary['maximum_triangle_bound_ratio']},indent=2))
if __name__=='__main__':main()
