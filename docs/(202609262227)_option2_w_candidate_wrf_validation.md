# Option-2 W candidate validation addendum

Local timestamp: 2026-09-26 22:27 JST

This addendum extends [`(202609262159)_option2_w_composite_parity.md`](<(202609262159)_option2_w_composite_parity.md>) with full CMake/CTest, Make archive, and candidate-archive-first WRF evidence. The numerical source was fixed at candidate commit `268391faedef80d8ddb5b4d96cd7ced1852cab76`; `wrf_sdirk3_tile_unified_impl.cpp` SHA-256 was `b6e8498b522b9ec610dc874a11f2d1ce3b79b681cca6ee136579c0254e2743ef` before and after validation. No source file changed during this follow-up.

## Build and tests

The full CMake build completed with:

```text
cmake --build /tmp/sdirk3-w-composite-build --parallel 2
ctest --test-dir /tmp/sdirk3-w-composite-build --parallel 2 --output-on-failure
```

CTest passed all 103 tests, including `Option2_Momentum_Geometry_Source_Parity` and `Core_Archive_MakeParity`, in 157.00 seconds. The final log is `/tmp/sdirk3-w-composite-full-ctest-final.log` (SHA-256 `4e97b3d65db8d69fc1c8d59cecaa27889ebf8ee5f7224016644731e7e4845b0a`). The CMake archive SHA-256 was `f9ff981253b0dec079a6487c0337f799f52983a4ceb7296d9debe8c907c79c42`.

The production Make archive was rebuilt from clean objects in the candidate worktree:

```text
cd external/libtorch_wrf/sdirk3
LIBTORCH_ROOT=/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch
LIBTORCH_ABI=0
make clean
make -j2
```

Make completed successfully. Its candidate archive SHA-256 is `d1ad732ed2de1d66d52a9c4bb07868d1d1d9951049486fd9435248390317de7e`.

## Candidate archive relink and 60-second run

I copied the #239 clean validation tree into `/Users/yhlee/SDIRK3-w-composite-wrf-validation`, preserved its clean WRF Fortran objects, and placed the candidate Make archive at the production archive path before relinking. The candidate archive hash at that path was `d1ad732ed2de1d66d52a9c4bb07868d1d1d9951049486fd9435248390317de7e`. The incremental command `./compile em_b_wave -j 2` succeeded. Its link command names the candidate copy’s `external/libtorch_wrf/sdirk3/libwrf_sdirk3_libtorch.a`; the resulting `main/wrf.exe` SHA-256 is `f4b709701677b395dda39c169e01881955938ae02d7aa2da07f872b0bbb8660f`. The binary contains `TileSDIRK3UnifiedSolver::compute_defor13`. The relink log is `.validation/pr239-cleanbuild/runs/w_composite_candidate/relink/compile.log` (SHA-256 `ec76e03dfdf854172169447fa64adc6a802cadb35de45f6061d0581f4803e95d`).

All 760 copied `.o` files and 502 `.mod` files were compared with the #239 clean build. Only `main/ideal_em.o` changed during the relink step; all WRF Fortran objects remained byte-identical. In particular, `dyn_em/module_implicit_sdirk3.o` stayed at SHA-256 `17e5059d8bf70b724df35b98f2c77fedbb40447e1ed849c5c909282756b1b0f2`, and `dyn_em/module_diffusion_em.o` stayed at `63bc5663cd981b3fa9a8239186007d6fd343b027e7b81cb5788e114fbcd97e78`. The source submodule `phys/MYNN-EDMF` remains pinned to `90f36c25259ec1960b24325f5b29ac7c5adeac73`.

PC2 and RK3 both used the archived one-rank namelists and initial input. Input SHA-256 was `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`; diagnostics sidecar SHA-256 was `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`. Namelist hashes were PC2 `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850` and RK3 `86d1ee65708470aa4c1683a34131f6e249d03a574678f5c7f6ca03ef55bca8f2`. Both ran `run_seconds=60`, `time_step=60`, `diff_opt=2`, `khdif=1000`, `kvdif=10`, on the 17×17×17 one-tile layout. Both exited with `SUCCESS COMPLETE WRF` and produced two frames; every numeric output value was finite (379,482 values checked in each file). PC2 reported 3 solver steps, zero failures, last dt 60 s; stages 2–4 converged in two Newton iterations with 13 GMRES iterations each and scaled residuals from `1.73e-6` to `1.81e-6`.

Endpoint PC2-minus-RK3 field differences from these candidate runs were:

| Field | RMS | Maximum absolute |
|---|---:|---:|
| U (m s⁻¹) | 0.00209013 | 0.0106077 |
| V (m s⁻¹) | 2.22667e-5 | 1.13775e-4 |
| W (m s⁻¹) | 0.000630028 | 0.00156310 |
| T (K) | 0.00995724 | 0.0338745 |
| PH (m² s⁻²) | 0.291640 | 0.831055 |
| P (Pa) | 0.0367470 | 0.190735 |
| MU (Pa) | 0.0415023 | 0.144714 |

The t=0 U/V/W/T/PH/P/MU fields matched exactly. Candidate RK3 output is byte-identical to the #239 RK3 reference (`bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336`). Candidate PC2 output SHA-256 is `2ef81a3636358ee3d50b4cffae1e992791c5f79baa7b284b7b066712949e6d9d`; the #239 PC2 output hash differs. For selected endpoint fields, U/T/PH/P/MU matched the #239 PC2 arrays exactly, while V differed by RMS `1.07e-11` (max `4.66e-10`) and W by RMS `3.32e-11` (max `4.66e-10`).

WRF main-step time was 0.61170 s for PC2 and 0.10966 s for RK3; external wall time was 2.12 s and 0.75 s, respectively. These are single-step observations and do not support a general performance claim. This is an incremental candidate archive relink against #239 Fortran objects, not a clean full WRF rebuild at candidate source.
