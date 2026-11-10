from pathlib import Path
import gzip,io,tarfile,shutil,json,hashlib
E=Path(__file__).resolve().parent;W=Path('/Users/yhlee/SDIRK3-review-worktrees/w-error-envelope-20261110');D=W/'docs/evidence/w_error_20261110';D.mkdir(parents=True,exist_ok=True)
for name in '''w_full_mpfr.cpp run_w_full_mpfr.py test_w_full_mpfr.py prepare_w_full_manifest.py test_prepare_w_full_manifest.py norm_exact.py test_norm_exact.py full_w_error.json norm_gap.json w_full_algorithm_receipt.json w_full_preanalysis_test_receipt.json w_normalizer_test_receipt.json reference_registration.json reference_execution.json reference_rusage.log reference.stdout.log reference.stderr.log execute_w_reference.py reference_launcher_syntax_attempt.txt reference_launcher_correction.json data_inventory.json green_complete_w_source_plan.json red_mass_tendency_reference_plan.json red_full_w_reference_plan.json red_complete_w_definition_review.json red_q_reference_check.json red_w_capture_source_review.json red_w_full_reference_preflight.json red_w_normalizer_preflight.json red_norm_exact_plan.json red_norm_exact_preflight.json red_norm_exact_postreview.json red_full_w_reference_postreview.json green_full_w_capture_postreview.json green_w_reference_numeric_review.json green_full_w_algorithm_postreview.json native_registration.json native_registration_draft.json build_receipt.json helper_test_receipt.json link_receipt.json stack_feasibility.json build_w_error.py run_helper_test.py link_single_archive.py run_w_reference.py runner_preflight_receipt.json runner_reference_synthetic_receipt.json helper_build.log helper_run.log build_driver.log link.stdout.log link.stderr.log link_driver_probe.txt wrf_sdirk3_native_pressure_reference.cpp.log wrf_sdirk3_newton_solver.cpp.log wrf_sdirk3_pressure_stage_probe.cpp.log wrf_sdirk3_tile_unified_impl.cpp.log wrf_sdirk3_v_primary_probe.cpp.log graph_before_pins.json graph_before_navigation.json'''.split():
 assert(E/name).exists(),name;shutil.copyfile(E/name,D/name)
(D/'run').mkdir(exist_ok=True)
for name in '''attempt.json result.json accepted_step_probe.json rsl.out.0000 rsl.error.0000 launcher.stdout.log launcher.stderr.log rank_rusage.log namelist.input namelist.output'''.split():shutil.copyfile(E/'run'/name,D/'run'/name)
shutil.copyfile(E/'normalized_case/manifest.json',D/'normalized_manifest.json')
with(D/'w_full_inputs.tar.gz').open('wb')as f:
 with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0)as g:
  with tarfile.open(fileobj=g,mode='w')as t:
   for p in sorted((E/'normalized_case').glob('*.npy')):
    b=p.read_bytes();info=tarfile.TarInfo(p.name);info.size=len(b);info.mode=0o644;info.mtime=0;t.addfile(info,io.BytesIO(b))
with(D/'wrf_link.map.gz').open('wb')as f:
 with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0)as g:g.write((E/'wrf_link.map').read_bytes())
shutil.copyfile(__file__,D/'publish_w_error.py');print('inputs archive bytes',(D/'w_full_inputs.tar.gz').stat().st_size)
