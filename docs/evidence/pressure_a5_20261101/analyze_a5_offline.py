#!/usr/bin/env python3
"""Read frozen A5 once complete; no model execution or fallback synthesis."""
import argparse,csv,hashlib,json,math,struct
from pathlib import Path
import numpy as np
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt

H=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
def records(path):
 data=path.read_bytes();assert len(data)>=16 and data[:8]==b'SDIRKA53'; count=struct.unpack_from('<Q',data,8)[0];assert count<=128;pos=16;out={}
 for _ in range(count):
  assert pos+80<=len(data); name=data[pos:pos+32].split(b'\0',1)[0].decode('ascii');item,nd,*rest=struct.unpack_from('<II4qQ',data,pos+32);dims=rest[:4];size=rest[4];pos+=80
  assert name and name not in out and item in (4,8) and nd<=4 and all(x>=0 for x in dims[:nd]) and all(x==0 for x in dims[nd:]);n=math.prod(dims[:nd]);assert size==n*item and pos+size<=len(data)
  a=np.frombuffer(data,dtype='<f4' if item==4 else '<f8',count=n,offset=pos).reshape(tuple(dims[:nd]));assert np.isfinite(a).all(),name;out[name]=a;pos+=size
 assert pos==len(data);return out

def norm(a):return float(np.linalg.norm(a))
def stats(a,b):
 na=norm(a);nb=norm(b);dot=float(np.sum(a*b));alpha=dot/(nb*nb) if nb else None
 return {'mismatch_l2':na,'candidate_l2':nb,'unit_squared_ratio':float((norm(a-b)/na)**2) if na else None,'cosine':dot/(na*nb) if na and nb else None,'fit_alpha_descriptive':alpha,'orthogonal_fraction_descriptive':norm(a-alpha*b)/na if na and nb else None}
def main():
 ap=argparse.ArgumentParser();ap.add_argument('--case',type=Path,required=True);ap.add_argument('--out',type=Path,required=True);opt=ap.parse_args();d=opt.case.resolve();out=opt.out.resolve();assert not out.exists();result=json.loads((d/'result.json').read_text());raw=d/'pressure_a5_direction.bin';act=d/'pressure_a5_actions.bin';assert raw.is_file();x=records(raw)
 names=('v_scaled','z_scaled','Az_scaled','S_diag','P0z_physical','eos_t','constants');assert all(k in x for k in names)
 S=x['S_diag'].ravel();v=x['v_scaled'].ravel();z=x['z_scaled'].ravel();az=x['Az_scaled'].ravel();p=x['P0z_physical'].ravel();assert all(a.shape==S.shape for a in (v,z,az,p)) and np.all(S>0)
 V=S*v;Z=S*z;A=S*az;e=V-A;q=V-p;m=p-A;closure=e-q-m
 ny,nz,nx=x['eos_t'].shape;assert (ny,nz,nx)==(81,64,41);n=V.size
 dims=[(81,64,42),(82,64,41),(81,65,41),(81,65,41),(81,64,41),(81,41)];sizes=[math.prod(t) for t in dims];assert sum(sizes)==n;starts=np.cumsum([0]+sizes);blocks=['ru','rv','rw','ph','t','mu']
 report={'scope':'one first Arnoldi direction, physical rate coordinates are arbitrarily normalized; not mixed-units energy, no cost or late forecast claim','result_status':result.get('status'),'model_completed':result.get('model_completed'),'result_sha256':H(d/'result.json'),'raw_sha256':H(raw),'raw_records':{k:{'shape':list(a.shape),'dtype':str(a.dtype)} for k,a in x.items()},'global_scaled_inverse_norm':norm(v-p/S)/norm(v) if norm(v) else None,'packed_closure':[],'hypothesis':'INCONCLUSIVE','actions_available':act.is_file()}
 for i,name in enumerate(blocks):
  sl=slice(starts[i],starts[i+1]);scale=max(float(np.max(np.abs(t[sl]))) for t in (V,p,A));bound=128*np.finfo(float).eps*scale;err=float(np.max(np.abs(closure[sl])));report['packed_closure'].append({'name':name,'max_abs':err,'bound':bound,'passed':bool(err<=bound)})
 # Persist valid capture and closure even if helper/actions fail. No fabricated actions.
 out.mkdir();(out/'capture_closure.json').write_text(json.dumps(report,indent=2)+'\n')
 if not act.is_file():return
 y=records(act);required={'Qz_physical','V_Jphi','V_Jp','V_Jalpha','V_Jfwad','V_P0_difference'};assert set(y)==required
 rv=slice(starts[1],starts[2]);shape=dims[1];vv=V[rv].reshape(shape);zz=Z[rv].reshape(shape);pp=p[rv].reshape(shape);aa=A[rv].reshape(shape); mm=pp-aa; a=float(x['constants'].ravel()[-1]); assert a>0
 assert all(y[k].shape==shape for k in required-{'Qz_physical'}); assert np.array_equal(y['V_P0_difference'],zz-pp)
 phi=y['V_Jphi'];press=y['V_Jp'];alpha=y['V_Jalpha'];fw=y['V_Jfwad'];corr=a*(phi+press+alpha)-(zz-pp);corr_fw=a*fw-(zz-pp);core=lambda t:t[1:80,:,0:40];aderr=np.abs(core(phi+press+alpha-fw));ad_ok=bool(np.all(aderr<=1e-12+1e-9*np.abs(core(fw))))
 # Independent NumPy EOS and primary V derivative, using projected Qz and captured actual geometry.
 qq=y['Qz_physical'].ravel();assert qq.shape==V.shape;ph=qq[starts[3]:starts[4]].reshape(dims[3]);tt=qq[starts[4]:starts[5]].reshape(dims[4]);mu=qq[starts[5]:starts[6]].reshape(dims[5]);constants=x['constants'].ravel();rd,cv,cp,p0,t0,rdx,rdy,vel_eps,step=map(float,constants);assert step==a
 eos_t=x['eos_t'];mass=x['eos_mu']+x['mu_base'];level_mass=x['c1h'].ravel()[:nz][None,:,None]*mass[:,None,:]+x['c2h'].ravel()[:nz][None,:,None];alt=x['alt'];pfull=x['p_pert']+x['p_base'];T=eos_t+t0;assert np.all(level_mass>0) and np.all(alt>0) and np.all(T>0)
 da=x['rdnw'].ravel()[:nz][None,:,None]*(ph[:,1:,:]-ph[:,:-1,:])/level_mass-alt*x['c1h'].ravel()[:nz][None,:,None]*mu[:,None,:]/level_mass;dp=(cp/cv)*pfull*(tt/T-da/alt)
 # Only physical interior V rows; native ydiff/average on adjacent mass rows.
 factor=-x['cqv_at_v'][1:80,:,0:40]*x['msfvy'][1:80,0:40][:,None,:]*rdy
 yp=.5*((ph[1:80,1:,0:40]-ph[:79,1:,0:40])+(ph[1:80,:-1,0:40]-ph[:79,:-1,0:40]))
 independently={'V_Jphi':factor*yp,'V_Jp':factor*.5*(alt[1:80,:,0:40]+alt[:79,:,0:40])*(dp[1:80,:,0:40]-dp[:79,:,0:40]),'V_Jalpha':factor*.5*(da[1:80,:,0:40]+da[:79,:,0:40])*(pfull[1:80,:,0:40]-pfull[:79,:,0:40])}
 comparisons={k:{'max_abs':float(np.max(np.abs(v0-core(y[k])))),'norm':norm(v0-core(y[k]))} for k,v0 in independently.items()}
 # Source performs ppert differences then pbase differences; report roundoff, do not assert bit equality.
 measures=stats(core(mm),core(corr));fwmeasures=stats(core(mm),core(corr_fw));independent_sum=a*sum(independently.values())-core(zz-pp)
 independent_stats=stats(core(mm),independent_sum)
 wall=np.concatenate((mm[0,:,:40].ravel(),mm[80,:,:40].ravel()));xalias=mm[:,:,40];yalias=mm[81,:,:40];wm=m[starts[2]:starts[3]].reshape(dims[2])[:80,:,:40];
 report.update(actions_sha256=H(act),actions_records={k:{'shape':list(v0.shape),'dtype':str(v0.dtype)} for k,v0 in y.items()},captured_constants=constants.tolist(),source_report=result.get('A5'),source_decomposition=result.get('decomposition'),result_attribution=result.get('attribution'),ad_pointwise_independent=ad_ok,source_evidence_scope='FD arrays not serialized; only source-reported flags available',numpy_analytic_comparisons=comparisons,unit_score=measures,fwad_unit_score=fwmeasures,numpy_analytic_unit_score=independent_stats,ownership={'V_interior_l2':norm(core(mm)),'V_walls_l2':norm(wall),'V_x_alias_l2':norm(xalias),'V_y_alias_l2':norm(yalias),'W_top_l2':norm(wm[:,64,:]),'W_interior_l2':norm(wm[:,1:64,:])})
 obs=result.get('A5') or {};checks=result.get('attribution') or {};eligible=bool(result.get('status')=='completed' and obs.get('eligible') is True and ad_ok and all(b['passed'] for b in report['packed_closure']) and checks.get('outcome') not in (None,'INCONCLUSIVE'))
 if eligible:report['hypothesis']='ATTRIBUTION_THRESHOLD_MET' if measures['unit_squared_ratio']<=.5 else 'ATTRIBUTION_GATE_FAILED_STOP'
 report['eligibility_gate_passed']=eligible
 # Plot signed components at x=20 on native interior V rows, native mass-level k; stage height is unavailable.
 series=[('mV',core(mm)),('unit candidate c',core(corr)),('a Jphi',core(a*phi)),('a Jp',core(a*press)),('a Jalpha',core(a*alpha)),('mV − c',core(mm-corr))];limit=max(float(np.max(np.abs(arr[:,:,20]))) for _,arr in series) or 1.;fig,axes=plt.subplots(2,3,figsize=(14,8),constrained_layout=True)
 for ax,(title,arr) in zip(axes.flat,series):
  im=ax.imshow(arr[:,:,20].T,origin='lower',aspect='auto',cmap='RdBu_r',vmin=-limit,vmax=limit,extent=(1,79,0,63));ax.set_title(title);ax.set_xlabel('physical interior V-face j');ax.set_ylabel('native mass-level k');fig.colorbar(im,ax=ax,label='arbitrary-normalized m/s²')
 fig.suptitle('A5 first-direction V mismatch and unit-coefficient terms; common signed scale, x index 20');fig.savefig(out/'signed_components.png',dpi=150);plt.close(fig)
 fig,ax=plt.subplots(figsize=(7,7),constrained_layout=True)
 for title,arr in series:
  line=np.sqrt(np.mean(arr*arr,axis=(0,2)));ax.plot(np.where(line>0,line,np.nan),np.arange(64),label=title)
 ax.set_xscale('log');ax.set_xlabel('XY RMS, arbitrary-normalized m/s²');ax.set_ylabel('native mass-level k');ax.legend();ax.grid(alpha=.3);fig.savefig(out/'vertical_rms.png',dpi=150);plt.close(fig)
 report['scope_notes']='c=a*(Jphi+Jp+Jalpha)-(z-P0z)_V; raw action J units m/s3, multiplied by a; plotted rate m/s2. Single direction and no late-noise causation.'
 (out/'analysis.json').write_text(json.dumps(report,indent=2,allow_nan=False)+'\n');print(json.dumps({'hypothesis':report['hypothesis'],'unit_score':measures,'output':str(out)},indent=2))
if __name__=='__main__':main()
