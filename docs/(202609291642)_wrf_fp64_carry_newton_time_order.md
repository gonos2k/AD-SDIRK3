# WRF FP64 carry and Newton-tolerance time refinement

Local time: **2026-09-29 16:42:23 JST**. This follows PR #267 HEAD `9ee6c7bd600de1861d7813d27af1ae56e4db9e7e` and the merged #266 internal-FP64 implementation. No production numerical source changed in this report.

## Question and controlled setup

The strict dry `em_b_wave` WRF forecast converges with FP64 calculations inside each ARK step, but FP32 publication between steps obscured temporal refinement. PR #267's autonomous tile A/B showed that inter-step precision matters, while a serial WRF counterfactual carrying FP64 state still gave nonmonotone W/PH differences at small steps. The new experiment holds that FP64 carry, initial state, model configuration, and 15 s endpoint fixed and changes only the Newton absolute tolerance from `1e-7` to `1e-10`. A `1e-11` control checks the last two resolutions.

The copied WRF executable is **not** the PR's production path. It has a local, thread-local FP64 carry probe that reuses the last projected FP64 six-field state only if the next packed Fortran FP32 state exactly matches its publication; it also dumps the final packed FP64 state. This guard does not cover all Fortran diagnostics, boundary context, solver identity, retained adjoints, MPI, or multiple tiles. The [complete experimental C++ difference](evidence/pr267_time_refinement/experimental_source.patch) has 50 lines. The unchanged Fortran bridge, `solve_em.F`, header, and config source are byte-identical to PR #267. The experimental C++ SHA-256 is `f7877b2484b0029c4fc28cbf2a3f0cfd235af0fbe8b551c2720c5bdc5c0624ac`; the relinked `wrf.exe` SHA-256 is `94d3b145a18cea78713601a8f5fc89de8e12b686bfcd850d9653ab704b13e6a4`.

All cases use the same archived `wrfinput_d01` (SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`), one MPI rank and one thread, `diff_opt=2`, `km_opt=1`, `khdif=1000`, `kvdif=10`, dry fixed coefficients, and the same 15 s final time. Each final raw dump has 24,531 Float64 values in packed U/V/W/PH/T/MU order, including edge entries. NetCDF output has frames at 0 and 15 s, but the rates below use **internal FP64** dumps. NetCDF `T` is a derived `th_phy_m_t0` diagnostic; `THM` maps to the prognostic `t`, and FP32 output quantization can hide its differences.

The local stack is macOS with Apple Clang 21.0.0, GNU Fortran 15.2.0, Open MPI 5.0.9, and Homebrew PyTorch 2.10.0. The executable links the Homebrew netCDF 4.9.3_2 and netCDF-Fortran 4.6.2 libraries. The raw archive includes the exact executable and configured namelists, rather than relying on today's `nf-config` output for its link provenance.

## Result

At `1e-7`, the solver accepted the initial implicit-stage guess after one residual evaluation 0, 4, 152, and 607 times at h = 0.46875, 0.234375, 0.1171875, and 0.05859375 s respectively. Each h has 32, 64, 128, and 256 completed steps, or 96, 192, 384, and 768 converged implicit stages. The largest scaled exit residual for the last three h was `9.5482e-8`, `9.9244e-8`, and `9.9974e-8`, just under the configured `1e-7` threshold. At `1e-10`, all those stage counts still completed, with **zero** initial-guess acceptances; the largest scaled exit residuals were at most `1.2864e-14` across the ladder. No gate abort occurred.

Adjacent-resolution RMS differences at 15 s are `D_h=||U_h-U_{h/2}||` on each full packed field. The order estimate is `log2(D_h/D_{h/2})`, not an exact-solution error order.

| Field | `1e-10` RMS at h=.46875/.234375/.1171875 pairs | Observed orders | `1e-7` finest two orders |
|---|---:|---:|---:|
| U | `1.10374e-10, 1.44726e-11, 1.85364e-12` | `2.931, 2.965` | `-3.124, -1.066` |
| V | `1.64304e-10, 2.15900e-11, 2.76522e-12` | `2.928, 2.965` | `-3.080, -1.111` |
| W | `3.79850e-7, 4.85192e-8, 6.09678e-9` | `2.969, 2.992` | `-3.025, -1.112` |
| PH | `7.90395e-6, 9.98094e-7, 1.24926e-7` | `2.985, 2.998` | `-3.045, -1.048` |
| T | `6.76954e-13, 1.29074e-13, 1.21517e-13` | `2.391, 0.087` | `-3.255, -0.888` |
| MU | `1.32271e-11, 1.63762e-12, 2.05155e-13` | `3.014, 2.997` | `-3.255, -0.888` |

T is at a much smaller signal level and does not support a third-order claim here. For the two finest h, lowering the Newton tolerance again from `1e-10` to `1e-11` produced **bitwise-identical final packed FP64 states** in all six blocks. Thus this result is not resolved by merely asking for a still tighter nonlinear solve over that interval. [Machine-readable cases, hashes, and full differences](evidence/pr267_time_refinement/results.json) are included. The raw WRF run inputs/logs/dumps, source, and executable are retained locally in `~/Documents/AD-SDIRK3-worktree-archive/20260929/pr267_time_refinement/raw.tar.zst` (SHA-256 `ffb382e635b4513a22a36c2d0ecf207e9a521da793a11bb0dea29edc51a3eeac`).

## Interpretation and next action

For this same serial dry WRF counterfactual, the fixed `1e-7` scaled Newton threshold permits stage guesses to pass without correction as h shrinks, and that tolerance-controlled branch explains the observed fine-h reversal. With the same FP64 carry and `1e-10` criterion, U/V/W/PH/MU show adjacent-solution rates close to three over three spacings; the extra `1e-11` control leaves the fine solutions unchanged. This is evidence of temporal self-convergence for a **locally altered forward trajectory**, not a production WRF third-order or exact-solution accuracy qualification.

The smallest forward implementation task remains explicit, solver-owned FP64 state continuity with a contract for Fortran's post-step updates; the present thread-local probe is not that implementation. Keep the published FP32 state as the external interface, but reject or consistently incorporate any changed host input before reusing FP64 state. Validate the production path against this ladder and retain the `1e-10` solve-error control when measuring order. Then test the active multistep pullback on the same forward map. No new same-setup RK3 forecast or runtime comparison was performed in this experiment; the archived RK3 reference was not rerun.
