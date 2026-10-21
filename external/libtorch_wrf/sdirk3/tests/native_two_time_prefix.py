#!/usr/bin/env python3
"""Run one frozen two-time lower-prefix VJP; never overwrite or retry."""
import argparse, hashlib, json, os, re, shutil, signal, subprocess, time
from datetime import datetime
from pathlib import Path
import numpy as np
from netCDF4 import Dataset, chartostring

W = Path(__file__).resolve().parents[4]
EOLD = Path('/Users/yhlee/AD-SDIRK3-review-evidence/native-terminal-adjoint-20261020')
OLD_P = 'b96cbe382b1e992c64e21b2ef3f413cf3aa7ba6b7bfaa31e0a43b91390c9ce45'
P6_P = '9971cb56d28022951dfe00c1b7707532cf53c9cc71ca88983c738812751e2650'
N, H, CAP, WALL = 24531, .9375, 18*1024**3, 7200
ENDS = (128, 96, 64, 32)
INPUTS = {'wrfinput_d01':'e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9',
          'namelist.input':'a6ea8f8ab62a74d72840ea46de4ac39a20d5d4247db77ebced0b84a1433dbdc5',
          'diagnostics.txt':'bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224'}

def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''): h.update(b)
    return h.hexdigest()

def need(ok,msg):
    if not ok: raise ValueError(msg)

def value_path(x): return x.get('path') if isinstance(x,dict) else x
def value_sha(x): return x.get('sha256') if isinstance(x,dict) else x

def prefix_namelist(text,n):
    need(text.count(' &dynamics\n')==1 and 'sdirk3_retain_graph_for_adjoint' not in text and
         'sdirk3_imex_slow_in_tangent' not in text,'source namelist template changed')
    text=text.replace(' &dynamics\n',' &dynamics\n sdirk3_retain_graph_for_adjoint = .true.,\n sdirk3_imex_slow_in_tangent = .true.,\n',1)
    text,count=re.subn(r'(run_seconds\s*=\s*)240(\s*,)',lambda m:m[1]+str(n*15//16)+m[2],text)
    need(count==1,'expected one 240-second run_seconds entry')
    return text

def expected_times(n):
    seconds=n*15//16
    need(seconds%15==0,'prefix horizon is off the history cadence')
    return [f'0001-01-01_00:{s//60:02d}:{s%60:02d}' for s in range(0,seconds+1,15)]

def load_frozen(e):
    p,b=e/'protocol.json',e/'launch_binding.json'
    need(p.is_file() and b.is_file(),'protocol.json/launch_binding.json missing')
    protocol,binding=json.loads(p.read_text()),json.loads(b.read_text());psha=sha(p)
    need(protocol.get('schema')=='native_two_time_adjoint_protocol_v1' and
         binding.get('protocol')=={'path':str(p),'sha256':psha},'protocol binding mismatch')
    runner=binding.get('runner',{})
    need(Path(runner.get('path','')).resolve()==Path(__file__).resolve() and runner.get('sha256')==sha(__file__),
         'launch binding does not pin this runner')
    sources=binding.get('source_files',[])
    need(len(sources)==10 and protocol.get('source_files')==sources,'expected ten identical source pins')
    for s in sources:
        q=Path(s['path']);need(q.resolve().is_relative_to(W) and q.is_file() and sha(q)==s['sha256'],f'source SHA mismatch: {q}')
    exeinfo=binding.get('executable',{});exe=Path(exeinfo.get('path',''))
    need(exe.is_file() and exe.stat().st_size==exeinfo.get('bytes') and sha(exe)==exeinfo.get('sha256') and
         protocol.get('executable')==exeinfo,'P6 executable pin mismatch')
    t=protocol.get('target',{})
    need(Path(protocol.get('previous_evidence_root','')).resolve()==EOLD and t.get('N')==N and
         t.get('total_steps')==256 and
         t.get('h_seconds')==H and t.get('prefix_ends')==list(ENDS) and t.get('ranks')==1 and t.get('omp')==1 and
         t.get('time_seconds')==240 and t.get('midpoint_step')==128 and t.get('midpoint_seconds')==120 and
         t.get('new_model_steps')==320 and t.get('new_implicit_stages')==960 and
         t.get('newton_abs')==1e-9 and t.get('newton_rel')==0 and t.get('krylov')==1e-7 and t.get('ewt')==1e-6 and
         t.get('per_case_rss_cap_bytes')==CAP and t.get('per_case_wall_cap_s')==WALL,'protocol target mismatch')
    base=Path(protocol.get('base_case','')).resolve();source=Path(protocol.get('source_case','')).resolve()
    need(base==EOLD/'runs_v5'/'baseline' and source.is_dir(),'baseline/source case path mismatch')
    for name,h in INPUTS.items():
        need(value_sha(protocol.get('input_pins',{}).get(name))==h and (source/name).is_file() and sha(source/name)==h,
             f'registered input pin mismatch: {name}')
    ir=protocol.get('injection_receipt',{});ip=Path(value_path(ir)).resolve()
    need(ip.is_file() and ip.stat().st_size==ir.get('bytes') and sha(ip)==value_sha(ir),'injection receipt pin mismatch')
    firstpin=protocol.get('first_lambda128',{});first=Path(value_path(firstpin)).resolve()
    need(first.is_file() and first.stat().st_size==N*4 and sha(first)==value_sha(firstpin),'lambda128 file pin mismatch')
    inj=json.loads(ip.read_text())
    need(inj.get('schema')=='native_midpoint_cotangent_injection_v1' and inj.get('accepted_step')==128 and
         inj.get('accepted_time_s')==120.,'midpoint injection role/time mismatch')
    oldpin=inj.get('old_terminal_lambda128',{});old=Path(oldpin.get('path','')).resolve()
    sr=inj.get('old_source_receipt',{});srpath=Path(sr.get('path','')).resolve()
    need(srpath.is_file() and srpath.stat().st_size==sr.get('bytes') and sha(srpath)==sr.get('sha256'),
         'prefix160 source receipt pin mismatch')
    oldr=json.loads(srpath.read_text())
    need(old==EOLD/'runs_v6'/'baseline_prefix_160'/'terminal.lambda_out_f32.bin' and old.is_file() and
         old.stat().st_size==N*4 and sha(old)==oldpin.get('sha256') and oldr.get('status')=='completed' and
         oldr.get('label')=='baseline_prefix_160' and oldr.get('protocol_sha256')==P6_P and
         oldr.get('expected_steps')==160 and oldr.get('expected_implicit_stages')==480 and
         oldr.get('expected_h_seconds')==H and oldr.get('input_sha256')==INPUTS['wrfinput_d01'] and
         oldr.get('executable')==protocol.get('executable') and
         oldr.get('actual',{}).get('output_hashes',{}).get(old.name,{}).get('sha256')==oldpin.get('sha256'),
         'injected old cotangent is not the pinned prefix160 output')
    qpin=inj.get('q128',{});q=Path(qpin.get('path','')).resolve();new=inj.get('new_lambda128',{})
    midpoint=protocol.get('fixed_midpoint_objective',{});midpath=Path(midpoint.get('path','')).resolve()
    need(midpath.is_file() and midpath.stat().st_size==midpoint.get('bytes') and
         sha(midpath)==midpoint.get('sha256'),'frozen midpoint objective pin mismatch')
    need(q.is_file() and q.stat().st_size==N*4 and sha(q)==qpin.get('sha256') and
         value_path(protocol.get('q128')) and Path(value_path(protocol['q128'])).resolve()==q and
         value_sha(protocol['q128'])==qpin.get('sha256') and
         first==Path(new.get('path','')).resolve() and sha(first)==new.get('sha256'),'q128/new lambda pins disagree')
    expected=(np.fromfile(old,dtype='<f4').astype(np.float64)+np.fromfile(q,dtype='<f4').astype(np.float64)).astype('<f4')
    need(expected.tobytes()==first.read_bytes(),'lambda128 is not the once-rounded old+q128 sum')
    ref=base/'terminal.checkpoints_f64.bin';brp=base/'run_receipt.json'
    need(brp.is_file(),'V5 baseline receipt missing')
    br=json.loads(brp.read_text())
    need(br.get('status')=='completed' and br.get('protocol_sha256')==OLD_P and br.get('expected_steps')==256 and
         ref.is_file() and ref.stat().st_size==257*N*8 and sha(ref)==br.get('actual',{}).get('output_hashes',{}).get(ref.name,{}).get('sha256'),
         'canonical V5 full checkpoint ledger mismatch')
    return dict(protocol=protocol,psha=psha,binding=binding,bsha=sha(b),exe=exe,base=base,source=source,
                reference=ref,injection=ip,first=first,old_receipt=br)

def previous_lambda(e,n,frozen):
    label=f'two_time_prefix_{n+32:03d}';d=e/'runs'/label;rpath=d/'run_receipt.json';out=d/'terminal.lambda_out_f32.bin'
    need(rpath.is_file(),'previous prefix missing: '+label)
    r=json.loads(rpath.read_text());a=r.get('actual',{});exe=frozen['binding']['executable'];sources=frozen['binding']['source_files']
    need(r.get('status')=='completed' and r.get('protocol_sha256')==frozen['psha'] and r.get('label')==label and
         r.get('primary_label')=='two_time' and r.get('prefix_reexecution') is True and
         r.get('expected_steps')==n+32 and r.get('expected_implicit_stages')==3*(n+32) and r.get('expected_h_seconds')==H and
         r.get('input_sha256')==INPUTS['wrfinput_d01'] and r.get('executable')==exe and r.get('source_files')==sources and
         a.get('step_telemetry_count')==n+32 and a.get('newton_convergence_record_count')==3*(n+32) and out.is_file() and
         out.stat().st_size==N*4 and a.get('output_hashes',{}).get(out.name,{}).get('sha256')==sha(out),
         'previous prefix cotangent is incomplete/unpinned: '+label)
    return out,rpath

def native_rss(pid):
    rows=[]
    for line in subprocess.check_output(['/bin/ps','-axo','pid=,ppid=,rss=,comm='],text=True).splitlines():
        p=line.split(None,3)
        if len(p)==4:
            try:rows.append((int(p[0]),int(p[1]),int(p[2])*1024,p[3]))
            except ValueError:pass
    family={pid}
    for _ in range(8):family.update(p for p,pp,_,_ in rows if pp in family)
    return [(p,rss,cmd) for p,_,rss,cmd in rows if p in family and 'wrf_native_terminal_' in cmd]

def same_prefix(actual,reference,size):
    if not actual.is_file() or actual.stat().st_size!=size or reference.stat().st_size!=257*N*8:return False
    with actual.open('rb') as a,reference.open('rb') as b:
        left=size
        while left:
            count=min(1<<20,left);x=a.read(count);y=b.read(count)
            if len(x)!=count or x!=y:return False
            left-=count
    return True

def collect_result(outdir,n,reference,incoming,rc,stop_reason,start,samples,log_text):
    actual={'return_code':rc,'runner_stop_reason':stop_reason,'elapsed_seconds':time.monotonic()-start,
        'native_rss_monitor_has_samples':bool(samples),
        'peak_sampled_native_rss_bytes':max((s['rss_bytes'] for s in samples),default=0),
        'success_marker':'SUCCESS COMPLETE WRF' in log_text,
        'step_telemetry_count':log_text.count('[STEP TELEMETRY]'),
        'newton_convergence_record_count':log_text.count('[Newton] CONVERGED'),
        'stage_csv_count':len(list(outdir.glob('accepted_ark_w_step_*.csv')))}
    try:
        times=[];files=[];finite=True
        minima={'theta_K':float('inf'),'column_mass_Pa':float('inf'),'layer_dph':float('inf')}
        for nc in sorted(outdir.glob('wrfout_d01_*')):
            with Dataset(nc) as ds:
                ts=chartostring(ds.variables['Times'][:]).tolist();times.extend(ts)
                for field in ('U','V','W','PH','T','THM','MU','MUB'):
                    finite &= bool(np.isfinite(ds.variables[field][:]).all())
                minima['theta_K']=min(minima['theta_K'],float(np.min(np.asarray(ds.variables['THM'][:],np.float64)+300)))
                minima['column_mass_Pa']=min(minima['column_mass_Pa'],float(np.min(np.asarray(ds.variables['MU'][:],np.float64)+np.asarray(ds.variables['MUB'][:],np.float64))))
                phi=np.asarray(ds.variables['PH'][:],np.float64)+np.asarray(ds.variables['PHB'][:],np.float64)
                minima['layer_dph']=min(minima['layer_dph'],float(np.min(np.diff(phi,axis=1))))
            files.append({'path':nc.name,'bytes':nc.stat().st_size,'sha256':sha(nc),'times':ts})
        actual.update(netcdf_times=times,netcdf_files=files,all_eight_fields_finite=finite,physical_minima=minima)
        for key in ('prepare','endpoint','result'):
            p=outdir/f'terminal.{key}.json'
            if p.is_file():actual[f'terminal_{key}']=json.loads(p.read_text())
        actual['output_hashes']={p.name:{'bytes':p.stat().st_size,'sha256':sha(p)}
            for p in sorted(outdir.iterdir()) if p.is_file() and p.name not in ('run_receipt.json','rss_samples.json')}
        cp=outdir/'terminal.checkpoints_f64.bin';size=(n+1)*N*8
        equal=same_prefix(cp,reference,size)
        actual.update(checkpoint_bytes=cp.stat().st_size if cp.exists() else 0,
                      full_baseline_prefix_byte_equal=bool(equal))
        need(rc==0 and stop_reason is None and bool(samples) and actual['success_marker'] and
             actual['step_telemetry_count']==n and actual['newton_convergence_record_count']==3*n and
             actual['stage_csv_count']==0 and times==expected_times(n) and finite and
             all(v>0 for v in minima.values()) and equal,
             'native process/3N/history/admissibility/prefix-byte gate failed')
        result=actual.get('terminal_result',{})
        prep=actual.get('terminal_prepare',{});endpoint=actual.get('terminal_endpoint',{})
        need(prep.get('mode')=='prefix' and prep.get('chain')=='baseline' and prep.get('eta')==0 and
             prep.get('expected_steps')==n and endpoint.get('mode')=='prefix' and
             endpoint.get('chain')=='baseline' and endpoint.get('expected_steps')==n and
             endpoint.get('host_steps')==n and endpoint.get('reference_path')==str(reference),
             'terminal prepare/endpoint role or reference-path mismatch')
        need(result.get('pullback_checked_by_fortran') is True and
             result.get('mode')=='prefix' and result.get('chain')=='baseline' and
             result.get('expected_steps')==n and result.get('eta')==0 and
             result.get('gradient_control_step')==n-32 and
             result.get('gradient_scope')=='local_32_step_window' and
             result.get('reference_checkpoints_expected')==n+1 and
             result.get('reference_checkpoints_matched')==n+1 and
             result.get('endpoint_reference_exact') is True,
             'Fortran pullback/reference metadata gate failed')
        if n>32:
            need(result.get('directional_local_precast_f64') is None and
                 result.get('directional_vjp_f32_chain') is None,
                 'partial window gradient was mislabeled as the full-map directional derivative')
        lin=outdir/'terminal.lambda_in_f32.bin';lout=outdir/'terminal.lambda_out_f32.bin'
        g64=outdir/'terminal.wrapper_gradient_f64.bin'
        need(lin.stat().st_size==N*4 and incoming.stat().st_size==N*4 and
             lout.stat().st_size==N*4 and g64.stat().st_size==N*8,
             'Fortran cotangent/gradient output has the wrong size')
        lambda_in=np.fromfile(lin,dtype='<f4');lambda_out=np.fromfile(lout,dtype='<f4')
        gradient64=np.fromfile(g64,dtype='<f8')
        need(np.isfinite(lambda_in).all() and np.isfinite(lambda_out).all() and
             np.isfinite(gradient64).all() and lin.read_bytes()==incoming.read_bytes() and
             lout.read_bytes()==gradient64.astype('<f4').tobytes(),
             'actual Fortran input or local pre-cast-to-FP32 output differs bytewise')
    except Exception as exc:
        actual['validation_error']=str(exc)
    return actual

def run(args):
    e=args.evidence_root.resolve();n=args.prefix_end;label=f'two_time_prefix_{n:03d}'
    frozen=load_frozen(e);outdir=e/'runs'/label
    need(not outdir.exists(),f'refuse overwrite existing case: {outdir}')
    reference=frozen['reference']
    if n==128: incoming,source_receipt=frozen['first'],frozen['injection']
    else: incoming,source_receipt=previous_lambda(e,n,frozen)
    source=frozen['source']
    outdir.mkdir(parents=True)
    for name in ('wrfinput_d01','diagnostics.txt'):shutil.copy2(source/name,outdir/name)
    (outdir/'namelist.input').write_text(prefix_namelist((source/'namelist.input').read_text(),n))
    (outdir/'namelist.diff').write_text('Existing graph keys enabled; only run_seconds shortened to this prefix horizon.\n')
    need(all(sha(outdir/name)==INPUTS[name] for name in ('wrfinput_d01','diagnostics.txt')),
         'copied registered input hash mismatch')
    env=os.environ.copy()
    for k in list(env):
        if k.startswith(('WRF_SDIRK3_','WRF_PARITY_')):del env[k]
    overrides={'OMP_NUM_THREADS':'1','WRF_SDIRK3_RHS_COUNT':'1','WRF_SDIRK3_STAGE_OPERAND_DIAG':'0',
        'WRF_SDIRK3_NATIVE_TERMINAL_MODE':'prefix','WRF_SDIRK3_NATIVE_TERMINAL_ETA':'0',
        'WRF_SDIRK3_NATIVE_TERMINAL_CHAIN':'baseline','WRF_SDIRK3_NATIVE_TERMINAL_STEPS':str(n),
        'WRF_SDIRK3_NATIVE_TERMINAL_OUT':str(outdir/'terminal'),
        'WRF_SDIRK3_NATIVE_TERMINAL_REFERENCE':str(reference),
        'WRF_SDIRK3_NATIVE_TERMINAL_LAMBDA32_IN':str(incoming)}
    env.update(overrides);exe=frozen['exe'];command=['/usr/bin/time','-l','-o','time.log',
        '/opt/homebrew/bin/mpirun','-np','1','--bind-to','none',str(exe)]
    rp=outdir/'run_receipt.json'
    receipt={'schema':'native_two_time_prefix_v1','label':label,'primary_label':'two_time',
        'prefix_reexecution':True,'status':'prepared','created_at':datetime.now().astimezone().isoformat(),
        'protocol_sha256':frozen['psha'],'protocol_path':str(e/'protocol.json'),
        'launch_binding_sha256':frozen['bsha'],'runner_sha256':sha(__file__),
        'source_files':frozen['binding']['source_files'],'executable':frozen['binding']['executable'],
        'input_sha256':INPUTS['wrfinput_d01'],'namelist_sha256':sha(outdir/'namelist.input'),
        'expected_steps':n,'expected_implicit_stages':3*n,'expected_h_seconds':H,'new_model_timestep':False,
        'command':command,'environment':overrides,
        'bound_incoming_files':[{'role':'baseline_checkpoint_reference','path':str(reference),
            'sha256':sha(reference),'bytes':reference.stat().st_size},
            {'role':'incoming_cotangent_f32','path':str(incoming),'sha256':sha(incoming),'bytes':incoming.stat().st_size}],
        'incoming_lambda_source':str(source_receipt)}
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
    actual=collect_result(outdir,n,reference,incoming,rc,reason,start,samples,log)
    ok='validation_error' not in actual
    receipt.update(status='completed' if ok else 'failed',actual=actual,
                   ended_at=datetime.now().astimezone().isoformat())
    rp.write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n')
    summary={'label':label,'status':receipt['status'],'receipt':str(rp),'receipt_sha256':sha(rp),
        'elapsed_seconds':actual['elapsed_seconds'],'peak_rss_bytes':actual['peak_sampled_native_rss_bytes']}
    print(json.dumps(summary,indent=2),flush=True)
    need(ok,actual.get('validation_error','native two-time prefix failed'))

def self_test():
    template=' &time_control\n run_seconds = 240,\n &end\n &dynamics\n &end\n'
    for n,seconds in ((128,120),(96,90),(64,60),(32,30)):
        need(f'run_seconds = {seconds},' in prefix_namelist(template,n) and
             len(expected_times(n))==seconds//15+1,f'prefix helper mismatch at {n}')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--evidence-root',type=Path)
    ap.add_argument('--prefix-end',type=int,choices=ENDS)
    ap.add_argument('--self-test',action='store_true')
    a=ap.parse_args()
    if a.self_test:self_test();print('runner helper checks passed');return
    need(a.evidence_root is not None and a.prefix_end is not None,'--evidence-root and --prefix-end are required')
    run(a)

if __name__=='__main__':main()
