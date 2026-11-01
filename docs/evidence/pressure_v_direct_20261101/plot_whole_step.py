#!/usr/bin/env python3
"""Visualize one existing instrumented pair; no model execution."""
from pathlib import Path
import hashlib,json
import numpy as np
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
E=Path(__file__).resolve().parent;out=E/'comparison_analysis';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();audit=json.loads((E/'whole_step_timing_audit.json').read_text());data={};hashes={}
for case in ('control','candidate'):
 source=audit['data'][case]
 for path,digest in source['sha256'].items():assert sha(path)==digest,path;hashes[path]=digest
 d=json.loads((E/'runs'/case/'result.json').read_text());assert d['status']=='completed' and d['model_completed'] and d['return_code']==0
 rows=d['existing_jvp_summaries']['whole_step_records'];assert len(rows)==6
 assert rows[4]['calls']==source['fifth_jvp_calls'] and abs(rows[4]['time_ms']-source['fifth_jvp_ms'])<1e-6
 data[case]={'counts':[x['calls'] for x in rows],'seconds':[x['time_ms']/1000 for x in rows],**{k:source[k] for k in ('rsl_main_step_s','rank_real_s','rank_cpu_s')}}
labels=['S2 N0','S2 N1','S3 N0','S3 N1','S4 N0','S4 N1'];xs=np.arange(6);width=.38;fig,ax=plt.subplots(2,2,figsize=(13,8),constrained_layout=True)
for offset,case,color in [(-width/2,'control','#467aa5'),(width/2,'candidate','#d17a45')]:
 ax[0,0].bar(xs+offset,data[case]['counts'],width,label=case,color=color);ax[0,1].bar(xs+offset,data[case]['seconds'],width,label=case,color=color)
for a in ax[0]:a.set_xticks(xs,labels);a.set_xlabel('Stage and Newton correction');a.legend()
ax[0,0].set_ylabel('JVP calls');ax[0,0].set_title('Six linear solves: JVP counts')
ax[0,1].set_ylabel('Reported JVP elapsed (s)');ax[0,1].set_title('Six linear solves: JVP elapsed time')
ax[1,0].bar(['control','candidate'],[data[x]['rsl_main_step_s'] for x in data],color=['#467aa5','#d17a45']);ax[1,0].set_ylabel('Seconds');ax[1,0].set_title('RSL whole-step elapsed')
x=np.arange(2)
for offset,key,label in [(-.18,'rank_real_s','rank elapsed'),(.18,'rank_cpu_s','rank user + system CPU')]:ax[1,1].bar(x+offset,[data[c][key] for c in ('control','candidate')],.36,label=label)
ax[1,1].set_xticks(x,['control','candidate']);ax[1,1].set_ylabel('Seconds');ax[1,1].set_title('Rank process, separate clock scope');ax[1,1].legend()
fig.suptitle('One instrumented pair: first-point screen positive; whole step slower\nDifferent clock scopes are not additive; later state identity and delay cause are unknown')
fig.savefig(out/'whole_step_context.png',dpi=160);plt.close(fig)
receipt={'scope':'One sequential instrumented pair. First-point screen remains positive; no overall speedup, adoption or causal delay claim. JVP is nested, clocks have different windows and are not additive. Later state/operator identity unknown.','data':data,'source_hashes':hashes,'audit_sha256':sha(E/'whole_step_timing_audit.json'),'script_sha256':sha(Path(__file__)),'figure_sha256':sha(out/'whole_step_context.png')};(out/'whole_step_context_receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
