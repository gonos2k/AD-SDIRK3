#!/usr/bin/env python3
"""Offline first-Arnoldi physical rate-coordinate defect; no model execution."""
import argparse,csv,hashlib,importlib.util,json,math,struct
from pathlib import Path
import numpy as np
from netCDF4 import Dataset
from scipy.fft import dct,idct,dst,idst
import matplotlib;matplotlib.use('Agg')
import matplotlib.pyplot as plt
E=Path(__file__).resolve().parent; source='b0be4a94578739db91c77ee4d80dcc739483e03c'; regsha='a5a6e2c3b8143aac70e48e0084c5bdebad03455df599f0ca6230acfc081215b7'; RK=Path('/Users/yhlee/AD-SDIRK3-review-evidence/noise-cost-coupling-20261031/modal/runs/baseline_rk3'); helperpath=Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_screen/benchmark_wrf_accuracy_cost.py')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
rms=lambda x:float(np.sqrt(np.mean(x*x)))
parser=argparse.ArgumentParser();parser.add_argument('--a0',type=Path,required=True);parser.add_argument('--b0',type=Path,required=True);parser.add_argument('--out',type=Path,required=True);args=parser.parse_args();cases={'A0':args.a0.resolve(),'B0':args.b0.resolve()};results={};buffers={};observations={};pinmeta={}
assert sha(E/'a4_registration.json')==regsha;registration=json.loads((E/'a4_registration.json').read_text());assert registration['source_commit']==source and registration['status']=='registered'
for label,d in cases.items():
 result=json.loads((d/'result.json').read_text());assert result['case']==label and result['status']=='completed' and result['a4_eligible'] and result['model_completed'] and result['return_code']==0 and result['source_commit']==source and result['input_sha256']==registration['case']['input_sha256']
 obs=result['a4'];assert obs['eligible'] and obs['pair_written'] and obs['capture']=='fgmres_restart0_arnoldi0_raw_A_M_v_before_D_and_orthogonalization' and obs['physical_transform']=='S_times_scaled_krylov_vectors' and obs['pair_format']=='SDIRKA41,u64le_numel,physical_v_f64le,physical_w_f64le' and obs['block_scope']=='full_packed_block_not_owned_core'
 pinfile=d.parent.parent/(label+'_pins.json');intentfile=d.parent.parent/(label+'_intent.json');intent=json.loads(intentfile.read_text()); assert intent['source_commit']==source and intent['registration']['sha256']==regsha and sha(d/'wrfinput_d01')==result['input_sha256'] and sha(d/'namelist.input')==result['namelist_sha256']; pins=json.loads(pinfile.read_text())
 for p,h in pins.items():assert sha(p)==h,p
 assert result['executable_sha256']==intent['executable']['sha256']==sha(intent['executable']['path'])
 pair=Path(result['pair']['path']);assert pair.resolve().parent==d and sha(pair)==result['pair']['file_sha256'];data=pair.read_bytes();assert data[:8]==b'SDIRKA41';n=struct.unpack('<Q',data[8:16])[0];assert n and len(data)==16+16*n and n==result['pair']['numel'];v=np.frombuffer(data,dtype='<f8',count=n,offset=16);w=np.frombuffer(data,dtype='<f8',count=n,offset=16+8*n);assert np.isfinite(v).all() and np.isfinite(w).all();assert hashlib.sha256(data[16:16+8*n]).hexdigest()==result['pair']['v_sha256']
 results[label]=result;buffers[label]=(v,w);observations[label]=obs;pinmeta[label]={'result_sha256':sha(d/'result.json'),'pair_sha256':sha(pair),'intent_sha256':sha(intentfile),'pins_sha256':sha(pinfile)}
assert results['A0']['pair']['v_sha256']==results['B0']['pair']['v_sha256'] and buffers['A0'][0].tobytes()==buffers['B0'][0].tobytes()
assert results['A0']['preflight']['digest_fnv64']==results['B0']['preflight']['digest_fnv64']; assert results['A0']['namelist_sha256']==results['B0']['namelist_sha256']
blocks=observations['A0']['blocks'];other=observations['B0']['blocks'];assert [(b['name'],b['start'],b['size']) for b in blocks]==[(b['name'],b['start'],b['size']) for b in other]
assert [b['name'] for b in blocks]==['ru','rv','rw','ph','t','mu'];assert blocks[0]['start']==0 and all(a['start']+a['size']==b['start'] for a,b in zip(blocks,blocks[1:])) and blocks[-1]['start']+blocks[-1]['size']==len(buffers['A0'][0])
ny,nx,nz=80,40,64
# One-tile packed layout includes extra east and north aliases beyond
# 40x80 physical mass cells; see tile_unified_impl.cpp:7962-7963.
px,py=nx+1,ny+1
dims={'ru':(py,nz,px+1),'rv':(py+1,nz,px),'rw':(py,nz+1,px),'ph':(py,nz+1,px),'t':(py,nz,px),'mu':(py,px)}
assert all(math.prod(dims[b['name']])==b['size'] for b in blocks)
# Match WRF history ownership: U 80x64x40, V 81x64x40 with both
# physical wall faces, mass 80x40, W/PH bottom excluded and top included.
def own(vector,b):
 name=b['name'];a=vector[b['start']:b['start']+b['size']].reshape(dims[name])
 if name=='mu':return a[:ny,:nx]
 if name=='rv':a=a[:ny+1,:,:nx]
 elif name in ('rw','ph'):a=a[:ny,1:,:nx]
 else:a=a[:ny,:,:nx]
 return a.transpose(1,0,2)
def highband(a,name):
 axis=0 if name=='mu' else 1
 if name=='rv':
  assert np.all(a[:,0,:]==0) and np.all(a[:,-1,:]==0),'DST-I requires zero V wall faces';c=dst(a[:,1:-1,:],type=1,axis=1,norm='ortho');c[:,:39,:]=0;out=np.zeros_like(a);out[:,1:-1,:]=idst(c,type=1,axis=1,norm='ortho');return out
 c=dct(a,type=2,axis=axis,norm='ortho');sl=[slice(None)]*c.ndim;sl[axis]=slice(0,40);c[tuple(sl)]=0;return idct(c,type=2,axis=axis,norm='ortho')
units={'ru':'m s^-2','rv':'m s^-2','rw':'m s^-2','ph':'m2 s^-3','t':'K s^-1','mu':'Pa s^-1'};rows=[];defects={}
for label,(vf,wf) in buffers.items():
 for block in blocks:
  name=block['name'];v=own(vf,block);w=own(wf,block);e=v-w;nv=float(np.linalg.norm(v));nw=float(np.linalg.norm(w));ne=float(np.linalg.norm(e));dot=float(np.sum(v*w));alpha=dot/nv**2 if nv else None;band=highband(e,name);defects[(label,name)]=e
  rows.append({'case':label,'field':name,'units':units[name],'owned_points':v.size,'v_l2':nv,'v_rms':rms(v),'w_l2':nw,'w_rms':rms(w),'defect_l2':ne,'defect_rms':rms(e),'cosine':dot/(nv*nw) if nv and nw else None,'best_alpha':alpha,'shape_error':float(np.linalg.norm(w-alpha*v)/nv) if nv else None,'amplitude_ratio':nw/nv if nv else None,'defect_highband_rms':rms(band),'defect_highband_squared_share':float(np.sum(band*band)/np.sum(e*e)) if ne else None})
# Forecast comparison is separate from stage-coordinate plots and gated using post-init records.
spec=importlib.util.spec_from_file_location('h',helperpath);helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper);rkr=json.loads((RK/'forecast_receipt.json').read_text());assert rkr['status']=='completed' and rkr['return_code']==0
states={name:helper.load_initial_state(d,{'nx':nx,'ny':ny,'nz':nz},dry_case_no_qv=True,use_theta_m=1,source='wrfout') for name,d in {'RK3':RK,**cases}.items()}
for name in cases:assert helper.compare_initial_states({'rk3_h600':states['RK3'],name:states[name]},require_input_hash=True)['status']=='passed'
forecast={}
for name,d in {'RK3':RK,**cases}.items():
 p=d/'wrfout_d01_0001-01-01_00:10:00';assert p.is_file() and (d/'wrfoutReady_d01_0001-01-01_00:10:00').is_file()
 with Dataset(p) as f:
  w=np.asarray(f['W'][0,1:,:,20],dtype='float64'); z=(np.asarray(f['PH'][0,1:,:,20],dtype='float64')+np.asarray(f['PHB'][0,1:,:,20],dtype='float64'))/9810;assert np.isfinite(w).all() and np.isfinite(z).all();forecast[name]=(w,z)
 pinmeta[name+'_forecast']={'path':str(p),'sha256':sha(p)}
out=args.out.resolve();out.mkdir(exist_ok=False)
fig,ax=plt.subplots(3,2,figsize=(11,11),constrained_layout=True)
for i,name in enumerate(('rw','t','rv')):
 lim=max(float(np.abs(defects[(lab,name)][:,:,20]).max()) for lab in cases);lim=max(lim,1e-300)
 for j,lab in enumerate(cases):
  q=defects[(lab,name)][:,:,20];m=ax[i,j].imshow(q,origin='lower',aspect='auto',cmap='RdBu_r',vmin=-lim,vmax=lim);ax[i,j].set_title(f'{lab} {name} (v − w)');ax[i,j].set_xlabel('Y index: V faces; W/T mass centers' if name=='rv' else 'Y mass-center index');ax[i,j].set_ylabel('Owned vertical index (W excludes bottom)');fig.colorbar(m,ax=ax[i,j],label=units[name])
fig.suptitle('First Arnoldi direction: physical rate-coordinate defect, arbitrary normalization\nFixed x index 20; vertical index, not forecast height');fig.savefig(out/'first_defect.png',dpi=150);plt.close(fig)
fig,ax=plt.subplots(3,1,figsize=(10,11),constrained_layout=True);lim=max(float(np.abs(v[0]).max()) for v in forecast.values());y=(np.arange(ny)+.5)*100
for i,(name,(w,z)) in enumerate(forecast.items()):
 m=ax[i].pcolormesh(np.broadcast_to(y,z.shape),z,w,shading='auto',cmap='RdBu_r',vmin=-lim,vmax=lim);ax[i].set_title(f'{name} actual W at 10 min');ax[i].set_xlabel('Y km');ax[i].set_ylabel('native height km');fig.colorbar(m,ax=ax[i],label='m/s')
fig.suptitle('Separate forecast context; not the frozen first-direction defect');fig.savefig(out/'forecast_context.png',dpi=150);plt.close(fig)
with (out/'defect_blocks.csv').open('x',newline='') as f:
 wr=csv.DictWriter(f,fieldnames=rows[0].keys());wr.writeheader();wr.writerows(rows)
(out/'receipt.json').write_text(json.dumps({'scope':'One first Arnoldi direction; scaled vector L2-normalized, transformed coordinates are rate units with arbitrary normalization. No mixed-unit energy and no late-forecast noise proof.','source_commit':source,'registration_sha256':regsha,'reference_v_byte_equal':True,'packed_layout':dims,'owned_policy':'packed 41x81 mass includes east/north aliases; U physical 80x64x40, V 81x64x40 including both physical walls, W/PH physical 80x64x40 bottom omitted top included; mass 80x40','highband':'V DST-I with zero wall faces, other fields DCT-II; k>=40 inverse physical-space squared share within each block','pins_and_forecast':pinmeta,'blocks':rows,'script_sha256':sha(Path(__file__))},indent=2)+'\n');print(out)
