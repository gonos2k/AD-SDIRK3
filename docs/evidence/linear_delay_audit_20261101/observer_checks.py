"""Draft parser for the frozen observer; no model execution."""
import hashlib,json,math,pathlib,struct
import numpy as np
NAMES=('linear_delay_u_eval.npy','linear_delay_u_ref_first_jvp.npy','linear_delay_first_tangent.npy','linear_delay_first_jvp.npy')
def validate(text,directory,old_raw_paths):
 records=[json.loads(line.split('[LINEAR_DELAY] ',1)[1]) for line in text.splitlines() if '[LINEAR_DELAY] ' in line]
 if len(records)!=6 or [r.get('solve_ordinal') for r in records]!=list(range(1,7)):raise ValueError('expected six distinct ordered observer records')
 if [(r.get('stage'),r.get('newton')) for r in records]!=[(2,0),(2,1),(3,0),(3,1),(4,0),(4,1)]:raise ValueError('registered solve identity sequence mismatch')
 for r in records:
  if not r.get('timing_eligible') or not r.get('clock_valid') or not r.get('success') or r.get('torch_intra_threads')!=1 or r.get('torch_inter_threads')!=1:raise ValueError('observer/clock/solve/thread invalid')
  for k in ('wall_ms','process_user_ms','process_system_ms','JVP_wall_ms','JVP_process_user_ms','JVP_process_system_ms','A_wall_ms','M_wrapped_wall_ms'):
   if not isinstance(r.get(k),(int,float)) or not math.isfinite(r[k]) or r[k]<0:raise ValueError('invalid '+k)
  if r['wall_ms']<=0:raise ValueError('nonpositive wall')
  for k in ('JVP_calls_in_linear','selected_ad_calls_outer','selected_fd_calls_outer','internal_fwad_to_fd_fallbacks','rhs_calls_through_linear_end','rhs_calls_linear','rhs_fast_calls_through_linear_end','A_calls','M_wrapped_calls'):
   if not isinstance(r.get(k),int) or r[k]<0:raise ValueError('invalid '+k)
  if r['selected_ad_calls_outer']+r['selected_fd_calls_outer']!=r['JVP_calls_in_linear']:raise ValueError('AD+FD count differs from JVP count')
  r['expected_path_match']=(r['selected_fd_calls_outer']==0 and r['internal_fwad_to_fd_fallbacks']==0)
  all_modes=r.get('rhs_modes_through_linear_end');linear_modes=r.get('linear_rhs_modes')
  if not isinstance(all_modes,list) or not isinstance(linear_modes,list) or len(all_modes)!=3 or len(linear_modes)!=3 or any(type(v)!=int or v<0 for v in all_modes+linear_modes):raise ValueError('invalid RHS mode coverage')
  if sum(all_modes)!=r['rhs_calls_through_linear_end'] or sum(linear_modes)!=r['rhs_calls_linear'] or any(b>a for a,b in zip(all_modes,linear_modes)):raise ValueError('inconsistent RHS mode counters')
 selected=[r for r in records if r.get('selected_stage4_newton0')]
 if len(selected)!=1 or selected[0]['solve_ordinal']!=5 or selected[0]['stage']!=4 or selected[0]['newton']!=0:raise ValueError('selected solve mismatch')
 s=selected[0];ctx=s.get('selected_context',{})
 if not ctx.get('context_stable') or not ctx.get('EOS_captured') or not s.get('sample_captured') or not s.get('raw_npy_written') or s.get('sample_phase')!='first_arnoldi_restart0_j0':raise ValueError('missing selected context/sample')
 for k in ('state_fnv64','reference_first_fnv64','sample_u_eval_fnv64','sample_reference_fnv64','sample_tangent_fnv64','sample_jvp_fnv64','frozen_meta','source_handles','EOS_fnv64','geometry_fnv64','topology','flags'):
  if k not in ctx:raise ValueError('missing context '+k)
 if type(s.get('sampled_first_arnoldi_fallbacks'))!=int or not 0<=s['sampled_first_arnoldi_fallbacks']<=s['internal_fwad_to_fd_fallbacks']:raise ValueError('invalid sample fallback count')
 frac=s['capture_clone_ms_included']/s['wall_ms']
 if not 0<=frac<.01:raise ValueError('capture clone fraction >=1%')
 directory=pathlib.Path(directory); files={}; total=0
 keys=('sample_u_eval_fnv64','sample_reference_fnv64','sample_tangent_fnv64','sample_jvp_fnv64')
 for name,key in zip(NAMES,keys):
  p=directory/name; a=np.load(p,mmap_mode='r',allow_pickle=False)
  if a.dtype!=np.dtype('<f8') or a.shape!=(1080491,) or not np.isfinite(a).all():raise ValueError('invalid NPY '+name)
  h=hashlib.sha256();
  with p.open('rb') as f:
   for b in iter(lambda:f.read(1048576),b''):h.update(b)
  fnv=14695981039346656037
  for byte in struct.pack('<Biqqq',1,7,1,a.size,a.size)+a.tobytes():fnv=((fnv^byte)*1099511628211)&0xffffffffffffffff
  if format(fnv,'x')!=ctx[key]:raise ValueError('NPY context hash mismatch '+name)
  total+=p.stat().st_size;files[name]={'bytes':p.stat().st_size,'sha256':h.hexdigest(),'observer_fnv64':format(fnv,'x')}
 old_bytes=sum(pathlib.Path(p).stat().st_size for p in old_raw_paths)
 if total>64*1024**2 or len(old_raw_paths)!=3 or total+old_bytes>192*1024**2:raise ValueError('combined artifact size limit')
 return {'records':records,'capture_clone_fraction':frac,'npy_files':files,'npy_total_bytes':total,'old_three_raw_total_bytes':old_bytes,'combined_raw_total_bytes':total+old_bytes,'timing_scope':'diagnostic; process CPU is nested in wall; JVP is nested, never add to A'}
