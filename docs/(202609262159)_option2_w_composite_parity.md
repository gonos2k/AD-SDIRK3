# Option-2 W composite parity

Local timestamp: 2026-09-26 21:59 JST

## Context

The option-2 W terrain comparison had no full source-extracted chain for one fixed state. This check compares WRF `compute_diff_metrics`, `cal_deform_and_div`, `horizontal_diffusion_w_2`, `vertical_diffusion_w_2`, and `rk_addtend_dry` against the real compiled C++ helpers. The baseline is draft PR #241 at `9513b3aad2eb494077206f2a60bb1774bccdac78`.

Graphify had no graph artifact for this checkout, so a deterministic graph was built from the six exact dependency files in `/tmp/sdirk3-w-composite-corpus.TNdXEy`. The initial extraction contained 421 nodes and 1,636 edges; the final refresh contained 423 nodes and 5,674 edges (graph SHA-256 `81eb7591a1ad53c136df27b1cb24430cf739966c1155bc4456f7a418d7389c9f`). The graph shows `computeUnifiedRHS` calling `compute_horizontal_diffusion_w_wrf`, which calls `compute_defor13/23`; source inspection confirms the Fortran caller path. The AST extractor did not link Fortran `vertical_diffusion_w_2` to `rk_addtend_dry`, so those edges were verified from the authoritative Fortran source and call sites.

## Change

The extracted case reproduced a D13 and horizontal-W difference. WRF's `cal_deform_and_div` uses `zx(i,k,j)` at the u-vorticity point in the terrain correction. C++ averaged `zx` from adjacent x indices. `compute_defor13` now uses the source location directly. The compiled W fixture binds `rdzw_3d_` to the same stage metric as Fortran, and compares metrics, D13/D23/D33, horizontal and vertical W terms, their raw sum, and the dry-RK normalized W tendency.

The fixture uses a physical mass-grid X extent of `nx=8` for W. It has no packed duplicate mass column. The `zx` array has a separate periodic endpoint at `nx`; the test checks that endpoint against index zero for both language implementations. W comparisons use owned interior cells `j=1..ny-2`, `i=1..nx-2`, and `k=1..nz-1`.

## Evidence

Command:

```text
cmake -S external/libtorch_wrf/sdirk3 -B /tmp/sdirk3-w-composite-build -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/sdirk3-w-composite-build --target test_scalar_diffusion_contract -j 6
ctest --test-dir /tmp/sdirk3-w-composite-build -R 'Scalar_Diffusion_Contract|Option2_Momentum_Geometry_Source_Parity' --output-on-failure
```

Both CTest cases passed. The parity script compiled the extracted routines with gfortran at FP32 O0/O2 and REAL64 O0/O2. Across those builds, maximum errors were: `zx` `3.73e-8`, `rdzw` `2.31e-10`, D13 `1.35e-10`, D23 zero, D33 `1.17e-10`, horizontal W `8.08e-9`, vertical W `5.62e-9`, raw composite `1.13e-8`, and post-`rk_addtend_dry` W tendency `9.01e-9`. The fixed relative tolerance remained `2e-6`; no tolerance was relaxed. Wrong-coefficient controls produced horizontal and vertical changes of `3.84e-3` and `2.09e-2`. A wrong-slope control deliberately feeds `0.5*(zx[i-1]+zx[i])`, the former adjacent-X interpolation, to the direct-location implementation; it fails D13 by `3.125e-6` and horizontal W by `1.093e-3`, well above tolerance. The existing U geometry parity within the same harness also passed.

## Provenance

| Source | SHA-256 |
|---|---|
| `dyn_em/module_diffusion_em.F` | `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea` |
| `dyn_em/module_em.F` | `fb424486dbf9c903f77da6a15baf35e6772847e786b18d0297139eb48f0d586e` |
| `dyn_em/module_first_rk_step_part2.f90` | `5e3886a49f9674c3894e73fb12518a292e8c5a0c79b5992ff9bd38b56e590312` |
| `external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp` | `b6e8498b522b9ec610dc874a11f2d1ce3b79b681cca6ee136579c0254e2743ef` |
| `external/libtorch_wrf/sdirk3/tests/test_scalar_diffusion_contract.cpp` | `3d49c2ad5eedf01886b6a4bc166066d37d83164db1833a3753634013d95c72d0` |
| `external/libtorch_wrf/sdirk3/tests/test_option2_momentum_geometry.py` | `91a0a2ce2bb3258e0794b98a9a3d8256c8dcc8d0e4cfa61f0c8702c55bb57089` |
| `external/libtorch_wrf/sdirk3/CMakeLists.txt` | `6de5e8f14d0729e645d799140393a77af8d55b68ea48f31b7a396b5dd63d014c` |
| Built `test_scalar_diffusion_contract` executable | `1164859dd421b4a30d14c0db9d48620bbd4e1361e70efec43f71e26ef6767dec` |

The environment used AppleClang 21.0.0.21000334 and LibTorch `/opt/homebrew/lib/libtorch.dylib`; extracted Fortran used gfortran. No `test/em_b_wave` forecast or same-setup RK3 comparison was run.

## Next actions

Red review completed with no P0/P1 blockers; the reviewer confirmed the D13 source-location formula, fixture indexing, CMake wiring, and old-adjacent-zx negative control. Coordinate the one-line C++ source change with the concurrent V-agent worktree before integration; this branch has not been pushed or merged.
