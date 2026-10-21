#!/usr/bin/env python3
"""Portable raw-binary NumPy recompute for the frozen two-time adjoint.

`--self-test` uses synthetic arrays only. After all four new prefixes complete,
pass --evidence-root E --old-root O to verify raw receipts, package the NPZ and
recompute full/midpoint FD and Fortran-FP32-chain directional values.
"""
import argparse, hashlib, json, math
import re
from pathlib import Path
import numpy as np

N=24531; ENDS=(128,96,64,32)
CASES=('baseline','replay','plus_0p01','minus_0p01','plus_0p03','minus_0p03')
P='c34274526f96434f9c7581d1d41ca701952887f29680431111c733199b00d3bb'
P5='b96cbe382b1e992c64e21b2ef3f413cf3aa7ba6b7bfaa31e0a43b91390c9ce45'
P6='9971cb56d28022951dfe00c1b7707532cf53c9cc71ca88983c738812751e2650'
OLD_BIND5='1279e2d15fc068cc8fd71fef8c81508c6ce35a0f56fba46b3d130c040cf9061b'
OLD_BIND6='714b5e500da2e0bd40e49e77498b99e503ed61be39700d0103f02562dbfb7439'
BINDING='c217c5a9012a8254e9b6c7ed305d9b1a9c1779561892f9bb059a943c96cafdbd'
RUNNER='b11948b406c323616c0f00179533dbb8c889ba07ba5f9ac2c4ef8f844eaaa9d8'
EXE='71927be0736ecd7c8e51b4a22177122af1a38b924e345836d1d6590470514249'
Q='dcd695ef7c6fa2fefabefc7bf12d57948891917bee983dc17ed47aa96f959bf8'
OLD_Q='c7acf9288f6fc2b563b62bb38aec52a31ac720c39ca7924a3c2cff93ec66ea90'
OLD_V='ee4699af7d105308e70355befa268c6916d39ef4dcb345c80f6b6f54334c8347'
OLD_G0='9c5477d4629073e946c6465a1f2686d8e76378e19c121b7c0d32e786838e96c0'
OLD_L128='d15976e5b36601236ee4832e3ca65dcbcf404614013343e24611210c914aa532'
MID_SHA='40e92b2b9702c34d1e51a23dc27b2dab8505196753808e04fecc65ccdfbce756'
OLD_FD_PRE_AD='8638a1fe4c0d73bdcbed7964682f8498c9d09a46b0b32c223ac62ae3b8491f90'
OLD_RETAINED_NPZ='af502c902b6aee3b80cc6b54f330818e20af7d245f9b3af3b47e86118192b749'
OLD_PACKET_MANIFEST='4f47146ae7fd3dcfaa282d92d209eceb7572bf67729cfa2001ddff7689a5cff4'
H=.9375; CAP=18*1024**3; EPS=np.finfo(np.float64).eps
OLD_DIR='/Users/yhlee/AD-SDIRK3-review-evidence/native-terminal-adjoint-20261020'
E_DEFAULT='/Users/yhlee/AD-SDIRK3-review-evidence/native-two-time-adjoint-20261021'


def need(ok,msg):
    if not bool(ok): raise ValueError(msg)
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''): h.update(b)
    return h.hexdigest()
def sha_bytes(b): return hashlib.sha256(b).hexdigest()
def rb(p,dtype,count):
    need(Path(p).is_file(),f'missing raw binary {p}')
    x=np.fromfile(p,dtype=dtype); need(x.size==count,f'{p}: expected {count}, got {x.size}')
    return x
def fstate(path,k):
    with Path(path).open('rb') as f: f.seek(k*N*8); b=f.read(N*8)
    need(len(b)==N*8,f'{path}: missing checkpoint {k}')
    return np.frombuffer(b,dtype='<f8').copy()
def pref_equal(actual,reference,n):
    size=(n+1)*N*8
    if not Path(actual).is_file() or Path(actual).stat().st_size!=size or Path(reference).stat().st_size!=257*N*8:return False
    with open(actual,'rb') as a,open(reference,'rb') as b:
        while size:
            m=min(size,1<<20); x=a.read(m); y=b.read(m)
            if len(x)!=m or x!=y:return False
            size-=m
    return True
def finite(x,name): need(np.isfinite(x).all(),f'{name}: nonfinite')
def vecstats(x):
    x=np.asarray(x,dtype=np.float64)
    return {'max_abs':float(np.max(np.abs(x))),'rms':float(np.sqrt(np.mean(x*x))),
            'l2':float(np.linalg.norm(x)),'different_elements':int(np.count_nonzero(x))}
def times(n):
    seconds=n*15//16
    return [f'0001-01-01_00:{s//60:02d}:{s%60:02d}' for s in range(0,seconds+1,15)]


def frozen(e,old):
    p=e/'protocol.json'; b=e/'launch_binding.json'
    need(sha(p)==P and sha(b)==BINDING,'frozen protocol/binding SHA mismatch')
    prot=json.loads(p.read_text()); bind=json.loads(b.read_text())
    need(prot.get('schema')=='native_two_time_adjoint_protocol_v1' and bind.get('protocol')=={'path':str(p),'sha256':P},'binding schema/path mismatch')
    need(prot.get('source_files')==bind.get('source_files') and bind.get('runner',{}).get('sha256')==RUNNER and
         sha(bind['runner']['path'])==RUNNER and bind.get('executable',{}).get('sha256')==EXE and
         prot.get('executable')==bind.get('executable') and sha(bind['executable']['path'])==EXE,'source/runner/executable pin mismatch')
    for s in bind['source_files']: need(sha(s['path'])==s['sha256'],f"source pin mismatch {s['path']}")
    t=prot['target']; need(t['N']==N and t['h_seconds']==H and t['midpoint_step']==128 and t['prefix_ends']==list(ENDS),'protocol target mismatch')
    need(sha(old/'protocol_v5.json')==P5 and sha(old/'protocol_v6.json')==P6 and
         sha(old/'launch_binding_v5.json')==OLD_BIND5 and sha(old/'launch_binding_v6.json')==OLD_BIND6,
         'old protocol/launch binding SHA mismatch')
    need(Path(prot['base_case']).resolve()==old/'runs_v5'/'baseline','old baseline path mismatch')
    need(sha(prot['fixed_midpoint_objective']['path'])==MID_SHA and sha(prot['q128']['path'])==Q,'frozen midpoint objective/q mismatch')
    return prot,bind


def old_vectors(e,old,prot,bind):
    pre=json.loads((e/'fd_pre_ad.json').read_text()); npzp=e/'retained_states.npz'
    oldmanifest=Path(__file__).resolve().parents[1]/'native_terminal_adjoint'/'manifest.json'
    need(sha(e/'fd_pre_ad.json')==OLD_FD_PRE_AD and pre.get('protocol_sha256')==P and
         sha(npzp)==OLD_RETAINED_NPZ==pre['retained_states_npz_sha256'] and
         sha(oldmanifest)==OLD_PACKET_MANIFEST,'old retained state/pre-AD/portable manifest pin mismatch')
    with np.load(npzp,allow_pickle=False) as z:
        need(tuple(z['case_order'].tolist())==CASES,'old six-case order mismatch')
        mid=np.asarray(z['mid_states_f64']); term=np.asarray(z['terminal_states_f64']); eta=np.asarray(z['eta_actual_f32'])
        qm=np.asarray(z['q128_f32']); qn=np.asarray(z['qN_f32']); v=np.asarray(z['direction_f32']); g0=np.asarray(z['terminal_only_g0_f32'])
    need(mid.shape==(6,N) and mid.dtype==np.float64 and term.shape==(6,N) and term.dtype==np.float64 and eta.shape==(6,) and eta.dtype==np.float32,'old state schema mismatch')
    for name,x in [('qmid',qm),('qN',qn),('v',v),('oldg0',g0)]: need(x.shape==(N,) and x.dtype==np.float32,f'{name} old vector schema mismatch'); finite(x,name)
    finite(mid,'old midpoint states'); finite(term,'old terminal states')
    need([x['case'] for x in pre['raw_case_pins']]==list(CASES),'old six-case receipt pin order mismatch')
    for path,digest in [(prot['old_terminal_q']['path'],OLD_Q),(prot['old_direction']['path'],OLD_V),
                        (prot['old_g0']['path'],OLD_G0),(prot['old_lambda128']['path'],OLD_L128)]: need(sha(path)==digest,'old vector pin mismatch '+path)
    qnraw=rb(prot['old_terminal_q']['path'],'<f4',N); vraw=rb(prot['old_direction']['path'],'<f4',N)
    need(qnraw.tobytes()==qn.astype('<f4').tobytes() and vraw.tobytes()==v.astype('<f4').tobytes(),'old qN/v not same as retained vectors')
    l128p=Path(prot['old_lambda128']['path']); l128=rb(l128p,'<f4',N)
    lrec=json.loads((l128p.parent/'run_receipt.json').read_text()); lmeta=json.loads((l128p.parent/'terminal.result.json').read_text())
    need(lrec.get('label')=='baseline_prefix_160' and lrec.get('status')=='completed' and lrec.get('protocol_sha256')==P6 and
         lrec.get('expected_steps')==160 and lrec.get('expected_implicit_stages')==480 and
         lrec['actual']['output_hashes'][l128p.name]['sha256']==OLD_L128 and
         lmeta.get('gradient_control_step')==128,'old lambda128 provenance mismatch')
    g0p=Path(prot['old_g0']['path']); g0raw=rb(g0p,'<f4',N)
    grec=json.loads((g0p.parent/'run_receipt.json').read_text()); gmeta=json.loads((g0p.parent/'terminal.result.json').read_text())
    need(grec.get('label')=='baseline_prefix_032' and grec.get('status')=='completed' and grec.get('protocol_sha256')==P6 and
         grec.get('expected_steps')==32 and grec.get('expected_implicit_stages')==96 and
         grec['actual']['output_hashes'][g0p.name]['sha256']==OLD_G0 and gmeta.get('gradient_control_step')==0,
         'old terminal g0 provenance mismatch')
    q=rb(prot['q128']['path'],'<f4',N); ip=Path(prot['injection_receipt']['path']); inj=json.loads(ip.read_text())
    need(sha(ip)==prot['injection_receipt']['sha256'] and q.tobytes()==qm.astype('<f4').tobytes() and
         inj.get('old_source_receipt',{}).get('sha256')==sha(l128p.parent/'run_receipt.json') and
         Path(inj.get('old_source_receipt',{}).get('path','')).resolve()==(l128p.parent/'run_receipt.json').resolve() and
         Path(inj.get('old_terminal_lambda128',{}).get('path','')).resolve()==l128p.resolve() and
         inj.get('old_terminal_lambda128',{}).get('sha256')==sha(l128p) and
         Path(inj.get('q128',{}).get('path','')).resolve()==Path(prot['q128']['path']).resolve() and
         inj.get('q128',{}).get('sha256')==sha(prot['q128']['path']), 'injection receipt/q provenance mismatch')
    injected=rb(prot['first_lambda128']['path'],'<f4',N)
    expected=(l128.astype(np.float64)+q.astype(np.float64)).astype('<f4')
    need(injected.tobytes()==expected.tobytes() and sha(prot['first_lambda128']['path'])==prot['first_lambda128']['sha256'] and
         Path(inj.get('new_lambda128',{}).get('path','')).resolve()==Path(prot['first_lambda128']['path']).resolve() and
         inj.get('new_lambda128',{}).get('sha256')==sha(prot['first_lambda128']['path']) and
         inj.get('accepted_step')==128 and inj.get('accepted_time_s')==120.,'frozen single q injection arithmetic mismatch')
    need(g0raw.tobytes()==g0.astype('<f4').tobytes(),'old g0 differs from retained state archive')
    arr={'case_labels':np.asarray(CASES,dtype='<U10'),'old_eta_f32':eta.astype('<f4'),
         'old_mid_states_f64':mid.astype('<f8'),'old_terminal_states_f64':term.astype('<f8'),
         'q_mid_f32':q.astype('<f4'),'q_terminal_f32':qn.astype('<f4'),'direction_v_f32':v.astype('<f4'),
         'old_g0_terminal_f32':g0raw.astype('<f4'),'old_lambda128_terminal_f32':l128.astype('<f4')}
    provenance={'old_retained_states_sha256':sha(npzp),'old_fd_pre_ad_sha256':sha(e/'fd_pre_ad.json'),
        'reused_old_portable_manifest':{'path':str(oldmanifest),'sha256':sha(oldmanifest)},
        'old_six_case_raw_receipt_and_state_pins':pre['raw_case_pins'],
        'old_lambda128_source':{'path':str(l128p),'sha256':sha(l128p),'gradient_control_step':128,
          'source_receipt_sha256':sha(l128p.parent/'run_receipt.json')},
        'old_g0_source':{'path':str(g0p),'sha256':sha(g0p),'gradient_control_step':0},
        'frozen_injection':{'path':str(prot['first_lambda128']['path']),'sha256':sha(prot['first_lambda128']['path']),
         'delta_minus_q_stats':vecstats(injected.astype(np.float64)-l128.astype(np.float64)-q.astype(np.float64))}}
    return arr,provenance,pre,injected


def new_prefixes(e,prot,bind,old,basecp,injected):
    ins=[]; outs=[]; locals64=[]; recs=[]; prev=None
    source=Path(prot['source_case'])
    original_nml=(source/'namelist.input').read_text()
    need(original_nml.count(' &dynamics\n')==1 and 'sdirk3_retain_graph_for_adjoint' not in original_nml and
         'sdirk3_imex_slow_in_tangent' not in original_nml,'registered source namelist template changed')
    for n in ENDS:
        label=f'two_time_prefix_{n:03d}'; d=e/'runs'/label; rp=d/'run_receipt.json'
        need(rp.is_file(),label+': completed receipt missing')
        r=json.loads(rp.read_text()); a=r['actual']; pfx=d/'terminal'
        need(r.get('status')=='completed' and r.get('label')==label and r.get('primary_label')=='two_time' and
             r.get('prefix_reexecution') is True and r.get('protocol_sha256')==P and
             r.get('expected_steps')==n and r.get('expected_implicit_stages')==3*n and r.get('expected_h_seconds')==H and
             r.get('executable')==bind['executable'] and r.get('source_files')==bind['source_files'] and
             r.get('runner_sha256')==RUNNER and r.get('launch_binding_sha256')==BINDING and
             r.get('input_sha256')==prot['input_pins']['wrfinput_d01'],label+': receipt/source/step gate')
        need(a.get('return_code')==0 and a.get('runner_stop_reason') is None and a.get('success_marker') is True and
             a.get('step_telemetry_count')==n and a.get('newton_convergence_record_count')==3*n and
             a.get('stage_csv_count')==0 and a.get('native_rss_monitor_has_samples') is True and
             a.get('peak_sampled_native_rss_bytes',CAP+1)<=CAP and
             a.get('netcdf_times')==times(n) and a.get('all_eight_fields_finite') is True and
             all(float(x)>0 for x in a.get('physical_minima',{}).values()),label+': process/history/admissibility gate')
        if 'newton_failure_record_count' in a: need(a['newton_failure_record_count']==0,label+': Newton failure')
        for name,pin in a.get('output_hashes',{}).items():
            p=d/name; need(p.is_file() and p.stat().st_size==pin['bytes'] and sha(p)==pin['sha256'],label+': output hash '+name)
        nml=original_nml.replace(' &dynamics\n',' &dynamics\n sdirk3_retain_graph_for_adjoint = .true.,\n sdirk3_imex_slow_in_tangent = .true.,\n',1)
        nml,count=re.subn(r'(run_seconds\s*=\s*)240(\s*,)',lambda m:m[1]+str(n*15//16)+m[2],nml)
        need(count==1 and (d/'namelist.input').read_text()==nml and sha(d/'namelist.input')==r.get('namelist_sha256') and
             sha(d/'wrfinput_d01')==prot['input_pins']['wrfinput_d01'] and
             sha(d/'diagnostics.txt')==prot['input_pins']['diagnostics.txt'],label+': exact input/namelist settings mismatch')
        ref=Path(prot['base_case'])/'terminal.checkpoints_f64.bin'; cp=Path(str(pfx)+'.checkpoints_f64.bin')
        incoming=Path(prot['first_lambda128']['path']) if n==128 else e/'runs'/f'two_time_prefix_{n+32:03d}'/'terminal.lambda_out_f32.bin'
        source_receipt=Path(prot['injection_receipt']['path']) if n==128 else e/'runs'/f'two_time_prefix_{n+32:03d}'/'run_receipt.json'
        bound={str(Path(x['path']).resolve()):x for x in r.get('bound_incoming_files',[])}
        for required in (ref.resolve(),incoming.resolve()):
            entry=bound.get(str(required)); need(entry is not None and entry['sha256']==sha(required) and
                entry['bytes']==required.stat().st_size,label+': incoming reference/lambda is not receipt-bound')
        need(Path(r.get('incoming_lambda_source','')).resolve()==source_receipt.resolve(),label+': wrong incoming lambda receipt provenance')
        need(pref_equal(cp,basecp,n),label+': prefix states differ from old baseline')
        warm=n-32; ep=json.loads(Path(str(pfx)+'.endpoint.json').read_text())
        prep=json.loads(Path(str(pfx)+'.prepare.json').read_text()); result=json.loads(Path(str(pfx)+'.result.json').read_text())
        win=json.loads(Path(str(pfx)+f'.window_{n}.json').read_text())
        need(prep.get('mode')=='prefix' and prep.get('expected_steps')==n and prep.get('chain')=='baseline' and prep.get('eta')==0 and
             ep.get('expected_steps')==n and ep.get('warm_steps')==warm and ep.get('graph_start_step')==warm+1 and
             ep.get('control_input64_calls')==n and ep.get('initial_control_seed_applied') is (warm>0) and
             ep.get('warm_endpoint_capture_count')==warm and ep.get('reference_path')==str(ref) and
             ep.get('reference_checkpoints_expected')==n+1 and ep.get('reference_checkpoints_matched')==n+1 and
             ep.get('endpoint_reference_exact') is True and ep.get('objective') is None and
             ep.get('objective_is_registered_terminal') is False and result.get('mode')=='prefix' and
             result.get('chain')=='baseline' and result.get('eta')==0 and result.get('expected_steps')==n and
             result.get('gradient_control_step')==n-32 and
             result.get('gradient_scope')=='local_32_step_window' and result.get('pullback_checked_by_fortran') is True and
             result.get('wrapper_gradient_f32_equals_cast_f64') is True and win.get('window_endpoint_count')==32 and
             win.get('expected_steps')==n and win.get('window_start')==warm and win.get('window_end')==n and
             win.get('reference_checkpoints_expected')==n+1 and win.get('reference_checkpoints_matched')==n+1 and
             win.get('endpoint_reference_exact') is True,
             label+': endpoint/VJP/reference metadata')
        suffix=Path(str(pfx)+'.suffix_seed_f64.bin')
        if warm==0 and not suffix.exists(): seed=fstate(cp,0); seed_source='checkpoint0_initial_seed_no_warm_rearm'
        else: seed=rb(suffix,'<f8',N); seed_source='saved_suffix_seed'
        need(seed.tobytes()==fstate(cp,warm).tobytes(),label+': suffix/initial checkpoint seed mismatch')
        need(pref_equal(cp,basecp,n),label+': final prefix byte comparison')
        lin=rb(Path(str(pfx)+'.lambda_in_f32.bin'),'<f4',N); lout=rb(Path(str(pfx)+'.lambda_out_f32.bin'),'<f4',N)
        l64=rb(Path(str(pfx)+'.lambda_in_f64.bin'),'<f8',N); g64=rb(Path(str(pfx)+'.wrapper_gradient_f64.bin'),'<f8',N)
        g32=rb(Path(str(pfx)+'.gradient_f32.bin'),'<f4',N); gf64=rb(Path(str(pfx)+'.gradient_f64.bin'),'<f8',N)
        need(lin.tobytes()==(injected if n==128 else prev).tobytes() and l64.tobytes()==lin.astype('<f8').tobytes(),
             label+': exact incoming cotangent chain')
        need(g32.tobytes()==lout.tobytes() and gf64.tobytes()==g64.astype('<f8').tobytes() and
             lout.tobytes()==g64.astype('<f4').tobytes(),label+': Fortran FP32 return not byte cast of FP64 capture')
        need(result.get('lambda_in_f64_semantics')=='actual_fp32_cotangent_promoted_for_audit' and
             result.get('gradient_f64_semantics')=='local_precast_fp64',label+': precision semantic metadata')
        if n>32: need(result.get('directional_local_precast_f64') is None and result.get('directional_vjp_f32_chain') is None,
                       label+': partial-window scalar mislabeled as g0')
        ins.append(lin); outs.append(lout); locals64.append(g64); prev=lout
        recs.append({'label':label,'steps':n,'stage_records':int(a['newton_convergence_record_count']),
          'peak_rss_bytes':int(a['peak_sampled_native_rss_bytes']),'receipt_sha256':sha(rp),
          'checkpoint_sha256':sha(cp),'prefix_endpoint_count_byte_equal':n+1,
          'lambda_in_sha256':sha(Path(str(pfx)+'.lambda_in_f32.bin')),
          'lambda_out_sha256':sha(Path(str(pfx)+'.lambda_out_f32.bin')),
          'local_precast_sha256':sha(Path(str(pfx)+'.wrapper_gradient_f64.bin')),
          'suffix_seed_audit_source':seed_source})
    arr={'new_prefix_ends_i32':np.asarray(ENDS,dtype='<i4'),'new_lambda_in_f32':np.stack(ins).astype('<f4'),
         'new_lambda_out_f32':np.stack(outs).astype('<f4'),'new_local_precast_f64':np.stack(locals64).astype('<f8')}
    return arr,{'new_prefixes':recs}


def compare(fd,ad):
    den=max(abs(fd),abs(ad)); return {'fd':float(fd),'ad':float(ad),'relative_shared_signal':float(abs(fd-ad)/den) if den else None}
def fd_component(js,eta,ad,base_norm):
    out={}
    for tag,ip,im in [('0p01',2,3),('0p03',4,5)]:
        ep,em=float(eta[ip]),float(eta[im]); need(ep>0 and em<0 and ep==-em,tag+': eta pair asymmetric')
        jp,jm,j0=float(js[ip]),float(js[im]),float(js[0]); c=(jp-jm)/(ep-em)
        floor=100*EPS*max(abs(jp),abs(jm),float(base_norm))
        sp=(jp-j0)/ep; sm=(j0-jm)/(-em); sd=max(abs(sp),abs(sm)); gap=abs(sp-sm)/sd if sd else None
        out[tag]={'eta_plus_actual':ep,'eta_minus_actual':em,'J_plus':jp,'J_minus':jm,'J0':j0,
          'central_fd':c,**compare(c,ad),'one_sided_plus':sp,'one_sided_minus':sm,'one_sided_relative_gap':gap,
          'signal':abs(jp-jm),'signal_floor':float(floor),'signal_above_floor':bool(abs(jp-jm)>floor)}
    f1,f3=out['0p01']['central_fd'],out['0p03']['central_fd']; den=max(abs(f1),abs(f3),abs(ad))
    plateau=abs(f1-f3)/den if den else None
    relpass=all(out[w]['relative_shared_signal'] is not None and out[w]['relative_shared_signal']<=.01 for w in ('0p01','0p03'))
    out['central_width_plateau_relative']=plateau
    out['central_widths_pass_1_percent']=plateau is not None and plateau<=.01
    out['both_widths_fd_ad_pass_1_percent']=relpass
    out['both_signals_above_floor']=all(out[w]['signal_above_floor'] for w in ('0p01','0p03'))
    out['one_sided_gap_over_1_percent_both_widths']=all(out[w]['one_sided_relative_gap'] is not None and
        out[w]['one_sided_relative_gap']>.01 for w in ('0p01','0p03'))
    out['finite_width_unresolved_by_one_sided']=out['one_sided_gap_over_1_percent_both_widths']
    out['frozen_component_checks_pass']=out['central_widths_pass_1_percent'] and relpass and out['both_signals_above_floor'] and not out['finite_width_unresolved_by_one_sided']
    return out

def taylor(js,eta,ad):
    residual={CASES[i]:float(js[i]-js[0]-ad*float(eta[i])) for i in range(1,6)}; orders={}
    for side in ('plus','minus'):
        a=abs(residual[f'{side}_0p01']); b=abs(residual[f'{side}_0p03'])
        orders[side]={'abs_residual_0p01':a,'abs_residual_0p03':b,
            'empirical_order_diagnostic':float(math.log(b/a)/math.log(3)) if a>0 and b>0 else None}
    return {'scope':'descriptive only; not an acceptance gate','first_order_residuals':residual,
        'same_sign_order_diagnostics':orders,'note':'Midpoint increment may be roundoff-limited after FP32 chain/injection; no fitting or objective change.'}

def validate_portable_arrays(a,manifest):
    spec={'case_labels':((6,),'<U10'),'old_eta_f32':((6,),'<f4'),
      'old_mid_states_f64':((6,N),'<f8'),'old_terminal_states_f64':((6,N),'<f8'),
      'q_mid_f32':((N,),'<f4'),'q_terminal_f32':((N,),'<f4'),'direction_v_f32':((N,),'<f4'),
      'old_g0_terminal_f32':((N,),'<f4'),'old_lambda128_terminal_f32':((N,),'<f4'),
      'new_prefix_ends_i32':((4,),'<i4'),'new_lambda_in_f32':((4,N),'<f4'),
      'new_lambda_out_f32':((4,N),'<f4'),'new_local_precast_f64':((4,N),'<f8')}
    need(set(a)==set(spec),'portable NPZ array key set mismatch')
    for name,(shape,dtype) in spec.items():
        need(a[name].shape==shape and a[name].dtype==np.dtype(dtype),f'{name}: portable shape/dtype mismatch')
        if name!='case_labels': finite(a[name],name)
    need(tuple(a['case_labels'].tolist())==CASES,'portable case order mismatch')
    need(a['new_prefix_ends_i32'].tolist()==list(ENDS),'portable prefix-end order mismatch')
    expected=(a['old_lambda128_terminal_f32'].astype(np.float64)+a['q_mid_f32'].astype(np.float64)).astype('<f4')
    need(expected.tobytes()==a['new_lambda_in_f32'][0].astype('<f4').tobytes(),
         'portable first midpoint injection is not exact Q32(old lambda128 + q128)')
    for i in range(1,4):
        need(a['new_lambda_in_f32'][i].tobytes()==a['new_lambda_out_f32'][i-1].tobytes(),
             f'portable FP32 cotangent chain edge {i} mismatch')
    for i in range(4):
        need(a['new_lambda_out_f32'][i].tobytes()==a['new_local_precast_f64'][i].astype('<f4').tobytes(),
             f'portable Fortran output cast mismatch at prefix {ENDS[i]}')
    rows=manifest.get('new_prefix_provenance',{}).get('new_prefixes',[])
    need([x.get('steps') for x in rows]==list(ENDS) and
         [x.get('prefix_endpoint_count_byte_equal') for x in rows]==[n+1 for n in ENDS],
         'portable manifest prefix horizons/counts mismatch')

def analyze(a,prov):
    validate_portable_arrays(a,prov)
    qmid=a['q_mid_f32'].astype(np.float64); qn=a['q_terminal_f32'].astype(np.float64)
    v=a['direction_v_f32'].astype(np.float64); eta=a['old_eta_f32'].astype(np.float32).astype(np.float64)
    mids=a['old_mid_states_f64'].astype(np.float64); terms=a['old_terminal_states_f64'].astype(np.float64)
    jm=np.asarray([np.dot(qmid,x) for x in mids]); jt=np.asarray([np.dot(qn,x) for x in terms]); j2=jm+jt
    oldg=a['old_g0_terminal_f32'].astype(np.float64); newg=a['new_lambda_out_f32'][-1].astype(np.float64)
    adold=float(np.dot(v,oldg)); adfull=float(np.dot(v,newg)); admid=float(np.dot(v,newg-oldg))
    full_scale=float(np.linalg.norm(qn)*np.linalg.norm(terms[0])+np.linalg.norm(qmid)*np.linalg.norm(mids[0]))
    mid_scale=float(np.linalg.norm(qmid)*np.linalg.norm(mids[0]))
    old_scale=float(np.linalg.norm(qn)*np.linalg.norm(terms[0]))
    delta=a['new_lambda_in_f32'][0].astype(np.float64)-a['old_lambda128_terminal_f32'].astype(np.float64)-qmid
    casts=[]
    for i,n in enumerate(ENDS):
        d=a['new_lambda_out_f32'][i].astype(np.float64)-a['new_local_precast_f64'][i]
        casts.append({'prefix_end':n,'window_control_step':n-32,'local_cast':vecstats(d),
          'f32_matches_precast_cast_bytewise':a['new_lambda_out_f32'][i].tobytes()==a['new_local_precast_f64'][i].astype('<f4').tobytes()})
    return {'schema':'native_two_time_raw_recomputed_v1','protocol_sha256':P,
      'source_manifest_sha256':prov.get('manifest_sha256'),'case_order':list(CASES),'eta_actual_fp32_promoted_f64':eta.tolist(),
      'objectives':{'midpoint':jm.tolist(),'terminal':jt.tolist(),'two_time_sum':j2.tolist()},
      'directional_derivatives':{'old_terminal_only_dAD':adold,'new_full_two_time_dAD':adfull,'midpoint_increment_dAD':admid,
        'midpoint_formula':'dot(v, FP64(new_g0_f32)-FP64(old_terminal_g0_f32)); vector subtraction after promotion',
        'full_formula':'dot(FP64(v_f32),FP64(new_g0_f32))',
        'full_minus_old_minus_mid_roundoff_diagnostic':float(adfull-adold-admid)},
      'finite_difference':{'two_time_full':fd_component(j2,eta,adfull,full_scale),
        'midpoint_increment':fd_component(jm,eta,admid,mid_scale),'old_terminal_only_diagnostic':fd_component(jt,eta,adold,old_scale)},
      'taylor_diagnostics':{'old_terminal_only':taylor(jt,eta,adold),'two_time_full':taylor(j2,eta,adfull),
        'midpoint_increment':taylor(jm,eta,admid)},
      'midpoint_injection_roundoff':{'meaning':'actual delta128 - old lambda128 - frozen q128; diagnostic only, q128 is not redefined',**vecstats(delta)},
      'per_window_local_precast_casts':casts,
      'claim_scope':{'actual_fortran_fp32_chain_only':True,'global_exact_fp64_g0_certified':False,
        'partial_prefix_dots_used_as_global_dAD':False,'second_full_gradient_replay_certified':False,
        'general_all_direction_differentiability_claim':False,'finite_width_checks_are_not_classicality_proof':True}}


def schema(arr):
    return {k:{'shape':list(v.shape),'dtype':v.dtype.str,'sha256':hashlib.sha256(np.ascontiguousarray(v).tobytes()).hexdigest()} for k,v in arr.items()}
def self_test():
    # Positive two-component finite-difference construction.
    v=np.array([1.,-2.,.5]);qm=np.array([.2,.3,-.1]);qn=np.array([-.4,.1,.7])
    eta=np.array([0,0,.01,-.01,.03,-.03],dtype=np.float32).astype(np.float64)
    gm=.25*v;gt=-2*v; dmid=float(v@gm);dterm=float(v@gt)
    sm=(dmid/(qm@qm))*qm;st=(dterm/(qn@qn))*qn
    m0=np.array([2.,3.,4.]);t0=np.array([5.,6.,7.])
    jm=np.array([qm@(m0+x*sm) for x in eta]);jt=np.array([qn@(t0+x*st) for x in eta]);
    posm=fd_component(jm,eta,dmid,0.);posf=fd_component(jm+jt,eta,dmid+dterm,0.)
    need(posm['frozen_component_checks_pass'] and posf['frozen_component_checks_pass'],'synthetic positive FD test failed')
    old=np.array([1.,-3.,5.],dtype=np.float32); q=np.array([.125,.5,-.25],dtype=np.float32)
    correct=(old.astype(np.float64)+q.astype(np.float64)).astype('<f4'); omitted=old.astype('<f4')
    need(correct.tobytes()!=omitted.tobytes(),'synthetic injection omission not detected')
    # Omitting the midpoint dAD contributes only about 0.148% to a large full slope,
    # while its separate normalized midpoint error is 100%; that check must fail.
    fd_full=-11036.2739; fd_mid=16.373758; old_ad=-11052.6478
    hidden=compare(fd_full,old_ad); caught=compare(fd_mid,0.)
    need(hidden['relative_shared_signal']<.01 and caught['relative_shared_signal']==1.,'midpoint omission trap failed')
    need(compare(fd_mid,fd_mid)['relative_shared_signal']==0.,'synthetic midpoint-positive check failed')
    return {'positive_full_and_midpoint_fd_pass':True,'injection_omission_byte_negative':True,
      'missing_midpoint_full_relative_error':hidden['relative_shared_signal'],
      'missing_midpoint_separate_relative_error':caught['relative_shared_signal'],
      'separate_midpoint_check_detects_omission':True}


def main():
    ap=argparse.ArgumentParser(description=__doc__); ap.add_argument('--self-test',action='store_true')
    ap.add_argument('--evidence-root',type=Path); ap.add_argument('--old-root',type=Path)
    ap.add_argument('--npz',type=Path); ap.add_argument('--manifest',type=Path)
    ap.add_argument('--output-dir',type=Path,default=Path(__file__).resolve().parent)
    args=ap.parse_args()
    if args.self_test:
        print(json.dumps({'schema':'native_two_time_recompute_self_test_v1','result':self_test()},indent=2)); return
    out=args.output_dir.resolve(); out.mkdir(parents=True,exist_ok=True)
    if args.npz is not None:
        need(args.manifest is not None,'--npz requires --manifest')
        npz=args.npz.resolve(); mp=args.manifest.resolve(); manifest=json.loads(mp.read_text())
        need(manifest.get('protocol_sha256')==P and sha(npz)==manifest.get('npz_sha256'),'portable NPZ/protocol SHA mismatch')
        with np.load(npz,allow_pickle=False) as z: arr={k:np.asarray(z[k]) for k in z.files}
        need(schema(arr)==manifest.get('arrays'),'portable NPZ array schema/hash mismatch')
        manifest['manifest_sha256']=sha(mp)
    else:
        need(args.evidence_root is not None and args.old_root is not None,'--evidence-root and --old-root required after all prefixes complete')
        e=args.evidence_root.resolve(); old=args.old_root.resolve()
        prot,bind=frozen(e,old); arr,oldprov,pre,injected=old_vectors(e,old,prot,bind)
        newarr,newprov=new_prefixes(e,prot,bind,old,Path(prot['base_case'])/'terminal.checkpoints_f64.bin',injected)
        arr.update(newarr); npz=out/'native_two_time_adjoint.npz'; np.savez_compressed(npz,**arr)
        manifest={'schema':'native_two_time_portable_npz_manifest_v1','protocol_sha256':P,
          'protocol_path':str(e/'protocol.json'),'launch_binding_sha256':sha(e/'launch_binding.json'),
          'runner_sha256':RUNNER,'executable_sha256':EXE,'packed_dimension':N,'new_prefix_ends':list(ENDS),
          'old_provenance':oldprov,'new_prefix_provenance':newprov,
          'npz_path':str(npz),'npz_sha256':sha(npz),'npz_bytes':npz.stat().st_size,'arrays':schema(arr),
          'nonclaims':['no global exact-FP64 g0','no partial-window dot as global dAD','no second global gradient replay','no general smoothness claim']}
        mp=out/'manifest.json'; mp.write_text(json.dumps(manifest,indent=2,sort_keys=True,allow_nan=False)+'\n')
        manifest['manifest_sha256']=sha(mp)
    recomputed=analyze(arr,manifest); rp=out/'recomputed.json'; rp.write_text(json.dumps(recomputed,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(json.dumps({'npz':str(npz),'npz_sha256':sha(npz),'manifest':str(mp),'manifest_sha256':sha(mp),
      'recomputed':str(rp),'recomputed_sha256':sha(rp),
      'full_fd_pass':recomputed['finite_difference']['two_time_full']['frozen_component_checks_pass'],
      'midpoint_fd_pass':recomputed['finite_difference']['midpoint_increment']['frozen_component_checks_pass']},indent=2))

if __name__=='__main__': main()
