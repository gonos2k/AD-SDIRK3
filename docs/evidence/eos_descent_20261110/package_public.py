from pathlib import Path
import gzip,hashlib,io,json,shutil,tarfile
E=Path(__file__).resolve().parent
W=Path('/Users/yhlee/SDIRK3-review-worktrees/eos-descent-20261110')
D=W/'docs/evidence/eos_descent_20261110'
D.mkdir(parents=True,exist_ok=True)
names='''descent_budget.py descent_budget.json eos_domain_bounds.py evaluate_eos_domain.py test_eos_domain_bounds.py test_evaluate_eos_domain.py eos_domain_analysis.json eos_mpfr.cpp run_eos_mpfr.py test_eos_mpfr.py eos_mpfr_test_receipt.json eos_mpfr_analysis.json native_registration.json native_registration_draft.json run_eos_endpoint.py runner_final_preflight_receipt.json runner_raw_synthetic_receipt.json stack_feasibility.json check_stack.py graph_before_pins.json graph_after_pins.json graph_final_pins.json graph_final_navigation.json refresh_graph.py refresh_graph_final.py red_eos_capture_review.json green_eos_capture_review.json red_eos_interval_implementation.json red_eos_wrapper_receipt.json red_mpfr_reference_review.json red_eos_domain_postreview.json red_eos_mpfr_postreview.json green_eos_result_review.json red_norm_component_math.json green_norm_component_plan.json'''.split()
for name in names:
 assert (E/name).exists(),name
 shutil.copyfile(E/name,D/name)
for base,names in [('raw_capture','''build_receipt.json link_receipt.json helper_test_receipt.json rebuild_raw.py run_helper_first.py link_single_archive.py link_driver_probe.txt link.stderr.log link.stdout.log newton_compile.log helper_build.log helper_run.log'''),('run','''attempt.json result.json accepted_step_probe.json rank_rusage.log rsl.out.0000 rsl.error.0000 launcher.stdout.log launcher.stderr.log namelist.input namelist.output''')]:
 (D/base).mkdir(exist_ok=True)
 for name in names.split():shutil.copyfile(E/base/name,D/base/name)
for src,dst in [(E/'raw_capture/wrf_link.map',D/'raw_capture/wrf_link.map.gz'),(E/'graph_final.json',D/'graph_final.json.gz')]:
 with dst.open('wb') as f:
  with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as g:g.write(src.read_bytes())
with (D/'eos_inputs.tar.gz').open('wb') as f:
 with gzip.GzipFile(filename='',mode='wb',fileobj=f,mtime=0) as g:
  with tarfile.open(fileobj=g,mode='w') as t:
   for p in sorted((E/'run/eos_raw').glob('*.npy')):
    b=p.read_bytes();i=tarfile.TarInfo('eos_raw/'+p.name);i.size=len(b);i.mode=0o644;i.mtime=0;t.addfile(i,io.BytesIO(b))
shutil.copyfile(__file__,D/'package_public.py')
manifest={'scope':'Public EOS component analysis and source/link/native receipts; not full WRF replay or full residual certification','files':{str(p.relative_to(D)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(D.rglob('*')) if p.is_file() and p.name!='manifest.json'}}
(D/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print('published',len(manifest['files']),'files; raw bundle', (D/'eos_inputs.tar.gz').stat().st_size,'bytes')
