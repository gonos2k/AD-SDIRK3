#!/usr/bin/env python3
"""Render recorded aggregate rank rusage for one diagnostic pair."""
from pathlib import Path
import hashlib,json,re
import numpy as np
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
E=Path(__file__).resolve().parent;out=E/'analysis';sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();data={};hashes={}
for case in ('control','candidate'):
 p=E/'runs'/case/'rank_rusage.log';s=p.read_text();hashes[str(p)]=sha(p);m=re.search(r'([\d.]+)\s+real\s+([\d.]+)\s+user\s+([\d.]+)\s+sys',s);assert m
 def get(name):
  found=re.findall(r'^\s*(\d+)\s+'+re.escape(name)+r'\s*$',s,re.M);assert len(found)==1,name;return int(found[0])
 data[case]={'real_s':float(m[1]),'cpu_s':float(m[2])+float(m[3]),'instructions':get('instructions retired'),'cycles':get('cycles elapsed'),'involuntary_switches':get('involuntary context switches'),'maxrss_bytes':get('maximum resident set size'),'peak_footprint_bytes':get('peak memory footprint')}
fig,ax=plt.subplots(2,2,figsize=(12,8),constrained_layout=True);x=np.arange(2);cases=('control','candidate');w=.36
for off,key,label in [(-w/2,'real_s','rank elapsed'),(w/2,'cpu_s','user + system CPU')]:ax[0,0].bar(x+off,[data[c][key] for c in cases],w,label=label)
ax[0,0].set_title('Rank elapsed and CPU');ax[0,0].set_ylabel('Seconds');ax[0,0].legend()
for off,key,label in [(-w/2,'instructions','instructions retired'),(w/2,'cycles','cycles elapsed')]:ax[0,1].bar(x+off,[data[c][key]/1e9 for c in cases],w,label=label)
ax[0,1].set_title('Recorded aggregate counts');ax[0,1].set_ylabel('Billions');ax[0,1].legend()
ax[1,0].bar(x,[data[c]['involuntary_switches'] for c in cases]);ax[1,0].set_yscale('log');ax[1,0].set_title('Involuntary switches');ax[1,0].set_ylabel('Count (log scale)')
for off,key,label in [(-w/2,'maxrss_bytes','max RSS'),(w/2,'peak_footprint_bytes','peak footprint')]:ax[1,1].bar(x+off,[data[c][key]/1024**3 for c in cases],w,label=label)
ax[1,1].set_title('Recorded peak memory measures');ax[1,1].set_ylabel('GiB');ax[1,1].legend()
for a in ax.flat:a.set_xticks(x,cases)
fig.suptitle('One diagnostic pair: whole-rank aggregates, not fifth-solve counters\nElapsed, CPU and counters differ in scope; no cause or measured frequency inferred');fig.savefig(out/'aggregate_runtime.png',dpi=150);plt.close(fig)
(out/'aggregate_runtime_receipt.json').write_text(json.dumps({'scope':'One rank per case, whole-process aggregate counters; no causal attribution; instructions are not synonymous with CPU time; cycles divided by CPU time is not a measured frequency.','data':data,'source_hashes':hashes,'script_sha256':sha(Path(__file__)),'figure_sha256':sha(out/'aggregate_runtime.png')},indent=2)+'\n')
