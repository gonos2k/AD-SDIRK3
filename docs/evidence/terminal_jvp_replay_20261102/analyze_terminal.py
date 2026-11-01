#!/usr/bin/env python3
"""Recompute descriptive local timings; never changes the registered run verdict."""
import argparse,csv,hashlib,json,math,statistics
from pathlib import Path

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def stats(values):
 return {'n':len(values),'mean_ms':statistics.mean(values)*1000,'median_ms':statistics.median(values)*1000,'min_ms':min(values)*1000,'max_ms':max(values)*1000,'max_over_min':max(values)/min(values),'sample_cv':statistics.stdev(values)/statistics.mean(values) if len(values)>1 else None}
def main():
 parser=argparse.ArgumentParser();parser.add_argument('evidence',type=Path);args=parser.parse_args();e=args.evidence
 result=json.loads((e/'run/result.json').read_text());d=json.loads((e/'run/terminal_jvp_probe.json').read_text());assert d==result['diagnostic']
 assert result['status']=='inconclusive' and result['errors']==['clock_gap'] and result['launcher_return_code']==86
 assert d['status']=='terminal_diagnostic_passed' and d['sample_count']==6 and d['forecast_complete'] is False
 samples=[d['original']]+d['repeats'];assert len(samples)==6
 for i,s in enumerate(samples):
  assert s['valid'] is True and s['clock_valid'] is True and s['path_count']==i+1
  assert s['fallback_count']==s['internal_fd_count']==0 and s['context']==samples[0]['context']
  assert all(math.isfinite(s[k]) and s[k]>0 for k in ('wall_seconds','cpu_seconds'))
  if i:assert all(s[k] is True for k in ('primal_exact','tangent_exact','inputs_unchanged'))
 out=e/'analysis';out.mkdir(exist_ok=True)
 with (out/'samples.csv').open('w',newline='') as f:
  writer=csv.writer(f,lineterminator='\n');writer.writerow(['sample','kind','wall_ms','process_cpu_ms','arithmetic_wall_minus_cpu_ms','primal_exact','tangent_exact','rhs_cumulative','fallback_cumulative'])
  for i,s in enumerate(samples):writer.writerow([i,'original' if i==0 else 'repeat',1000*s['wall_seconds'],1000*s['cpu_seconds'],1000*(s['wall_seconds']-s['cpu_seconds']),True if i==0 else s['primal_exact'],True if i==0 else s['tangent_exact'],s['path_count'],s['internal_fd_count']])
 summary={'registered_run_outcome':result['status'],'registered_errors':result['errors'],'new_forecast_complete':False,'cost_eligible':False,'within_run_value_path_block':d['status'],'run_result_sha256':sha(e/'run/result.json'),'terminal_json_sha256':sha(e/'run/terminal_jvp_probe.json'),'source_commit':(e/'source_commit.txt').read_text().strip(),'all_six':{k:stats([s[k] for s in samples]) for k in ('wall_seconds','cpu_seconds')},'five_additional':{k:stats([s[k] for s in samples[1:]]) for k in ('wall_seconds','cpu_seconds')},'global_clock_gap_seconds':result['clock_gap_seconds'],'qualification':'One fixed observed context and direction. Values are exact within this sample. First selected call is not process-cold; intercall validation conditions caches. Process CPU and steady elapsed have matched call scopes; global registered clock gate failed. No reproducible performance, waiting-cause, P0/P1 or continuation claim.'}
 import matplotlib;matplotlib.use('Agg')
 import matplotlib.pyplot as plt
 import numpy as np
 plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False})
 fig,ax=plt.subplots(figsize=(9.6,5.2));x=np.arange(6);bw=.36
 ax.bar(x-bw/2,[s['wall_seconds']*1000 for s in samples],bw,label='Steady elapsed',color='#3268a8')
 ax.bar(x+bw/2,[s['cpu_seconds']*1000 for s in samples],bw,label='Process user + system CPU',color='#e59b35')
 ax.set_xticks(x,['Original']+[f'Repeat {i}' for i in range(1,6)]);ax.set_ylabel('Matched call time (ms)');ax.set_ylim(0,max(s['wall_seconds']*1000 for s in samples)*1.20);ax.legend(frameon=False,loc='upper left');ax.grid(axis='y',alpha=.18);ax.set_axisbelow(True)
 ax.set_title('Native forward-AD JVP at stage 4 / Newton 0\nSame fixed U, reference and physical direction; primal/JVP exact; FD/fallback 0',loc='left',fontsize=12)
 for i,s in enumerate(samples):ax.text(i,max(s['wall_seconds'],s['cpu_seconds'])*1000+9,f"{s['wall_seconds']*1000:.1f} / {s['cpu_seconds']*1000:.1f}",ha='center',fontsize=8.5)
 fig.text(.10,.045,'Registered run: INCONCLUSIVE (global wall-clock minus monotonic = -1.794 s).\nLocal timings are descriptive; no forecast, continuation, P0/P1 or speed acceptance.',fontsize=9,color='#9c3633')
 fig.subplots_adjust(left=.10,right=.98,top=.79,bottom=.23);fig.savefig(out/'terminal_jvp_timing.png',dpi=170);plt.close(fig)
 summary['artifacts']={n:{'bytes':(out/n).stat().st_size,'sha256':sha(out/n)} for n in ['samples.csv','terminal_jvp_timing.png']}
 (out/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n');print(json.dumps(summary['five_additional'],indent=2))
if __name__=='__main__':main()
