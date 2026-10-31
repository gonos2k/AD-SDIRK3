#!/usr/bin/env python3
"""After A1 timing completes, render per-level owned XY RMS of first-direction defects."""
import hashlib,json,struct,sys
from pathlib import Path
import numpy as np
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
E=Path(__file__).resolve().parent; analysis=E/'a4_defect_analysis'; receipt=json.loads((analysis/'receipt.json').read_text());R=E/'a4_case_draft/runs'
if sys.argv[1:]!=['--after-a1-complete']:raise SystemExit('Requires --after-a1-complete; no vector arrays read')
a1=json.loads((R/'A1/result.json').read_text());assert a1['status']=='completed' and a1['a4_eligible'] and a1['model_completed'],'A1 not completed'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest();arrays={}
for label in ('A0','B0'):
 d=R/label;r=json.loads((d/'result.json').read_text());p=Path(r['pair']['path']);b=p.read_bytes();assert sha(p)==receipt['pins_and_forecast'][label]['pair_sha256']==r['pair']['file_sha256'] and b[:8]==b'SDIRKA41';n=struct.unpack('<Q',b[8:16])[0];assert len(b)==16+16*n;v=np.frombuffer(b,dtype='<f8',count=n,offset=16);w=np.frombuffer(b,dtype='<f8',count=n,offset=16+8*n);assert np.isfinite(v).all() and np.isfinite(w).all();blocks={x['name']:x for x in r['a4']['blocks']}
 for name,shape in [('rw',(81,65,41)),('rv',(82,64,41)),('t',(81,64,41))]:
  block=blocks[name];sl=slice(block['start'],block['start']+block['size']);x=v[sl].reshape(shape);y=w[sl].reshape(shape)
  if name=='rw':x=x[:80,1:,:40];y=y[:80,1:,:40]
  elif name=='rv':x=x[:81,:,:40];y=y[:81,:,:40];assert np.all(x[0]==0) and np.all(x[-1]==0) and np.all(y[0]==0) and np.all(y[-1]==0)
  else:x=x[:80,:,:40];y=y[:80,:,:40]
  arrays[label,name]=(np.sqrt(np.mean(x*x,axis=(0,2))),np.sqrt(np.mean((x-y)**2,axis=(0,2))))
for name in ('rw','rv','t'):assert np.array_equal(arrays['A0',name][0],arrays['B0',name][0])
fig,axs=plt.subplots(1,3,figsize=(13,7),constrained_layout=True,sharey=False)
for ax,name,title,unit in zip(axs,('rw','rv','t'),('W-rate','V-rate','T-rate'),('m s⁻²','m s⁻²','K s⁻¹')):
 k=np.arange(1,65) if name=='rw' else np.arange(64)
 for label,color,style in [('A0','tab:blue','-'),('B0','tab:orange','-')]:
  x=arrays[label,name][1];ax.plot(np.where(x>0,x,np.nan),k,color=color,linestyle=style,label=label+' |v−w| RMS')
 x=arrays['A0',name][0];ax.plot(np.where(x>0,x,np.nan),k,color='0.5',linestyle='--',label='common v RMS')
 ax.set_xscale('log');ax.set_title(title);ax.set_xlabel('XY RMS ('+unit+')');ax.set_ylabel('Native W-face k (bottom omitted, top included)' if name=='rw' else 'Native mass-level k');ax.grid(alpha=.25);ax.legend(fontsize=8)
fig.suptitle('First Arnoldi direction: arbitrary-normalized physical rate coordinates\nTop W face: A0 99.8%, B0 2.1% of owned W-defect squared norm; B0 V shape error 0.745')
fig.savefig(analysis/'vertical_rms_profiles.png',dpi=150);plt.close(fig)
(analysis/'vertical_rms_profiles_receipt.json').write_text(json.dumps({'scope':'one captured first direction; not a long-term noise explanation','source_receipt_sha256':sha(analysis/'receipt.json'),'source_A1_result_sha256':sha(R/'A1/result.json'),'script_sha256':sha(Path(__file__)),'plot_sha256':sha(analysis/'vertical_rms_profiles.png'),'W_native_k':'1..64, bottom excluded and dynamic top included','V_T_native_k':'0..63 mass levels','metric':'physical rate-coordinate XY RMS separately for each staggered component; no mixed-unit energy'},indent=2)+'\n')
