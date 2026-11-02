#!/usr/bin/env python3
"""Inspect the frozen local prepared-A cost screen; never changes its verdict."""
import argparse,csv,hashlib,json,math
from pathlib import Path

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 ap=argparse.ArgumentParser();ap.add_argument('evidence',type=Path);args=ap.parse_args();e=args.evidence
 r=json.loads((e/'run/result.json').read_text());d=json.loads((e/'run/matched_pressure_probe.json').read_text());assert r['diagnostic']==d
 out=e/'analysis';out.mkdir(exist_ok=True);rows=[]
 for idx,a in enumerate(d.get('arms',[])):
  row={'arm':['A0','B0','B1','A1'][idx],'kind':a['kind'],'solved':a['solved'],'native_checked':a['native_checked'],'iterations':a['iterations'],'A_calls':a['A_calls'],'M_calls':a['M_calls'],'inner_arnoldi':a['inner_arnoldi'],'pressure_subsolves':a['pressure_subsolves'],'native_scaled_rel':a['native_scaled_rel'],'guard_events':a['guard_events'],'sketch_count':a['sketch_count']}
  for key in ['setup','solve','A_time','M_time','sketch_overhead','guard_overhead','native_residual','charged_total','accounted_total']:
   for clock in ['wall','cpu']:row[f'{key}_{clock}_s']=a[key][clock]
  rows.append(row)
 if rows:
  with (out/'arms.csv').open('w',newline='') as f:
   writer=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');writer.writeheader();writer.writerows(rows)
 ratios={}
 if len(rows)==4 and all(x['solved'] and x['native_checked'] for x in rows):
  for metric in ['charged_total','accounted_total']:
   ratios[metric]={clock:[rows[i][f'{metric}_{clock}_s']/rows[j][f'{metric}_{clock}_s'] if rows[j][f'{metric}_{clock}_s']>0 else None for i,j in [(0,1),(3,2)]] for clock in ['wall','cpu']}
 ceiling={}
 if len(rows)==4 and all(x['solved'] and x['native_checked'] for x in rows):
  for clock in ['wall','cpu']:
   ceiling[clock]=[]
   for i,j in [(0,1),(3,2)]:
    denominator=rows[j][f'charged_total_{clock}_s']-rows[j][f'setup_{clock}_s']-rows[j][f'M_time_{clock}_s']
    ceiling[clock].append(rows[i][f'charged_total_{clock}_s']/denominator)
 summary={'source_commit':(e/'source_commit.txt').read_text().strip(),'registered_status':r['status'],'cost_decision':r['cost_decision'],'errors':r['errors'],'cpp_status':d['status'],'phase':d['phase'],'cpp_error':d['error'],'forecast_complete':False,'scope':d.get('scope'),'clock':r.get('clock'),'paired_P0_over_P1':ratios,'conditional_P1_zero_M_setup_and_wrapper_speed_ceiling':ceiling,'ceiling_assumption':'Hold observed A costs, counts and all other costs fixed; idealize P1 M setup and every wrapped M application as free. Arithmetic bound, not an achievable or future performance prediction.','within_one_process_not_independent_repeats':True,'modeled_cost_not_whole_ABBA_elapsed':True,'raw_result_sha256':sha(e/'run/result.json'),'raw_diagnostic_sha256':sha(e/'run/matched_pressure_probe.json')}
 if len(rows)==4 and all(x['solved'] and x['native_checked'] for x in rows):
  import matplotlib;matplotlib.use('Agg')
  import matplotlib.pyplot as plt
  import numpy as np
  plt.rcParams.update({'font.size':10,'axes.spines.top':False,'axes.spines.right':False})
  fig,axes=plt.subplots(1,2,figsize=(11,5.2));labels=[x['arm']+' / '+x['kind'] for x in rows];colors=['#99a7b5','#537eb0','#e5a64c','#65a694']
  for ax,clock in zip(axes,['wall','cpu']):
   common=d['common'][clock]+d['tape_preparation'][clock];parts=[[common]*4,[a['setup'][clock] for a in d['arms']],[a['solve'][clock] for a in d['arms']],[a['native_residual'][clock] for a in d['arms']]];bottom=np.zeros(4)
   for data,name,color in zip(parts,['Common + tape (fully charged)','Fresh M setup','Instrumented FGMRES','Native residual'],colors):ax.bar(labels,data,bottom=bottom,label=name,color=color);bottom+=data
   ax.set_ylabel('Seconds');ax.set_title('Modeled standalone '+('elapsed' if clock=='wall' else 'process CPU')+' cost',loc='left');ax.grid(axis='y',alpha=.18);ax.set_axisbelow(True)
   for i,v in enumerate(bottom):ax.text(i,v+.02*max(bottom),f'{v:.3f}',ha='center',fontsize=9)
   ax.set_ylim(0,max(bottom)*1.2)
  handles,names=axes[0].get_legend_handles_labels();fig.legend(handles,names,frameon=False,fontsize=8,ncol=4,loc='upper center',bbox_to_anchor=(.53,.92));fig.suptitle('Same prepared A, b, x0 and S; fresh P0 / P1 / P1 / P0',x=.08,ha='left',fontsize=12)
  fig.text(.08,.03,'Cost verdict: '+r['cost_decision']+'. Modeled totals, not total experiment/forecast or native-FWAD speed.\nCommon preparation is measured once and fully charged to each arm; verification work is reported separately.',fontsize=9,color='#793f2d');fig.subplots_adjust(left=.08,right=.98,top=.75,bottom=.22,wspace=.23);fig.savefig(out/'matched_cost.png',dpi=170);plt.close(fig)
 (out/'summary.json').write_text(json.dumps(summary,indent=2,allow_nan=False)+'\n');print(json.dumps({k:summary[k] for k in ['registered_status','cost_decision','cpp_status','phase','paired_P0_over_P1']},indent=2))
if __name__=='__main__':main()
