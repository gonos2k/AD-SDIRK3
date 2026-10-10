#!/usr/bin/env python3
"""Compare saved native whole states with the pinned source wave; no native execution."""
import argparse, csv,hashlib,io,json,platform,sys,tempfile,zipfile,contextlib,subprocess
from pathlib import Path
import numpy as np
import scipy
from scipy.linalg import expm
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[3])
parser.add_argument('--accepted-zip',type=Path,required=True)
parser.add_argument('--baseline-zip',type=Path,required=True)
parser.add_argument('--forecast-zip',type=Path,required=True)
parser.add_argument('--out',type=Path,required=True,help='JSON receipt output path')
args=parser.parse_args()
ROOT=args.repo.resolve();sys.path.insert(0,str(ROOT/'tools'))
import probe_small_step_inverse as driver
import probe_fine_split_pullbacks as split
import test_fine_wave_inverse as inverse
import wave_quadratic_reference as source

ACCEPTED_ZIP=args.accepted_zip.resolve()
ACCEPTED_SHA='8dbe3f9d20ac2d5591cab455c67086978bd51676ed7b2acb8c167d370372bcbd'
ORIGIN_ZIP=args.baseline_zip.resolve()
ORIGIN_SHA=driver.PR283_RESUME_ZIP_SHA
FORECAST_ZIP=args.forecast_zip.resolve()
FORECAST_SHA='6a6b894f60824724dd89d8a886e2dba861952e98f600ec3d6019a864427eb862'
out=Path(tempfile.mkdtemp(prefix='pr286-whole-state-'))
assert hashlib.sha256(ACCEPTED_ZIP.read_bytes()).hexdigest()==ACCEPTED_SHA
assert hashlib.sha256(ORIGIN_ZIP.read_bytes()).hexdigest()==ORIGIN_SHA
assert hashlib.sha256(FORECAST_ZIP.read_bytes()).hexdigest()==FORECAST_SHA
accepted,ar,_=driver.copy_resume_bundle(ACCEPTED_ZIP,ACCEPTED_SHA,out/'accepted')
origin,oroot,_=driver.copy_resume_bundle(ORIGIN_ZIP,ORIGIN_SHA,out/'origin')
with zipfile.ZipFile(FORECAST_ZIP) as z:
    assert z.testzip() is None
    f_report=json.loads(z.read('measurement/report.json'))
    for c in f_report['call_reports']:
        for suffix in ('.csv','.initial_state.txt','.stdout.log','.stderr.log','.time.txt'):
            member='measurement/'+c['label']+suffix
            (out/(c['label']+suffix)).write_bytes(z.read(member))
assert f_report['accepted_zip_sha256']==ACCEPTED_SHA and f_report['baseline_zip_sha256']==ORIGIN_SHA
assert accepted['accepted_updates']==10 and accepted['status']=='completed_bounded_continuation'
assert origin['status']=='completed_bounded_updates' and origin['accepted_updates']==2
current_fingerprints=driver.source_fingerprints()
producer_fingerprints=dict(current_fingerprints)
historical_source_entries=[]
for rel, expected_sha in f_report['native_source_sha256'].items():
 if current_fingerprints[rel] != expected_sha:
  pinned=subprocess.check_output(['git','show',f"{f_report['source_git_head']}:{rel}"],cwd=ROOT)
  historical_sha=hashlib.sha256(pinned).hexdigest()
  assert historical_sha==expected_sha, rel
  producer_fingerprints[rel]=expected_sha
  historical_source_entries.append({'path':rel,'producer_sha256':expected_sha,'current_sha256':current_fingerprints[rel],'verified_via_git_show_at':f_report['source_git_head']})
assert producer_fingerprints==f_report['native_source_sha256']
for module,record in f_report['source_reference_modules'].items():
    assert hashlib.sha256((ROOT/'tools'/f'{module}.py').read_bytes()).hexdigest()==record['sha256']
assert hashlib.sha256((ROOT/'tools/probe_accepted_analysis_forecast.py').read_bytes()).hexdigest()==f_report['forecast_helper_sha256']
# All artifacts use the same source fixture, descriptor, basis and observation file.
inputs=ar/'inputs'; inputs0=oroot/'inputs'
with zipfile.ZipFile(ROOT/'tools/fixtures/pr282-exact-returned-state.zip') as z: manifest=json.loads(z.read('manifest.json'))
for name in ('initial_state.txt','observations.txt','descriptor_coarse.csv','descriptor_fine.csv','canonical_basis.npy'):
    assert (inputs/name).read_bytes()==(inputs0/name).read_bytes()
    if name in manifest['files']:
        assert hashlib.sha256((inputs/name).read_bytes()).hexdigest()==manifest['files'][name]['sha256']
base_words=(inputs/'initial_state.txt').read_text().split();base=np.asarray([float(v) for v in base_words[1:]],dtype=np.float64)
B=np.load(inputs/'canonical_basis.npy',allow_pickle=False)
assert B.shape==(8480,4) and B.dtype==np.float64 and inverse.digest(B)==accepted['canonical_basis_sha256']
_,descriptor,_,_=inverse.read_payload(inputs/'descriptor_fine.csv')
obs=np.loadtxt(inputs/'observations.txt',skiprows=2);assert obs.shape==(105,5) and np.isfinite(obs).all()
# Absolute source coefficients form qtruth; local delta10 only reconstructs the accepted native state.
coarse=source.build_case(8,4,native_csv=inputs/'descriptor_coarse.csv')
fine=source.build_case(16,8,native_csv=inputs/'descriptor_fine.csv')
_,basis=inverse.profile_basis(coarse,fine)
q0=sum((inverse.TRUTH[j]*basis[j] for j in range(4)),np.zeros(4*int(fine['nz'])+1,dtype=np.complex128))
A=source.source_matrix(fine)
xyz,qobs=inverse.source_observation_data(fine,q0,(8,6,4),coarse)
anchor_error=float(np.max(np.abs(qobs.T-obs[:,3:5])))
anchor_tolerance=128*np.finfo(float).eps*max(1.0,float(np.max(np.abs(obs[:,3:5]))))
assert np.array_equal(xyz,obs[:,:3]) and anchor_error<=anchor_tolerance
qsha=hashlib.sha256(np.ascontiguousarray(q0,dtype=np.complex128).tobytes()).hexdigest()
assert f_report['source_qtruth']['dtype']=='<c16' and f_report['source_qtruth']['shape']==[33]
# State offsets: source U/V/W are wave fields. Source PH/theta/MU are increments
# above descriptor-backed native base PH/theta/MU. Compare at shared eta/staggered indices.
blocks={'u':(0,1632,'m s-1','u mass/profile, periodic-x faces'),
 'v':(1632,1664,'m s-1','v mass/profile, y faces'),
 'w':(3296,1728,'m s-1','w vertical faces'),
 'ph':(5024,1728,'m2 s-2','PH/geopotential vertical faces; base PH plus source perturbation'),
 'theta':(6752,1536,'K','theta mass points; base theta plus source perturbation'),
 'mu':(8288,192,'Pa','column MU points; base MU plus source perturbation')}
equilibrium=np.asarray(descriptor['base'],dtype=np.float64)
assert equilibrium.shape==base.shape and np.isfinite(equilibrium).all()
base_offsets={k:equilibrium[off:off+n] for k,(off,n,_,_) in blocks.items() if k in ('ph','theta','mu')}
assert equilibrium.shape==(8480,) and np.isfinite(equilibrium).all()
def source_state(t):
 packed=inverse.pack_source_mode(expm(float(t)*A)@q0,(16,12,8));full=packed.copy()
 for k in ('ph','theta','mu'):
    off,n,_,_=blocks[k];full[off:off+n]+=base_offsets[k]
 return packed,full

def read_arrays(path): return inverse.read_payload(path)[1]
# Saved baseline delta0 and analysis delta10 checkpoints at t=0/150/300.
origin_call=next(c for c in origin['calls'] if c['label']=='gradient_u0')
final_call=next(c for c in accepted['calls'] if c['label']==accepted['terminal_gradient']['call_label'])
origin_arrays=read_arrays(oroot/origin_call.get('bundle_csv','gradient_u0.csv'))
analysis_arrays=read_arrays(ar/final_call['bundle_csv'])
assert np.array_equal(origin_arrays['initial_state'],base)
assert np.array_equal(analysis_arrays['initial_state'],base+B@np.asarray(accepted['final_delta']))
actual={'delta0':{t:origin_arrays['initial_state' if t==0 else f'checkpoint_{t}'] for t in (0,150,300)},
        'delta10':{t:analysis_arrays['initial_state' if t==0 else f'checkpoint_{t}'] for t in (0,150,300)}}
# Forecast state-only restart checkpoints at t=600/900 for both branches.
assert len(f_report['call_reports'])==4 and f_report['active_call'] is None
branches={'analysis':'delta10','baseline':'delta0'}
future_actual={'delta10':{},'delta0':{}}
for branch,state_name in branches.items():
 calls=[c for c in f_report['call_reports'] if c['branch']==branch]
 assert [(c['absolute_start_s'],c['absolute_end_s']) for c in calls]==[(300,600),(600,900)]
 previous=actual[state_name][300]
 for c in calls:
  label=c['label'];state_path=out/(label+'.initial_state.txt');csv_path=out/(label+'.csv')
  assert state_path.is_file() and csv_path.is_file()
  state=split.read_literal_vector(state_path)
  assert np.array_equal(state,previous) and inverse.digest(state)==c['state_array_sha256']
  assert hashlib.sha256(state_path.read_bytes()).hexdigest()==c['state_file_sha256']
  assert hashlib.sha256(csv_path.read_bytes()).hexdigest()==c['output_sha256'] and c['return_code']==0
  meta,arr,scalars,text=inverse.read_payload(csv_path)
  driver.validate_trial(meta,arr,scalars,text,expected_initial=state,center_arrays=None,gradient=False)
  for key in (*split.CONTEXT_ARRAYS,'observed_150','observed_300'):
   assert np.array_equal(arr[key],analysis_arrays[key]),(label,key)
  for t in (150,300):
   idx=arr[f'bracket_index_{t}']
   assert idx.shape==(420,) and np.isfinite(idx).all() and np.all(idx==np.floor(idx)) and np.all((idx>=0)&(idx<8))
   assert inverse.digest(idx)==c[f'bracket_{t}_sha256']
  previous=arr['checkpoint_300'];future_actual[state_name][c['absolute_end_s']]=previous
# Full-field per-variable metrics at all five times; no mixed-unit aggregate.
def per_variable_metrics(actual_state,source_wave,source_expected):
 rows={}
 spatial_shapes={'u':(12,8,17),'v':(13,8,16),'w':(12,9,16),'ph':(12,9,16),
                 'theta':(12,8,16),'mu':(12,16)}
 expected_owned_count={'u':1536,'v':1664,'w':1536,'ph':1536,'theta':1536,'mu':192}
 owned_policy={'u':'exclude periodic duplicate x-face at i=nx',
  'v':'include all physical nonperiodic y-faces',
  'w':'exclude fixed lower z-face; include dynamic top face',
  'ph':'exclude fixed lower z-face; include dynamic top face',
  'theta':'include all mass points','mu':'include all mass points'}
 for name,(off,n,unit,mapping) in blocks.items():
  err=(actual_state[off:off+n]-source_expected[off:off+n]).reshape(spatial_shapes[name])
  wave=source_wave[off:off+n].reshape(spatial_shapes[name])
  if name=='u': err=err[:,:,:-1];wave=wave[:,:,:-1]
  elif name in ('w','ph'): err=err[:,1:,:];wave=wave[:,1:,:]
  assert err.size==expected_owned_count[name] and wave.size==expected_owned_count[name]
  err_rms=float(np.sqrt(np.mean(err*err)));wave_rms=float(np.sqrt(np.mean(wave*wave)))
  rows[name]={'unit':unit,'stagger_and_mapping':mapping,'owned_point_policy':owned_policy[name],
   'owned_point_count':int(err.size),'owned_point_error_rms':err_rms,
   'owned_point_error_max':float(np.max(np.abs(err))),'owned_point_source_wave_rms':wave_rms,
   'error_rms_over_source_wave_rms':err_rms/wave_rms if wave_rms else None}
 return rows
state_metrics={'delta0':{},'delta10':{}}
for branch in ('delta0','delta10'):
 for t,state in actual[branch].items():
  wave,expected=source_state(t);state_metrics[branch][str(t)]=per_variable_metrics(state,wave,expected)
for branch in ('delta0','delta10'):
 for t,state in future_actual[branch].items():
  wave,expected=source_state(t);state_metrics[branch][str(t)]=per_variable_metrics(state,wave,expected)
# Separately compare the 105 physical-W samples at t=150/300/600/900.
actual_predictions={'delta0':{},'delta10':{}}
for branch,arrays in (('delta0',origin_arrays),('delta10',analysis_arrays)):
 actual_predictions[branch][150]=arrays['predicted_150'];actual_predictions[branch][300]=arrays['predicted_300']
for branch,state_name in branches.items():
 for c in f_report['call_reports']:
  if c['branch']!=branch: continue
  arr=read_arrays(out/(c['label']+'.csv'));actual_predictions[state_name][c['absolute_end_s']]=arr['predicted_300']
prediction_errors={}
for branch in ('delta0','delta10'):
 prediction_errors[branch]={}
 for t,pred in actual_predictions[branch].items():
  truth=inverse.source_physical_w_samples(fine,expm(float(t)*A)@q0,(16,12,8),xyz)
  diff=pred-truth
  prediction_errors[branch][str(t)]={'rms_m_s':float(np.sqrt(np.mean(diff*diff))),
   'max_abs_m_s':float(np.max(np.abs(diff))),'rms_sigma':float(np.sqrt(np.mean(diff*diff))/3e-4)}
# Pinned source-reported forecast error metrics are reproduced by the local source at numerical tolerance.
def errmetrics(pred,truth):
 e=pred-truth;norm=float(np.linalg.norm(truth));rms=float(np.sqrt(np.mean(e*e)))
 return {'error_max_abs_m_s':float(np.max(np.abs(e))),'error_rms_m_s':rms,
  'error_rms_sigma':rms/3e-4,'error_relative_l2':float(np.linalg.norm(e)/norm) if norm else None}
source_forecast_metrics={}
for branch,state_name in branches.items():
 source_forecast_metrics[branch]={}
 for t in (600,900):
  actual_pred=actual_predictions[state_name][t];truth=inverse.source_physical_w_samples(fine,expm(t*A)@q0,(16,12,8),xyz)
  metrics=errmetrics(actual_pred,truth);source_forecast_metrics[branch][str(t)]=metrics
  recorded=f_report['forecast_metrics'][branch][str(t)]
  for k,v in metrics.items(): assert np.isclose(v,recorded[k],rtol=1e-6,atol=1e-12),(branch,t,k,v,recorded[k])
# BLAS/runtime fingerprint for any last-bit source-reference variation.
buf=io.StringIO()
with contextlib.redirect_stdout(buf): np.__config__.show()
report_out={'schema':'pr286-whole-physical-state-source-audit-v1','accepted_zip_sha256':ACCEPTED_SHA,
 'origin_zip_sha256':ORIGIN_SHA,'forecast_zip_sha256':FORECAST_SHA,
 'source_git_head_forecast':f_report['source_git_head'],'audit_worktree_head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
 'historical_native_source_entries_verified_at_producer_revision':historical_source_entries,
 'source_module_sha256':f_report['source_reference_modules'],'native_source_sha256_producer':f_report['native_source_sha256'],'native_source_sha256_current_worktree':current_fingerprints,'source_qtruth_sha256_local':qsha,
 'source_qtruth_sha256_forecast_report':f_report['source_qtruth']['sha256'],
 'source_qtruth_hash_bitwise_match':qsha==f_report['source_qtruth']['sha256'],
 'source_anchor_max_abs_m_s':anchor_error,'source_anchor_roundoff_tolerance_m_s':anchor_tolerance,
 'same_profile_stagger_unit_mapping':blocks,'source_state_offset_policy':
  'U/V/W compare directly to packed source wave. PH/theta/MU source perturbations add to descriptor[base] native equilibrium blocks; the literal saved initial_state is used only for local-chart reconstruction. Source vertical profiles and native states share eta levels/staggers; no interpolation to common fixed Eulerian heights is claimed.',
 'states_compared':state_metrics,'physical_W_prediction_error_metrics':prediction_errors,
 'forecast_report_metrics_recomputed':source_forecast_metrics,
 'handoff_scope':'delta10 and delta0 source/native state comparisons at 0/150/300/600/900; 600 and 900 use the two actual cold-restart output checkpoints.',
 'whole_state_scope':'per-variable owned/active spatial RMS and maximum over the stated stagger locations; duplicate periodic U x-face and fixed lower W/PH z-faces are excluded, dynamic W/PH top faces are included, all theta/MU mass points and all physical V y-faces are included. No mixed-unit aggregate is computed.',
 'local_delta_vs_absolute_source_controls':'accepted local delta10 reconstructs actual initial state with base+Bcan@delta; source qtruth is separately reconstructed from absolute inverse.TRUTH coordinates. No coefficient equality is asserted.',
 'numpy_version':np.__version__,'scipy_version':scipy.__version__,'platform':platform.platform(),
 'numpy_blas_config':buf.getvalue(),'eg_status':'open; no full Eg/stationarity certificate',
 'native_calls_started_by_audit':False}
args.out.resolve().parent.mkdir(parents=True,exist_ok=True)
args.out.resolve().write_text(json.dumps(report_out,indent=2)+'\n')
print(json.dumps({'result':'passed','source_anchor_m_s':anchor_error,'source_qtruth_hash_exact':report_out['source_qtruth_hash_bitwise_match'],
 'state_times':[0,150,300,600,900],'branches':['delta0','delta10'],
 'prediction_rmse_600_delta10':prediction_errors['delta10']['600']['rms_m_s'],
 'prediction_rmse_900_delta10':prediction_errors['delta10']['900']['rms_m_s'],
 'receipt':str(args.out.resolve()),'native_calls_started':False},sort_keys=True))
