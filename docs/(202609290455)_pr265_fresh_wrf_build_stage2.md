# PR #265 fresh WRF build and Stage-2 diagnostic pair

Local timestamp: 2026-09-29 04:55:39 JST (Asia/Tokyo).

## Context and build

Draft PR #265 source is `14e1dbd051ffb438c3f4a69c35141fa4e4114e88` (tree `3aaa3d8013b7d28e257a5d32349266ca9e176d74`). An isolated detached worktree at `/private/tmp/sdirk3-pr265-fresh-20260929` was created at that exact commit with no prebuilt WRF objects. Its only submodule, `phys/MYNN-EDMF`, was initialized at the pinned `90f36c25259ec1960b24325f5b29ac7c5adeac73`. The validated `configure.wrf` recipe from the PR #246 clean build was copied into this fresh tree; its paths use the current `WRF_SRC_ROOT_DIR` and its SHA-256 is `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab`. It selects GNU/OpenMPI WRF with SDIRK3, Homebrew Torch and NetCDF. No compiled object, module, archive, or executable was copied.

`J='-j 4' ./compile em_b_wave` completed successfully from 2026-09-29 04:36:52 to 04:53:48 JST and produced both `ideal.exe` and `wrf.exe`. The build log has ignored optional `test_io_idx`/`diffwrf` probe errors, followed by successful production archive and executable construction; no required build target failed. Build log SHA-256: `8d394c3d3644c9107960ee422eb94579ed5bd6540b202123bff9268115a1de23`.

| Fresh-build artifact | SHA-256 |
| --- | --- |
| `main/wrf.exe` | `8f7d66c12a124bde6e9098e7b9933d436b5dcd47ea62b28fb2291b7c9f9af3da` |
| `main/ideal.exe` | `697ebcc8dad6b7c2b7b85216b9437ddd31ce128bb191f259f9c313abb15f2ed7` |
| SDIRK3 production archive | `f6b4f216f17ba343693e1e736ae139d02d180844a6e8bcfa6005e880a072b682` |
| `dyn_em/module_implicit_sdirk3.F` | `4f6903102b53f0671de6d87458c5c20ef34b39688e267276b9296c0f4b1183d7` |

The fresh executable differs in bytes from the prior affected-component relink because the full source and C++ archive were rebuilt under the configured WRF toolchain. `otool -L` resolves Homebrew libtorch/libtorch_cpu/libc10, libgfortran and Open MPI. The fresh build makes no claim that `ideal.exe` was run: the strict experiment reused an archived initial condition.

## Same-binary strict 15 s pair

The one-rank, one-thread OFF/ON arms both used this fresh `wrf.exe` through the same relative symlink. `wrfinput_d01`, `namelist.input`, and `diagnostics.txt` hashes in each arm are respectively `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`, and `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`. The only deliberate runtime difference was `WRF_SDIRK3_STAGE_OPERAND_DIAG` unset vs `1`.

The ON arm emitted a Stage-2 history success record with `fp32_replay_exact=1`, `hist_rel=1.574367e-2`, `hist_max_rel=6.964121e+1`, followed by `reapply_res_max=0` and zero reported per-block residuals. No stage-operand closure-failed marker appeared. Both arms later reached the same Stage-2 iter9 `ZeroStepStall`, `R_last=1.274e-6`, fail-closed outcome 20, without completing the first 15 s forecast step.

The OFF/ON partial `wrfout` files are byte-identical, SHA-256 `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`; their terminal Stage-2 JSON and PT snapshots are also byte-identical (`78f555ef994840bf4ea081630b3a8866df77f8e94b53b92416af9d93cc69a2f0` and `f99fd53b18fc859c5648ade3a654358c45b10b5ae99d0d286cbdc879c2f3d053`). The same three hashes match the earlier affected-component relink pair despite the different executable hash. OFF/ON `rsl.error.0000` hashes are `8c020bbac1c751c3a8650da511dcd6faf027c90ef43b84acbdb2eb717263bb01` / `878c761fe4ce1cc39fdc59a37de987426d3e8861328bb7dbcee64239357bf194`.

This closes the clean-build existence and the single-rank strict-input diagnostic ON/OFF check for this exact PR source. It does **not** close strict 15 s convergence, a completed forecast, same-setup split-explicit RK3 field/runtime parity, long-time stability, MPI multi-rank behavior, full L34 spatial parity, or a whole-call adjoint. No same-setup RK3 comparison was possible because the SDIRK3 step failed before forecast output.
