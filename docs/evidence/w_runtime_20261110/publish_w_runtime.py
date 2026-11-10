from pathlib import Path
import gzip,shutil,json,hashlib
E=Path(__file__).resolve().parent;W=Path('/Users/yhlee/SDIRK3-review-worktrees/w-runtime-contract-20261110');D=W/'docs/evidence/w_runtime_20261110';D.mkdir(parents=True,exist_ok=True)
for name in '''analyze_w_runtime.py test_analyze_w_runtime.py observed_w_component.json candidate_observation_comparison.json native_registration.json native_registration_draft.json build_receipt.json helper_test_receipt.json link_receipt.json stack_feasibility.json dependency_feasibility.json build_w_runtime.py run_helper_test.py link_single_archive.py run_w_runtime.py check_stack.py build_driver.log helper_build.log helper_run.log link.stdout.log link.stderr.log link_driver_probe.txt wrf_sdirk3_native_pressure_reference.cpp.log wrf_sdirk3_newton_solver.cpp.log wrf_sdirk3_pressure_stage_probe.cpp.log wrf_sdirk3_tile_unified_impl.cpp.log wrf_sdirk3_v_primary_probe.cpp.log runner_final_preflight_receipt.json runner_preflight_before_target_packet_fix.json runner_w_synthetic_receipt_target.json red_w_observation_plan.json red_w_source_review.json red_w_source_fix_review.json red_w_analysis_preflight.json red_w_runtime_postreview.json green_w_runtime_postreview.json graph_before_pins.json graph_before_navigation.json'''.split():
 shutil.copyfile(E/name,D/name)
for base,names in [('run','''attempt.json result.json accepted_step_probe.json rsl.out.0000 rsl.error.0000 launcher.stdout.log launcher.stderr.log rank_rusage.log namelist.input namelist.output'''),('build_attempt_8706','''build_driver.log wrf_sdirk3_newton_solver.cpp.log wrf_sdirk3_native_pressure_reference.cpp.log''')]:
 (D/base).mkdir(exist_ok=True)
 for name in names.split():shutil.copyfile(E/base/name,D/base/name)
with(D/'wrf_link.map.gz').open('wb')as f:
 with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0)as g:g.write((E/'wrf_link.map').read_bytes())
shutil.copyfile(__file__,D/'publish_w_runtime.py')
print('copied',sum(p.is_file()for p in D.rglob('*')))
