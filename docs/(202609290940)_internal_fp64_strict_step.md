# Internal FP64 K-form path: strict step and 240 s forecast

Local timestamp: 2026-09-29 09:40:03 JST (Asia/Tokyo).

## Context and change

Base source is `main` at `300f624923193d822d85a73ea9eb182a604ce794` (PR #265). Its strict 15 s `em_b_wave` case stopped in implicit Stage 2 with `R_last=1.274e-6` against `sdirk3_newton_tol=1e-7`. The same input completed under split-explicit RK3. No V/W conversion or trust-region fallback was changed here.

`sdirk3_internal_fp64` is a default-off ARK mode-3 option, wired through Registry, Fortran, C++ namelist/environment/setter/validation and effective output. On one CPU tile covering the domain, the FP32 WRF input is promoted before boundary projection and ARK stage assembly. Stage histories, the existing K-form Newton unknown/trial state, RHS work arrays, and base-state tensors use the evaluated state dtype. The completed state is cast once to FP32 for WRF output. The input and output casts remain in the retained one-step AD graph. The existing Newton equation, acceptance threshold, JVP method, and physical options are unchanged. The opt-in currently has direct evidence only for a dry, fixed-coefficient, one-rank case.

## Build and exact evidence

The isolated validation copy is `/private/tmp/sdirk3-strict-fp64-wrf-probe-20260929`. An initial incremental build failed because new Registry fields met old Fortran `.mod` files. In that disposable copy, `clean -a` followed by a Registry regeneration and `./compile em_b_wave` rebuilt the affected WRF objects. The final C++ source correction was compiled and relinked after that build. This was not a fresh checkout build; it reused the copied source tree and then cleaned its build products.

| Artifact | SHA-256 |
| --- | --- |
| Final `wrf.exe` | `3c2ace45c4721bfae844d430aede05fe8e929a3aa89357faf0a44f375e6bd702` |
| Production SDIRK3 archive | `99968c3fdd0a2052e71f2cc871a215ca1f3736d953cb339c37e7ec8009b74212` |
| Changed tile implementation | `667fb12a0f5d65f0a365cf16091ada63718a605ef2c6ddbf4280e31f50d4984e` |
| `configure.wrf` | `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab` |
| Strict-ON namelist | `a4434efb24786212cf4e12d512c6b19a8edc59c796c15f7523a1c24c9e8f55d9` |
| Shared `wrfinput_d01` | `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9` |
| Strict-ON `rsl.error.0000` | `5340f01f3040bf0b5c937be73e605cb0aeb3c702fcbf209370a21c04f4b8a17f` |
| Non-MPI CTest log | `0fb3f19a4fb23c941e38c78610cdedb21ef18a87633281c6fca51e0489eff727` |

The validation stack was GNU Fortran 15.2.0, Open MPI 5.0.9, and Homebrew PyTorch/libtorch 2.10.0. `mpiexec -np 1` stalled before WRF produced an `rsl` log while trying a host network connection. The one-rank executable ran to completion as an MPI singleton (`./wrf.exe`, `OMP_NUM_THREADS=1`). This does not validate MPI launch or multiple ranks.

The source-level CMake build and 103/103 non-MPI CTests passed, including default-FP32 full-tile adjoint, internal-FP64 one-step/dtype/active-diffusion adjoint, numeric config, and production-Make/core-archive parity. Six MPI tests and the MPS-only test were not run locally. The Make archive parity test used the production archive built from the same copied source, temporarily linked into the source worktree; that temporary symlink was removed after testing.

## Strict forecast and same-binary control

With `diff_opt=2`, `khdif=1000`, `kvdif=10`, `dt=15 s`, and the original `1e-7` Newton tolerance, the final executable completed 240 s: 16 steps and all 48 implicit stages converged. The largest reported scaled Newton residual was `5.1523e-9`; there was no `ZeroStepStall` or logged forward-AD-to-FD JVP fallback. The three NetCDF frames each had 174 finite floating fields. Across the frames, the minimum dry full-column mass was at least `89088.63`, the minimum full potential temperature at least `302.25 K`, and the minimum W-level thickness at least `942.65 m`.

The *same executable* with `internal_fp64=false` stopped in Stage 2 before its first completed step, with `R_last=1.274e-6` and fail-closed outcome 20. Its initial output SHA-256 was `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`, matching the archived pre-change strict output. Thus the default path retained this case's prior behavior; it was not silently rescued by a relaxed acceptance rule.

## RK3 and time-step comparison

The archived pre-change split-explicit `wrf.exe` (`8f7d66c12a124bde6e9098e7b9933d436b5dcd47ea62b28fb2291b7c9f9af3da`) completed a new 240 s RK3 control using the same `wrfinput_d01` and strict namelist settings apart from `time_integration_scheme=0`. At the final frame, SDIRK3-minus-RK3 RMS differences were U `8.42e-3`, V `9.54e-4`, W `6.26e-4`, PH `1.333`, T `4.14e-2`, MU `4.49e-1` in each output variable's units. RK3 is not the exact solution. One-run sums of WRF's `Timing for main` lines were `6.44013 s` for SDIRK3 and `0.26174 s` for RK3; these are step-log sums, not end-to-end wall times or same-accuracy performance evidence.

The final executable also completed 240 s at `h=15, 7.5, 3.75, 1.875 s`, with respectively 16, 32, 64, 128 steps and 48, 96, 192, 384 converged implicit stages. No JVP FD fallback was logged. At 240 s, RMS differences for U over adjacent refinements were `9.55e-7`, `1.71e-6`, `4.14e-6`; for W, `2.22e-5`, `1.76e-4`, `7.15e-5`; for PH, `5.33e-3`, `3.92e-3`, `6.62e-3`. These do **not** establish third-order time convergence. Tightening the 15 s Newton tolerance from `1e-7` to `1e-9` still completed all 48 stages; final-field RMS differences from the original solve were U `3.59e-8`, W `2.90e-7`, PH `4.01e-5`, smaller than the adjacent time-step differences. The cause of the non-monotone refinement differences remains open; possible FP32 inter-step handoff, step-dependent RHS/physics, and boundary handling need separation, not a post-hoc acceptance threshold.

## Next actions and limits

The strict one-step and 240 s multi-step convergence gates are closed for this exact dry one-rank setup. Time order, longer stability, same-accuracy RK3 performance, moist/variable-coefficient cases, MPI, and a whole-WRF active multi-step adjoint remain open. The one-step tile ON test checks the active diffusion increment's pullback, but it is not an adjoint of the entire Fortran/WRF call chain. The next numerical task is to isolate the time-refinement discrepancy using the same equation and step context, then test the active multi-step adjoint of that accepted forward path.
