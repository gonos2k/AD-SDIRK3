from pathlib import Path
import json,csv,math,hashlib
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
E=Path(__file__).resolve().parent;r=json.loads((E/'run/result.json').read_text());assert r['status']=='diagnostic_completed' and not r['errors'];d=r['diagnostic'];rows=[];by={}
for direction in d['directions']:
 vals=direction['samples'];by[direction['name']]={'implemented_candidate_residual_ratio':direction['linear_residual_over_base_G'],'minimum_sampled_gap_over_G':min(v['gap_over_base_G']for v in vals),'maximum_sampled_gap_over_G':max(v['gap_over_base_G']for v in vals),'coarse_to_fine_gap_ratios':[vals[i+1]['gap_over_base_G']/vals[i]['gap_over_base_G']for i in range(2)]}
 for s in vals:
  rows.append({'direction':direction['name'],**s,'implemented_residual_over_G':direction['linear_residual_over_base_G'],'requested_tolerance':d['gmres_tolerance'],'implemented_plus_FD_sample_gap':direction['linear_residual_over_base_G']+s['gap_over_base_G']})
with(E/'gap_samples.csv').open('w',newline='')as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator="\n");w.writeheader();w.writerows(rows)
fig,axs=plt.subplots(1,2,figsize=(10.5,4),layout='constrained');labels={'used_p':'Postprocessed candidate p','Q_seed':'Q-projected seed','boundary_complement':'Boundary complement (I-Q)seed'}
for direction in d['directions']:
 v=direction['samples'];x=[s['epsilon']for s in v];y=[s['gap_over_base_G']for s in v];axs[0].loglog(x,y,'o-',label=labels[direction['name']]);axs[1].loglog(x[1:],[s['fd_adjacent_scaled_l2']/d['base_G_scaled_l2']for s in v[1:]],'o-',label=labels[direction['name']])
axs[0].axhline(d['gmres_tolerance'],color='black',ls='--',lw=1,label='Requested Krylov tolerance');axs[0].set_ylabel(r'$\|S^{-1}(\widehat A d-D_\epsilon G[d])\|/\|S^{-1}G\|$');axs[1].set_ylabel('Adjacent FD difference / scaled G norm')
for a in axs:a.set_xlabel('Dimensionless perturbation epsilon (dt = 600 s)');a.grid(True,which='both',alpha=.2)
axs[0].legend(fontsize=8);axs[0].set_title('Sampled gap; not a certified Jacobian bound');axs[1].set_title('FD epsilon-squared regime not established')
fig.savefig(E/'gap_vs_epsilon.png',dpi=160);plt.close(fig)
summary={'status':'sampled_evidence_only','source_commit':r['source_commit'] if 'source_commit'in r else json.loads((E/'run/attempt.json').read_text())['target_head'],'result_sha256':hashlib.sha256((E/'run/result.json').read_bytes()).hexdigest(),'base_G_scaled_l2':d['base_G_scaled_l2'],'requested_tolerance':d['gmres_tolerance'],'raw_gmres_relative':d['gmres_raw_relative'],'fresh_used_p_relative':d['directions'][0]['linear_residual_over_base_G'],'directions':by,'all_input_perturbation_loss_counts_zero':all(s['k_lost_components']==s['state_lost_components']==0 for x in d['directions']for s in x['samples']),'projection_sample_errors':{k:d[k]for k in ['used_p_Q_error_l2','projection_idempotence_l2','boundary_Q_l2']},'interpretation':['The used-p and boundary-complement sampled gaps grow approximately as 1/epsilon; consistent with differencing roundoff, not proof of its exclusive cause.','Input perturbations are not wholly lost in any recorded component; this does not measure all floating point rounding.','The three epsilon values do not establish a native truncation error regime or a rigorous bound on A_star minus Ahat.','implemented_plus_FD_sample_gap is a numerical proxy only, not a certified upper bound on exact forcing.','Q_seed and boundary-complement forcing ratios are not Newton acceptance claims; only used_p is the postprocessed search candidate.'],'exact_forcing_certified':False,'forecast_complete':False,'complete_steps':0,'new_RK3_run':False,'new_endpoint_comparison':False,'performance_eligible':False}
(E/'analysis_summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(by)
