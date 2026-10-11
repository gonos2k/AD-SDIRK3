# PR290 narrow rotating two-control forecast closure

Local timestamp: 2026-10-11 14:26:29 JST (+0900)

**Status: empirical closure passed for the declared narrow case; independent Green and Red reviews of the actual archives passed.**

Finalized: 2026-10-11 14:28:20 JST (+0900)

## Context and scope

This record covers the fixed narrow case: dry, flat, constant-f rotating-wave source; fixed coefficients; one CPU tile; declared X/Y boundaries; two selected controls; linear source reference with noise-free observations; forward/replay, bounded optimization, and cold-restart unused-time forecast. It does not cover an Eg bound, a Hessian, added physics, full-state stationarity, or general weather validation.

The P2 source-coordinate metric correction is commit `59b301aedc3de7352ca208eb4de01a7344666ff7`, based on `5baf7b4acebb7e02688372a4cd0432a75b5dc785` and merged into main `1b0ccea8371d70d6de95a124ef389beff3ee31df`. The archived inverse and forecast remain pinned to the original producer revision `746ad78d029c6cf92c498288daee445fe4b3c22d` and its reported executable SHA-256 `485e90cc7e313b81785a3db0a7a5e6944192710a3d38925bdb5a157ffeb747a5`. This preserves the original source and artifact lineage. The same-host metric descriptor audit separately found the old/fixed mode arrays and all four input files byte-identical; no inverse or forecast rerun on the P2 commit is claimed. The executable binary is not contained in the workflow artifacts, so its digest is report-matched rather than re-hashed here.

## Inverse and forecast lineage

Inverse run [38106368123](https://github.com/gonos2k/AD-SDIRK3/actions/runs/38106368123) completed at the pinned producer revision. Artifact `11691711140` is [the archived inverse ZIP](evidence/20261011/pr291_session_audited_closure.zip) (member `run-38106368123/inverse.zip`), SHA-256 `e83f88c5284875e0634d854e2824c4993940eac9d9d273dcc78a788461e081c1`; its report SHA-256 is `6b2e13227745ee53c937d6a3cd93a7e98ecfc979357ff338b1e4b6af6fff1c9b`. The independent raw-archive review passed all 18 producer-source pins and all four input pins. The bounded objective decreased from `7019.471904439247` to `4.650331862412872e-5`, then `4.639781075332348e-5`; projected-gradient norm ended at `8.323828831096476e-7` under the unchanged `1e-5` threshold.

Forecast run [38112463542](https://github.com/gonos2k/AD-SDIRK3/actions/runs/38112463542) completed from tag `research/pr289-forecast-20261011134301`, at the same producer revision and reported executable digest. Forecast artifact `11693311070` is [the archived forecast ZIP](evidence/20261011/pr291_session_audited_closure.zip) (member `run-38112463542/forecast.zip`), SHA-256 `ccc89601639fd94ebfe09c6be6c6651380fda889ab8dcdcbf7fc10b5e2eff107`; ZIP CRC passed, and the forecast report SHA-256 is `a9adc3cac7710e9b2a94446e89ce5b98485b53169cbd5c2109c1dd7f3995da89`. The report binds this forecast to the inverse ZIP SHA and inverse report SHA above. Its 19-entry initial and final source-pin maps match exactly and pass audit checks.

The four state-only forward calls follow exact cold restarts `300→600→900` for both start and terminal controls. The archive audit verified each 600-second initial state against its 300-second checkpoint and each 900-second initial state against its 600-second checkpoint. Every call reports the physical-observation-domain and vertical-height guards passed and admissibility checked each step. The constant Coriolis input was `f=9.999999747378752e-5`; the archived descriptor uses `e=sina=0`, `cosa=1`. The four calls took 2,226.58277778 seconds total. No optimizer or gradient native calls were added after the inverse.

## Unused-time comparison

The table gives owned-point error RMS at each unused time as `start → terminal`, followed by terminal/start error ratio. All 16 variable/diagnostic comparisons improved. Ratios below one indicate smaller terminal-control error for this archived comparison; this is not a general acceptance threshold.

| Quantity | Unit | 600 s start → terminal RMS | 600 s ratio | 900 s start → terminal RMS | 900 s ratio |
|---|---|---:|---:|---:|---:|
| U | m s⁻¹ | 3.083e-3 → 5.533e-7 | 1.794e-4 | 3.083e-3 → 7.104e-7 | 2.304e-4 |
| V | m s⁻¹ | 1.985e-3 → 8.409e-7 | 4.237e-4 | 1.985e-3 → 1.033e-6 | 5.207e-4 |
| W | m s⁻¹ | 2.596e-3 → 4.979e-7 | 1.918e-4 | 2.596e-3 → 6.366e-7 | 2.452e-4 |
| PH | m² s⁻² | 6.294e-2 → 1.502e-5 | 2.386e-4 | 6.294e-2 → 2.111e-5 | 3.354e-4 |
| theta | K | 6.805e-4 → 1.248e-7 | 1.834e-4 | 6.805e-4 → 1.910e-7 | 2.806e-4 |
| MU | Pa | 1.312e-1 → 2.058e-5 | 1.569e-4 | 1.312e-1 → 2.629e-5 | 2.005e-4 |
| ζ | s⁻¹ | 2.513e-8 → 2.434e-11 | 9.683e-4 | 2.513e-8 → 2.178e-11 | 8.667e-4 |
| D | s⁻¹ | 6.964e-7 → 2.475e-10 | 3.554e-4 | 6.964e-7 → 3.111e-10 | 4.467e-4 |

The largest terminal/start ratio is ζ: `0.00096833245` at 600 seconds (99.90316675% lower terminal error) and `0.00086673230` at 900 seconds (99.91332677% lower). Among the six state variables, V has the largest ratio at each time: `0.00042370337` at 600 seconds and `0.00052071503` at 900 seconds. The full 16-row record includes source-signal RMS, absolute errors, ratios, and percent reductions in [`unused_forecast_error_comparison.csv`](evidence/20261011/pr290_unused_forecast_error_comparison.csv). The full native/source vorticity, divergence, and six-field audit is [the archived horizontal audit](evidence/20261011/pr291_session_audited_closure.zip) (member `actual_forecast_horizontal_audit.json`); its own work is offline and made zero native calls.

## Validation limits and next actions

The final Green and Red reviews both passed the actual inverse and forecast archives. They independently checked the ZIP hashes and CRCs, all 18 inverse and 19 forecast/final source pins, matching reported executable SHA, all four raw calls, rotating profile and guards, and exact cold-restart links. Green confirmed all 16 owned-field and horizontal-error comparisons; Red independently applied the FP32 difference stencils using `rdx=rdy=0x39d1b717` and reproduced the eight terminal/start ratios at 600 and 900 seconds. Both reviews made zero native calls. The earlier over-strict cross-host chart gate failure and stopped monitor remain preserved; the corrected `gamma4` chart check is evidence-only and did not modify the producer or native files. Existing no-rotation case closure is unchanged.

No WRF `em_b_wave` run or same-setup RK3 field/runtime comparison was performed. Global production WRF/RK3 validation remains separate. Do not infer a formal Eg bound, Hessian result, full-state stationarity, added-physics result, or general weather forecast claim from this dry constant-f linear-source case.

No further action is required for this narrow case. Any future generalization or broader WRF/RK3 validation is a separate goal. No new numerical experiment is added by this documentation/evidence follow-up.

Execution stack: Linux x86_64 / glibc 2.39, Python 3.12.15, GNU C++ 13.3.0 (`/usr/bin/c++`, Release `-O3 -DNDEBUG`), Torch 2.10.0+cpu, NumPy 2.2.6; MPICH 4.2.0-5build3 via `/usr/bin/mpicxx`. Exact package/build logs and CMake cache are retained with the forecast artifact. This CPU tile run is not a multi-rank MPI validation.

## Publication and last-bit frequency note

The evidence archive above is committed with SHA-256 `bb43af748bcdc61c40e915c90d667e30d0cbe3026fa982c9d4f7f3d368470ce5`; its embedded snapshot manifest verifies each member. The archive retains original raw ZIPs, reports, offline analysis scripts, failed-monitor history, and the fresh final team audit and resolutions current at its creation. Historical status entries and the original PR #290 description describe their timestamp, not the final closure state. The fresh session audit receipts and current summary are separate files in this PR.

The exact native descriptor gives old/fixed angular frequency `0.0025701755450031286 s^-1` on the recorded local audit host; the earlier independently computed decimal `0.0025701755450031065` differs by `2.21e-17` (`8.6e-15` relative). The selector audit used `1e-11` relative tolerance and confirmed old/fixed mode arrays and all four same-host input files were identical. This last-bit host difference is not a correction-induced frequency change.
