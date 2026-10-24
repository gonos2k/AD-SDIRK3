# PR #319 review resolution and bounded native cost evidence

See the [report](<../../(202610242213)_forward_cost_profile_and_rejected_candidate.md>) and `archive_verification.json` for the archive SHA-256, item count and independent per-payload SHA/CRC verification.

`evidence.zip` preserves three new ARK600 first-step attempts (all RC0): one explicitly sampled diagnostic run, then one baseline and one candidate run without a sampler. **The candidate was rejected, and production Newton code was restored. No whole-model speedup, new six-hour/five-day completion or accuracy acceptance is claimed.** The current baseline remains the prior validated executable. Existing RK3 references and closed derivative/twin cases were not rerun.

The archive contains:

- `resolution_checklist.json`: completed fixes, negative candidate result and open long-run/resource/accuracy items.
- `final_sources/`: exact final analyzer/test/inventory/solver sources and accepted code patch. Production Newton is SHA `507d1ef4…` and has no final change.
- `optimization/`: prospective candidate, rejected patch/source, post-extraction graph and final rejection status. These are experimental evidence, not accepted production code.
- `pre_graph_binding.json`, `graph_post/`, `final_source_graph_binding.json`: exact source bindings and explicit extraction gaps. The candidate graph is separately retained under `optimization/graph_post/`.
- `analysis/`: sample accounting and replay parser, positive analyzer/static inventory/baseline-FGMRES receipts, component-only microbench and AD summaries, and the **negative** matched native comparison. The original inline microbench harness is not preserved; those numbers are contextual, not a performance acceptance test.
- `native_sample.txt`, sample intent/receipt and logs: raw main-thread stack evidence. Inclusive categories must not be added; the exclusive parser conserves 9,689 samples.
- `profile_cases/` and `native_pair/`: all six native NetCDF frames at t=0/600 s, unchanged namelists, initialization gates, process/exit receipts and RSL logs. Candidate endpoint differences are directly inspectable. These runs have diagnostic telemetry enabled and are not clean production cost samples.
- `build/fused_mgs/`: compiler/link commands, exact member/archive/executable hashes and focused test receipts. `baseline_final_fgmres/` is the final accepted 49/49 regression against the original archive; earlier candidate/fallback test runs are separately preserved.
- `execution/`: exact runners and matched-pair analyzer. No model launch is needed to inspect the arrays, hashes or logs.

After extraction, `python analysis/reproduce_sample_accounting.py` verifies the pinned raw sample and writes the bucket reproduction. That script and `execution/analyze_native_pair.py` resolve the extraction root relative to themselves. The pair analyzer writes comparison details and **exits nonzero intentionally**, because the prospective exact-output gate failed. Do not convert that exit to success or redefine the objective/acceptance after seeing the result. Run the static CTest script with the repository as the working directory; it is explicitly source-only, assumes MPI registrations and excludes an absent optional source, and does not replace configured CTest discovery.

The first analyzer unit-test invocation used a module path without the tools import directory and failed to import; the direct invocation then passed 10/10. Selected Ruff E4/E7/E9/F checks pass. All-rules lint reports nine style findings; a full lint-clean claim is not made. No new GitHub CI result is inferred from local receipts.

The shared input is SHA `c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912`. Unchanged input, baseline libraries/executable and same-setup RK3 six-hour/five-day results are dependencies of the archived [standard comparison](../standard_forward_rk3_baseline/README.md), [six-hour screen](../standard_ark_six_hour/README.md) and [quiet validation](../quiet_forward_diagnostics/README.md). Binaries and static libraries are not duplicated here; the exact build/source/stack hashes and commands are retained. Native standard-grid mean-state/field comparisons and the large RK3 cost gap remain documented in those records.

ARK five-day execution and same-accuracy performance acceptance remain open. The previously declared real-time cap is not silently increased, and pending resource/physical-goal questions are not treated as approvals. The candidate's tiny rounding differences alone do not establish a physical equation defect; its rejection is a bounded engineering decision under this trial's predeclared gate and absent native speed benefit.
