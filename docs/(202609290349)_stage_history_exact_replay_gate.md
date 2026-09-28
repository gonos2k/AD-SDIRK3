# Stage-history diagnostic exact-replay gate

Local timestamp: 2026-09-29 03:49:33 JST (+0900)

## Context and change

The integrated source starts at `84d1c10b8a99d864cec87d72c75ca93e41dc1e5c`. In `wrf_sdirk3_stage_history_diag.h:1514-1531`, the diagnostic already treats an exact FP32 replay as authority when aggregate FP64 `hist_rel` exceeds `1e-6`; however, it independently rejected `hist_max_rel>0.1`. A valid rounded stage could therefore pass exact recurrence but still emit `SDIRK3_STAGE_OPERAND_CLOSURE_FAILED` from the per-element metric. Its caller at `wrf_sdirk3_tile_unified_impl.cpp:10072` treats a nonempty diagnostic result as a controlled fatal, so this was a diagnostic false positive that could preempt later solver outcomes.

The gate now computes the exact FP32 recurrence whenever either existing metric exceeds its unchanged limit. If all values are finite, bitwise equality with `U_stage` authorizes the history; if replay differs, either exceeded metric still fails. No threshold was relaxed, and no solver equations or physical RHS code changed.

Contract case55 uses `U_n=1,000,000` and an increment of `0.03125`, half an FP32 ULP at that base. The actual rounded history is zero, while the FP64 increment sum is nonzero. It asserts `hist_rel=1`, `hist_max_rel=3.125e298`, exact FP32 replay, and acceptance. A 3x coefficient mutation changes the replay and must still fail closure. The existing stage contract ratchet increases from 155 to 159 checks.

## Validation

Built the contract and core with AppleClang 21 and homogeneous Homebrew Torch 2.10.0 (`CMAKE_PREFIX_PATH=/opt/homebrew`, `Torch_DIR=/opt/homebrew/share/cmake/Torch`). Standalone tests use Apple libc++; this does not validate the production WRF ABI0 link.

- `Stage_Operand_Decomposition_Contract`: passed 1/1; 159/159 internal checks, 3.42 s.
- `Full_Tile_Step_Adjoint`: passed 1/1, 21.89 s. This existing fixture toggles `stage_operand_diag` on for the hybrid tile and verifies accepted history records for stages 2/3 and exact per-source attribution.
- The case55 direct output records `hist_rel=1.000000e+00`, `hist_max_rel=3.125000e+298`, `fp32_replay_exact=1`. The mutated coefficient produces `SDIRK3_STAGE_OPERAND_CLOSURE_FAILED` with `hist_max_rel=9.375000e+298`.

No exact-source WRF executable was available for strict `test/em_b_wave` diagnostic ON/OFF. The integration checkpoint documents that no integrated WRF build or strict run was performed; I did not reuse an older ABI-linked archive. Thus the strict WRF diagnostic result remains unverified. No forecast, RK3 comparison, or claim that the separate Stage-2 convergence stall is fixed.

## Provenance

- Worktree: `agent/p2-stage-history-replay-20260929`, base revision `84d1c10b8a99d864cec87d72c75ca93e41dc1e5c`.
- Header SHA-256: `66000e5c623c6b54de7595914afbbdda41fb7b62a326225b47f619d7d6899bfd`.
- Contract test SHA-256: `0b909eaaae9ac282f475c209f1bb19af70ffbd0393bf0bbb270112d41b8c3795`.
- Unchanged production caller SHA-256: `5bdcddeea7c8e6bb7ce52adba66f55f8c391738e54e538964b4d5644ae81f832`.
- Contract executable SHA-256: `3209a133eef80a611c25f0eb4942a28b4708343a855f1c2c19a36062b7e5b5ea`.
- Full-tile executable SHA-256: `cdd6c7e0988c42dc8e43c785e63f5fd5df83f85f059b89985729c2bfffaa8899`.
- Core archive SHA-256: `435b06b64c695e5f76bad111201042c51717cd65d352c9bc668d19a4ab425de2`.
- CMake cache SHA-256: `e5658afe4be094bb549f9b59339d2dbdcde5eae2634d0b8bdd932100b4dbbfb7`.
- CTest inventory remains 109 names.
- Graphify refreshed after edits on the focused header/caller/contract-test corpus at `/private/tmp/sdirk3-p2-stage-history-graph-pre-20260929`: 196 nodes, 593 edges, 14 communities. Metadata records base commit `84d1c10b`; both edited files were re-extracted. Graph artifacts remain outside the repository. `CMakeLists.txt` was excluded because Graphify classified it as documentation; the CTest registration/inventory was verified directly.

## Next action

Red review the exact-replay gate and case55. Keep strict WRF ON/OFF evidence open until an ABI-matched executable is available.
