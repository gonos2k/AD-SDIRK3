# PR296 packed-U diffusion cause and acceptance criteria

Local timestamp: 2026-10-12 01:57:37 JST

This note records the confirmed option-2 packed-boundary causes, the narrow fixes under review, and the evidence required before making a forecast-quality claim. It does not certify the complete spatial operator or the full WRF trajectory.

## Captured case and source boundary

The case is dry `test/em_b_wave`, with 16 physical cells per axis, periodic X, and symmetric nonperiodic Y. The packed WRF storage carries an extra mass endpoint in X and Y; the physical operator is 16×16×16. The archived `namelist.output` has `DIFF_OPT=2`, `KHDIF=1000`, `KVDIF=10`, `KM_OPT=1`, `MIX_FULL_FIELDS=F`, `MOMENTUM_ADV_OPT=1`, `ZADVECT_IMPLICIT=0`, and `DIFF_6TH_OPT=0`. It prints `HYPSOMETRIC_OPT=2`, while WRF reports that ideal cases force the effective value to 1.

`module_implicit_sdirk3.F` sets C++ `diffusion_option` from `config_flags%diff_opt`, and passes `khdif` and `kvdif` from those standard WRF fields. Thus this archived run exercises option-2 stress diffusion with nonzero horizontal and vertical coefficients. Do not interpret the separate `SDIRK3_DIFFUSION_OPTION=0` output key as the C++ runtime setting.

The first-RK-step U-diffusion before/after capture in `dyn_em/module_first_rk_step_part2.F` is diagnostic-only and was enabled for the single-rank capture. Its I/O path is frozen; keep it out of the numerical fix. The `*.F` source is authoritative for preprocessing.

## Confirmed causes and fixes

The exact-state captures exposed two packed-periodic stress issues and one symmetric-Y strain boundary issue.

1. Option-2 U vertical stress formed `rho_vort` and `Kv_vort` over the stored X length, leaving the owned periodic west face at its zero initialization. The physical seam must use the last unique mass column and its periodic neighbor. The fix preserves the existing fnm/fnp-weighted pair averages, closes the owned west face from the correctly computed physical closing face, and restores the padding alias. The final isolated U vertical-diffusion difference is RMS `1.49e-12 m/s²`, max `1.41e-11 m/s²` over 4,352 saved faces.
2. Option-2 U horizontal stress produced the west seam contribution, but the helper published the interior range without folding the computed closing face back to owned U face 0. The same packed-face closure now publishes that value and restores the duplicate and padding slots. This term was smaller than the vertical-stress seam error, but had the same zero-at-west publication failure pattern.
3. `compute_defor13` copied symmetric-Y row 1 over row 0. In this packed one-tile case, row 0 is an owned physical south U/D13 row. Fortran computes `jds` directly when the boundary is symmetric and does not apply the copy guard. The option-2 packed path now preserves row 0 and retains the top storage copy. A saved Fortran witness has D13 south-row max `4.37e-6` versus `8.66e-5` on the next row; replaying the substitution predicts the captured south-row U VDiff mismatch to float32 roundoff (correlation `0.9999999999997`).

The C++ closure helper and its call sites are in `external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp:613, 36536-36564, 36913-36977, 38558-38563`. The Fortran diffusion contract is in `dyn_em/module_diffusion_em.F`; the capture-only brackets are in `dyn_em/module_first_rk_step_part2.F:1061-1200`. The helper regression verifies option-2 periodic face closure, preserves option-1 digests, preserves the symmetric-Y south D13 row, passes translation equivariance, and passes mixed-rho D13 AD versus finite differences without fallback.

There is no arbitrary output clipping. The correction uses the physical periodic value already computed by the stress stencil and explicitly restores the storage aliases. Option-1 digests are unchanged.

## Exact-state RHS evidence and limits

The saved full-RHS comparisons require exact equality of all six physical input blocks U/V/W/PH/T/MU and exact periodic closing U face before differencing. That gate passes for the initial and stored captures; the full storage audit is reported separately. C++ U/V full RHS exports are already physical accelerations. Convert Fortran coupled U/V once with the documented mass/map relation; do not apply a second conversion to C++.

At the stored state, U full-RHS RMS discrepancy drops from the old baseline `7.74e-7` to `7.49e-8 m/s²` (max `2.65e-7`). V remains `7.62e-8 m/s²`; MU remains `3.37e-8 Pa/s`. The isolated horizontal-U diffusion captures are about `1.02e-8 m/s²` in both models, and their difference is about `6.5e-10 m/s²`. The isolated vertical-U diffusion difference is at roundoff after the boundary correction. The quoted whole-RHS values use 4,096 owned U faces. The term-analysis headline uses 4,352 saved rows (including the Y storage endpoint), so its RMS is a different mask and must not be substituted into this comparison. These results confirm the targeted U diffusion causes and show a substantial U RHS improvement; they do not certify the remaining full-RHS discrepancy, V/MU/PH/T/W equations, or integrated VJP closure.

An independent pressure replay predicts about `2.88e-8 m/s²` at the original hotspot; a separate mass-normalization counterfactual changes it by about `1e-11 m/s²`. Neither explains the original `2.78e-5 m/s²` hotspot. The remaining full-RHS difference must stay visible as a remainder rather than being assigned to corrected diffusion terms.

The repository scalar contract was compiled from the current test source against the final library. Its new boundary CLI and complete default suite pass; relinking the same test object against the prior library makes the boundary CLI fail. Receipt: `local_contract/validation_receipt.json`.

## Proposed named-case criteria

The v3 acceptance contract is a root-proposed case target, not a user-selected project-wide threshold. It uses the dry flat 16³ case, 240 s final time, one rank and one thread, exact input SHA `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, and checkpoints at 60/120/180/240 s. The fixed per-field budget is `max(1% of the h=1.875 s reference response RMS, 3× reference output ULP RMS)` and applies unchanged at all checkpoints:

| Field | Units and physical sampling | Fixed epsilon |
| --- | --- | ---: |
| U | m/s; unique periodic-X faces | 7.7444e-4 |
| V | m/s; both physical Y walls | 1.0122e-4 |
| W | m/s; lower W face excluded, top included | 7.7944e-6 |
| PH | m²/s²; perturbation, lower face excluded | 1.0250e-2 |
| T | K; physical potential temperature `T+300` | 3.6196e-5 |
| MU | Pa; total dry column mass `MU+MUB` | 1.4591e-2 |

The frozen JSON `sampling.PH` label describes height in metres, whereas its `budgets.PH` and executed gate use geopotential in m²/s². The separate metadata erratum records this inconsistency without changing the frozen bytes or numeric gate; the PH epsilon in the table is authoritative.

One h=0.9375 s RK3 refinement was run against the archived h=1.875 s reference. It fails the required epsilon/3 qualification at every field/checkpoint, so no further refinement ladder is authorized by this contract. The h=15 s ARK run was compared at all checkpoints; all six 240 s RMS differences exceed their fixed epsilon. Because the RK3 reference is unqualified, those candidate comparisons are **provisional** and neither acceptance nor rejection is claimed. Whole-path runtimes are context only, not an acceptance gate.

There were twelve short 15 s term-capture runs across the baseline/X/XY phases; each phase used the same two initial inputs for paired Fortran/C++ evaluations. The native qualification receipt records exactly three fresh 240 s runs: ARK h=15 s, stock RK3 h=3.75 s control, and RK3 h=0.9375 s qualifier. Initial raw eight-field states match; all source, executable, library, input, and contract hashes rechecked after the runs. The h=3.75 s control is bitwise equal to the archived RK3 outputs at all five output times. The candidate is not certified until the reference gate passes and each field independently meets epsilon at every checkpoint.

At 240 s, ARK h=15 s minus the provisional RK3 h=1.875 s reference has RMS differences U `0.0135072 m/s`, V `0.00357184 m/s`, W `0.000297601 m/s`, PH `0.257211 m²/s²`, T `0.000164905 K`, and MU `0.504095 Pa`. Fresh whole-process wall times are ARK h=15 s `9.573 s`, RK3 h=3.75 s `1.604 s`, and RK3 h=0.9375 s `2.209 s`. These are single-run launch-to-exit measurements, not same-accuracy costs or a speed advantage.

## Remaining work and evidence pins

- Reconcile the remaining complete RHS remainder without claiming a full six-equation or spatial-operator closure.
- Keep W/PH/theta RHS parity and integrated VJP closure open; the rotating closed-boundary case was not rerun.
- The bounded graph binds 55 source files, verified against the final working source bytes; only the changed C++ test was re-extracted in the last refresh. Extraction gaps remain explicit and graph edges are navigation, not numerical proof.
- Preserve the exact source/executable/input receipts and compare the post-commit source blobs before changing any acceptance status.

Evidence used: `final_initial_full_rhs_analysis.json`, `final_stored_full_rhs_analysis.json`, `final_initial_term_analysis.json`, `final_stored_term_analysis.json`, `baseline_initial_term_analysis.json`, `baseline_stored_term_analysis.json`, `green_pressure/vdiff_y0_replay.json`, `green_probe/periodic_regression_final_xy_receipt.json`, `acceptance_candidate_v3.json`, and `green_acceptance/native_qualification_v3/evaluation_v3.json` under `AD-SDIRK3-review-evidence/seam-cause-acceptance-20261012`. The raw-data/source pin bundle is [pr296_seam_cause_acceptance_evidence.zip](evidence/20261012/pr296_seam_cause_acceptance_evidence.zip); its adjacent SHA-256 file records the archive hash. Executable and library bytes are retained locally; the distributed bundle records their hashes. The later documentation-only receipt binds the run source bytes to a reachable committed producer revision.

Committed producer binding: `78255e14ad4f2d8e07e9e764b47b9eb24266dd53`. The [source binding receipt](evidence/20261012/pr296_committed_source_binding.json) verifies seven native-run source files and the compiled regression test against that commit. Final WRF executable SHA-256 is `f7c4e1dbb69a3ec68e5376294e326cbaff339531f31dac13ed47e0fcbf413b64`; final solver archive SHA-256 is `5f28e24cc3019c59c45ed8942e9ad104fc6a8a2c62f59416fe0d04fec317556f`. The native runs used these bytes before commit; no clean-checkout rerun is claimed.

The ZIP SHA-256 is `8adbea641fdd3e785b6c1853768783af2012cedcfb8e91acc4725fe1a4db5274`. All 305 payload hashes and ZIP CRCs pass. [Offline verification](evidence/20261012/pr296_offline_zip_verification.json) re-extracts the bundle and reproduces both final full-RHS and term comparisons with NumPy; all four commands match the saved numeric results.
