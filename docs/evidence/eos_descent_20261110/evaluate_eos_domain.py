"""Translate terminal accepted-step EOS scalars into conditional offline bounds.

No production callbacks are invoked. The packet's same-call helper asserts fixed
coefficient value/hash equality; this script cannot independently reconstruct the
full tensors or prove hidden context invariance. The captured alb is the computed
base inverse-density coefficient; its upstream pow and rounding error are not bounded.
"""
import json, math, sys
from fractions import Fraction
from eos_domain_bounds import evaluate, exact


def evaluate_packet(packet):
    try:
        if packet['status'] != 'completed' or packet['mask_scope'] != 'identity' or packet['pair_equality'] != 'numeric_finite':
            raise ValueError('unsupported or unmatched pair')
        if packet['mu_dtype'] != 7 or packet['mub_dtype'] != 7 or packet['mu_count'] <= 0:
            raise ValueError('mu/mub must be nonempty binary64')
        e=packet['eos_scalars']
        if e['scope'] != 'same_call_EOS_endpoint_outputs_no_error_bound':
            raise ValueError('unexpected EOS capture scope')
        n=e['levels']
        if isinstance(n,bool) or not isinstance(n,int) or n<=0:
            raise ValueError('invalid level count')
        for name in ('c1h','c2h','rdnw'):
            if e[name+'_dtype'] not in (6,7) or not str(e[name+'_hash']).isdigit() or len(e[name])<n:
                raise ValueError('invalid fixed coefficient metadata: '+name)
            for v in e[name]: exact(v)
            if name == 'rdnw' and any(exact(v) <= 0 for v in e[name][:n]):
                raise ValueError('captured rdnw must be positive; no absolute-value substitution')
        for name in ('rd','cv','cp','p0'):
            if exact(e[name])<=0:raise ValueError('nonpositive thermodynamic constant: '+name)
        exact(e['t0'])
        for endpoint in ('base','accepted'):
            ep=e[endpoint]
            for name in ('theta_pert','dphi_pert_computed','alb','alt','p_pert'):
                if len(ep[name])!=n:raise ValueError('level shape mismatch: '+name)
                for rec in ep[name]:
                    lo,hi,ab=(exact(rec[k]) for k in ('min','max','absmax'))
                    if lo>hi or ab<max(abs(lo),abs(hi)):raise ValueError('invalid extrema: '+name)
        data={'a':packet['a'],'t0':e['t0'],'rd':e['rd'],'p0':e['p0'],
              'mu':[packet['mu_min'],packet['mu_absmax']],
              'mub':[packet['mub_min'],packet['mub_absmax']],
              'B_mu_absmax':packet['B_mu_absmax'],
              'K0_mu_absmax':packet['K_before_mu_absmax'],
              'K1_mu_absmax':packet['K_after_mu_absmax']}
        for name in ('B_ph_absmax','K0_ph_absmax','K1_ph_absmax','B_t_absmax','K0_t_absmax','K1_t_absmax'):
            data[name]=e[name]
        data['levels']=[]
        for i in range(n):
            base=e['base']; accepted=e['accepted']
            if base['alb'][i]!=accepted['alb'][i]:raise ValueError('alb extrema differ across captures')
            def pair(name):return [base[name][i]['min'],base[name][i]['max']]
            data['levels'].append({'t':pair('theta_pert'),'dph':pair('dphi_pert_computed'),
                                   'alb':pair('alb'),'c1':e['c1h'][i], 'c2':e['c2h'][i],
                                   'rdnw_abs':e['rdnw'][i]})
        out=evaluate(data)
        a=exact(data['a']);rad=a*(exact(data['K0_t_absmax'])+exact(data['K1_t_absmax']))+1
        for i,level in enumerate(out['levels']):
            if level['status']!='bounded_positive':continue
            alt_lo=Fraction(level['alt']['lower_exact']);alt_hi=Fraction(level['alt']['upper_exact'])
            t0=e['base']['theta_pert'][i]; low=exact(t0['min'])-rad; high=exact(t0['max'])+rad
            for ep in ('base','accepted'):
                x=e[ep]
                obs=x['alt'][i];t=x['theta_pert'][i]
                if not (alt_lo<=exact(obs['min'])<=exact(obs['max'])<=alt_hi and
                        low<=exact(t['min'])<=exact(t['max'])<=high):
                    level.update(status='unresolved',reason='observed endpoint outside interval; source/model/magnitude assumption unresolved')
                    break
        if any(x['status']!='bounded_positive' for x in out['levels']):out['status']='unresolved'
        out['endpoint_check']='base and accepted observed alt and theta perturbation are enclosed at bounded levels'
        out['fixed_inputs_evidence']='same-call helper checked tensor equality and hashes; no independent full tensor reconstruction'
        out['alb_scope']='stored computed base inverse-density coefficient treated as fixed; upstream pow and calculation error not bounded'
        return out
    except (KeyError, TypeError, ValueError, OverflowError) as exc:
        return {'status':'unresolved','reason':str(exc),'scope':'packet or source/model assumptions unresolved'}

if __name__=='__main__':
    with open(sys.argv[1]) as f: result=evaluate_packet(json.load(f))
    print(json.dumps(result,indent=2))
    sys.exit(0 if result['status']=='bounded_positive' else 1)
