# PR #239 clean full-WRF build and one-step validation

Recorded: 2026-09-26T09:58:40+09:00 JST

## Source and scope

- Clean validation worktree: `/Users/yhlee/SDIRK3-pr239-cleanbuild`
- Source commit: `1c5c828470b858146ad6b8f8e539a1ee79dade46`
- Pinned `phys/MYNN-EDMF` submodule: `90f36c25259ec1960b24325f5b29ac7c5adeac73`
- Worktree started with zero `.o` and `.mod` files. Existing `/Users/yhlee/SDIRK3` and `/Users/yhlee/SDIRK3-integration` were not cleaned or built.
- Graphify code graphs were refreshed from cached graphs and now identify commit `1c5c8284`. The `dyn_em` graph has 1,766 nodes / 2,567 edges; the SDIRK3 C++ graph has 3,517 nodes / 6,562 edges. The graph shows `solve_em.F` using `module_implicit_sdirk3`, whose C-bound bridge calls feed the C++ archive. Graphify’s full semantic extraction was unavailable without an LLM API key; code-only extraction completed. Source inspection corroborated the call path and archive manifest.

## Toolchain and clean build

The host was macOS 26.7 arm64 with Apple clang 21.0.0 (`/usr/bin/g++`), GNU Fortran 15.2.0, Open MPI 5.0.9, netCDF 4.9.3, and netCDF-Fortran 4.6.2. WRF used `NETCDF=/opt/homebrew`. The C++ archive used Homebrew PyTorch/LibTorch 2.10.0 from `/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch` and explicit `LIBTORCH_ABI=0`. `SDKROOT` was `/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk`.

The final build sequence was:

```sh
printf '37\n1\n' | ./configure
./compile em_b_wave -j 8
```

Both commands ran with `SDKROOT`, `NETCDF=/opt/homebrew`, `LIBTORCH_ROOT` set to the Homebrew PyTorch 2.10 installation, and `LIBTORCH_ABI=0`. A full `./clean -a` preceded this configure/build. The build log reports success at 09:51 JST and produced `main/ideal.exe`, `main/wrf.exe`, and `external/libtorch_wrf/sdirk3/libwrf_sdirk3_libtorch.a`.

The first configure/build attempt omitted `SDKROOT`: configure reported “One of compilers testing failed,” and the ignored `test_io_idx` helper link failed with `ld: library 'System' not found`. I stopped that attempt before the main build completed, cleaned only this isolated worktree, and repeated configure/build with the Xcode SDK selected. The corrected configure had no compiler-test failure and the full build succeeded. This is environment setup evidence, not a source change.

## One-step PC2/RK3 run

The forecast pair used the byte-identical archived initial `wrfinput_d01` (SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`). I copied the archived diagnostics sidecar (SHA-256 `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`) and used the exact archived namelist bytes:

| Setting | PC2 | RK3 |
|---|---:|---:|
| `time_integration_scheme` | 6 | 0 |
| `time_step` / `run_seconds` | 60 / 60 | 60 / 60 |
| Grid / decomposition | 17×17×17 / 1×1 | 17×17×17 / 1×1 |
| `diff_opt`, `khdif`, `kvdif` | 2, 1000, 10 | 2, 1000, 10 |
| Namelist SHA-256 | `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850` | `86d1ee65708470aa4c1683a34131f6e249d03a574678f5c7f6ca03ef55bca8f2` |

Both runs exited 0 and reached 00:01:00 with `SUCCESS COMPLETE WRF`. PC2 reported three solver stage steps, zero failures, last dt 60 s; Newton converged in two iterations for stages 2–4, with scaled RMS residuals 1.75–1.81e-6. PC2’s WRF main-step time was 0.44935 s and external wall time 0.89 s. RK3’s WRF main-step time was 0.07419 s and external wall time 0.51 s. These are single-step timings, not a speedup claim.

Each output contains two frames (00:00 and 00:01), 174 floating-point variables and 378,352 float values. All values are finite. The t=0 U/V/W/T/PH/P/MU fields match between PC2 and RK3 exactly. Endpoint PC2-minus-RK3 differences are:

| Field | RMS difference | Maximum absolute difference |
|---|---:|---:|
| U (m s⁻¹) | 0.00209013 | 0.0106077 |
| V (m s⁻¹) | 2.22667e-5 | 1.13775e-4 |
| W (m s⁻¹) | 0.000630028 | 0.00156310 |
| T (K) | 0.00995724 | 0.0338745 |
| PH (m² s⁻²) | 0.291640 | 0.831055 |
| P (Pa) | 0.0367470 | 0.190735 |
| MU (Pa) | 0.0415023 | 0.144714 |

The final output SHA-256 values are PC2 `1bb59b0122997db6572e4aa9f2c2ebd3eb9a84e80e95b0c4b58a9fff89d733ca` and RK3 `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336`.

The earlier incremental evidence at `/Users/yhlee/SDIRK3-option2-vertical-fix/.validation/option2_vertical_exact` records C++ source digest `ddec376d3c398502571d8d43ea2d97c764e2e9eaea2d19b81c706c8b0389dba5`, executable SHA `87f24858113ab0e42ca339cc8dbec8bc3ebae05a302e315f431556c5eb582405`, and the same archived input and exact namelist hashes. The clean RK3 output is byte-identical to that earlier RK3 output. The clean PC2 output differs; for final U/V/W/PH/T/MU, clean-vs-incremental RMS / max-absolute differences are:

| Field | RMS difference | Maximum absolute difference |
|---|---:|---:|
| U | 1.94211e-9 | 1.19209e-7 |
| V | 4.41007e-10 | 9.31323e-9 |
| W | 2.42224e-8 | 1.58278e-6 |
| PH | 7.40160e-6 | 4.88281e-4 |
| T | 0 | 0 |
| MU | 1.11510e-7 | 9.53674e-7 |

The archived and clean PC2 outputs have different file hashes, despite matching T exactly and differing by only the values above in these selected fields. The result is a bounded one-step comparison, not a numerical acceptance or forecast-quality claim.

The first PC2 launch failed before stepping because the `diagnostics.txt` named by the namelist was missing from the run directory. Its logs are preserved under `.validation/pr239-cleanbuild/runs/pc2/attempt1_missing_diagnostics/`. Adding the archived sidecar fixed startup; the successful reruns kept the same namelist and physics.

## Separate ideal initialization check

I ran the clean-built `ideal.exe` in `.validation/pr239-cleanbuild/ideal-prep` using the archived preparation namelist, `input_jet`, and diagnostics sidecar. It exited 0 with `SUCCESS COMPLETE IDEAL INIT`. The newly produced `wrfinput_d01` has SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, exactly matching the archived initial file. U/V/W/T/PH/PHB/P/PB/MU/MUB/QVAPOR arrays are bit-identical to the archived file.

## Artifact hashes and bounds

- `configure.wrf`: `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab`
- `main/ideal.exe`: `30cb97d2f2a37b642be4d22aaa1350432b96972629208ba4d3e5e0245ed47fdb`
- `main/wrf.exe`: `edeeefd1e5c16d2600a8e604186572232665c4aef0f3d99de89ee020a57cf9e3`
- SDIRK3 archive: `918fbd46ce7b2aa7770c2c8d3cc5ef96c2fb7e3cf7472593c3f1f1e4c0b3a1e7`
- Final clean build log: `98ce7ff92f148bc701e31083c4c3740e7038e5309656061f5038307459dd9a59`
- PC2 `rsl.error.0000`: `34bbecdf588fba7ef0292a4130fcba3d28c68c4e69fa748e1f01a1806e86ee2e`
- RK3 `rsl.error.0000`: `efb2d35a3023b696293baf27b224b9c77ea430eac74bfa1cf8e4a25124396d5f`

Detailed build and run logs, namelists, inputs, output files, Graphify artifacts, and the separately preserved failed first launch remain under `.validation/pr239-cleanbuild/`. This validation covers the exact commit’s clean macOS WRF build, an ideal initialization reproduction, and one 60-second single-rank PC2/RK3 pair. It does not establish long-run stability, temporal convergence, operational timestep suitability, or general forecast quality.
