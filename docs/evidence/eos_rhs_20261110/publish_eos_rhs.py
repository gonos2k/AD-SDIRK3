from pathlib import Path
import gzip,io,json,hashlib,shutil,tarfile
E=Path(__file__).resolve().parent;W=Path('/Users/yhlee/SDIRK3-review-worktrees/eos-rhs-propagation-20261110');D=W/'docs/evidence/eos_rhs_20261110';D.mkdir(parents=True,exist_ok=True)
for name in '''recover_alpert_bound.py test_recover_alpert_bound.py alpert_registration.json alpert_execution.json alpert_bound.json red_alpert_recovery_plan.json red_alpert_recovery_preflight.json red_alpert_recovery_postreview.json prepare_w_coefficients.py w_pressure_sensitivity.py test_w_pressure_sensitivity.py w_pressure_preanalysis_test_receipt.json w_coefficients.json w_sensitivity_registration.json w_sensitivity_execution.json w_pressure_component.json green_uv_rhs_path_audit.json red_w_eos_consumer_audit.json green_metric_weight_bound_audit.json red_w_coeff_input_feasibility.json red_w_rdn_initial_evidence.json red_w_sensitivity_preflight.json red_w_sensitivity_postreview.json green_result_review.json graph_before_pins.json graph_before_navigation.json'''.split():shutil.copyfile(E/name,D/name)
raw=Path('/Users/yhlee/AD-SDIRK3-review-evidence/eos-descent-20261110/run/eos_raw')
with(D/'w_inputs.tar.gz').open('wb')as f:
 with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0)as g:
  with tarfile.open(fileobj=g,mode='w')as t:
   for name in ('base_mu','accepted_mu','mu_base','rdnw'):
    b=(raw/(name+'.npy')).read_bytes();i=tarfile.TarInfo('eos_raw/'+name+'.npy');i.size=len(b);i.mode=0o644;i.mtime=0;t.addfile(i,io.BytesIO(b))
shutil.copyfile(__file__,D/'publish_eos_rhs.py')
print('W fragment bytes',(D/'w_inputs.tar.gz').stat().st_size)
