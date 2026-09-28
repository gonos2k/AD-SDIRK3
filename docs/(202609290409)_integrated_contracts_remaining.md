# Integrated contracts and remaining acceptance gates

Local timestamp: 2026-09-29 04:09:35 JST (Asia/Tokyo).

## Context and changes

At source `454e3a531c2b04322226df2e1460890b8fd1914b` (tree `a0c6b18d348b328a336d444ce2fcff106ff229ae`), the integration branch contains upstream `main` plus the reviewed W option-2 and T1 sibling stacks, the native W whole-step test, and the stage-history diagnostic replay fix. The pre-existing strict 15-second Stage-2 stall is not fixed. No experimental VJP fallback is in production source.

The native option-2 W whole-step test exercises a fixed scalar `Kv` with an active W direction through one accepted mode-3 step. Its ON−OFF step-increment FD/VJP comparison resolves at both predeclared widths. PH resolves only at the wider width, and MU remains below the FP32 output-quantization estimate; those two whole-step derivative directions remain open. The direct W RHS PH/MU contract from #264 has a narrower, `ExplicitOnly` scope.

The opt-in stage-history gate now treats an exact, finite FP32 replay of the production recurrence as the authority when either pre-existing FP64-relative history metric is large. Its new half-ULP positive control and wrong-coefficient negative control pass. The strict-input disposable probe that motivated it found `fp32_replay_exact=1` at a zero-net PH history cell, so the earlier stage-history closure fatal was a diagnostic false positive. This does not remove the independent Stage-2 convergence failure, nor prove that every small PH update is accurately representable.

## Validation tied to this source

The homogeneous Homebrew PyTorch 2.10 standalone CMake build completed, and the production Make archive was rebuilt from the same source with `LIBTORCH_ABI=0`. `Core_Archive_MakeParity` passed; it checks the shared 24-member manifest, not byte identity across compilers. The earlier full CTest run established that 108 tests passed and the sole failure was an absent Make parity archive; after building it, the missing test passed. After the W test and diagnostic changes, their affected CTests passed: the W actual-RHS contract, W RHS AD contract, `Full_Tile_Step_Adjoint`, `Stage_Operand_Decomposition_Contract` (159/159 internal checks), and Make parity. A complete 109-test rerun on this final code revision is reserved for exact-head remote CI.

CMake archive SHA-256: `b3fd5f93f96b528c1e188e2b6cc7f1ea3659784dc27a0a99559199a43c04daff`; production Make archive SHA-256: `798fd2dc36f1594fdd288cbcbdcae883f0cd7ac3b0b35b42cfc66fc9eee4d7ed`. `test_full_tile_step` SHA-256: `f954e3324da2fcb14838fd0a3819b3605283abaae3a09b132c8156f3729cf1ef`; `test_stage_operand_decomposition_contract` SHA-256: `3209a133eef80a611c25f0eb4942a28b4708343a855f1c2c19a36062b7e5b5ea`.

An affected-component WRF rebuild/relink from the protected PR #246 clean object set produced an ABI0-linked `wrf.exe` containing this Fortran source and C++ archive. With the same strict 15 s input, diagnostic OFF and ON both reach identical Stage-2 `ZeroStepStall` at iter9; ON prints `fp32_replay_exact=1` and zero per-source reapplied-delta residual rather than the former closure fatal. The partial `wrfout` and terminal snapshots are byte-identical. See `docs/(202609290408)_integrated_wrf_stage_diag_on_off.md` for exact inputs, executable and output hashes. This is not a new whole-tree clean WRF build or a completed forecast. No new same-setup archived RK3 field/runtime comparison was performed. The bounded T1 retry prototype is evidence about its separate experimental executable, not a forecast qualification of this integration tree.

## Remaining checklist

| Item | Current disposition | Next closure evidence |
| --- | --- | --- |
| Branch integration | Draft PR #265 open; local build, affected contracts, and bounded WRF diagnostic ON/OFF verified | Finish exact-head remote CI and review; keep draft until numerical acceptance gates close. |
| Native W whole-step derivative | W state direction closed for the specified dry single-tile fixture | Resolve PH/MU above the FP32 floor without post-hoc tolerance changes; then broaden state/geometry. |
| Stage-history opt-in diagnostic | Exact-replay false positive fixed in standalone contracts and matching ABI0 `em_b_wave` ON/OFF run | Preserve the exact source/executable/input receipt; broader topology remains outside this single-rank result. |
| Strict 15-second T1 | Open; integrated-head WRF run still stalls at Stage 2 iter9, `R_last=1.274e-6`. The separate three-scale fallback prototype also stalls above `1e-7`, with 9 scales beyond the old one-candidate allowance: 5 use remaining shared RHS tokens and 4 overrun that shared budget. | Find a mathematically justified affordable direction/model or reject this timestep; require a complete forecast before comparing cost. |
| L34 full spatial operator | Open beyond bounded W and other existing component contracts | Same-state U/V/W/scalar Fortran parity with variable coefficients, real boundaries/maps and production decomposition. |
| G1 fully weighted budget/time order | Open | Stage flux/source and layer-mass/area budget; timestep refinement above solve error. |
| A1 full active adjoint | Open | Whole-call packing/boundary/halo transpose and state-dependent coefficient policy with an actual objective derivative check. |

The next useful action is to finish exact-head integration CI, then investigate the persistent Stage-2 convergence failure independently of the repaired diagnostic. A whole-tree clean WRF build and same-setup RK3 comparison remain open. The experimental T1 retry is not an accepted solver change.
