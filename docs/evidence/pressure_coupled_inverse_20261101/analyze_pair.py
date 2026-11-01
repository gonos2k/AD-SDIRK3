#!/usr/bin/env python3
import csv,hashlib,importlib.util,json,math,struct,sys
from pathlib import Path
import numpy as np
from netCDF4 import Dataset
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
E=Path(__file__).resolve().parent; R=E; RK=Path('/Users/yhlee/AD-SDIRK3-review-evidence/noise-cost-coupling-20261031/modal/runs/baseline_rk3');H=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
spec=importlib.util.spec_from_file_location('read',Path('/Users/yhlee/AD-SDIRK3-review-evidence/pressure-a5-20261101/analyze_a5_offline.py'));reader=importlib.util.module_from_spec(spec);spec.loader.exec_module(reader)
spec=importlib.util.spec_from_file_location('helper',R/'benchmark_wrf_accuracy_cost.py');helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
if sys.argv[1:]!=['--completed']:raise SystemExit('requires --completed')
results={};X={};records={};D={};hashes={}
for name in ('control','candidate'):
 d=R/'runs'/name;D[name]=d;r=json.loads((d/'result.json').read_text());assert (r['status']=='completed' if name=='control' else r['status']=='failed_or_incomplete' and r['decision']=='INCONCLUSIVE' and r['failure_class']=='paired_quality_and_work') and r['return_code']==0 and r['model_completed'] and r['steps']==1 and r['wrapper_rows'][-1]==[3,0] and r['t0_gate']['status']=='passed';assert set(r['stage_residual_scaled_rms'])=={'2','3','4'} and all(0<=x<1e-9 for x in r['stage_residual_scaled_rms'].values());assert r['raw_artifact_cap_met'] and r['postrun_disk_floor_met'] and abs(r['clock_gap_seconds'])<1
 pin=R/(name+'_pins.json');pins=json.loads(pin.read_text());assert all(H(path)==h for path,h in pins.items());intent=json.loads((R/(name+'_intent.json')).read_text());assert r['source_commit']==intent['source_commit'] and r['executable_sha256']==H(intent['executable']['path'])==intent['executable']['sha256']; assert H(d/'wrfinput_d01')==intent['case_input_sha256'];assert H(d/'namelist.input')==intent['case_namelist_sha256']
 x=reader.records(d/'pressure_a5_direction.bin'); data=(d/'pressure_a4_first_pair.bin').read_bytes();n=struct.unpack('<Q',data[8:16])[0];assert data[:8]==b'SDIRKA41' and len(data)==16+16*n
 v=x['v_scaled'].ravel();z=x['z_scaled'].ravel();az=x['Az_scaled'].ravel();S=x['S_diag'].ravel();assert v.size==n and all(q.size==n for q in (z,az,S)) and np.all(S>0);assert (S*v).astype('<f8').tobytes()==data[16:16+8*n] and (S*az).astype('<f8').tobytes()==data[16+8*n:]
 results[name]=r;X[name]=x;records[name]={'v':S*v,'z':S*z,'Az':S*az,'e':S*(v-az),'scaled_e':v-az,'S':S};hashes[name]={'result':H(d/'result.json'),'a4':H(d/'pressure_a4_first_pair.bin'),'direction':H(d/'pressure_a5_direction.bin'),'actions':H(d/'pressure_a5_actions.bin'),'intent':H(R/(name+'_intent.json')),'pins':H(pin)}
assert results['control']['old05_endpoint_parity']['passed'] and results['control']['control_old05_scaled_v_z_Az_S_bytes_equal'];assert all(x['violations']==0 for x in results['candidate']['owned_field_comparison'].values())
assert np.array_equal(X['control']['v_scaled'],X['candidate']['v_scaled']) and np.array_equal(X['control']['S_diag'],X['candidate']['S_diag'])
blocks=results['control']['a4']['blocks']; assert [b['name'] for b in blocks]==['ru','rv','rw','ph','t','mu'] and [(b['name'],b['start'],b['size']) for b in blocks]==[(b['name'],b['start'],b['size']) for b in results['candidate']['a4']['blocks']]
dims={'ru':(81,64,42),'rv':(82,64,41),'rw':(81,65,41),'ph':(81,65,41),'t':(81,64,41),'mu':(81,41)};assert all(math.prod(dims[b['name']])==b['size'] for b in blocks)
rows=[]; owned={}
for b in blocks:
 name=b['name'];sl=slice(b['start'],b['start']+b['size']);row={'field':name}
 for case in D:
  e=records[case]['e'][sl].reshape(dims[name]);a=e[:80,:40] if name=='mu' else e[:81,:,:40] if name=='rv' else e[:80,1:,:40] if name in ('rw','ph') else e[:80,:,:40];owned[(case,name)]=a;row[case+'_physical_owned_l2']=float(np.linalg.norm(a));row[case+'_scaled_fullblock_l2']=float(np.linalg.norm(records[case]['scaled_e'][sl]));row[case+'_physical_owned_rms']=float(np.sqrt(np.mean(a*a)))
 rows.append(row)
controlnorm=float(np.linalg.norm(records['control']['scaled_e']));candnorm=float(np.linalg.norm(records['candidate']['scaled_e']))
grid={'nx':40,'ny':80,'nz':64}; states={name:helper.load_initial_state(d,grid,dry_case_no_qv=True,use_theta_m=1,source='wrfout') for name,d in {'RK3':RK,**D}.items()}
for name in D: assert helper.compare_initial_states({'rk3_h600':states['RK3'],name:states[name]},require_input_hash=True)['status']=='passed'
forecast={}
for name,d in {'RK3':RK,**D}.items():
 f=d/'wrfout_d01_0001-01-01_00:10:00';assert (d/'wrfoutReady_d01_0001-01-01_00:10:00').is_file()
 with Dataset(f) as ds:forecast[name]=(np.asarray(ds['W'][0,1:,:,20],dtype=float),(np.asarray(ds['PH'][0,1:,:,20],dtype=float)+np.asarray(ds['PHB'][0,1:,:,20],dtype=float))/9810)
 hashes[name+'_forecast']=H(f)
out=E/'comparison_analysis';out.mkdir(exist_ok=False)
with (out/'defect_blocks.csv').open('x',newline='') as f:
 w=csv.DictWriter(f,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
fig,ax=plt.subplots(3,2,figsize=(12,11),constrained_layout=True)
for i,(name,title,unit) in enumerate((('rv','V first-direction defect','m/s²'),('mu','MU first-direction defect','Pa/s'),('ph','PH first-direction defect','m²/s³'))):
 vals=[owned[(case,name)] if name=='mu' else owned[(case,name)][:,:,20].T for case in D];lim=max(float(np.max(np.abs(x))) for x in vals) or 1
 for j,(case,a) in enumerate(zip(D,vals)):
  im=ax[i,j].imshow(a,origin='lower',aspect='auto',cmap='RdBu_r',vmin=-lim,vmax=lim);ax[i,j].set_title(case+' '+title);ax[i,j].set_xlabel('mass x index' if name=='mu' else 'V-face j index' if name=='rv' else 'mass j index');ax[i,j].set_ylabel('mass y index' if name=='mu' else 'native vertical index (bottom omitted for PH)');fig.colorbar(im,ax=ax[i,j],label=unit+' (arbitrary normalized)')
fig.suptitle('Descriptive first-direction defect; original pair INCONCLUSIVE; candidate not adopted');fig.savefig(out/'rate_defects.png',dpi=150);plt.close(fig)
fig,ax=plt.subplots(figsize=(9,5),constrained_layout=True);x=np.arange(6);ax.bar(x-.2,[r['control_scaled_fullblock_l2'] for r in rows],.4,label='control');ax.bar(x+.2,[r['candidate_scaled_fullblock_l2'] for r in rows],.4,label='candidate');ax.set_xticks(x,blocks and [b['name'] for b in blocks]);ax.set_yscale('log');ax.set_ylabel('scaled-space full-block L2 (dimensionless)');ax.set_title('Descriptive only: original pair INCONCLUSIVE; candidate not adopted');ax.legend();fig.savefig(out/'scaled_blocks.png',dpi=150);plt.close(fig)
fig,ax=plt.subplots(3,1,figsize=(10,11),constrained_layout=True);lim=max(float(np.max(np.abs(t[0]))) for t in forecast.values());y=(np.arange(80)+.5)*100
for i,(name,(w,z)) in enumerate(forecast.items()):
 im=ax[i].pcolormesh(np.broadcast_to(y,z.shape),z,w,shading='auto',cmap='RdBu_r',vmin=-lim,vmax=lim);ax[i].set_title(name+' actual W at 10 min');ax[i].set_xlabel('Y km');ax[i].set_ylabel('native height km');fig.colorbar(im,ax=ax[i],label='m/s')
fig.suptitle('Separate forecast context; original pair INCONCLUSIVE; candidate not adopted');fig.savefig(out/'forecast_context.png',dpi=150);plt.close(fig)
report={'source_commit':results['control']['source_commit'],'pair_decision':results['candidate']['decision'],'candidate_original_status':results['candidate']['status'],'candidate_failure_reason':results['candidate']['failure_reason'],'common_v_and_S_exact':True,'candidate_base_z_equals_control_z':(X['candidate']['base_z_scaled'].tobytes()==X['control']['z_scaled'].tobytes()) if 'base_z_scaled' in X['candidate'] else None,'control_174_bitwise_parity':results['control']['old05_endpoint_parity'],'candidate_six_fields_8eps':results['candidate']['owned_field_comparison'],'first_scaled_norms':{'control':controlnorm,'candidate':candnorm},'block_rows':rows,'source_hashes':hashes,'scope':'DESCRIPTIVE ONLY; original candidate failed base-z exact identity: INCONCLUSIVE, candidate not adopted. One instrumented first direction; rate-coordinate vectors arbitrary normalized. Legacy A5 FD and inverse gates not candidate eligibility; coupled P1 inner work nested, no promotion or longer-term forecast claim.','script_sha256':H(Path(__file__))}
(out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps({'norms':report['first_scaled_norms'],'rows':rows},indent=2))
