#!/usr/bin/env python3
"""Portable cost/chain checker; raw receipts and checkpoint ledgers are external evidence."""
from __future__ import annotations
import argparse, hashlib, json
from pathlib import Path
import numpy as np
import recompute as core

N=24531; P='b65b2b01bbac7d00ecf18e71e4492701581c1b7059b6db8ac23fabf68f94acfb'
CORE_SHA='b15f647b90967af1c95abd7915d4b496a60d6bbc7e793147c783f2040c143d89'
CASES=('baseline','replay','plus_0p01','minus_0p01','plus_0p03','minus_0p03')
ENDS=(256,224,192,160,128,96,64,32)
LABELS=tuple(f'residual_prefix_{n:03d}' for n in ENDS)

def need(ok,msg):
    if not bool(ok): raise ValueError(msg)
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()
def safe(x):
    if isinstance(x,dict): return {k:safe(v) for k,v in x.items()}
    if isinstance(x,(list,tuple)): return [safe(v) for v in x]
    if isinstance(x,np.ndarray): return x.tolist()
    if isinstance(x,np.generic): return x.item()
    return x

def check_arrays(a,manifest):
    spec={'case_labels':((6,),'<U10'),'eta_actual_f32':((6,),'<f4'),
      'mid_states_f64':((6,N),'<f8'),'terminal_states_f64':((6,N),'<f8'),
      'q_mid_f32':((N,),'<f4'),'q_terminal_f32':((N,),'<f4'),'direction_f32':((N,),'<f4'),
      'y_f64':((2,),'<f8'),'sigma_f64':((2,),'<f8'),
      'old_terminal_g0_f32':((N,),'<f4'),'old_two_time_g0_f32':((N,),'<f4'),
      'prefix_ends_i32':((8,),'<i4'),'lambda_in_f32':((8,N),'<f4'),
      'lambda_out_f32':((8,N),'<f4'),'local_precast_f64':((8,N),'<f8')}
    need(set(a)==set(spec),'packet array-key set mismatch')
    for k,(shape,dtype) in spec.items():
        need(a[k].shape==shape and a[k].dtype==np.dtype(dtype),f'{k}: fixed packet shape/dtype mismatch')
        if a[k].dtype.kind not in ('U','S'): need(np.isfinite(a[k]).all(),f'{k}: nonfinite packet values')
        pin=manifest['arrays'][k]
        need(pin['shape']==list(shape) and pin['dtype']==np.dtype(dtype).str and
             hashlib.sha256(np.ascontiguousarray(a[k]).tobytes()).hexdigest()==pin['sha256'],f'{k}: manifest array pin mismatch')
    need(tuple(a['case_labels'].tolist())==CASES and a['prefix_ends_i32'].tolist()==list(ENDS),'case/prefix order mismatch')
    need(a['y_f64'].tobytes()==np.array([0.,0.],dtype='<f8').tobytes() and
         a['sigma_f64'].tobytes()==np.array([1.,1.],dtype='<f8').tobytes(),'frozen y/sigma changed')
    eta=a['eta_actual_f32'].astype(np.float64)
    need(eta[0]==0 and eta[1]==0 and eta[2]>0 and eta[3]==-eta[2] and eta[4]>0 and eta[5]==-eta[4],
         'actual FP32 eta set is not the frozen symmetric pair set')
    need(eta[2]==float(np.float32(.01)) and eta[4]==float(np.float32(.03)),'actual FP32 eta widths changed')
    need(a['mid_states_f64'][0].tobytes()==a['mid_states_f64'][1].tobytes() and
         a['terminal_states_f64'][0].tobytes()==a['terminal_states_f64'][1].tobytes(),
         'baseline/replay midpoint or terminal states differ')
    cot=core.objective_pair(a['mid_states_f64'][0],a['terminal_states_f64'][0],
        a['q_mid_f32'],a['q_terminal_f32'],a['y_f64'],a['sigma_f64'])
    terminal_seed=cot['cotangent_terminal_f64'].astype('<f4')
    need(a['lambda_in_f32'][0].tobytes()==terminal_seed.tobytes(),'first terminal Fortran seed is not Q32(alphaN*qN)')
    for i in range(1,8):
        if i==4:
            expected=core.inject_midpoint_f32(a['lambda_out_f32'][3],cot['cotangent_mid_f64'])
        else:
            expected=a['lambda_out_f32'][i-1]
        need(a['lambda_in_f32'][i].tobytes()==expected.tobytes(),f'FP32 handoff/injection edge {i} mismatch')
    for i in range(8):
        need(a['lambda_out_f32'][i].tobytes()==a['local_precast_f64'][i].astype('<f4').tobytes(),
             f'Fortran FP32 return/local FP64 cast mismatch at prefix {ENDS[i]}')

def check_manifest(path:Path,npz:Path):
    m=json.loads(path.read_text())
    need(m.get('schema')=='native_residual_objective_portable_packet_v1' and m.get('protocol_sha256')==P,
         'portable manifest schema/protocol mismatch')
    need(m.get('helper_sha256')==CORE_SHA and sha(Path(__file__).with_name('recompute.py'))==CORE_SHA,
         'authoritative objective helper SHA mismatch')
    need(m.get('npz_sha256')==sha(npz) and m.get('npz_bytes')==npz.stat().st_size,
         'portable NPZ file hash/size mismatch')
    need(len(m.get('executable_sha256',''))==64 and len(m.get('source_files',[]))==10 and
         len(m.get('receipt_pins',[]))==8 and m.get('old_provenance'),
         'manifest lacks complete external receipt/source/executable/old provenance pins')
    need(all(len(s.get('sha256',''))==64 and s.get('path') for s in m['source_files']) and
         all(len(r.get('receipt_sha256',''))==64 for r in m['receipt_pins']),'manifest provenance hashes malformed')
    need([x.get('label') for x in m['receipt_pins']]==list(LABELS) and
         all(len(x.get('receipt_sha256',''))==64 for x in m['receipt_pins']),
         'manifest receipt pin labels/hash format mismatch')
    return m

def _array_schema(a):
    return {k:{'shape':list(v.shape),'dtype':v.dtype.str,
        'sha256':hashlib.sha256(np.ascontiguousarray(v).tobytes()).hexdigest()} for k,v in a.items()}

def self_test():
    n=N; qm=np.zeros(n,dtype='<f4');qn=np.zeros(n,dtype='<f4');v=np.ones(n,dtype='<f4')
    qm[0]=2;qn[1]=3;zm=np.zeros((6,n),dtype='<f8');zn=np.zeros_like(zm)
    zm[0,0]=zm[1,0]=1.5;zn[0,1]=zn[1,1]=2.
    y=np.array([0.,0.],dtype='<f8');sigma=np.ones(2,dtype='<f8')
    cot=core.objective_pair(zm[0],zn[0],qm,qn,y,sigma)
    ins=np.zeros((8,n),dtype='<f4');outs=np.zeros_like(ins)
    ins[0]=cot['cotangent_terminal_f64'].astype('<f4')
    for i in range(4):outs[i]=np.float32(i+1);ins[i+1]=outs[i]
    ins[4]=core.inject_midpoint_f32(outs[3],cot['cotangent_mid_f64'])
    for i in range(4,8):outs[i]=np.float32(i+1)
    for i in range(5,8):ins[i]=outs[i-1]
    arrays={'case_labels':np.asarray(CASES,dtype='<U10'),'eta_actual_f32':np.array([0,0,.01,-.01,.03,-.03],dtype='<f4'),
      'mid_states_f64':zm,'terminal_states_f64':zn,'q_mid_f32':qm,'q_terminal_f32':qn,'direction_f32':v,
      'y_f64':y,'sigma_f64':sigma,'old_terminal_g0_f32':np.ones(n,dtype='<f4'),
      'old_two_time_g0_f32':np.ones(n,dtype='<f4'),'prefix_ends_i32':np.asarray(ENDS,dtype='<i4'),
      'lambda_in_f32':ins,'lambda_out_f32':outs,'local_precast_f64':outs.astype('<f8')}
    manifest={'arrays':_array_schema(arrays),'new_prefix_provenance':{'new_prefixes':[{'label':f'residual_prefix_{e:03d}'} for e in ENDS]}}
    check_arrays(arrays,manifest)
    bad={k:v.copy() for k,v in arrays.items()};bad['lambda_in_f32'][4]=bad['lambda_out_f32'][3]
    rejected=False
    try:check_arrays(bad,{'arrays':_array_schema(bad)})
    except ValueError:rejected=True
    need(rejected,'synthetic omitted midpoint injection was not rejected')
    return {'synthetic_valid_packet_pass':True,'synthetic_missing_midpoint_injection_rejected':True}

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--self-test',action='store_true')
    ap.add_argument('--npz',type=Path);ap.add_argument('--manifest',type=Path);ap.add_argument('--output',type=Path)
    x=ap.parse_args()
    if x.self_test:
        print(json.dumps({'schema':'native_residual_packet_synthetic_checks_v1','checks':self_test()},indent=2));return
    need(x.npz is not None and x.manifest is not None and x.output is not None,'--npz --manifest --output required')
    npz=x.npz.resolve();manifest=check_manifest(x.manifest.resolve(),npz)
    with np.load(npz,allow_pickle=False) as z:a={k:np.asarray(z[k]) for k in z.files}
    check_arrays(a,manifest)
    r=core.recompute_components(a['mid_states_f64'],a['terminal_states_f64'],a['q_mid_f32'],a['q_terminal_f32'],
        a['y_f64'],a['sigma_f64'],a['eta_actual_f32'],a['direction_f32'],a['lambda_out_f32'][-1],
        a['old_terminal_g0_f32'],a['old_two_time_g0_f32'])
    casts=core.summarize_local_casts(a['lambda_out_f32'],a['local_precast_f64'])
    costs=r['fd_full_quadratic_cost'];mid=r['fd_midpoint_quadratic_cost'];term=r['fd_terminal_residual_guard'];h=r['old_linear_h_one_sided_checks']
    checks={'full_cost':costs['frozen_cost_fd_ad_plateau_signal_pass'],
      'midpoint_cost':mid['frozen_cost_fd_ad_plateau_signal_pass'],
      'terminal_residual_guard':term['frozen_cost_fd_ad_plateau_signal_pass'],
      'underlying_linear_h_midpoint':h['midpoint_h']['frozen_linear_h_one_sided_check_pass'],
      'underlying_linear_h_terminal':h['terminal_h']['frozen_linear_h_one_sided_check_pass'],
      'eight_window_FP32_chain':True,'eight_local_casts':all(c['byte_cast_equal'] for c in casts),
      'baseline_replay_states_equal':True,'frozen_injection_and_handoffs_equal':True}
    report={'schema':'native_residual_objective_portable_check_v1','protocol_sha256':P,
      'manifest_sha256':sha(x.manifest.resolve()),'npz_sha256':sha(npz),
      'external_provenance':{'receipt_pins':manifest['receipt_pins'],'source_files':manifest['source_files'],
        'executable_sha256':manifest['executable_sha256'],'old_provenance':manifest['old_provenance']},
      'external_evidence_scope':'Receipt/source/executable pins are reported from the manifest; this portable checker does not reparse raw checkpoint ledgers or receipts. The raw collector owns that verification.',
      'component_checks':checks,'overall_pass':bool(all(checks.values())),
      'recomputed':r,'per_window_local_casts':casts}
    x.output.parent.mkdir(parents=True,exist_ok=True);x.output.write_text(json.dumps(safe(report),indent=2,sort_keys=True,allow_nan=False)+'\n')

if __name__=='__main__':main()
