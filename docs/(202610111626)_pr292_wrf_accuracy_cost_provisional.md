# PR292 WRF accuracy-cost comparison (provisional)

Local timestamp: 2026-10-11 16:26:17 JST (+0900)

**Status: the narrow option-2 theta-mixing correction, four post-rebuild runs, offline comparison, and independent Green/Red audit passed.** This is a narrow `em_b_wave` accuracy-cost sample, not a general phase closure.

## Scope and source change

The comparison uses dry `em_b_wave` on a 16×16×16 grid through 240 seconds, one MPI rank and one OpenMP thread. It compares RK3 at 1.875 and 3.75 seconds with SDIRK3 at 15 and 30 seconds, all from the same archived initial input and against the provisional RK3 h=1.875 reference. RK3 h=3.75 is retained as a reference-resolution sensitivity check.

The setup retains fifth-order horizontal and third-order vertical advection, `phi_adv_z=1`, active `diff_opt=2`, `km_opt=1`, `khdif=1000`, `kvdif=10`, `mix_full_fields=false`, no damping, periodic X and symmetric Y walls. All listed moist/radiation/PBL/surface/cumulus physics are disabled. Complete semidiscrete-operator equality between the stock Fortran and C++ paths has not been established.

The rebuilt SDIRK code applies the `t_base` vertical theta-mixing correction to `diff_opt=2` only. WRF option 1 follows the separate `vertical_diffusion_3dmp(..., t_init, ...)` path; this evidence makes no option 1 parity claim. Other residual field differences have not been attributed to a root cause.

Raw arrays, run plans, source pins, and validation logs are preserved in [the evidence archive](evidence/20261011/wrf_theta_mixing_accuracy_cost.zip). The ZIP has 171 entries: 170 hashed payloads and `manifest.json`. Member names below refer to that archive. Executable snapshots are retained locally with rechecked SHA-256 receipts; executable bytes and system libraries are not included in this ZIP. Its `before/runs.json` and `after/runs.json` are portable array-analysis plans, which retain recorded executable hashes without requiring local executable bytes. Original execution receipts remain in `records/`.

## Build and run receipts

Worktree revision: `de23ed32509ada2c90fbf7dabbf261da480c78ed`. The authoritative five-file source pin and production diff hash are recorded in ``production_source_pin.json``. The final clean C++ plus affected WRF rebuild receipt is ``build_receipt_after.json``: it records executable SHA-256 `2b629416618ddef4ceeea8ba51d07b8a7df514f5df6abab107c51e3f78fcc2de`, LibTorch archive SHA-256 `4369098b25d74cd3f44b792c1537661d8a6c3b4256a75afb3a7c0690ad284ffd`, and `configure.wrf` SHA-256 `8cdf647666634cdb8cc2624d5d627c6bdd79472b3ed99481831f2a3e27873289`. The `final compile log` reports GNU Fortran 15.2.0 and a successful WRF link; the `selected CTest log` shows `Base_State_Checked_ABI` and `Scalar_Diffusion_Contract` passed. The recorded toolchain is GNU Fortran 15.2.0, Apple Clang 21.0.0, Open MPI 5.0.9, netCDF 4.9.3, netCDF-Fortran 4.6.2, and Homebrew LibTorch 2.10.0; this Python analyzer uses NumPy/netCDF4, not a separate Python Torch runtime.

All four run rows in ``after/runs.json`` completed with return code zero. Each has the same `wrfinput_d01` SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, records its namelist and executable hashes, and checks the executed route from WRF logs: RK3 routes false for the C++ SDIRK marker; SDIRK3 routes true. The frozen source namelist hash is `6258b997543f95b6cdde538aa37e14084f17862210e94c02dd23fb7fd91a8e06`. Run plans record 1 MPI rank and 1 OpenMP thread.

The offline output is ``analysis_after.json``. It was produced with:

```sh
wrf_evidence_dir=/absolute/path/to/extracted-evidence
python3 tools/benchmark_wrf_accuracy_cost.py \
  --runs-json "$wrf_evidence_dir/after/runs.json" \
  --reference fortran_rk3_h1p875 \
  --reference-check fortran_rk3_h3p75 \
  --out ./analysis_after.json
```

## Current comparison

At 240 seconds, the SDIRK h=15 errors against the RK3 h=1.875 reference changed from the preserved pre-correction run to the final rebuilt run as follows:

| Field (unit) | Pre-correction RMS | Final RMS | Readout |
|---|---:|---:|---|
| T (K) | 0.0414308901 | 0.0001650408 | About 251× lower |
| PH (m²/s²) | 1.367894476 | 0.257221258 | Lower; residual remains |
| W (m/s) | 0.0006750960 | 0.0002975664 | Lower; residual remains |
| U (m/s) | 0.0135132928 | 0.0135136502 | Essentially unchanged |
| V (m/s) | 0.0035718062 | 0.0035718418 | Essentially unchanged |
| MU (Pa) | 0.5040832618 | 0.5040950885 | Essentially unchanged |

Final SDIRK h=30 errors were similar to h=15 for this case. The analysis reports finite errors for all fields. The T change is associated with the `diff_opt=2` `t_base` correction in this rebuilt source; these results do not show that larger timestep alone caused the accuracy change.

Whole-process wall times were 1.9358 s for RK3 h=1.875, 0.9490 s for RK3 h=3.75, 6.8814 s for SDIRK h=15, and 4.0816 s for SDIRK h=30. These are single-run launch-to-exit measurements including whole-path overhead. The analysis marks them as whole-path receipts, not integrator-only timings. They do not establish a same-accuracy cost win.

RK3 h=1.875 uses `time_step=1` plus `7/8` second and logged 128 main steps; RK3 h=3.75 uses `3` plus `3/4` second and logged 64. Their endpoint U RMS difference is `0.0058636674`, so reference discretization is non-negligible. The analysis labels h=3.75 as sensitivity, not exact truth or a temporal-order result.

## Limits and next actions

Earlier failed and duplicate attempts and the rejected stale-build log remain separately preserved and excluded. Three successfully completed directories originally named `rk3_h*` actually used scheme 6; their recorded namelists/logs establish SDIRK, so they are retained only as pre-correction SDIRK under corrected analysis labels. Their raw receipts were not rewritten. The final rebuilt source and binary have dedicated source/diff and executable receipts above. Green verified all 53 refreshed graph/source snapshot hashes and reviewed the profile mapping, ABI guard, owned snapshot, option-2-only dispatch, and trajectory fingerprint. Red independently checked source/executable/receipt hashes, effective routes, exact initial fields, and the unchanged RK3 h=3.75 trajectory; all 174 floating output variables were finite at the five saved times. SDIRK h=15/h=30 completed 48/24 converged stages with zero failed steps, fallback, retries, or nonfinite telemetry. Both teams found no blocker within this correction’s scope. Their receipts are `green_graph_source_review.json` and `final_red_wrf_fix_closure_audit.json` in the archive. The new whole-RHS regression checks background stationarity/full-field response; its independent amplitude formula comparison is for the scalar helper, not a new complete-RHS oracle.

Residual W, PH, and other field differences remain unresolved; potential RHS, wind-base, and PH explanations are not established. This sample does not prove an integrator-only speedup, equal-accuracy advantage, or the cause of all remaining field discrepancies. It also does not close general WRF accuracy/performance validation. The next integration question is the remaining field/path and reference-step discrepancy; broader accuracy-per-cost claims need separate evidence. The completed constant-f rotating inverse/forecast case remains closed.
