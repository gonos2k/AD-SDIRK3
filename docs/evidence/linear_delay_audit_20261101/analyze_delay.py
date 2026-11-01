#!/usr/bin/env python3
"""Postprocess completed linear-delay diagnostic; no model execution."""
import csv,hashlib,json
from pathlib import Path
import numpy as np
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
E=Path(__file__).resolve().parent; H=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest(); post=json.loads((E/'control_postvalidation.json').read_text());assert post['status']=='PASSED';res={};allrec={};arrays={};hashes={}
files={'U_eval':'linear_delay_u_eval.npy','U_ref':'linear_delay_u_ref_first_jvp.npy','tangent':'linear_delay_first_tangent.npy','Jv':'linear_delay_first_jvp.npy'}
for case in ('control','candidate'):
 d=E/'runs'/case;p=d/'result.json';r=json.loads(p.read_text());assert r['model_completed'] and r['return_code']==0 and r['steps']==1 and r['wrapper_rows'][-1]==[3,0] and (post['original_result_sha256']==H(p) if case=='control' else r['status']=='completed' and r['decision']=='DIAGNOSTIC_COMPLETED'); rec=r['linear_delay']['records'];assert len(rec)==6 and [(x['stage'],x['newton']) for x in rec]==[(2,0),(2,1),(3,0),(3,1),(4,0),(4,1)] and all(x['clock_valid'] and x['timing_eligible'] and x['expected_path_match'] and x['success'] for x in rec);assert rec[4]['selected_stage4_newton0'] and rec[4]['sample_captured'] and rec[4]['raw_npy_written'];res[case]=r;allrec[case]=rec;hashes[str(p)]=H(p)
 arrays[case]={}
 for name,filename in files.items():
  f=d/filename;a=np.load(f,allow_pickle=False);assert a.dtype==np.float64 and a.ndim==1 and np.isfinite(a).all();arrays[case][name]=a;hashes[str(f)]=H(f)
assert res['control']['source_commit']==res['candidate']['source_commit'];layout=allrec['control'][4]['layout'];assert layout==allrec['candidate'][4]['layout'] and [b['name'] for b in layout]==['ru','rv','rw','ph','t','mu'];n=layout[-1]['start']+layout[-1]['size'];assert all(a.size==n for case in arrays.values() for a in case.values())
rows=[];timings=[]
for k in range(6):
 for case in ('control','candidate'):
  q=allrec[case][k];wall=q['wall_ms'];cpu=q['process_user_ms']+q['process_system_ms'];jw=q['JVP_wall_ms'];jc=q['JVP_process_user_ms']+q['JVP_process_system_ms'];calls=q['JVP_calls_in_linear'];assert calls>0
  timings.append({'case':case,'stage':q['stage'],'newton':q['newton'],'solve_wall_ms':wall,'solve_cpu_ms':cpu,'arithmetic_wall_minus_cpu_ms':wall-cpu,'jvp_wall_ms':jw,'jvp_cpu_ms':jc,'jvp_calls':calls,'jvp_wall_ms_per_call':jw/calls,'jvp_cpu_ms_per_call':jc/calls,'selected_ad_calls_outer':q['selected_ad_calls_outer'],'selected_fd_calls_outer':q['selected_fd_calls_outer'],'internal_fwad_to_fd_fallbacks':q['internal_fwad_to_fd_fallbacks'],'sampled_first_arnoldi_fallbacks':q['sampled_first_arnoldi_fallbacks'],'rhs_modes':q['linear_rhs_modes'],'guard_events':q['guard_events']})
for name in files:
 for b in layout:
  s=slice(b['start'],b['start']+b['size']);a=arrays['control'][name][s];c=arrays['candidate'][name][s];delta=c-a
  rows.append({'array':name,'block':b['name'],'dtype':str(a.dtype),'elements':a.size,'control_l2':float(np.linalg.norm(a)),'candidate_l2':float(np.linalg.norm(c)),'difference_l2':float(np.linalg.norm(delta)),'difference_max_abs':float(np.max(np.abs(delta))),'byte_equal':a.tobytes()==c.tobytes()})
out=E/'analysis';out.mkdir(exist_ok=False)
for filename,table in [('timing.csv',timings),('fifth_state_blocks.csv',rows)]:
 with (out/filename).open('x',newline='') as f:w=csv.DictWriter(f,fieldnames=table[0].keys());w.writeheader();w.writerows(table)
labels=['S2 N0','S2 N1','S3 N0','S3 N1','S4 N0','S4 N1'];ix=np.arange(6);width=.38;fig,ax=plt.subplots(2,2,figsize=(14,9),constrained_layout=True)
for case,off,color in [('control',-width/2,'#4278a0'),('candidate',width/2,'#cf7842')]:
 r=[x for x in timings if x['case']==case]
 for i,(field,title) in enumerate((('solve_wall_ms','Solve elapsed'),('solve_cpu_ms','Solve process CPU'),('jvp_wall_ms_per_call','JVP elapsed per call'),('jvp_cpu_ms_per_call','JVP process CPU per call'))):ax.flat[i].bar(ix+off,[x[field] for x in r],width,color=color,label=case);ax.flat[i].set_title(title);ax.flat[i].set_ylabel('ms');ax.flat[i].set_xticks(ix,labels);ax.flat[i].legend()
fig.suptitle('One diagnostic pair: six corresponding stage/Newton solves\nCPU is process user + system; per-call values divide measured totals; scopes nested, not additive');fig.savefig(out/'solve_timing.png',dpi=150);plt.close(fig)
wb=layout[2];sl=slice(wb['start'],wb['start']+wb['size']);panels={}
for name in ('U_eval','U_ref'):
 for case in ('control','candidate'):panels[(name,case)]=arrays[case][name][sl].reshape(81,65,41)[:80,1:,:40][:,:,20].T
rawlim=max(float(np.abs(x).max()) for x in panels.values());diffs={name:panels[(name,'candidate')]-panels[(name,'control')] for name in ('U_eval','U_ref')};difflim=max(float(np.abs(x).max()) for x in diffs.values());rawlim=max(rawlim,1e-300);difflim=max(difflim,1e-300);fig,ax=plt.subplots(2,3,figsize=(14,8),constrained_layout=True)
for i,name in enumerate(('U_eval','U_ref')):
 for j,(lab,x,lim) in enumerate((('control',panels[(name,'control')],rawlim),('candidate',panels[(name,'candidate')],rawlim),('candidate − control',diffs[name],difflim))):
  im=ax[i,j].imshow(x,origin='lower',aspect='auto',cmap='RdBu_r',vmin=-lim,vmax=lim);ax[i,j].set_title(name+' W: '+lab);ax[i,j].set_xlabel('Y mass index');ax[i,j].set_ylabel('native W k, bottom excluded, top included');fig.colorbar(im,ax=ax[i,j],label='m/s')
fig.suptitle('Captured fifth-solve states, x index 20; not a forecast or proof of identical operators\nRaw panels share scale; differences share separate scale');fig.savefig(out/'fifth_W_states.png',dpi=150);plt.close(fig)
a=timings[8];b=timings[9];decomp={}
for prefix in ('solve','jvp'):
 dw=b[prefix+'_wall_ms']-a[prefix+'_wall_ms'];dc=b[prefix+'_cpu_ms']-a[prefix+'_cpu_ms'];decomp[prefix]={'delta_wall_ms':dw,'delta_process_cpu_ms':dc,'delta_arithmetic_wall_minus_cpu_ms':dw-dc,'qualification':'arithmetic remainder, not measured wait or cause'}
report={'scope':'Same-SDK new observer diagnostic pair only; raw control INCONCLUSIVE retained, offline control postvalidation passed. One pair, no causal scheduler/swap/host claim; later state inputs differ, operator identity not established.','source_commit':res['control']['source_commit'],'control_original_status':res['control']['status'],'control_postvalidation_sha256':H(E/'control_postvalidation.json'),'candidate_status':res['candidate']['status'],'candidate_decision':res['candidate']['decision'],'timing':timings,'fifth_difference':decomp,'block_scope':'full packed StateLayout blocks, including aliases; no mixed-unit energy', 'firstpoint_preflight_fingerprint_equal':{k:res['control']['preflight'].get(k)==res['candidate']['preflight'].get(k) for k in ('geometry_fnv64','raw_grid_fnv64','S_fnv64','mask_fnv64','eos_fnv64')},'block_comparisons':rows,'source_hashes':hashes,'script_sha256':H(Path(__file__))}
(out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'fifth_difference':decomp,'fifth_sample':timings[8:10],'output':str(out)},indent=2))
