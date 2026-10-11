# PR294 option-2 wind-base same-state and WRF validation

Local timestamp: 2026-10-11 22:42:38 JST (+0900)

Updated: 2026-10-11 22:45:14 JST (+0900)

**Status: the bounded same-state comparison and selected tests passed; full-RHS and endpoint attribution remain open.**

## Scope

This evidence covers the option-2 U/V vertical mixing operand at one archived 16³ WRF `em_b_wave` initial state, plus one RK3 h=3.75 run and one SDIRK3 h=15 run. WRF's `U_BASE` and `V_BASE` profiles are passed from Fortran through a checked C ABI setter and copied into owned C++ tensors. The option-2 mixing branch subtracts these profiles when `mix_full_fields` is false. This does not certify the full RHS or explain an endpoint error.

## Receipts and reproducible analysis

The raw arrays, scripts, build receipts, selected test logs, reference snapshots, and team audits are preserved in [the evidence archive](evidence/20261011/wrf_option2_wind_mixing_base.zip). Executable/library bytes remain locally archived; this ZIP records their rechecked hashes. The archive was extracted into a separate directory and its offline comparison rerun successfully. All 112 payload hashes and ZIP CRCs were verified. Member names below refer to that archive.

The build used base revision `35037bd2093e17beb23d27857a6d9c81ce08aed0` plus the five hash-pinned production changes in `records/production_source_pin.json`. Both new runs used initial input SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, the same newly built executable SHA-256 `d8319a09c5eb879afd7a5946e3420373988c1a59bd37bfe3b3cff56931734ff6`, one MPI rank, one OpenMP thread, and completed with return code zero. The recorded source pins cover five production files. Route checks identify RK3 h=3.75 as Fortran and SDIRK3 h=15 as the C++ solver path.

The four selected CTests (`records/red_four_selected_ctests.log` in the archive) passed: `Base_State_Checked_ABI`, `Scalar_Diffusion_Contract`, `Fixed_Trajectory_Derivative`, and `Fixed_Trajectory_Lifecycle`. The mixed theta/PH/MU AD comparison reported AD `4.20467e-8`, FD h=4 `4.20777e-8`, FD h=2 `4.20544e-8`, zero primal error, and no FD fallback. An earlier attempt crossed the value-based stress branch because its theta baseline was zero; the corrected nonzero-theta fixture holds the branch active across perturbations. Green's final audit (`records/green_final_audit.json` in the archive) confirmed no tangent severance; Red's source audit (`records/red_source_audit.json` in the archive) found no scoped P0/P1/P2 issues.

The saved-output analysis is reproducible with `analyze_wrf_wind_base_validation.py` (`records/analyze_wrf_wind_base_validation.py` in the archive). Prior after-theta and PR292 RK3 NetCDF references, with original run receipts and file hashes, are in `reference_snapshots` (`records/wrf_validation/reference_snapshots` in the archive). The resulting `wind_base_saved_state_analysis.json` (`records/wrf_validation/wind_base_saved_state_analysis.json` in the archive) contains the six-field comparisons, endpoint RMS values, finite/physical-state checks, and run receipts. These outputs and the reference snapshots are packaged in the [portable evidence bundle](evidence/20261011/wrf_option2_wind_mixing_base.zip). From an extracted bundle, run the script with `--repo` pointing to its `sources` checkout and the relative `runs.json`, `reference_snapshots.json`, and `records` evidence paths.

The new run plan and source/executable lineage are recorded in `runs.json` (`records/wrf_validation/runs.json` in the archive): base revision `35037bd2093e17beb23d27857a6d9c81ce08aed0`, initial input SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, executable SHA-256 `d8319a09c5eb879afd7a5946e3420373988c1a59bd37bfe3b3cff56931734ff6`, one rank/thread, and actual RK3/SDIRK route checks. The source pin covers five files. Archived after-theta and provisional RK3 h=1.875 references are pinned to revision `de23ed32509ada2c90fbf7dabbf261da480c78ed` and executable SHA-256 `2b629416618ddef4ceeea8ba51d07b8a7df514f5df6abab107c51e3f78fcc2de`, with exact NetCDF hashes in the snapshot manifest; no executable binaries were copied.

## Saved-state results

The new RK3 h=3.75 output matches the archived PR292 RK3 h=3.75 output exactly for U, V, W, PH, T, and MU at all five saved times (0, 60, 120, 180, and 240 seconds). This is output parity for this control, not full-RHS certification.

At 240 seconds, the new wind-base SDIRK h=15 errors against the new RK3 h=3.75 output are:

| Field | Unit | Absolute RMS error |
|---|---|---:|
| U | m s⁻¹ | 0.007656106 |
| V | m s⁻¹ | 0.002469783 |
| W | m s⁻¹ | 0.000186201 |
| PH | m² s⁻² | 0.160729869 |
| T | K | 0.000130085 |
| MU | Pa | 0.312861496 |

Against the provisional historical RK3 h=1.875 output, the endpoint RMS errors are U `0.013508557`, V `0.003571847`, W `0.000297588`, PH `0.257213964`, T `0.000165004`, and MU `0.504095271`. The h=1.875 series is provisional context, not exact truth or a temporal-order result.

Compared with the archived after-theta SDIRK h=15 output, the new wind-base SDIRK h=15 output differs at 240 seconds by RMS U `0.000339643`, V `0.000003761`, W `0.000001778`, PH `0.000426553`, T `0.000004886`, and MU `0.000013642`, in their respective units. The comparison spans two builds and does not isolate the complete RHS or establish an endpoint cause.

Against the same RK3 h=3.75 output, the U endpoint RMS error changes from `0.007664366` in the after-theta SDIRK run to `0.007656106` in the wind-base SDIRK run, about 0.108% lower. This small endpoint change does not close attribution or show that this operand explains the larger state differences.

A separate same-state shear-helper comparison found Fortran-to-C++ profile U error RMS `5.02e-11 s⁻¹`, max absolute `2.91e-10 s⁻¹` over 3,150 owned U points. The V profile was exactly zero in both outputs, so it is nondiscriminating. This helper result is not a full-RHS comparison.

Single-run wall times were 3.933 s for RK3 h=3.75 and 7.267 s for SDIRK h=15; RSL logs show 64 versus 16 main steps and 48 converged SDIRK stages with 96 Newton iterations. These include whole-process overhead and do not establish an accuracy-cost advantage.

## Limits

All analyzed saved fields were finite, and the analysis checked positive total theta, column mass, and layer depth. The remaining state differences do not identify whether wind-base, pressure, or another RHS term controls any endpoint discrepancy. The results establish one bounded initial-state shear operand comparison and two whole-path run records, not complete U/V/MU RHS parity, endpoint attribution, or a general forecast claim. Any wider RHS or accuracy-cost claim needs separate evidence.

## Reanalysis and next action

After extracting the archive, run:

```sh
python3 records/analyze_wrf_wind_base_validation.py \
  --repo sources --new-plan runs.json --evidence-dir records \
  --reference-snapshots records/wrf_validation/reference_snapshots/reference_snapshots.json \
  --out reanalysis.json
```

The bounded same-state wind-profile saved-output comparison is complete. The next missing evidence is a complete same-state tendency comparison in common physical units, especially U/V/MU; compare instantaneous operators before attributing the residual to the time integrator. A project-wide window and physical error budget still need selection. The completed rotating-wave inverse/forecast remains closed; no optimizer, wave, or timestep ladder was rerun here.
