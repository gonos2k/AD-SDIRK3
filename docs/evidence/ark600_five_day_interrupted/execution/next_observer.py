#!/usr/bin/env python3
"""Future case-side reader: copy into a fresh run root; never restart or modify the model."""
from pathlib import Path
from datetime import datetime,timedelta
import hashlib,importlib.util,json,subprocess,time,os,traceback
ROOT=Path(__file__).resolve().parents[1]
OLD=Path('/Users/yhlee/AD-SDIRK3-review-evidence/standard-forward-comparison-20261024')
HELPER=ROOT/'execution/frozen_benchmark_wrf_accuracy_cost.py'
HELPER_SHA='823ed95d2b312eb7e9ec3719dd13cc6d84f84ab0c87439ffd0895d21f9dd15c3'
CASE=ROOT/'runs/ark_h600'
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,d):
    p.parent.mkdir(parents=True,exist_ok=True);tmp=p.with_suffix(p.suffix+'.tmp');tmp.write_text(json.dumps(d,indent=2)+'\n');tmp.replace(p)
def stamp(seconds):return (datetime(1,1,1)+timedelta(seconds=seconds)).isoformat(sep='_',timespec='seconds')
def closed(seconds):
    p=CASE/('wrfout_d01_'+stamp(seconds));ready=CASE/('wrfoutReady_d01_'+stamp(seconds))
    if not p.exists() or not ready.exists():return False
    first=p.stat();time.sleep(.1);last=p.stat()
    if (first.st_size,first.st_mtime_ns)!=(last.st_size,last.st_mtime_ns):return False
    q=subprocess.run(['/usr/sbin/lsof','-F','pa','--',str(p)],capture_output=True,text=True,timeout=5)
    if q.returncode not in (0,1):return False
    return not any(line in ('aw','au') for line in q.stdout.splitlines())
def view(source,label,seconds):
    d=ROOT/'checkpoint_views'/str(seconds)/label;d.mkdir(parents=True,exist_ok=True)
    for name in ('wrfinput_d01','namelist.input'):
        dest=d/name;target=source/name
        if dest.is_symlink():assert dest.resolve()==target.resolve()
        elif dest.exists():raise FileExistsError(dest)
        else:dest.symlink_to(target)
    for n in range(0,seconds+1,21600):
        name='wrfout_d01_'+stamp(n);p=source/name
        if not p.is_file():raise FileNotFoundError(p)
        dest=d/name
        if dest.is_symlink():assert dest.resolve()==p.resolve()
        elif dest.exists():raise FileExistsError(dest)
        else:dest.symlink_to(p)
    return d

def analyze(seconds,final=False):
    manifest=json.loads((ROOT/'manifest.json').read_text());plan=dict(manifest['analysis_contract']);plan.update(duration_seconds=seconds,expected_history_states=seconds//21600+1,output_interval_seconds=21600,extra_metrics=['mean_bias','per_z','max_location','boundary_error_fraction'])
    baseline=json.loads((OLD/'forecast_manifest_lean.json').read_text());labels=['rk3_h600','rk3_h300','rk3_h150'] if final else ['rk3_h600'];runs=[]
    for label in labels:
        c=baseline['cases'][label];source=Path(c['directory']);path=source if final else view(source,label,seconds)
        runs.append({'label':label,'path':str(path),'method':'RK3','h_seconds':c['h_seconds'],'input_sha256':manifest['common_input']['sha256'],'executable_path':c['executable_path'],'executable_sha256':c['executable_sha256'],'namelist_sha256':c['namelist_sha256']})
    path=CASE if final else view(CASE,'ark_h600',seconds);c=manifest['cases']['ark_h600']
    runs.append({'label':'ark_h600','path':str(path),'method':'ARK324','h_seconds':600,'input_sha256':manifest['common_input']['sha256'],'executable_path':manifest['executable']['path'],'executable_sha256':manifest['executable']['sha256'],'namelist_sha256':c['namelist_sha256']})
    if seconds==21600 and not final:
        old=OLD/'quiet_forward_diagnostics/v2_runs/ark_h600'
        oldm=json.loads((OLD/'quiet_forward_diagnostics/manifest_v2.json').read_text());prior=next(v for v in oldm['cases'].values() if v['method']=='ARK324')
        runs.append({'label':'prior_quiet_ark_h600','path':str(view(old,'prior_quiet_ark_h600',seconds)),'method':'ARK324 prior','h_seconds':600,'input_sha256':manifest['common_input']['sha256']})
        # Equal-step implementation comparison is not timestep sensitivity.
        plan.pop('comparison_pairs',None)
    if final:
        plan['comparison_pairs']=[{'name':'rk3_600_300','left':'rk3_h600','right':'rk3_h300'},{'name':'rk3_300_150','left':'rk3_h300','right':'rk3_h150'}]
    plan['runs']=runs;plan['comparison_scope']='State/phase/budget comparisons only; archived RK3 comparator not truth; no physical accuracy budget or same-accuracy cost claim. No wall receipts mixed into checkpoint views.'
    p=ROOT/'analysis'/str(seconds)/'plan.json';save(p,plan)
    spec=importlib.util.spec_from_file_location('pinned_wrf_comparison',HELPER);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    result=module.analyze(p,'rk3_h600',p.parent/'comparison.json','rk3_h300' if final else None)
    if seconds==21600 and not final:
        prior_plan=dict(plan);prior_plan['runs']=[row for row in runs if row['label']!='rk3_h600']
        prior_plan['comparison_scope']='Same-step implementation comparison; not timestep sensitivity or exact truth.'
        prior_path=p.parent/'prior_ark_plan.json';save(prior_path,prior_plan)
        module.analyze(prior_path,'prior_quiet_ark_h600',p.parent/'vs_prior_ark.json')
    return {'status':'written','seconds':seconds,'output':str(p.parent/'comparison.json'),'helper_sha256':HELPER_SHA,'accuracy_pass_claim':False,'same_accuracy_speedup_claim':False,'note':'No forecast or restart was launched by this observer.'}

def publish_terminal_progress(outcome, checks):
    """Use the controller receipt as authority when the observer exits."""
    steps = outcome['actual_outer_step_count']
    save(ROOT/'progress.json', {
        'observed_at_local': datetime.now().astimezone().isoformat(),
        'status': outcome['status'],
        'forecast_return_code': outcome['return_code'],
        'stop_reason': outcome['stop_reason'],
        'completed_outer_step_log_records': steps,
        'model_seconds_from_completed_step_logs': steps * 600,
        'target_steps': 720,
        'model_target_seconds': 432000,
        'elapsed_seconds': outcome['elapsed_seconds'],
        'wall_seconds': outcome['wall_seconds'],
        'cost_eligible': outcome['cost_eligible'],
        'five_day_complete': outcome['status'] == 'completed' and steps == 720,
        'accuracy_pass_claim': False,
        'checkpoint_results': checks,
    })

def main():
    assert sha(HELPER)==HELPER_SHA
    launch=json.loads((ROOT/'launch_receipt.json').read_text());pid=launch['runner_pid'];checks=json.loads((ROOT/'checkpoint_results.json').read_text()) if (ROOT/'checkpoint_results.json').exists() else {}
    runner_script=next((arg for arg in launch['command'] if arg.endswith('.py')), None)
    if runner_script is None:raise ValueError('launch receipt needs the actual Python controller script')
    cap_seconds=json.loads((ROOT/'manifest.json').read_text())['limits']['case_wall_seconds']
    deadline=json.loads((CASE/'launcher_pid.json').read_text())['wall_start_ns']/1e9+cap_seconds
    save(ROOT/'checkpoint_observer_launch.json',{'at_local':datetime.now().astimezone().isoformat(),'pid':os.getpid(),'source_sha256':sha(__file__),'read_only_to_forecast':True,'checkpoints_model_seconds':[21600,86400,432000],'runner_pid':pid,'no_model_or_retry':True})
    while True:
        try:
            for seconds in (21600,86400):
                if str(seconds) not in checks and closed(seconds):
                    try:checks[str(seconds)]=analyze(seconds)
                    except Exception as exc:checks[str(seconds)]={'status':'analysis_error','reason':repr(exc),'traceback':traceback.format_exc()}
                    save(ROOT/'checkpoint_results.json',checks)
            receipt=CASE/'forecast_receipt.json'
            if receipt.exists():
                outcome=json.loads(receipt.read_text())
                if outcome['status']=='completed':
                    try:checks['432000']=analyze(432000,final=True)
                    except Exception as exc:checks['432000']={'status':'analysis_error','reason':repr(exc),'traceback':traceback.format_exc()}
                save(ROOT/'checkpoint_results.json',checks)
                publish_terminal_progress(outcome, checks)
                save(ROOT/'checkpoint_observer_receipt.json',{'at_local':datetime.now().astimezone().isoformat(),'forecast_status':outcome['status'],'forecast_return_code':outcome['return_code'],'forecast_stop_reason':outcome['stop_reason'],'checkpoints':checks,'no_model_or_retry':True});return
            out=CASE/'rsl.out.0000';text=out.read_text(errors='replace') if out.exists() else '';steps=text.count('Timing for main:')
            save(ROOT/'progress.json',{'observed_at_local':datetime.now().astimezone().isoformat(),'status':'running_no_completion_claim','accepted_outer_step_log_records':steps,'model_seconds_from_completed_step_logs':steps*600,'target_steps':720,'model_target_seconds':432000,'elapsed_cap_seconds':cap_seconds,'checkpoint_results':checks})
            q=subprocess.run(['/bin/ps','-p',str(pid),'-o','command='],capture_output=True,text=True,timeout=5)
            if q.returncode or runner_script not in q.stdout or time.time()>deadline+300:
                save(ROOT/'checkpoint_observer_receipt.json',{'at_local':datetime.now().astimezone().isoformat(),'status':'controller_missing_or_receipt_not_available','checkpoints':checks,'no_signals_no_retry':True});return
        except Exception as exc:
            save(ROOT/'checkpoint_observer_error.json',{'at_local':datetime.now().astimezone().isoformat(),'reason':repr(exc),'traceback':traceback.format_exc(),'forecast_untouched':True})
        time.sleep(30)
if __name__=='__main__':main()
