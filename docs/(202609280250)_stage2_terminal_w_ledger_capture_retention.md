# Stage-2 terminal W ledger capture and retention

Local timestamp: 2026-09-28 02:50 JST

## Context and change

This diagnostic extends the existing default-off Stage-2 rejection snapshot for the strict `em_b_wave` Stage 2 `ImplicitOnly` Newton base RHS. It takes the already-produced W coupled-term inventory only around the production `F(U_eval)` call, records the same-call W conversion bridge and final packed W, and validates the ledger before publishing a schema-4 terminal receipt. JVP, trial RHS, and replay paths are not instrumented; no RHS call or numerical decision was added.

The initial ON run exposed a retention bug, not a TLS capture failure: Newton and Tile used the same thread-local slot, Tile observed it armed, and all nine required terms were captured. A base RHS ledger was then moved into each rejected trust-attempt snapshot. When one Newton iteration had multiple rejected attempts, the first move emptied the pending ledger, so the later terminal candidate failed closed as missing every term. The fix retains the detached, cloned base ledger by tensor-handle copy for each attempt. A contract fixture now checks two rejected attempts both retain a valid ledger.

Capture is limited to the existing snapshot gate and effective split mode >=2; other modes retain their prior schema-3 snapshot behavior. The raw input identity is the caller-side Newton `U_eval` FP32 digest; it is not a separate hash of a full-halo tensor after any callee transformation. The W bridge records the active canonical/fallback conversion operands and post-sanitization W tendency. Publishing requires the expected nine-term inventory, a payload under 32 MiB, matching candidate identity, source-operation-order FP32 conversion equality, exact `K[W]-F[W] == R[W]`, and exact same-call final packed W == `F[W]`.

## Source and graph provenance

Base revision: `ff33d7c47093c78eb32ee358bff8e2bbab883322` (`PR #250` head). The isolated worktree is `/private/tmp/sdirk3-terminal-r-w-ledger-20260928`; no root worktree files were changed. The final source file SHA256 values are:

- `wrf_sdirk3_newton_solver.cpp`: `5fe5daf5dc8704896de54fa0109bf64d8a7c78ca2f1b83f77b30a4bbefc59893`
- `wrf_sdirk3_tile_unified_impl.cpp`: `5bdcddeea7c8e6bb7ce52adba66f55f8c391738e54e538964b4d5644ae81f832`
- `wrf_sdirk3_rw_term_capture.h`: `afc06ead0a51ca93157a22966f571d01ae16043221a19fbebe73597adbc9eb58`
- `wrf_sdirk3_stage2_rejection_snapshot.h`: `a006ee94739964646b9a6c4c5c0440bac1932ef4b92c8cfb2b9eb9ea29e28155`
- `test_rw_term_capture_contract.cpp`: `0b1597e86c818677ada39c9deea672575c402a3c0a63a17224b0f6eb83fc605a`
- `test_stage2_rejection_snapshot.cpp`: `74419d5b9c75c1d1ca9264ab8d366a80e8f69381ae0050769b63c418b1ab7cfa`

Graphify pre-edit was built from the exact base revision for the focused seven-file dependency corpus (482 nodes, 2,038 edges; graph SHA256 `e37a9da7aa48efe7027dafcc9ef32737bae1ca8a0e7f6e83a086744172cf1650`). The post-edit focused corpus was rebuilt from the final source copies (481 nodes, 2,046 edges; graph SHA256 `68f3e97e12c13961f47c014aeb3c9ba68715b448486866336ee34fa689ecfc3c`). The graph is a navigation aid; equations and receipts below were checked against source and runtime evidence.

## Validation

The targeted contracts passed 2/2: `RW_Term_Capture_Contract` and `Stage2_Rejection_Snapshot_Archive`. The one-time full standalone CTest run passed 107/107 in 254.39 seconds. Full CTest log SHA256: `80b7e6f1e8b3567611cfd56ce671fcff8fa806402b904021713cb557364f8af8`.

The production SDIRK3 archive was incrementally rebuilt from the two changed production translation units with Homebrew clang 22.1.4, C++17, LibTorch ABI 0, and linked first against the candidate archive. This was not a clean WRF rebuild: unchanged WRF objects/libraries were reused from the matching PR244 clean-build tree after confirming no Fortran, Registry, or WRF object ABI source changes. Open MPI was 5.0.9. Archive SHA256: `3dfa091eca710d0f079866d3d2c890e3527c20da079c5a584046fb8fa1ad2b76`. Candidate `wrf.exe` SHA256: `ff75a7a026264ca9608e6b5abbaec157466a8bf171243c63b8bfe3fa7c7574cd`.

Strict `em_b_wave` used one MPI rank, a 17x17 full-patch tile, split mode 3, 15-second timestep, and the same `wrfinput_d01` and `diagnostics.txt` for OFF and ON. Their hashes were respectively `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9` and `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`. The OFF namelist hash is `246a148486b8f5bc8d3e22fe03ca7a1ff7090fdf5d89a471ebf8e35642e84d54`; ON is `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`. The only namelist difference is `sdirk3_stage2_rejection_snapshot_diag = .false.` versus `.true.`.

Both OFF and ON runs exited with code 1 at the same Stage-2 zero-step-stall / stage-gate failure. Selected trust-attempt decisions, termination, first-failure classification, and stage-gate lines match. Each run emitted 359 ordered RHS digests and the complete sequences match exactly. The partial `wrfout_d01_0001-01-01_00:00:00` SHA256 is `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a` in both runs. ON published the schema-4 terminal receipt at Newton iter 6, trust attempt 1:

- Archive SHA256: `7aa6be225c04db7ad43af214e8c640aa748779581552ca498aac02b3087e91ca`
- Metadata SHA256: `3c054acc4f28ea527cb5c8c6615a59bdf33a6e0dfb1bae7a4f070ee04722cefb`
- Nine expected W terms; tensor payload 294,780 bytes, below the 32 MiB cap.
- `fp32_rw_mass_to_velocity_conversion_matches`, `fp32_k_minus_f_w_equals_r_w`, and `same_call_final_w_equals_f_w` are all true.
- The archive is an observed FP32 same-call term ledger, not an independent RHS replay. Metadata digest is the Newton caller-side `U_eval` digest.

An independent read of the archived FP32 tensors confirms the W block scaled residual norm is `2.1322215e-4` against total `2.1323514e-4` (squared share `0.9998782`). Saved-FP32 conversion operands recomputed in FP64 differ by at most `2.88e-11`; that diagnostic FP64 reduction is distinct from the exact source-order FP32 equality gate. The term sum and pre/post mask observations also close exactly in this archived sample. These are bounded observations, not a causal diagnosis. No FP64 failure cause, independent replay, or same-setup RK3 field/runtime comparison was established or attempted.

The same archive indicates a follow-up check worth prioritizing: the PH block `U_trial-U_eval` is nonzero in 607/4,913 entries, with magnitudes up to `4.8828e-4` at FP32 quantization increments. Its L2 difference from the separately constructed `dt*gamma*dK_trial_PH` is `9.498e-3`, while the latter has L2 `4.168e-4`. More directly, evaluating the saved-FP32 operation `U_eval_PH + (dt*gamma)*dK_trial_PH` in Torch order gives `U_eval_PH` bitwise in all 4,913 entries, while the source construction `U_stage + dt*gamma*K_trial` differs at 607 entries (L2 `9.709e-3`, maximum `4.8828e-4`). Independent checks confirm the saved states themselves equal their source recompositions bitwise. Each changed PH entry is a one-ULP jump, while the nominal `dt*gamma*dK_trial` is only 0.00363 to 0.0464 ULP there (median 0.0230 ULP). This strongly motivates a PH-only RHS control but does not attribute the W response or overall stall.

## Review and next action

The independent Red source/runtime review is pending for this final diff and evidence. Do not push or open a PR until root review and Red review are complete. If promoted, keep claims limited to bounded capture and behavioral neutrality; identifying the numerical stall mechanism requires follow-up same-state derivative / cross-variable analysis.
