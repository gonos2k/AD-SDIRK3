# Option-2 V composite diffusion parity

Local timestamp: 2026-09-26 23:01:14 JST (+0900)

## Scope

This candidate starts from draft PR #241 HEAD `9513b3aad2eb494077206f2a60bb1774bccdac78` in a detached, isolated worktree. The primary source-extracted Fortran driver and C++ test compose one canonical dry, option-2, fixed state with periodic X and Y-constant halos, `khdif=2/3`, `kvdif=4/3`, `xkmh=2/3`, `xkmv=4/3`, `xkhh=2`, `xkhv=4`, `rho=1`, map factor `1.25`, `dx=dz=1000 m`, and `dn=dnw=-1/4`. The V field is an interior vertical pulse with periodic-X cosine structure. The suite also includes a packed periodic-X domain with seven unique X cells plus its duplicate endpoint, and a separate periodic-Y terrain/V profile with nonzero `zy` and `D22` stress.

The oracle extracts `compute_diff_metrics`, `cal_deform_and_div`, `cal_titau_23_32`, V vertical/horizontal diffusion, and `rk_addtend_dry` from WRF Fortran. The source driver analytically prepares the lower Y metric halo and periodic east D12 halo consumed by its stencils. These fixtures do not call WRF's physical-boundary routine or MPI exchange. The C++ side composes private stage-geometry, stress, and diffusion helpers, then applies the same map normalization; it does not call the complete `computeUnifiedRHS`.

## Findings and changes

The preserved first composite run failed at owned V `(j,k,i)=(4,0,0)`: horizontal tendency differed by `1.29938831e-3` against a `2.35e-7` predeclared budget. Vertical V stress already matched. Diagnosis separated three issues:

1. C++ `compute_defor12` left the owned periodic-west D12 column zero. It now computes that column from wrapped V values and wrapped terrain/metric operands, using the same bottom/top interpolation fallbacks as the adjacent path. The source mutation that zeros this column produces a D12 error of `3.6611691e-5`, against a `1.77e-10` budget.
2. The source-extracted test now prepares `zx/rdzw` at `jts-1` and the east D12 halo consumed by the V stress stencil. These are oracle fixture boundary values, not production boundary-exchange validation.
3. C++ D12 terrain interpolation no longer adds an X average to `zx`; the Fortran D12 stencil uses the metric at the matching X location. The V terrain correction now averages adjacent X stresses and adjacent Y stresses before its vertical difference, and zeros both W-level stress-average boundaries as the Fortran routine does.

The packed test sets WRF domain bounds so `isPackedPeriodicDomain()` is active. The seam predecessor is taken from the last unique mass column (`nx-2`); the duplicate endpoint receives the west-seam D12 and V tendency. The packed source oracle uses `ide=nx`, period `nx-1`, a periodic east D12 halo, and matching metric aliases. Its flat and terrain controls compare all seven unique X cells and verify the endpoint aliases. The flat packed V_H error is `4.64e-12`; the packed terrain D12/H/Z/post-map errors are `6.55e-11`, `3.96e-9`, `7.44e-9`, and `1.11e-8` against a `2.36e-7` budget. C++ D12, horizontal V, vertical V, and post-map aliases equal the west face exactly; Fortran's D12 alias also equals its west face.

The periodic-Y terrain control uses `h_y(j)=40 cos(2πj/Ny)`, a Y-varying V pulse, and zero U. It activates `D22` and `zy` while keeping D12 zero. The source-extracted Fortran/C++ errors are D22 `9.36e-10`, horizontal V `4.47e-8`, vertical V `2.98e-8`, and post-map RHS `5.98e-8`, below the `3.23e-7` budget; D12 error is zero. `D22` signal is `2.165e-3`, `zy` signal `4.000e-2`, and composite signal `0.16145`. A source-equation mutant that omits the Y-row pair in `titau2avg` changes the terrain contribution by `3.53988e-4`, over 1,000 times the same budget. This mutant uses the correct horizontal coefficient `khdif`.

With these corrections, the flat-terrain control matches V horizontal tendency to `3.39e-12` and D12 to `3.00e-14`. The terrain case matches D12 to `2.37e-11`; D22 is identically zero on this fixture. A separate direct-helper contract exercises periodic-west D12 at `nz=1` and `nz=2`; both shapes remain finite and produce a nonzero seam value.

## Validation

The source-extracted V chain passed FP32 and REAL64 Fortran builds at `-O0` and `-O2`. C++ uses the production FP32 helper build. Budgets were fixed at `2e-6 * max(1e-12, signal)` for each composite output and component.

| Fortran precision/build | Raw V composite error / budget | Horizontal error | Vertical error | Post-map RHS error / budget | D12 error / budget | D22 error |
|---|---:|---:|---:|---:|---:|---:|
| FP32 O0 | `1.23e-9 / 2.35e-7` | `1.23e-9` | `4.70e-10` | `7.49e-9 / 1.88e-7` | `2.36e-11 / 1.77e-10` | 0 |
| FP32 O2 | `1.23e-9 / 2.35e-7` | `1.23e-9` | `4.70e-10` | `7.49e-9 / 1.88e-7` | `2.36e-11 / 1.77e-10` | 0 |
| REAL64 O0 | `2.29e-8 / 2.35e-7` | `2.90e-9` | `2.00e-8` | `2.34e-8 / 1.88e-7` | `5.10e-11 / 1.77e-10` | 0 |
| REAL64 O2 | `2.29e-8 / 2.35e-7` | `2.90e-9` | `2.00e-8` | `2.34e-8 / 1.88e-7` | `5.10e-11 / 1.77e-10` | 0 |

The V raw signal is `0.117498` and the post-map RHS signal is `0.0939987`. The wrong-K vertical control changes the V tendency by `0.0575520`; the K0 control is exactly zero on both Fortran and C++. Bottom/top levels and seam columns are included. Per-level horizontal maximum errors are `1.23e-9`, `1.18e-10`, `9.32e-10`, and `1.28e-11` for `k=0..3`; west/east seam errors are `9.32e-10` and `1.23e-9`.

The existing source-extracted U composite remains passing: the script reports `PASS option-2 U-X compiled Fortran geometry parity`, with raw U max error `1.98e-8` or less across the tested precision builds. The option-1 momentum source oracle and C++ dedicated helper also pass REAL32/REAL64 at O0/O2, including packed U/V/W cases. Production `diffusion_option==1` dispatches through `compute_horizontal_diffusion_option1_momentum` before the shared option-2 stress helpers.

The full CMake build completed. After packed-X and Y-terrain changes, the final CTest run passed `102/102` with two workers in `189.69 s`; `Full_Tile_Temporal_Order` passed in `102.24 s`. The Make archive contains exactly 24 manifest members and the same member set as the CMake archive.

After the final packed-X source change, an incremental one-step WRF relink used unchanged Fortran objects/archive from cleanbuild revision `ca736343ec40786d611b9aa216559188dfd4188d`, with the candidate Make archive both first in `-L/-l` lookup and again as an explicit later link input. PC2 (`time_integration_scheme=6`) and RK3 (`time_integration_scheme=0`) both completed the same 60 s `em_b_wave` case with exit code 0. The six output fields U/V/W/PH/T/MU were all finite (`21,760` values total). Their arrays were bitwise identical to the prior PR #241 PC2 output and RK3 output. The case's V field is zero, so this confirms one-step regression neutrality but does not activate the V diffusion correction. Final external wall times were 3.031 s PC2 and 0.818 s RK3; no performance claim is made. This was an incremental relink, not a clean full WRF rebuild.

## Provenance

- Fortran source: `dyn_em/module_diffusion_em.F` SHA-256 `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea`.
- C++ source: `wrf_sdirk3_tile_unified_impl.cpp` SHA-256 `08904151c17945f6c0121c23169de62c24de0274b4977e2d7d6e3e681b1ee19e`.
- Test sources: `test_scalar_diffusion_contract.cpp` SHA-256 `052deea04997354df1ab4abf415a23286e9d914c308b766a684215d9cebeab97`; `test_option2_momentum_geometry.py` SHA-256 `ed69d6847e17646b5884a62fc8254beb4bd620d7eb874a0a9cbafecb609015d4`.
- Extracted Fortran routine concatenation SHA-256: `86fa68cc559c60021a014722abe83eff6ac0623809e18bdd734110afe6230131`.
- Standalone C++ contract executable SHA-256: `7888f67810e89e8c47ed35a3ca3c666d75a870c86a66fb439237e86c87e1948a`.
- Make archive SHA-256: `2d9ecb5f1f2bcb13316cc6e875f04bedeafdf2444c0adb20d9dcb230950ec3e1`; CMake archive SHA-256: `80fd26df5af4a81eb09b8f3dda4d1bf6bb9b362a9c37c51b4bf56f8abc59a0d8`.
- Linked WRF executable SHA-256: `ff4bd172dae2a0d7327ec199722040d0a6d635375af48acfb8c0b3e8426d01ae`. The exact argument vector is `.validation/v-composite-final/link_argv.json` (SHA-256 `89cfdb402355d21cbb6fb86c9006f3406e167f7b2eaf6e1d9c6a7ced837d8911`). Its first SDIRK3 search is `-L/private/tmp/sdirk3-v-composite-green/external/libtorch_wrf/sdirk3 -lwrf_sdirk3_libtorch`, and the same candidate archive appears later as an explicit argument. The linked executable exports `TileSDIRK3UnifiedSolver::compute_defor12` and `compute_horizontal_diffusion_v_wrf`; the candidate archive's `wrf_sdirk3_tile_unified_impl.o` SHA-256 is `f842f42010d37d6f38abc525c805d2218cb5c4897496aa79156e2e9681fa66a9`, versus `ab195e411ba22191fb9af6b654780fc2e2a4ae1f67ed4ae1ad39cc5c20864414` in the clean PR #241 archive.
- WRF input SHA-256: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`. PC2/RK3 namelist hashes: `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850` / `86d1ee65708470aa4c1683a34131f6e249d03a574678f5c7f6ca03ef55bca8f2`.
- Final candidate PC2/RK3 wrfout hashes: `2ebc4261a247b4541e25bf6cc4ca02b9a484c2bef9b8f6ad835dfda658a53eb7` / `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336`. The candidate PC2 arrays equal the previous PR #241 PC2 arrays for all six fields; the RK3 output is byte-identical to the prior reference.
- CTest log SHA-256: `9c4d8dc7b4cf5b76753d0689d14347493b8045d38d922ad7f58d91079a8fbcf7` (`102/102`). Final V/packed/Y matrix log SHA-256: `c1287d2da4b8d601978475fd14e5d4fdbcea9e77676fd5ef24819e5a546b78de`; option-1 regression log SHA-256: `aa7e43a956f8fd297736ab3495a9e1b0097b1dc2bf6f808b5ee44109e955a033`.
- Graphify focused corpus was built before editing from exact target-source copies (415 nodes, 977 edges); base source hashes matched PR #241. It was refreshed after all source/test changes (420 nodes, 986 edges); `/private/tmp/sdirk3-v-graph/graphify-out/graph.json` SHA-256 `8a4599d2c20d2f68c1501770e052e9c79f1b166de4c4a2622611700cb8306515`. The graph supports source navigation, not numerical proof.

This closes V composite parity only for the declared one-tile, dry, constant-coefficient fixture. Full `computeUnifiedRHS` parity, actual WRF physical boundary/MPI execution, other metric/map layouts, moist or supplied coefficients, full clean WRF rebuild, forecast acceptance, and adjoint behavior remain open.
