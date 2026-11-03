#!/usr/bin/env python3
"""Offline, conditional mu-floor branch analysis after terminal receipt validation."""
import hashlib,json,pathlib,sys
from mass_tube_bound import certify
E=pathlib.Path(__file__).resolve().parent
R=E/'run'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 if len(sys.argv)!=1: raise SystemExit('usage: evaluate_mass_tube.py')
 raw=R/'result.json'; probe=R/'accepted_step_probe.json'; r=json.loads(raw.read_text()); d=json.loads(probe.read_text())
 assert r['status']=='diagnostic_completed' and not r['errors'] and r['diagnostic']==d
 assert d['mu_dtype']==d['mub_dtype']==7 and d['mask_scope']=='identity'
 assert d['profile']=='mode3_single_tile_fp64_ref_update_off_reuse_off' and d['pair_equality']=='numeric_finite' and d['receipt_fingerprints_unchanged'] is True
 assert d['stage']==3 and d['newton']==0 and d['timestep']==0 and d['outcome'] in ('accepted_trust','accepted_recovery')
 assert [d['receipt_rhs_delta'],d['receipt_jvp_delta'],d['receipt_preconditioner_delta']]==[0,0,0]
 result={'diagnostic_sha256':sha(probe),'runner_result_sha256':sha(raw),'mass_tube_code_sha256':sha(E/'mass_tube_bound.py'),'source_contract':'native profile/fixed-context checks; Q_mu copy map requires reviewed source','bound':certify(d),'full_RHS_or_forcing_certified':False}
 with (E/'mass_tube_analysis.json').open('x') as f:json.dump(result,f,indent=2,allow_nan=False);f.write('\n')
 print(json.dumps(result,indent=2))
if __name__=='__main__':main()
