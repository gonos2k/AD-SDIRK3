# Solver-owned FP64 state carry in the dry WRF forward path

Local time: **2026-09-29 21:06:50 JST**. Numerical source commit: `14d449d012da7bf55af21c8e089b9a028bffb939`, stacked on PR #267 at `09be40135e526b14060bc20b322726b4a916035d`. This report covers a default-off forward option, not a general WRF or data-assimilation qualification.

## Problem and change

PR #266 kept each ARK/Newton step internally FP64 but published FP32 WRF state before the next step. PR #267 isolated that handoff and a local serial WRF counterfactual with FP64 carry plus `newton_tol=1e-10` showed near-third-order adjacent-solution rates in U/V/W/PH/MU over a short 15 s interval. The previous carry was `thread_local` test code and did not belong in production.

`sdirk3_internal_fp64_state_carry` now provides separate, default-off **solver-owned** FP64 forward state. The first step starts from WRF's packed FP32 state. A completed step stores its projected FP64 state and the exact FP32 tensor written to WRF. On the next real step, reuse requires the repacked host state to equal that publication, a consecutive process-visible host timestep, unchanged dt, matching geometry, and the same fixed RHS-context fingerprint. Changed input fails closed; unchanged per-call WRF index republication keeps the carry. Full/light reset, base-state and boundary republish, actual grid change, disabling the option, and a nonadvanced step clear it. Compatibility calls `rk_step=2,3` do not consume or publish carry.

The option is limited to internal FP64 ARK mode 3 on one CPU rank/tile, dry `mp_physics=0`, zero external tendencies, dry moisture corrections, fixed timestep and auxiliary inputs, and supported fixed boundaries. `split_explicit`, adaptive timesteps, specified/open/nested/polar boundaries, fixed-trajectory replay, and retained adjoints are rejected. Fortran checks effective post-environment dry status before solver creation. The mode-3 dry `Full`, `ExplicitOnly`, and `ImplicitOnly` RHS tests found no dependence on stale imported `p/al` at a fixed state, both with matching and displaced W stage references; the ordinary RHS reconstructs pressure from current PH/T/MU. This does not extend to other physical options.

## Verification tied to source and executable

The changed Fortran, Registry, config, MPI-safety, and solver sources in the validation copy were byte-identical to commit `14d449d`. A fresh WRF build after `clean -a` regenerated `grid_config_rec_type` and the namelist with the new field, compiled `module_implicit_sdirk3.F`, and produced `wrf.exe` and `ideal.exe`. After the process-visible timestep, split-mode, and guarded scalar-control checks were finalized, the affected C++ archive was rebuilt and the final `wrf.exe` relinked. The final executable SHA-256 is `ddc95dcbd7f22ce4fd0a4074715dc369cd5609b979e6b997f19891a562685b58`; its C++ archive SHA-256 is `97d3504197833d48072ccee0a057c1689dc0252e66d11285a838e2ff2cc12dc5`. The final `ideal.exe` was not run or relinked after the last C++ adjustment.

The local stack is macOS, Apple Clang 21.0.0, GNU Fortran 15.2.0, Open MPI 5.0.9, Homebrew PyTorch 2.10.0, linked netCDF 4.9.3_2 and netCDF-Fortran 4.6.2. All model runs used one rank and one thread with the same `wrfinput_d01` SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, dry `em_b_wave`, `diff_opt=2`, `km_opt=1`, `khdif=1000`, `kvdif=10`. Individual namelist, log, output, source, and executable hashes are in [results.json](evidence/pr268_forward_carry/results.json). The [local raw evidence archive](</Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20260929/pr268_forward_carry/raw.tar.zst>) has SHA-256 `7f6ba43540e03459adbf6e36e3298e3a14a0408db9ca07367e634c81b1a22a87` and includes the final executable, configured Registry output, eleven run directories, and their inputs and logs.

The two affected CTests, `Config_Numeric_Contract` and `FP64_State_Handoff_Contract`, passed. The latter checks exact equality with PR #267's isolated FP64 handoff arm, default-off publication, changed host state and RHS coefficient rejection, skipped-timestep rejection, independent solver instances, reset/reseed, and cross-thread host-timestep publication. The full local CTest run registered 111 tests: 110 passed, while `Core_Archive_MakeParity` initially failed solely because the source-tree Make archive had not yet been built. After building that archive, the exact 24-member Make/CMake parity test passed. This is **110 plus the targeted parity rerun**, rather than an uninterrupted 111/111 run. The full log and Make build receipt are retained with the raw evidence. An initial remote fast-contract run at `282d331` exposed four direct `.item()` control checks; commit `14d449d` moved them to the existing `guarded_item` path, and the exact lint ratchet and affected CTest passed locally.

CMake installation also completed. The installed public interface header was byte-identical to its source, and every extracted object member of the installed static library matched the build library; the `.a` containers themselves differ in archive metadata.

## Actual WRF runs and RK3 reference

At 7.5 s to a 15 s endpoint, OFF output was byte-identical to the earlier FP32-handoff baseline; ON output was byte-identical to the local FP64-carry counterfactual. Both completed two steps. At 15 s to 240 s, OFF and ON each completed 16 steps and 48/48 implicit stages, with largest scaled Newton residual `5.1523e-9`, zero logged finite-difference JVP calls, positive dry column mass and potential temperature, and all 174 output floating-point variables finite in each of three files. OFF reproduced the archived #266 files byte-for-byte; ON reproduced the earlier carry-counterfactual files byte-for-byte and logged 15 internal-state reuses.

With the new namelist ON and `newton_tol=1e-10`, the 15 s endpoint ladder at h = 0.46875, 0.234375, 0.1171875, and 0.05859375 s completed 32, 64, 128, and 256 steps. It logged 31, 63, 127, and 255 internal-state reuses, all 96/192/384/768 implicit stages converged, zero finite-difference JVP calls, and finite output. Each output file was byte-identical to its earlier manual FP64-carry counterpart. PR #267's retained raw FP64 dumps from that counterpart give adjacent-solution rates W `2.969/2.992`, PH `2.985/2.998`, and near three for U/V/MU; the final production run verifies output parity with that path, **not a new direct internal-FP64 dump**. T was below a useful order signal. `newton_tol=1e-7` is not interchangeable with the tight solve for this refinement result.

Two negative WRF runs failed before forecast completion: `mp_physics=1` triggered the Fortran dry-mode preflight, and `split_explicit=true` triggered the solver's unsupported-mode check. Neither produced a successful forecast.

The archived split-explicit RK3 reference uses the same initial file, 15 s timestep, 240 s endpoint, and diffusion configuration. Final-field RMS differences are comparisons between methods, not exact-solution errors:

| Field | FP64 carry vs RK3 RMS | Carry vs FP32-handoff SDIRK3 RMS |
|---|---:|---:|
| U | `8.42259e-3` | `4.72603e-7` |
| V | `9.54241e-4` | `4.24379e-7` |
| W | `6.25708e-4` | `1.60108e-6` |
| PH | `1.33299` | `3.58653e-4` |
| T | `4.14257e-2` | `9.58431e-6` |
| MU | `4.49024e-1` | `3.55301e-5` |

The single-run sums of WRF's `Timing for main` step records are RK3 `0.26174 s`, SDIRK3 carry `6.35745 s`, and SDIRK3 carry OFF `6.37824 s`. These are neither whole-program wall times nor equal-accuracy costs; no runtime advantage over RK3 is claimed.

## Remaining work

The forward result is scoped to this dry, fixed-coefficient, single-domain setup. MPI decomposition, moist physics, supplied or state-dependent coefficients, time-varying boundaries, and long-run accuracy remain separate. The carry mode deliberately rejects retained adjoints and fixed-trajectory replay because their current records describe FP32 publication between steps. The next numerical milestone is to define and validate the adjoint of this **same** carried forward map, then measure temporal order and cost at an application-specific accuracy target. This change does not modify the ARK coefficients, RHS physics, Newton tolerance defaults, or RK3 path.
