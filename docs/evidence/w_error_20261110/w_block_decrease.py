"""Combine W evaluation-error bounds with exact native W-block norms only."""
import argparse
from fractions import Fraction as F
import hashlib
import json
from pathlib import Path
import numpy as np
from norm_exact import sum_squares, sqrt_bounds, exact


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def validate_metadata(packet, manifest, error, packet_sha, manifest_sha):
    if error['status'] != 'passed_conditional' or error['manifest_sha256'] != manifest_sha or packet['mask_scope'] != 'identity':
        raise ValueError('different or unsupported target')
    if manifest['origins']['packet_sha256'] != packet_sha:
        raise ValueError('packet is not the source of this normalized case')
    if len(error['analysis']['states']) != 2:
        raise ValueError('two error endpoints required')
    rw = next(b for b in packet['w_reference']['layout'] if b['name']=='rw')
    if (packet['N'], rw['start'], rw['size']) != (1080491,432896,215865):
        raise ValueError('different W block')
    if not 0 < exact(rw['sinv_min']) <= exact(rw['sinv_max']):
        raise ValueError('invalid inverse-weight range')
    return rw


if __name__=='__main__':
    q=argparse.ArgumentParser()
    q.add_argument('packet',type=Path)
    q.add_argument('manifest',type=Path)
    q.add_argument('input_dir',type=Path)
    q.add_argument('w_error',type=Path)
    a=q.parse_args()
    p=json.loads(a.packet.read_text());m=json.loads(a.manifest.read_text());e=json.loads(a.w_error.read_text())
    rw = validate_metadata(p, m, e, sha(a.packet), sha(a.manifest))
    out=[]
    for name,s in zip(('R0','R1'),e['analysis']['states']):
        f=a.input_dir/(name+'.npy')
        if sha(f)!=m['files'][name]['sha256']:
            raise ValueError('residual data mismatch')
        r=np.load(f,allow_pickle=False)
        if r.dtype!=np.dtype('<f8') or r.shape!=(p['N'],) or not np.isfinite(r).all():
            raise ValueError('invalid native residual')
        square=sum_squares(r[rw['start']:rw['start']+rw['size']])
        lo=sqrt_bounds(exact(rw['sinv_min'])**2*square/p['N'])[0]
        hi=sqrt_bounds(exact(rw['sinv_max'])**2*square/p['N'])[1]
        eps=s['weighted_math_rms_upper']
        if float.fromhex(s['weighted_math_rms_upper_binary64_hex'])!=eps:
            raise ValueError('invalid reference bound encoding')
        eps=exact(eps)
        if eps < 0:
            raise ValueError('negative evaluation error')
        out.append({'name':name,'native_W_merit_lower_exact':str(lo),'native_W_merit_upper_exact':str(hi),
                    'evaluation_error_upper_exact':str(eps),
                    'target_W_merit_lower_exact':str(max(F(0),lo-eps)),
                    'target_W_merit_upper_exact':str(hi+eps)})
    margin=F(out[0]['target_W_merit_lower_exact'])-F(out[1]['target_W_merit_upper_exact'])
    print(json.dumps({'scope':'W-block mathematical merit only at defined fixed B/a/C/K profile',
          'conditional_on_fixed_reference_inputs_profile':True,
          'strict_W_merit_decrease_certified':margin>0,'full_merit_decrease_certified':False,'Stage3_convergence_certified':False,
          'states':out,'separation_margin_lower_exact':str(margin),
          'input_sha256':{k:sha(getattr(a,k))for k in('packet','manifest','w_error')}},indent=2))
