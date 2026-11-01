#!/usr/bin/env python3
"""Draft coupled inverse pair comparator; performs no model run or promotion."""
import json,math,pathlib
import numpy as np
from run_a5 import read_records

def exact(a,b):return a.dtype==b.dtype and a.shape==b.shape and a.tobytes()==b.tobytes()
def finite_nonnegative(x,name):
 if not isinstance(x,(int,float)) or isinstance(x,bool) or not math.isfinite(x) or x<0:raise ValueError('invalid '+name)
 return float(x)
def evaluate(control,candidate,control_payload,candidate_payload):
 """Returns registered physical/scaled quality, work and first-interval gates."""
 A=read_records(pathlib.Path(control_payload));B=read_records(pathlib.Path(candidate_payload))
 if set(B)-{'base_z_scaled'}!=set(A)-{'base_z_scaled'}:raise ValueError('paired typed record sets differ')
 dynamic={'z_scaled','Az_scaled','P0z_physical'}
 for key in sorted(set(A)-dynamic-{'base_z_scaled'}):
  if not exact(A[key],B[key]):raise ValueError('baseline context differs: '+key)
 if 'base_z_scaled' in B and not exact(A['z_scaled'],B['base_z_scaled']):raise ValueError('captured candidate base z differs from control z')
 for key in ('v_scaled','z_scaled','Az_scaled','S_diag'):
  if A[key].shape!=B[key].shape or A[key].dtype!=np.dtype('<f8') or B[key].dtype!=np.dtype('<f8'):raise ValueError('bad packed vector '+key)
 v=A['v_scaled'].reshape(-1);za=A['Az_scaled'].reshape(-1);zb=B['Az_scaled'].reshape(-1);S=A['S_diag'].reshape(-1)
 if not np.all(S>0):raise ValueError('invalid scaling')
 ea=v-za;eb=v-zb;na=float(np.linalg.norm(ea));nb=float(np.linalg.norm(eb));scaled_gate=nb<=na*(1+1e-8)
 blocks=control['a4']['blocks'];other=candidate['a4']['blocks'];identity=lambda b:(b['name'],b['start'],b['size'])
 if [identity(b) for b in blocks]!=[identity(b) for b in other] or len(blocks)!=6 or sum(b['size'] for b in blocks)!=v.size:raise ValueError('packed layout differs')
 pa=(S*v)-(S*za);pb=(S*v)-(S*zb);rows=[]
 units={'ru':'m s-2','rv':'m s-2','rw':'m s-2','ph':'m2 s-3','t':'K s-1','mu':'Pa s-1'}
 for b in blocks:
  sl=slice(b['start'],b['start']+b['size']);rows.append({'name':b['name'],'unit':units[b['name']],'control_physical_l2':float(np.linalg.norm(pa[sl])),'candidate_physical_l2':float(np.linalg.norm(pb[sl])),'control_scaled_l2':float(np.linalg.norm(ea[sl])),'candidate_scaled_l2':float(np.linalg.norm(eb[sl]))})
 ny,nz,nx=A['eos_t'].shape;vblock=blocks[1];wblock=blocks[2];vs=(ny+1,nz,nx);ws=(ny,nz+1,nx)
 if vblock['name']!='rv' or wblock['name']!='rw' or math.prod(vs)!=vblock['size'] or math.prod(ws)!=wblock['size']:raise ValueError('V/W stagger layout mismatch')
 vsl=slice(vblock['start'],vblock['start']+vblock['size']);wsl=slice(wblock['start'],wblock['start']+wblock['size']);vc=lambda x:x[vsl].reshape(vs)[1:ny-1,:,0:nx-1];wc=lambda x:x[wsl].reshape(ws)[0:ny-1,:,0:nx-1];rms=lambda x:float(np.sqrt(np.mean(x*x)))
 v_a=float(np.linalg.norm(vc(pa)));v_b=float(np.linalg.norm(vc(pb)));wa=wc(pa);wb=wc(pb);wtop_a=rms(wa[:,nz,:]);wtop_b=rms(wb[:,nz,:]);wint_a=rms(wa[:,1:nz,:]);wint_b=rms(wb[:,1:nz,:]);wbound=lambda a:a*(1+1e-8)+1e-15
 quality={'full_scaled_control':na,'full_scaled_candidate':nb,'full_scaled_not_increased':scaled_gate,'V_interior_core_control_l2':v_a,'V_interior_core_candidate_l2':v_b,'V_interior_core_strictly_decreased':v_b<v_a,'W_top_control_rms':wtop_a,'W_top_candidate_rms':wtop_b,'W_top_not_increased':wtop_b<=wbound(wtop_a),'W_interior_control_rms':wint_a,'W_interior_candidate_rms':wint_b,'W_interior_not_increased':wint_b<=wbound(wint_a),'rows':rows}
 la=control['linear'];lb=candidate['linear'];aa=control['a4'];ab=candidate['a4'];ja=control['existing_jvp_summaries'];jb=candidate['existing_jvp_summaries'];ca=ja['first_linear_solve']['calls'];cb=jb['first_linear_solve']['calls'];wa=ja['whole_step_calls_sum'];wb=jb['whole_step_calls_sum'];ta=finite_nonnegative(la['interval_from_newton_entry_ms'],'control inclusive interval');tb=finite_nonnegative(lb['interval_from_newton_entry_ms'],'candidate inclusive interval')
 if ta<=0 or tb<=0:raise ValueError('nonpositive first-point interval')
 coupled=candidate.get('coupled',{});applications=coupled.get('applications');m_calls=ab.get('wrapped_M_calls');physical_calls=ab.get('physical_M_calls');prep=finite_nonnegative(lb.get('setup_ms'),'candidate setup time');clone=finite_nonnegative(coupled.get('base_clone_ms'),'candidate first base clone time')
 if not all(type(x) is int and x>=0 for x in (applications,m_calls,physical_calls)):raise ValueError('missing coupled/M call counters')
 accounting_ok=applications>0 and applications==m_calls==physical_calls and prep>0;clone_ok=clone<.01*tb
 work={'control_first_jvp_calls':ca,'candidate_first_jvp_calls':cb,'first_jvp_strictly_decreased':cb<ca,'control_whole_jvp_calls':wa,'candidate_whole_jvp_calls':wb,'whole_jvp_not_increased':wb<=wa,'control_first_inclusive_ms':ta,'candidate_first_inclusive_ms':tb,'first_inclusive_cost_strictly_decreased':tb<ta,'candidate_setup_ms_included':prep,'candidate_first_base_clone_ms_included':clone,'candidate_first_base_clone_below_one_percent':clone_ok,'coupled_M_accounting_passed':accounting_ok,'candidate_coupled_applications':applications,'candidate_wrapped_M_calls':m_calls,'physical_M_nested_not_additive':True,'JVP_nested_in_A_not_additive':True}
 checks=[quality[k] for k in ('full_scaled_not_increased','V_interior_core_strictly_decreased','W_top_not_increased','W_interior_not_increased')]+[work[k] for k in ('first_jvp_strictly_decreased','whole_jvp_not_increased','first_inclusive_cost_strictly_decreased','candidate_first_base_clone_below_one_percent','coupled_M_accounting_passed')]
 return {'decision':'SCREEN_POSITIVE' if all(checks) else 'NO-GO','quality':quality,'work':work,'scope':'one instrumented pair only; no Gate B, adoption or forecast performance claim','base_z_control_bytes_equal':exact(A['z_scaled'],B['base_z_scaled']) if 'base_z_scaled' in B else None,'cost_instrumented_feasibility_only':True}
