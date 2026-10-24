# Standard ARK600 periodic-selection cost trial

The [report](<../../(202610242336)_jvp_periodic_permutation_cost.md>) describes one stateless PH periodic selection change, its source/build closure and a frozen four-run A–B–B–A trial. All four runs completed with identical public outputs and reported residual/work records. The measured median RSL reduction is **17.7902%**, and median measured JVP cost per call fell **18.8131%**. These are telemetry-on first-step results, not quiet-production, six-hour, five-day or same-accuracy performance certification.

Run receipts retain `cost_eligible=false`: these diagnostic runs are not eligible for the clean production benchmark dataset. The separately frozen diagnostic ABBA gates compare their matched measurements only; no receipt was relabeled as a production benchmark success.

Small, directly inspectable files:

- [protocol.json](protocol.json): pre-edit numerical/cost gates, fixed run order and caps. No retries or retrospective threshold changes.
- [costs.csv](costs.csv): all four whole-path/RSL/JVP measurements and binary hashes.
- [native_field_parity.csv](native_field_parity.csv): three comparison paths × two frames × all variables, including 1,044 exact finite numeric comparisons.
- [summary.json](summary.json): all gates, paired reductions, source/input provenance and counter limits.
- [resolution_checklist.json](resolution_checklist.json): closed items, this bounded result, and still-open long-window/resource/accuracy goals.
- [archive_verification.json](archive_verification.json): ZIP SHA-256, CRC/per-payload SHA checks, eight frame aliases and two unique NetCDF payloads.

`evidence.zip` preserves the prospective protocol, original runners and process receipts, unmodified namelists, RSL logs, compiler/link/archive/test receipts, source/graph bindings, old/new helper sources, exact microbenchmark harness, original stack sample and reproducible accounting, final Green/Red review and all native output content. Source metadata pins main `2ca17046…`, baseline binary `ad3abbbf…` and candidate binary `30b0ac22…`. The candidate archive differs only in the rebuilt Tile member. Executables, object/static archives and the unchanged shared input are not duplicated; exact hashes and build commands remain in the receipts.

Native outputs are deduplicated by SHA-256: `archive_manifest.json` maps the eight original frame paths to `native_arrays/<sha256>.nc`. After extraction:

```sh
python execution/materialize_native_frames.py
python execution/analyze_native_abba.py
python analysis/reproduce_sample_accounting.py --jvp-detail
```

The first command restores the original frame names without overwriting differing data; the second recomputes numerical/cost gates from those arrays and logs without running WRF. Use Python with NumPy and netCDF4 for the pair analyzer. The stack accounting uses only the standard library. Historical absolute source/build paths in receipts are provenance, not a claim that the archived binaries are installed on another machine.

The new helper is a permutation of the **current** primal/dual input, not a cache of gathered state, pressure, density or a previous tangent. Symmetric Y reflection, stencils, physics, tolerances, preconditioning and JVP selection remain unchanged. Tests cover FP32/FP64, multiple states/directions, contiguous/strided tensors, singleton and wrapped/negative shifts, plus full PH oracle/coordinate, diagnostics and fixed-trajectory behavior. All four focused targets passed against the rebuilt candidate archive; this was not a fresh full CTest run.

After the native block, the forward-dual test was clarified to hold the primal fixed while changing the direction. Only that final PH kernel test was rebuilt/rerun and passed. Its receipt is `build/phi_horizontal_roll/same_primal_phi_test_receipt.json`; header, native binary, protocol and native results were unchanged. The original prelaunch four-target evidence is retained separately.

The original sample indicates only four self samples in fixed arange/remainder setup versus 334 gather descendants inside the JVP bucket. These are stack counts, not exact wall-time or proof that this helper dominates all JVP work. The helper microbenchmark (4.49–5.62×) is component-only. The candidate native RSL timings vary more than baseline timings, so all four observations and both paired reductions are retained. Prior single-step timings were much faster; do not divide old timings by new ones or infer a general causal percentage.

Existing telemetry records 240 AD-selected and zero explicitly FD-selected JVP calls per run. This does not independently expose the internal fwAD-to-FD fallback counter; keep that qualification. The JVP implementation/mode remains unchanged and the new helper has direct forward-dual parity tests. Public FP32 output byte parity is not a newly captured internal FP64 trajectory.

No new RK3, six-hour or five-day run occurred here. The [archived same-setup quiet comparison](../quiet_forward_diagnostics/README.md) and [five-day RK3 references](../standard_forward_rk3_baseline/README.md) remain separate. Do not rescale their approximately 443.30× six-hour cost ratio by this first-step reduction. ARK five-day completion, 300/150 s sensitivity and physical accuracy/equal-accuracy cost acceptance remain open, with the prior long-run resource cap unchanged pending user choice. Previous PR320 P2 fixes and MGS rejection, closed derivative/twin cases and the separate MPI engineering issue are preserved.

Published CSVs use LF line endings; the archive retains the original CRLF exports. `csv_export_verification.json` verifies that every parsed cell is unchanged.
