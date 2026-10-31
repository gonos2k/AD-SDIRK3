#!/usr/bin/env python3
import csv,hashlib,importlib.util,json,math
from pathlib import Path
import numpy as np
from netCDF4 import Dataset
from scipy.fft import dct,idct
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
P=Path(__file__).resolve().parent
RK=Path('/Users/yhlee/AD-SDIRK3-review-evidence/noise-cost-coupling-20261031/modal/runs/baseline_rk3')
D={'RK3':RK,'Baseline':P/'a2_case/runs/baseline','P0 candidate':P/'a2_geometry_validation/run/runs/candidate'}
H=Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_screen/benchmark_wrf_accuracy_cost.py');spec=importlib.util.spec_from_file_location('helper',H);helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest(); stamp=['00:00:00','00:10:00'];grid={'nx':40,'ny':80,'nz':64}; states={}; sources={}; stages={}
for name,d in D.items():
 r=json.loads((d/('forecast_receipt.json' if name=='RK3' else 'result.json')).read_text())
 if name=='RK3':assert r['status']=='completed' and r['return_code']==0 and r['actual_outer_step_count']==1 and r['terminal_wrapper_calls_ok']
 else:
  assert r['return_code']==0 and r['model_completed'] and r['steps']==1 and r['wrapper_rows'][-1]==[3,0]
  if name=='Baseline':assert r['failure_class']=='timing_ineligible'
  else:assert r['status']=='completed' and r['cost_eligible'] is False
  st=r['stage_residual_scaled_rms'];assert set(st)=={'2','3','4'} and all(math.isfinite(x) and 0<=x<=1e-9 for x in st.values());stages[name]=st
 assert sha(d/'wrfinput_d01')=='c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912'
 for t in stamp:
  f=d/('wrfout_d01_0001-01-01_'+t); ready=d/('wrfoutReady_d01_0001-01-01_'+t);assert f.is_file() and ready.is_file();sources[name+' '+t]={'path':str(f),'sha256':sha(f),'ready_sha256':sha(ready)}
 states[name]=helper.load_initial_state(d,grid,dry_case_no_qv=True,use_theta_m=1,source='wrfout')
for name in ('Baseline','P0 candidate'):assert helper.compare_initial_states({'rk3_h600':states['RK3'],name:states[name]},require_input_hash=True)['status']=='passed'
def read(d):
 with Dataset(d/('wrfout_d01_0001-01-01_'+stamp[1])) as f:
  a={n:np.asarray(f[n][0],dtype=np.float64) for n in ('U','V','W','PH','PHB','T','MU')}; assert all(np.isfinite(x).all() for x in a.values());return a
fields={k:read(v) for k,v in D.items()};owned={k:helper._owned(v,grid) for k,v in fields.items()};rows=[]
for candidate,ref in [('Baseline','RK3'),('P0 candidate','RK3'),('P0 candidate','Baseline')]:
 for field in ('U','V','W','PH','T','MU'):
  a=owned[candidate][field]-owned[ref][field];rows.append({'case':candidate,'reference':ref,'field':field,'units':{'U':'m/s','V':'m/s','W':'m/s','PH':'m2/s2','T':'K','MU':'Pa'}[field],'rms':float(np.sqrt(np.mean(a*a))),'max_abs':float(np.max(np.abs(a)))})
plots={};y=(np.arange(80)+.5)*100
for name,a in fields.items():
 w=a['W'][1:,:,20];z=(a['PH'][1:,:,20]+a['PHB'][1:,:,20])/9810;c=dct(w,type=2,axis=1,norm='ortho');c[:,:40]=0;plots[name]=(w,z,idct(c,type=2,axis=1,norm='ortho'))
lims=[max(max(float(np.abs(v[i]).max()) for v in plots.values()),1e-30) for i in (0,2)];fig,axes=plt.subplots(3,2,figsize=(12,12),sharex=True,sharey=True,constrained_layout=True)
for i,name in enumerate(D):
 w,z,hp=plots[name]
 for j,(a,label) in enumerate(((w,'raw W'),(hp,'signed Y high band k ≥ 40'))):
  ax=axes[i,j];im=ax.pcolormesh(np.broadcast_to(y,z.shape),z,a,shading='auto',cmap='RdBu_r',vmin=-lims[j],vmax=lims[j],rasterized=True);ax.set_title(name+' '+label);ax.set_xlabel('Y (km)');ax.set_ylabel('Native height (km)');fig.colorbar(im,ax=ax,label='W (m/s)',shrink=.84)
fig.suptitle('10-minute W, x index 20: first FGMRES P0 only\nSame input and initial state; column color scales shared; numerical diagnostic')
out=P/'geometry_validation_analysis';out.mkdir(exist_ok=False);fig.savefig(out/'w_yz.png',dpi=150);plt.close(fig)
with (out/'six_field_differences.csv').open('x',newline='') as fd:
 w=csv.DictWriter(fd,fieldnames=rows[0].keys());w.writeheader();w.writerows(rows)
report={'scope':'One h600 first-FGMRES P0 numerical diagnostic, not long-term noise or cost evidence; baseline clock ineligible and candidate cost ineligible.','sources':sources,'initial_field_gate':'passed exact shared fields, grid and canonical input','stage_residual_scaled_rms':stages,'owned':'U omit periodic x duplicate; V both wall faces; W and PH omit bottom face; T and MU all mass points','W_plot':'x index 20, own native PH+PHB height, DCT-II k>=40 inverse, shared column scales','raw_scale_m_s':lims[0],'high_band_scale_m_s':lims[1],'field_rows':rows,'script_sha256':sha(Path(__file__))}
(out/'receipt.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(rows,indent=2))
