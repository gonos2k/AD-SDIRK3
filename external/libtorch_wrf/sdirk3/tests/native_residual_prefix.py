#!/usr/bin/env python3
"""Run one residual-objective native prefix; never overwrite or retry a case."""
import argparse, hashlib, importlib.util, json, os, shutil, signal, subprocess, time
from datetime import datetime
from pathlib import Path
import numpy as np

W=Path(__file__).resolve().parents[4]
EOLD=Path('/Users/yhlee/AD-SDIRK3-review-evidence/native-terminal-adjoint-20261020')
EOBJ=Path('/Users/yhlee/AD-SDIRK3-review-evidence/native-residual-objective-20261021')
SOURCE=Path('/Users/yhlee/AD-SDIRK3-review-evidence/strict-reference-240s-20261020/green_execution/h0p9375_0_240s_newton_tol1e-9')
HELPER=W/'external/libtorch_wrf/sdirk3/tests/native_two_time_prefix.py'
HELPER_SHA='b11948b406c323616c0f00179533dbb8c889ba07ba5f9ac2c4ef8f844eaaa9d8'
OLD_P='b96cbe382b1e992c64e21b2ef3f413cf3aa7ba6b7bfaa31e0a43b91390c9ce45'
P6_P='9971cb56d28022951dfe00c1b7707532cf53c9cc71ca88983c738812751e2650'
N=24531; ENDS=(256,224,192,160,128,96,64,32); H=.9375
CAP=18*1024**3; WALL=7200
REGISTERED_INPUTS={'wrfinput_d01':'e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9',
    'diagnostics.txt':'bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224',
    'namelist.input':'a6ea8f8ab62a74d72840ea46de4ac39a20d5d4247db77ebced0b84a1433dbdc5'}

def _sha(path):
    h=hashlib.sha256()
    with Path(path).open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()

if not HELPER.is_file() or _sha(HELPER)!=HELPER_SHA:
    raise RuntimeError('pinned two-time helper is missing or modified')
_helper_spec=importlib.util.spec_from_file_location('native_two_time_prefix_b119',HELPER)
helper=importlib.util.module_from_spec(_helper_spec);_helper_spec.loader.exec_module(helper)
sha,need=helper.sha,helper.need

def import_pinned(path, name):
    spec=importlib.util.spec_from_file_location(name,path)
    mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
    return mod

def hash_arrays(manifest, packet):
    with np.load(packet,allow_pickle=False) as data:
        required=('case_labels','direction_v_f32','old_mid_states_f64','old_terminal_states_f64',
                  'q_mid_f32','q_terminal_f32','old_g0_terminal_f32')
        for name in required:
            pin=manifest.get('arrays',{}).get(name,{})
            need(name in data and pin.get('shape')==list(data[name].shape) and
                 pin.get('dtype')==data[name].dtype.str and
                 hashlib.sha256(np.ascontiguousarray(data[name]).tobytes()).hexdigest()==pin.get('sha256'),
                 f'old NPZ array pin mismatch: {name}')
        labels=tuple(np.asarray(data['case_labels']).tolist())
        need(labels==('baseline','replay','plus_0p01','minus_0p01','plus_0p03','minus_0p03'),
             'old packet case order mismatch')
        need(data['old_mid_states_f64'].shape==(6,N) and data['old_terminal_states_f64'].shape==(6,N),
             'old packed state arrays have wrong shape')
        return {name:np.asarray(data[name]).copy() for name in required}

def load_frozen(e):
    protocol_path=e/'protocol.json'; binding_path=e/'launch_binding.json'
    need(protocol_path.is_file() and binding_path.is_file(),'protocol/launch binding missing')
    protocol=json.loads(protocol_path.read_text()); binding=json.loads(binding_path.read_text())
    psha=sha(protocol_path); bsha=sha(binding_path)
    need(protocol.get('schema')=='native_residual_objective_protocol_v1' and
         binding.get('schema')=='native_residual_launch_binding_v1' and
         binding.get('protocol')=={'path':str(protocol_path),'sha256':psha},'protocol binding mismatch')
    runner=binding.get('runner',{})
    need(Path(runner.get('path','')).resolve()==Path(__file__).resolve() and runner.get('sha256')==sha(__file__),
         'launch binding does not pin this runner')
    helper_pin=binding.get('reused_helper',{})
    need(Path(helper_pin.get('path','')).resolve()==HELPER.resolve() and helper_pin.get('sha256')==HELPER_SHA and
         HELPER.is_file() and sha(HELPER)==HELPER_SHA,'reused b119 helper pin mismatch')
    hmod=helper
    helper_pin=binding.get('reused_helper',{})
    need(Path(helper_pin.get('path','')).resolve()==HELPER.resolve() and helper_pin.get('sha256')==HELPER_SHA,
         'launch binding does not pin the reused helper')
    brpin=binding.get('build_receipt',{}); brpath=Path(brpin.get('path',''))
    need(brpath.is_file() and brpath.stat().st_size==brpin.get('bytes') and sha(brpath)==brpin.get('sha256'),
         'build receipt pin mismatch')
    build=json.loads(brpath.read_text())
    sources=binding.get('source_files',[])
    need(len(sources)==10 and build.get('source_pin_gate',{}).get('unchanged_v6_bound_sources_match') is True and
         build.get('source_pin_gate',{}).get('changed_cpp_matches_authorized_root_pin') is True,
         'ten source pins/build gate mismatch')
    built_sources=build.get('source_files',{})
    for src in sources:
        path=Path(src['path']); rel=str(path.resolve().relative_to(W)) if path.resolve().is_relative_to(W) else ''
        need(bool(rel) and path.is_file() and sha(path)==src['sha256'] and
             built_sources.get(rel,{}).get('sha256')==src['sha256'],f"source SHA mismatch: {path}")
    graphpin=protocol.get('pre_edit_graph',{}); graphpath=Path(graphpin.get('path',''))
    build_graph=build.get('graph_pre_edit_binding',{})
    need(graphpath.is_file() and graphpath.stat().st_size==graphpin.get('bytes') and
         sha(graphpath)==graphpin.get('sha256') and
         build_graph.get('path')==graphpin.get('path') and build_graph.get('sha256')==graphpin.get('sha256'),
         'pre-edit graph/build receipt pin mismatch')
    exeinfo=binding.get('executable',{}); exe=Path(exeinfo.get('path',''))
    need(exe.is_file() and exe.stat().st_size==exeinfo.get('bytes') and hmod.sha(exe)==exeinfo.get('sha256') and
         build.get('candidate_executable')==exeinfo,'residual executable pin mismatch')
    target=protocol.get('target',{})
    need(protocol.get('packed_dimension')==N and target.get('prefix_ends')==list(ENDS) and
         target.get('h_seconds')==H and target.get('window_steps')==32 and target.get('new_model_steps')==1152 and
         target.get('new_implicit_stages')==3456 and target.get('ranks')==1 and target.get('omp')==1 and
         target.get('newton_abs')==1e-9 and target.get('newton_rel')==0 and target.get('krylov')==1e-7 and
         target.get('ewt')==1e-6 and target.get('checkpoint_comparisons')==1160 and
         target.get('rss_cap_bytes')==CAP and target.get('wall_cap_seconds')==WALL and
         protocol.get('old_horizon')==256 and protocol.get('old_base_case')==str(EOLD/'runs_v5'/'baseline'),
         'residual protocol target mismatch')
    obj=protocol.get('objective',{})
    need(obj.get('accepted_steps')==[128,256] and obj.get('seconds')==[120,240] and
         obj.get('y')==[0.0,0.0] and obj.get('sigma')==[1.0,1.0],
         'two-time residual objective contract mismatch')
    old_manifest_pin=protocol.get('old_manifest',{}); old_packet_pin=protocol.get('old_packet',{})
    manifest_path=Path(old_manifest_pin.get('path','')).resolve(); packet=Path(old_packet_pin.get('path','')).resolve()
    need(manifest_path.is_file() and manifest_path.stat().st_size==old_manifest_pin.get('bytes') and
         hmod.sha(manifest_path)==old_manifest_pin.get('sha256') and packet.is_file() and
         packet.stat().st_size==old_packet_pin.get('bytes') and hmod.sha(packet)==old_packet_pin.get('sha256'),
         'old manifest/packet pin mismatch')
    old_manifest=json.loads(manifest_path.read_text())
    need(old_manifest.get('npz_sha256')==old_packet_pin.get('sha256') and
         old_manifest.get('npz_bytes')==old_packet_pin.get('bytes'), 'old manifest/NPZ pin mismatch')
    arrays=hash_arrays(old_manifest,packet)
    registered=protocol.get('registered_map',{}); registered_path=Path(registered.get('path',''))
    need(registered_path.is_file() and registered_path.stat().st_size==registered.get('bytes') and
         hmod.sha(registered_path)==registered.get('sha256'),'registered forward-map pin mismatch')
    source=Path(binding.get('source_case','')).resolve(); input_pins=binding.get('input_pins',{})
    need(source==SOURCE and source.is_dir() and input_pins==REGISTERED_INPUTS and
         all((source/name).is_file() and hmod.sha(source/name)==REGISTERED_INPUTS[name]
             for name in REGISTERED_INPUTS),'registered source input pins mismatch')
    source_nml=(source/'namelist.input').read_text()
    need(hashlib.sha256(source_nml.encode()).hexdigest()==REGISTERED_INPUTS['namelist.input'],
         'registered baseline namelist pin mismatch')
    costpin=binding.get('cost_helper',{}); cost_path=Path(costpin.get('path',''))
    need(cost_path.is_file() and hmod.sha(cost_path)==costpin.get('sha256'),'objective primitive SHA mismatch')
    cost=import_pinned(cost_path,'native_residual_cost_primitive')
    need(callable(getattr(cost,'objective_pair',None)) and callable(getattr(cost,'inject_midpoint_f32',None)),
         'authoritative objective helper API mismatch')
    objective_receipt_pin=binding.get('baseline_objective_receipt',{})
    objective_receipt_path=Path(objective_receipt_pin.get('path',''))
    need(objective_receipt_path.is_file() and objective_receipt_path.stat().st_size==objective_receipt_pin.get('bytes') and
         hmod.sha(objective_receipt_path)==objective_receipt_pin.get('sha256'),'baseline objective receipt pin mismatch')
    obj_receipt=json.loads(objective_receipt_path.read_text())
    seedpin=binding.get('terminal_seed',{}); midpin=binding.get('midpoint_cotangent',{})
    seed=Path(seedpin.get('path','')); midpoint=Path(midpin.get('path',''))
    for pin,path in ((seedpin,seed),(midpin,midpoint)):
        need(path.is_file() and path.stat().st_size==pin.get('bytes') and hmod.sha(path)==pin.get('sha256'),
             'baseline objective cotangent pin mismatch')
    need(obj_receipt.get('terminal_seed')==seedpin and obj_receipt.get('midpoint_cotangent')==midpin,
         'objective receipt cotangent pins differ from launch binding')
    objective=protocol.get('objective',{}); y=np.asarray(objective.get('y'),dtype=np.float64)
    sigma=np.asarray(objective.get('sigma'),dtype=np.float64)
    pair=cost.objective_pair(arrays['old_mid_states_f64'][0],arrays['old_terminal_states_f64'][0],
        arrays['q_mid_f32'],arrays['q_terminal_f32'],y,sigma)
    terminal32=np.asarray(pair['cotangent_terminal_f64']).astype('<f4')
    midpoint64=np.asarray(pair['cotangent_mid_f64'],dtype='<f8')
    need(seed.stat().st_size==N*4 and seed.read_bytes()==terminal32.tobytes() and
         midpoint.stat().st_size==N*8 and midpoint.read_bytes()==midpoint64.tobytes(),
         'pinned residual cotangents differ from authoritative objective_pair output')
    base=Path(protocol.get('old_base_case','')).resolve(); oldreceipt=base/'run_receipt.json'
    ref=base/'terminal.checkpoints_f64.bin'
    need(base==EOLD/'runs_v5'/'baseline' and oldreceipt.is_file() and ref.is_file(),
         'old base case path missing/mismatched')
    br=json.loads(oldreceipt.read_text())
    need(br.get('status')=='completed' and br.get('protocol_sha256')==hmod.OLD_P and
         br.get('expected_steps')==256 and ref.stat().st_size==257*N*8 and
         hmod.sha(ref)==br.get('actual',{}).get('output_hashes',{}).get(ref.name,{}).get('sha256'),
         'old baseline reference ledger is unpinned')
    return {'protocol':protocol,'psha':psha,'binding':binding,'bsha':bsha,'exe':exe,'cost':cost,
        'cost_sha':costpin['sha256'],'helper_sha':HELPER_SHA,
        'helper':hmod,'arrays':arrays,'pair':pair,'seed':seed,'midpoint':midpoint,
        'objective_receipt':objective_receipt_path,'reference':ref,'old_base_receipt':oldreceipt,
        'protocol_path':protocol_path,'terminal_seed_pin':seedpin,'midpoint_pin':midpin,
        'build_receipt':brpath,'source':source,'source_namelist':source_nml,
        'input_pins':input_pins}

def prefix_previous(e,n,frozen):
    label=f'residual_prefix_{n+32:03d}';d=e/'runs'/label;rpath=d/'run_receipt.json';out=d/'terminal.lambda_out_f32.bin'
    h=frozen['helper'];need(rpath.is_file(),'previous residual prefix missing: '+label)
    r=json.loads(rpath.read_text());a=r.get('actual',{})
    need(r.get('status')=='completed' and r.get('protocol_sha256')==frozen['psha'] and r.get('label')==label and
         r.get('objective_kind')=='residual' and r.get('expected_steps')==n+32 and
         r.get('expected_implicit_stages')==3*(n+32) and r.get('expected_h_seconds')==H and
         r.get('source_files')==frozen['binding']['source_files'] and
         r.get('executable')==frozen['binding']['executable'] and a.get('step_telemetry_count')==n+32 and
         a.get('newton_convergence_record_count')==3*(n+32) and a.get('full_baseline_prefix_byte_equal') is True and
         out.is_file() and out.stat().st_size==N*4 and
         a.get('output_hashes',{}).get(out.name,{}).get('sha256')==h.sha(out),
         'previous residual cotangent is incomplete/unpinned: '+label)
    return out,rpath,r

def native_rss(pid):
    rows=[]
    for line in subprocess.check_output(['/bin/ps','-axo','pid=,ppid=,rss=,comm='],text=True).splitlines():
        p=line.split(None,3)
        if len(p)==4:
            try: rows.append((int(p[0]),int(p[1]),int(p[2])*1024,p[3]))
            except ValueError: pass
    family={pid}
    for _ in range(8): family.update(p for p,pp,_,_ in rows if pp in family)
    return [(p,rss,cmd) for p,_,rss,cmd in rows if p in family and 'wrf_native_residual_objective' in cmd]

def old_midpoint_bytes(frozen,checkpoint_file,prefix_receipt):
    h=frozen['helper'];old=frozen['arrays']['old_mid_states_f64'][0].astype('<f8',copy=False)
    need(checkpoint_file.is_file() and checkpoint_file.stat().st_size==161*N*8 and
         h.sha(checkpoint_file)==prefix_receipt['actual']['output_hashes'][
             'terminal.checkpoints_f64.bin']['sha256'],'prefix160 checkpoint pin mismatch')
    with checkpoint_file.open('rb') as f:
        f.seek(128*N*8);actual=f.read(N*8)
    need(actual==old.tobytes(),'new prefix160 accepted Z128 differs from frozen baseline midpoint state')
    return hashlib.sha256(actual).hexdigest()

def make_midpoint_injection(outdir,previous_lambda,previous_receipt,frozen):
    h=frozen['helper'];cost=frozen['cost'];mid64=np.fromfile(frozen['midpoint'],dtype='<f8')
    old32=np.fromfile(previous_lambda,dtype='<f4')
    need(old32.size==N and mid64.size==N and np.isfinite(old32).all() and np.isfinite(mid64).all(),
         'midpoint injection operands malformed/nonfinite')
    injected=np.asarray(cost.inject_midpoint_f32(old32,mid64),dtype='<f4')
    incoming=outdir/'incoming_lambda128_residual_f32.bin'
    incoming.write_bytes(injected.tobytes())
    report={'schema':'native_residual_midpoint_injection_v1','accepted_step':128,'accepted_time_s':120,
        'formula':'Q32(FP64(lambda128_from_new_prefix160)+midpoint_cotangent_f64)',
        'previous_prefix_receipt_sha256':h.sha(previous_receipt),
        'previous_lambda_path':str(previous_lambda),'previous_lambda_sha256':h.sha(previous_lambda),
        'midpoint_cotangent_path':str(frozen['midpoint']),'midpoint_cotangent_sha256':h.sha(frozen['midpoint']),
        'baseline_Z128_sha256':hashlib.sha256(frozen['arrays']['old_mid_states_f64'][0].tobytes()).hexdigest(),
        'new_lambda_path':str(incoming),'new_lambda_sha256':h.sha(incoming),'bytes':incoming.stat().st_size}
    receipt=outdir/'midpoint_injection.json'
    receipt.write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
    return incoming,receipt,report

def run_case(e,n,frozen):
    h=frozen['helper']; label=f'residual_prefix_{n:03d}';outdir=e/'runs'/label
    h.need(not outdir.exists(),f'refuse overwrite existing residual case: {outdir}')
    reference=frozen['reference'];injection_receipt=None
    if n==256: incoming=frozen['seed'];incoming_source=frozen['objective_receipt']
    else:
        previous,previous_receipt,prevrec=prefix_previous(e,n,frozen)
        if n==128:
            cp=e/'runs'/'residual_prefix_160'/'terminal.checkpoints_f64.bin'
            z128_sha=old_midpoint_bytes(frozen,cp,prevrec)
        outdir.mkdir(parents=True)
        if n==128:
            incoming,injection_receipt,injection=make_midpoint_injection(
                outdir,previous,previous_receipt,frozen)
            injection['baseline_Z128_checkpoint_sha256']=z128_sha
            ip=outdir/'midpoint_injection.json';ip.write_text(json.dumps(injection,indent=2,sort_keys=True)+'\n')
        else: incoming=previous
        incoming_source=previous_receipt
    source=frozen['source']
    if not outdir.exists():outdir.mkdir(parents=True)
    for name in ('wrfinput_d01','diagnostics.txt'):shutil.copy2(source/name,outdir/name)
    (outdir/'namelist.input').write_text(h.prefix_namelist(frozen['source_namelist'],n))
    (outdir/'namelist.diff').write_text('Existing graph flags enabled; run_seconds changed only to this horizon.\n')
    h.need(all(h.sha(outdir/name)==frozen['input_pins'][name] for name in ('wrfinput_d01','diagnostics.txt')),
           'copied registered inputs changed')
    env=os.environ.copy()
    for k in list(env):
        if k.startswith(('WRF_SDIRK3_','WRF_PARITY_')):del env[k]
    overrides={'OMP_NUM_THREADS':'1','WRF_SDIRK3_RHS_COUNT':'1','WRF_SDIRK3_STAGE_OPERAND_DIAG':'0',
        'WRF_SDIRK3_NATIVE_TERMINAL_MODE':'prefix','WRF_SDIRK3_NATIVE_TERMINAL_ETA':'0',
        'WRF_SDIRK3_NATIVE_TERMINAL_CHAIN':'baseline','WRF_SDIRK3_NATIVE_TERMINAL_STEPS':str(n),
        'WRF_SDIRK3_NATIVE_TERMINAL_OUT':str(outdir/'terminal'),
        'WRF_SDIRK3_NATIVE_TERMINAL_REFERENCE':str(reference),
        'WRF_SDIRK3_NATIVE_TERMINAL_LAMBDA32_IN':str(incoming)}
    env.update(overrides)
    exe=frozen['exe'];command=['/usr/bin/time','-l','-o','time.log','/opt/homebrew/bin/mpirun',
        '-np','1','--bind-to','none',str(exe)]
    rp=outdir/'run_receipt.json';bound=[{'role':'old_full_baseline_reference','path':str(reference),
        'sha256':h.sha(reference),'bytes':reference.stat().st_size},
        {'role':'incoming_residual_cotangent_f32','path':str(incoming),'sha256':h.sha(incoming),'bytes':incoming.stat().st_size}]
    if injection_receipt:
        bound += [{'role':'midpoint_cotangent_f64','path':str(frozen['midpoint']),
                   'sha256':h.sha(frozen['midpoint']),'bytes':frozen['midpoint'].stat().st_size},
                  {'role':'midpoint_injection_receipt','path':str(injection_receipt),
                   'sha256':h.sha(injection_receipt),'bytes':injection_receipt.stat().st_size}]
    receipt={'schema':'native_residual_prefix_receipt_v1','label':label,'primary_label':'residual',
        'objective_kind':'residual','status':'prepared','created_at':datetime.now().astimezone().isoformat(),
        'protocol_sha256':frozen['psha'],'launch_binding_sha256':frozen['bsha'],'runner_sha256':h.sha(__file__),
        'reused_helper_sha256':frozen['helper_sha'],'cost_helper_sha256':frozen['cost_sha'],
        'source_files':frozen['binding']['source_files'],'executable':frozen['binding']['executable'],
        'input_sha256':frozen['input_pins']['wrfinput_d01'],
        'namelist_sha256':h.sha(outdir/'namelist.input'),'expected_steps':n,'expected_implicit_stages':3*n,
        'expected_h_seconds':H,'new_model_timestep':False,'command':command,'environment':overrides,
        'bound_incoming_files':bound,'incoming_lambda_source':str(incoming_source),
        'midpoint_injection_receipt':str(injection_receipt) if injection_receipt else None}
    rp.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n')
    start,samples,reason=time.monotonic(),[],None
    with (outdir/'run.log').open('wb') as log:
        proc=subprocess.Popen(command,cwd=outdir,env=env,stdout=log,stderr=subprocess.STDOUT,start_new_session=True)
        receipt.update(status='running',pid=proc.pid);rp.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n')
        while proc.poll() is None:
            native=native_rss(proc.pid);elapsed=time.monotonic()-start
            samples.extend({'elapsed_seconds':elapsed,'pid':p,'rss_bytes':rss} for p,rss,_ in native)
            if any(rss>CAP for _,rss,_ in native):reason='native RSS exceeded 18 GiB cap'
            if elapsed>WALL:reason='two-hour per-case wall budget exceeded'
            if reason:
                for p,_,_ in native:
                    try:os.kill(p,signal.SIGTERM)
                    except ProcessLookupError:pass
                os.killpg(proc.pid,signal.SIGTERM)
                try:proc.wait(timeout=10)
                except subprocess.TimeoutExpired:os.killpg(proc.pid,signal.SIGKILL);proc.wait()
                break
            time.sleep(.5)
        rc=proc.wait()
    (outdir/'rss_samples.json').write_text(json.dumps(samples,indent=2)+'\n')
    log=(outdir/'rsl.error.0000').read_text(errors='replace') if (outdir/'rsl.error.0000').exists() else ''
    actual=h.collect_result(outdir,n,reference,incoming,rc,reason,start,samples,log)
    result=actual.get('terminal_result',{})
    if n==128:
        result['midpoint_injection']={'receipt_path':str(injection_receipt),'receipt_sha256':h.sha(injection_receipt),
            'baseline_Z128_checkpoint_sha256':z128_sha,'midpoint_cotangent_sha256':h.sha(frozen['midpoint'])}
    receipt.update(status='completed' if 'validation_error' not in actual else 'failed',actual=actual,
        ended_at=datetime.now().astimezone().isoformat())
    rp.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n')
    print(json.dumps({'label':label,'status':receipt['status'],'receipt':str(rp),'receipt_sha256':h.sha(rp),
        'elapsed_seconds':actual['elapsed_seconds'],'peak_rss_bytes':actual['peak_sampled_native_rss_bytes']},indent=2),flush=True)
    h.need('validation_error' not in actual,actual.get('validation_error','residual prefix failed'))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence-root',type=Path,default=EOBJ)
    parser.add_argument('--prefix-end',type=int,choices=ENDS,required=True)
    args=parser.parse_args()
    frozen=load_frozen(args.evidence_root.resolve())
    run_case(args.evidence_root.resolve(),args.prefix_end,frozen)

if __name__=='__main__':
    main()
