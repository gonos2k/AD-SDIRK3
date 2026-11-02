from pathlib import Path
import hashlib,json,re,sys,csv
import numpy as np
from netCDF4 import Dataset
E=Path(__file__).resolve().parent
P=Path('/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101');sys.path.insert(0,str(P));import benchmark_wrf_accuracy_cost as helper
from base_screen_reference import all_numeric_bitwise
R=E/'native_regression';O=P/'runs/control';K=Path('/Users/yhlee/AD-SDIRK3-review-evidence/noise-cost-coupling-20261031/modal/runs/baseline_rk3');end='wrfout_d01_0001-01-01_00:10:00'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
raw=json.loads((R/'result.json').read_text());assert raw['status']=='pending_numerical_comparisons' and not raw['errors'] and raw['return_code']==0
parity=all_numeric_bitwise(R/end,O/end);assert parity['passed'] and parity['numeric_variables_compared']==174
text='\n'.join((R/n).read_text(errors='replace') for n in ['rsl.out.0000','rsl.error.0000','launcher.stdout.log','launcher.stderr.log'])
stages=re.findall(r'\[Newton\] CONVERGED stage=(\d+) iters=(\d+) \|\|S⁻¹R\|\|_rms=([0-9.eE+-]+)',text);assert [int(x[0]) for x in stages]==[2,3,4] and all(np.isfinite(float(x[2])) and float(x[2])<=1e-9 for x in stages)
wrap=re.findall(r'Tile\s+1:\s+Steps=\s*(\d+), Failed=\s*(\d+)',text);assert wrap and set(wrap)=={('3','0')}
paths={'new_native':R,'archived_A9':O,'archived_RK3':K};initial={k:helper.load_initial_state(v,{'nx':40,'ny':80,'nz':64},dry_case_no_qv=True,use_theta_m=1,source='wrfout')for k,v in paths.items()};gate=helper.compare_initial_states(initial,require_input_hash=False)
fields={};finite={}
for label,path in paths.items():
 with Dataset(path/end) as d:
  rawfields={name:np.asarray(d.variables[name][0],dtype=np.float64) for name in ['U','V','W','PH','T','MU']};owned=helper._owned(rawfields,{'nx':40,'ny':80,'nz':64});fields[label]=owned
  pressure=np.asarray(d.variables['P'][0],dtype=np.float64)+np.asarray(d.variables['PB'][0],dtype=np.float64);mass=np.asarray(d.variables['MU'][0],dtype=np.float64)+np.asarray(d.variables['MUB'][0],dtype=np.float64);geo=np.asarray(d.variables['PH'][0],dtype=np.float64)+np.asarray(d.variables['PHB'][0],dtype=np.float64)
  positive={'theta':float(np.min(rawfields['T']+300)),'total_pressure':float(np.min(pressure)),'total_mass':float(np.min(mass)),'geopotential_layer_delta':float(np.min(np.diff(geo,axis=0)))}
  assert all(np.isfinite(v).all()for v in owned.values()) and all(np.isfinite(x)and x>0 for x in positive.values());finite[label]=positive
rows=[]
for name in ['U','V','W','PH','T','MU']:
 new=fields['new_native'][name];old=fields['archived_A9'][name];rk3=fields['archived_RK3'][name];err=new-rk3
 rows.append({'field':name,'owned_points':new.size,'new_minus_old_max_abs':float(np.max(np.abs(new-old))),'new_minus_RK3_RMS':float(np.sqrt(np.mean(err*err))),'new_minus_RK3_max_abs':float(np.max(np.abs(err)))})
with(E/'rk3_comparison.csv').open('w',newline='')as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(rows)
record={'status':'PASSED','scope':'h600 single-step A9/P0 research-config noninterference, not default-native or long-forecast convergence/cost acceptance','source_head':json.loads((R/'attempt.json').read_text())['target_head'],'build_source_commit':json.loads((E/'newton_build_receipt.json').read_text())['source_commit'],'raw_runner_sha256':sha(R/'result.json'),'endpoint_parity':parity,'stage_scaled_RMS':[{'stage':int(s),'Newton_iterations':int(i),'scaled_rms':float(v)}for s,i,v in stages],'wrapper_calls_failed':wrap,'initial_gate':gate,'finite_and_positive_saved_endpoints':finite,'comparison_rows':rows,'files':{str(p/end):{'bytes':(p/end).stat().st_size,'sha256':sha(p/end)}for p in paths.values()},'helper_sha256':sha(P/'benchmark_wrf_accuracy_cost.py'),'array_compare_helper_sha256':sha(P/'base_screen_reference.py'),'new_RK3_run':False,'performance_eligible':False,'full_convergence_guarantee':False};(E/'native_validation.json').write_text(json.dumps(record,indent=2)+'\n');print('PASSED 174 endpoint arrays +3stage +wrapper3/0 +finite/RK3comparison; rawresultpreserved')
