# T1 Stage 2 rejected-trial snapshot

Recorded: 2026-09-26 23:09 JST

## Change and scope

The strict PC2 `dt=15 s` failure reproduces at Stage 2, but enabling the existing `stage_operand_diag` switch aborts earlier on its independent stage-history closure (`hist_rel=1.574734e-2`) before Newton reaches a trust-region trial. I added a separate default-off `sdirk3_stage2_rejection_snapshot_diag` switch. It is wired through Registry (default `.false.`), the Fortran config setter, C++ namelist and environment parsing, the runtime bool setter, config validation/effective output, and a single-rank/whole-patch preflight. The existing history-closure test and its fail-closed behavior remain intact.

When enabled, the Newton solver observes the first rejected candidate that reaches the common Stage 2 trust-decision site, after production `R_trial` and trust metrics already exist. It saves packed `{U_n,U_stage,K,U_eval,F,R,dK,dK_trial,K_trial,U_trial,F_trial,R_trial,S,S_inv,halo_mask,gmres_r_true}` plus trust metadata. It makes no additional RHS, JVP, or preconditioner calls and does not alter acceptance. The JSON scope identifies this as the *common trust-decision* path; earlier GMRES-quality `continue` and recovery-fallback paths are excluded. The one-shot flag is per solver instance. The `.pt` archive is published last and marks successful capture.

## Validation

A clean WRF build from PR #241 base `9513b3aad2eb494077206f2a60bb1774bccdac78` completed, followed by an incremental rebuild/relink for the final scope comment. The clean build exposed a Registry dependency issue: changing the included `Registry.EM_SDIRK3_OPTIMIZATIONS` file did not invalidate generated `Registry/Registry`. I forced regeneration by touching the authoritative `Registry/Registry.EM`, then found old Fortran modules still using the prior `grid_config_rec_type`. A clean rebuild removed that ABI mismatch. The generated `frame/module_configure.f90` contains the new field; it is generated build output and is not committed.

The full CTest inventory passed 103/103 after config wiring. After the snapshot schema/scope refinement, the two directly affected contracts, `Stage2_Rejection_Snapshot_Archive` and `WRF_Dynamics_Config`, passed 2/2. Graphify was checked before changes at the focused solver corpus (8 files, 247 nodes, 708 edges) and refreshed after changes for 11 relevant C++/Fortran/Registry files (253 nodes, 717 edges).

Using the archived tight namelist, initial input, and diagnostics sidecar, both final-binary runs exited 1 at the same Stage 2 strict-tolerance failure. The OFF and snapshot-ON runs reached the same terminal residual (`1.3034e-6`, 7 Newton iterations, 301 Krylov iterations); their partial `wrfout` files are byte-identical. The snapshot run captured the first common trust rejection at Newton iter 1, attempt 0, with step fraction 1, `rho=-0.11448129369445813`, actual reduction `-8.243886587024128e-9`, predicted reduction `7.201077417090022e-8`, and acceptance threshold 0.25. The solver continued after this rejected trial and later stalled after three zero-update iterations at Newton iter 6. This is a rejected-trial record, not a capture of the terminal iteration or a precision-floor diagnosis.

The archived tensor archive has 24,531-element FP32 packed vectors. An independent read-only archive audit found exact `R=K-F`, `R_trial=K_trial-F_trial`, and `K_trial=K+dK_trial` closures; `S_inv*S` is exact in this artifact. The stored scaled residual norms and actual trust reduction reproduce the JSON metadata. These checks use captured FP32 values; double-precision reductions over them do **not** constitute an FP64 RHS evaluation.

A separate `RHS_DETERMINISM_CHECK` run reports `||F1-F2||/||F1||=0` at the Newton iter-0 input and has the same partial output. This verifies repeat determinism at that one input only. The live `compute_rhs` closure and all mutable tile/forcing/operator state are not serialized, so I did not run an alpha ladder or claim faithful offline RHS replay. Precision-versus-model attribution remains unresolved. No negative control or RK3 comparison was run.

## Provenance

Final source branch: `agent/t1-stage2-first-failure-snapshot`; source commit: `3751bc984087ecf97038aa396c95e44dfaf855a5` (based on PR #241 HEAD above). Toolchain: macOS arm64, Apple clang 21.0.0, GNU Fortran 15.2.0, Open MPI 5.0.9, LibTorch 2.10.0, netCDF 4.9.3 / netCDF-Fortran 4.6.2.

SHA-256 values:

- `configure.wrf`: `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab`
- final `main/wrf.exe`: `cff867d6a291efb33fab913c17080c4fa20dbd78696d98e79791613620ebd059`
- linked `libwrf_sdirk3_libtorch.a`: `cc325873d1bb7f6285769b04582094589b41d31c07a22e02712e4a3511f2cc21`
- `wrfinput_d01`: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`
- tight `namelist.input`: `c9c8a0f1a8a66318de05631f9afae666a6fc793598d6cccf0924ec27f98b9e0b`
- `diagnostics.txt`: `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`
- OFF `rsl.error.0000`: `ae4c17c5de8e9c7cbd15d1de387c1f99443bdda721bd106a68f0da4f851076f8`
- ON `rsl.error.0000`: `2b94f022bb4391e6ac5da9a8804bea3ae6006d85187c66e21b09bae7b607b13d`
- ON snapshot archive: `bf806fd5ee4de04949e68640a94bf1df891df76574d01ed27ab1410938a3fe71`
- ON snapshot metadata: `767b8890218f5a504aedd4ab7eaab7c0c9b4b4e18df8cd3e5653292720db08ee`
- OFF, ON, and RHS-repeat partial `wrfout`: `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`

Raw run files and the clean-build/Registry rebuild logs are under `.validation/t1_stage2_snapshot/`. The implementation branch is separate; nothing was pushed and no PR was opened.

## Final PR #244 candidate evidence

Recorded: 2026-09-26 23:52 JST

The PR candidate branch `agent/t1-pr244-final` is based on the exact updated PR #243 head `a1d5f6a3c8c64e66510c81d5fbe98ce568ed180f`. It carries the T1 snapshot source/test, standalone report, and 104-test inventory updates only; the W/V reports are inherited from the base. The T1 production and test source is byte-identical to the source in the earlier integrated run tree. The base update since that run changes only the two W/V documentation files; there are no production or test-source differences.

A clean configure and build on this final branch completed 217/217. The integrated U/V/W source-parity oracle, `Stage2_Rejection_Snapshot_Archive`, and `WRF_Dynamics_Config` passed 3/3 on this exact branch. A full 104-test CTest run on the source-identical integrated tree `agent/t1-on-vw-integration` at HEAD `72d5a1cedf2616235d239622fb4bc19112fcd8c0` completed **104/104** with `--parallel 2` in 141.25 seconds, including `Core_Archive_MakeParity`. The full output detail is preserved at `.validation/t1-integrated-ctest-104-final.log`, SHA-256 `2bbd4d998f6becb1dec2302c0d3f76b0208e5addb83ff90c1d0334dcc116210f` (2,063,514 bytes). That local log is copied unchanged into the final candidate worktree. The Make archive used by this run has SHA-256 `05654e5272efd1c0aad19996f0ff7ace34be853b05f81f66e48b3e46cfe03178`.

The 104 pinned test names are unique and the three inventory references state 104. This exact-head candidate has a clean local worktree. Full WRF build/model evidence remains the standalone T1 result on PR #241 base `9513b3a`; no combined WRF rebuild or `dt=15 s` run was performed.
