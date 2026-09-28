# PR #265 current checklist after fresh WRF validation

Local timestamp: 2026-09-29 05:00:51 JST (Asia/Tokyo).

## Context and progress

Draft PR #265 integrated the reviewed W option-2 and T1 branches. The numerical source was last changed at `454e3a531c2b04322226df2e1460890b8fd1914b`; subsequent commits are dated evidence and checklist reports. On source HEAD `14e1dbd051ffb438c3f4a69c35141fa4e4114e88`, [CI run 36470423808](https://github.com/gonos2k/AD-SDIRK3/actions/runs/36470423808) passed all four required jobs. Linux CTest registered 109 tests: 108 passed, one MPS-only test skipped, zero failed. Fortran/C++ lifecycle ABI, option-2 parity, and install-contract steps passed. Any later docs-only PR HEAD should still receive its own exact-head CI receipt before review completion.

An entirely fresh worktree at the PR source HEAD initialized MYNN at pinned commit `90f36c25259ec1960b24325f5b29ac7c5adeac73`, reused only the validated `configure.wrf` recipe, and compiled `em_b_wave` from source without copied objects. The build produced `ideal.exe` and `wrf.exe`; the fresh executable hash is `8f7d66c12a124bde6e9098e7b9933d436b5dcd47ea62b28fb2291b7c9f9af3da`. In a one-rank strict 15 s ON/OFF pair, the repaired stage-history diagnostic passed exact FP32 replay and per-source reapply checks, while both arms still stopped at the same Stage-2 iter9 `ZeroStepStall` (`R_last=1.274e-6`) before any forecast step. Partial `wrfout` and terminal JSON/PT were byte-identical across arms and with the earlier affected-component relink. The exact build, input and output hashes are in `docs/(202609290455)_pr265_fresh_wrf_build_stage2.md`.

A separate **local, output-only** T1 probe evaluated one saved Newton-base candidate `K′=F(U_eval)` with one RHS call at its changed FP32 stage state. It reduced scaled residual RMS from `1.273917405e-6` to `4.417670709e-7`, still above `1e-7`; its scaled displacement was `199.5257` times the saved trust radius. It was never accepted and proves neither a precision floor nor a converged forecast. Its standalone diagnostic TU used clang++ `-O2` rather than WRF's normal g++ `-O3`; source/header/object/archive/executable provenance and same-binary controls are in `docs/(202609290500)_t1_fp32_base_f_candidate_probe.md`. No experimental Newton code is in this PR.

## Remaining checklist

| Area | Disposition | Required next evidence |
| --- | --- | --- |
| Branch/build integration | Fresh full WRF build, required CI, and bounded diagnostic ON/OFF now pass for the stated configurations. PR remains draft. | Review the latest docs-only HEAD CI; preserve exact source/configuration/executable/input receipts. |
| Stage-history opt-in diagnostic | Former zero-net-PH false positive closed for the strict single-rank case; exact replay and per-source checks pass. | Broader topology only if declared supported. |
| Native W whole-step derivative | W state direction passes two-width ON−OFF FD/VJP; PH/MU directions remain under the FP32 output floor. | Resolve PH/MU with a predeclared distinguishable signal and then broaden state/geometry. |
| Strict 15 s T1 | Open. Fresh WRF build still fails Stage 2; bounded VJP retry and one `K′=F` candidate do not converge or meet the original candidate budget. | A justified affordable solver step or explicit timestep rejection, followed by a completed same-source forecast. Do not relax tolerance from these observations. |
| L34 full spatial operator | Open beyond bounded scalar/U/V/W contracts. | Same-state Fortran/C++ parity with variable coefficients, actual boundaries/maps and production decomposition. |
| G1 budget/time order | Open. | Stage flux/source and layer-mass/area budgets; timestep refinement above solver error. |
| A1 whole-call active adjoint | Open. | Packing, physical boundary and halo transpose plus state-dependent coefficient policy in an actual objective derivative check. |

No new same-setup RK3 field/runtime comparison was performed, because strict SDIRK3 stopped before a forecast field existed. `ideal.exe` was built but not run; both strict arms reused the same archived initial condition. The fresh build and diagnostic fix are substantial engineering progress, but do not qualify the failed timestep for forecast or data-assimilation use.
