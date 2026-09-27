# Integrated W and V candidate validation

Local timestamp: 2026-09-26 23:35:58 JST (+0900)

## Revision and scope

This addendum validates integrated branch `agent/option2_v_composite_parity` at `c6eb9b5040d1ec837f3a5d00280d6422e44ba3e7`, stacking V source/test/report commits on the W candidate. The W periodic terrain-stress correction and the V periodic-west D12 / X-Y terrain-stress corrections are present together. The source-extracted geometry test retains the U, W, and V oracle paths and their independent controls in one CTest target. The V oracle includes physical-periodic and packed-periodic X fixtures, the periodic-Y terrain signal, small vertical extents, K0, wrong-K, old-west-seam, and wrong-Y-average controls.

This remains bounded operator evidence. The Fortran test composes source-extracted `compute_diff_metrics`, `cal_deform_and_div`, stress and diffusion routines, and `rk_addtend_dry`; it does not call `computeUnifiedRHS`. Its analytic halo values do not validate WRF physical-boundary routines or MPI exchange. The initial V and W fields in the one-step model input are exactly zero, so it checks integration/regression behavior and does not exercise the nonzero terrain/Y variation in the source-extracted V oracle. No full clean WRF rebuild or forecast acceptance is claimed.

## CMake and archive validation

The full CMake tree configured at `/private/tmp/sdirk3-v-on-w-build` against this integrated worktree. `cmake --build /private/tmp/sdirk3-v-on-w-build --parallel 2` completed all 188 build steps. The final `ctest --test-dir /private/tmp/sdirk3-v-on-w-build --output-on-failure -j2` passed **103/103** in **126.93 s**. The full output log is `.validation/integrated-final/ctest-full-103.log` (SHA-256 `cd1ce61d67a407a284b1bbb834e3bd3f793c623be3a0de4ffc0e71565c4e0d66`); CTest's detailed log is `.validation/integrated-final/ctest-full-103-detail.log` (SHA-256 `ab00d3a82e924babecaf05bf29b900a5744ab3aaaaa02cc98b4c027a0079fd4f`). `Core_Archive_MakeParity` passed in that run.

A clean Make archive was rebuilt from a source copy at `/private/tmp/sdirk3-v-on-w-make`, first running `make clean`, then `make -j2 LIBTORCH_ABI=0`. `make check-core-manifest` and `make check-core-archive` passed; the archive contains exactly the manifest's 24 members. The CMake archive and Make archive each pass `Core_Archive_MakeParity`; the separate receipt is `.validation/integrated-final/ctest-make-parity-rerun.log` (SHA-256 `1052c14e5b753a7c10dcea6e9dc1227818995f831bf8e80145d0c4090f8b4c2e`).

## Archive-first WRF relink and one-step comparison

I incrementally linked the Make-built integrated archive against the preserved clean #239 Fortran object tree in `/Users/yhlee/SDIRK3-pr239-cleanbuild/main`. The exact argv is `.validation/integrated-final/link_argv.json` (SHA-256 `7131c5260a8dcfbd5c09fe2a2f76c70eb5f791e4801bc891fdfecf943d0b5004`). Its first SDIRK3 lookup is `-L/private/tmp/sdirk3-v-on-w-make -lwrf_sdirk3_libtorch`; the same candidate archive is also an explicit later link argument. The link exited 0. The resulting executable exports `compute_defor12`, `compute_horizontal_diffusion_v_wrf`, and `compute_horizontal_diffusion_w_wrf`; the symbol receipt is `.validation/integrated-final/linked_symbols.txt` (SHA-256 `a4ada47de8a19f1572f9850fc31409e55a90b2598d07bd7ff09ed711016dc8d7`). This is an incremental relink, not a clean WRF rebuild.

PC2 and RK3 each used one MPI rank, `time_step=run_seconds=60`, and the same archived `wrfinput_d01` (SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`) and diagnostics sidecar (SHA-256 `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`). The saved run argv does not record the OpenMP thread count. The PC2 namelist hash is `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850`; RK3 is `86d1ee65708470aa4c1683a34131f6e249d03a574678f5c7f6ca03ef55bca8f2`. Both exited 0 and reached `SUCCESS COMPLETE WRF`.

The six selected prognostic fields U/V/W/PH/T/MU contain 21,760 finite values per run. PC2 output SHA-256 is `187e6e7e9331be5089c5aa730490d2171cf1ff8766bbce902f6e46215d6c40c4`; RK3 is `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336`, byte-identical to the clean #239 RK3 output and the standalone W/V RK3 outputs.

For the PC2 result, the following table gives RMS / maximum absolute differences over each final-time field. The first comparison is to the clean #239 output; the second and third are the standalone W and V candidate runs on the same archived input.

| Field | vs #239 clean | vs standalone W | vs standalone V |
|---|---:|---:|---:|
| U | `1.23e-8 / 4.77e-7` | `1.23e-8 / 4.77e-7` | `0 / 0` |
| V | `3.31e-7 / 5.90e-6` | `3.31e-7 / 5.90e-6` | `2.20e-11 / 9.31e-10` |
| W | `9.56e-8 / 3.42e-6` | `9.56e-8 / 3.42e-6` | `4.39e-11 / 9.31e-10` |
| PH | `1.48e-5 / 4.88e-4` | `1.48e-5 / 4.88e-4` | `0 / 0` |
| T | `0 / 0` | `0 / 0` | `0 / 0` |
| MU | `1.15e-6 / 1.38e-5` | `1.15e-6 / 1.38e-5` | `0 / 0` |

The integrated PC2 output differs from #239 and standalone W at FP32 scale, while the six fields nearly match standalone V; this is a regression comparison on the declared input, not a controlled test of the V oracle’s nonzero terrain/Y signal. The initial V and W fields are exactly zero. No forecast-quality, time-order, performance, or active-V acceptance claim follows.

Measured outer wall times were 1.971 s PC2 and 0.631 s RK3 for this run. The archived clean run recorded 0.89 s / 0.51 s, the standalone W run recorded 2.12 s / 0.75 s, and the standalone V run recorded 3.031 s / 0.818 s. These receipts use different wall-time wrappers and a single tiny step; they are recorded for provenance only and do not support a runtime comparison or speedup claim.

## Provenance

- Integrated source revision: `c6eb9b5040d1ec837f3a5d00280d6422e44ba3e7`.
- Fortran source `dyn_em/module_diffusion_em.F`: SHA-256 `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea`.
- Integrated C++ source `wrf_sdirk3_tile_unified_impl.cpp`: SHA-256 `6eb2aa365453c9673c27b3fa8c34091d97c3613027d81c5c0cf138dae37caba1`.
- Combined C++ test `test_scalar_diffusion_contract.cpp`: SHA-256 `6c72e9d7d5f20ed4bf6589da3544c376515268eb4735ebdf923ad76b3fe6888e`; Python oracle `test_option2_momentum_geometry.py`: SHA-256 `b0fbccd93b977da69c3e7a29d7c8c2209378644230d4daf5d22a608155503964`.
- Make archive: SHA-256 `05654e5272efd1c0aad19996f0ff7ace34be853b05f81f66e48b3e46cfe03178`; CMake archive: `cfa98831d2e232f86dc22a79a96d056183f077c04e3c21803804dc15c25f0298`. The Make archive's `wrf_sdirk3_tile_unified_impl.o` member SHA-256 is `e3c45ea923b69e5344513db271f9448c60da28fb007c62f7e6cfffd022ac65d9`; the saved PR #239 archive member is `ab195e411ba22191fb9af6b654780fc2e2a4ae1f67ed4ae1ad39cc5c20864414`.
- Linked WRF executable: SHA-256 `2e84a03096d101c1f8ecf828b9e2ec2bab98c3bdfadbf49e2769fad9fa8cad22`.
- Integrated PC2/RK3 wrfout hashes: `187e6e7e9331be5089c5aa730490d2171cf1ff8766bbce902f6e46215d6c40c4` / `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336`.
- PC2/RK3 `rsl.error.0000` hashes: `792330825268165ed273c3c900cfa05e7bfc7b557f8daf919a1ec91f94976768` / `68380fcfe33c0182902ee64a363671c7c7b9dd38752968c50d09a2dbeabfbbbe`.
- Field comparison receipt `.validation/integrated-final/field_comparison.json`: SHA-256 `67c82f6ab4d15bb49f2c2c7d9b2d05cadf9782c63032d95b7b7caecc61059939`.

The Graphify corpus was refreshed after the combined source/test resolution at `/private/tmp/sdirk3-v-graph`; its mirror hashes match the integrated implementation and both test sources. It has 422 nodes / 988 edges / 17 communities. Graphify is navigation evidence only. The graph JSON SHA-256 is `2d2c40505ec1e4731adae21c6e507999a1863df654a5e72a6f5d9443605fff29`.

Local integrated validation is complete. Exact-head CI and PR review remain pending. Full coupled RHS, active-V WRF forecast, WRF physical-boundary/MPI parity, clean full WRF rebuild, time accuracy, forecast acceptance, and adjoint behavior remain open.
