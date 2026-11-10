from pathlib import Path
import gzip,hashlib,json,shutil
E=Path(__file__).resolve().parent
W=Path('/Users/yhlee/SDIRK3-review-worktrees/alb-error-propagation-20261110')
D=W/'docs/evidence/alb_error_20261110';D.mkdir(parents=True,exist_ok=True)
for name in '''alb_mpfr.cpp evaluate_alb.py test_alb_mpfr.py reconstruct_alb_inputs.py alb_preanalysis_test_receipt.json alb_registration.json alb_execution.json alb_analysis.json register_and_run_alb.py widen_alb_tube.py test_widen_alb_tube.py widened_tube_analysis.json green_alb_input_feasibility.json red_alb_input_audit.json red_alb_propagation_plan.json red_alb_tube_plan.json red_alb_checker_preflight.json red_alb_tube_preflight.json green_alb_result_review.json red_alb_result_review.json red_alb_tube_postreview.json descent_replay_validation.json graph_before_pins.json graph_before_navigation.json'''.split():
 shutil.copyfile(E/name,D/name)
shutil.copyfile(E/'reconstructed/reconstruction.json',D/'reconstruction.json')
with(D/'theta_base.npy.gz').open('wb')as f:
 with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0)as g:g.write((E/'reconstructed/theta_base.npy').read_bytes())
shutil.copyfile(__file__,D/'publish_alb.py')
print(D)
