# PR #239 T1 timestep refinement exploration

Recorded: 2026-09-26T10:29:33+09:00 JST

## Source and setup

Runs used the clean-built `wrf.exe` from source commit `1c5c828470b858146ad6b8f8e539a1ee79dade46`, with pinned `phys/MYNN-EDMF` at `90f36c25259ec1960b24325f5b29ac7c5adeac73`. The current report branch adds documentation commits after that code commit; `git diff 1c5c828..HEAD` contains docs only. Executable SHA-256: `edeeefd1e5c16d2600a8e604186572232665c4aef0f3d99de89ee020a57cf9e3`.

This run reused the exact PR executable; no source or build changes were made. Toolchain was macOS 26.7 arm64, Apple clang 21.0.0, GNU Fortran 15.2.0, Open MPI 5.0.9, netCDF 4.9.3, netCDF-Fortran 4.6.2, and Homebrew LibTorch 2.10.0 with ABI 0. Forecast jobs used one MPI rank.

Each case used the archived initial `wrfinput_d01` (SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`) and `diagnostics.txt` (SHA-256 `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`). The archived 60-second PC2/RK3 namelists were extended to `run_seconds=240`; each pair differs only in `time_integration_scheme` (PC2 6, RK3 0), and each timestep ladder differs only in `time_step` (60, 30, 15). All runs used `diff_opt=2`, `khdif=1000`, `kvdif=10`, 17×17×17, dry physics, and the same solver settings. The namelists were diff-checked: cross-method pairs differ only in the integrator selector, and same-method ladder members differ only in the timestep.

No archived RK3 reference covered this exact 240-second profile, so I ran the matching RK3 ladder with the same executable, input and physics. Each output has times 00:00, 00:01, 00:02, 00:03 and 00:04 across the three history files.

## Run completion, finite fields and runtime

All six baseline runs exited 0 and reached 00:04:00 with `SUCCESS COMPLETE WRF`. Each output set contains 174 floating-point variables and 945,880 floating-point values over five times; all are finite.

| Method | dt (s) | PC2 solver steps / failures | External wall (s) | Sum of WRF main-step timings (s) |
|---|---:|---:|---:|---:|
| PC2 | 60 | 12 / 0 | 2.75 | 1.73480 |
| PC2 | 30 | 24 / 0 | 3.64 | 3.16877 |
| PC2 | 15 | 48 / 0 | 6.82 | 6.31440 |
| RK3 | 60 | — | 0.76 | 0.22988 |
| RK3 | 30 | — | 0.77 | 0.24306 |
| RK3 | 15 | — | 0.81 | 0.26754 |

PC2 completed all 12, 24 and 48 reported stage steps with zero failures and at most two Newton iterations per converged stage. Across stages 2–4, scaled RMS Newton residuals ranged from 1.6908e-6 to 1.8485e-6; unscaled true L2 residuals ranged from 2.6494e-4 to 2.8962e-4. Per-stage ranges were:

| dt (s) | Stage 2 scaled RMS / true L2 | Stage 3 scaled RMS / true L2 | Stage 4 scaled RMS / true L2 |
|---:|---:|---:|---:|
| 60 | 1.7290–1.8039e-6 / 2.7092–2.8264e-4 | 1.7325–1.8013e-6 / 2.7146–2.8222e-4 | 1.7971–1.8068e-6 / 2.8160–2.8309e-4 |
| 30 | 1.7243–1.8485e-6 / 2.7018–2.8962e-4 | 1.7405–1.7981e-6 / 2.7272–2.8173e-4 | 1.7256–1.8202e-6 / 2.7038–2.8520e-4 |
| 15 | 1.7133–1.8076e-6 / 2.6845–2.8322e-4 | 1.6908–1.8398e-6 / 2.6494–2.8825e-4 | 1.7214–1.8379e-6 / 2.6972–2.8797e-4 |

The converged FGMRES logs report 43 Krylov iterations for each solve at dt 15, with reported relative linear error around 9e-8. The RSL files retain the complete per-solve residual telemetry.

## Endpoint self-differences

For each method and field, I computed RMS differences between the 240-second endpoint at dt 60 versus 30 and dt 30 versus 15. The displayed exponent is the diagnostic ratio `log2(RMS(60−30) / RMS(30−15))`; it is a self-convergence proxy, not a measured temporal order against an exact solution.

| Method | Field | RMS 60−30 | RMS 30−15 | Apparent exponent |
|---|---|---:|---:|---:|
| PC2 | U | 1.01555e-6 | 1.30775e-6 | -0.365 |
| PC2 | V | 9.94925e-7 | 1.25828e-6 | -0.339 |
| PC2 | W | 3.32434e-5 | 5.01182e-5 | -0.592 |
| PC2 | PH | 4.94071e-2 | 2.09523e-2 | 1.238 |
| PC2 | T | 1.51015e-5 | 2.36600e-5 | -0.648 |
| PC2 | MU | 1.38026e-4 | 1.15940e-4 | 0.252 |
| RK3 | U | 5.21455e-4 | 1.01285e-3 | -0.958 |
| RK3 | V | 2.49272e-4 | 4.18522e-4 | -0.748 |
| RK3 | W | 4.61565e-5 | 5.47293e-5 | -0.246 |
| RK3 | PH | 4.87875e-2 | 4.00195e-2 | 0.286 |
| RK3 | T | 4.25108e-5 | 2.72382e-5 | 0.642 |
| RK3 | MU | 8.16584e-2 | 7.50594e-2 | 0.122 |

Most fields are nonmonotone or have weak slopes. I do not infer third-order convergence from these results. Some displayed field differences and scaled Newton residuals have similar numeric magnitudes, but they have different units and norms; that observation does not establish a solver-error cause. I ran one tolerance-sensitivity case to test whether tightening the solve materially changes the result.

## Tolerance-sensitivity first failure

The extra PC2 dt15 case kept the same 240-second setup and changed only `sdirk3_newton_tol` and `sdirk3_krylov_tol` from 1e-5 to 1e-7. It exited 1 in the first step at Stage 2. The residual fell from 3.98195e-4 to 1.3034e-6 scaled RMS, then stagnated for three iterations; after seven Newton iterations and 301 Krylov iterations the trust radius was at 1e-6 and candidate steps had negative rho (-0.8216). The requested 1e-7 Newton tolerance was not met, so the fail-closed stage gate aborted the run. The full first-failure logs and partial output remain in `pc2_dt15_tight/`; no further model runs were started after this failure.

This tolerance failure, together with the nonmonotone endpoint differences, leaves the timestep refinement signal below the level needed to support a temporal-order claim. I did not run dt7.5 because the current solver tolerance sensitivity must be resolved before a finer ladder would be interpretable.

## Hash manifest

Common source, executable, initial input and diagnostics:

- Source commit: `1c5c828470b858146ad6b8f8e539a1ee79dade46`
- `main/wrf.exe`: `edeeefd1e5c16d2600a8e604186572232665c4aef0f3d99de89ee020a57cf9e3`
- `wrfinput_d01`: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`
- `diagnostics.txt`: `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`

Per-case hashes; output-file hashes are listed in order 00:00, 00:02, 00:04. The empty mpirun stdout `run.log` has SHA-256 `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` for all six successful cases.

| Case | Namelist | `rsl.error.0000` | `rsl.out.0000` | `time.log` |
|---|---|---|---|---|
| PC2 dt60 | `151350f225b6430aab2e41263965feacdc431165be8a31202f4d0bed89db8b48` | `55ad59a0fe3a4874e51b7777c19c42b724920244bf5f6aca13f39af23757101d` | `8329bb68deaf8c6201468abac3b34ba7be1a40ca12cbda647b55adf0eef5548d` | `a693b45473544a72048d246d6a8b6790bd622d72f8d506a7d72d3342bbdc108e` |
| PC2 dt30 | `3d86c25dde04561c2362e9f46db3e17f9cfc79aa50df5f7f5180da50a88b77d2` | `36cc8990b4966cac8c2c6ef62211e3081eef1c2547e9c54e8dd0464492f095e2` | `364fe4346854fc6411d58ca8de3947a5d1d96d5d562f9efff4fb0d9325650753` | `08f26e79dd157260955e46f53d79238fefb34f21dc68c627f154b97ec563d7ec` |
| PC2 dt15 | `fc3d9490446dcb4e7856abc73ea5f6ef3cf5c7ef9bc79ca678c50a2c69f50d4e` | `bf091b63571e0b3cdae072e831b7a51ef3b45292b48bfb5afb4d4b3d655a7690` | `c2abaed1dd7d28eddeb5fea216e5f831a90ab526f80b6dc56d87bcf4622dce56` | `b2f27e2485442dcc9f5bb94615540f2c03e58dd845ed91c24bc2c29697f1a662` |
| RK3 dt60 | `fa5d75f088dbced3728b3b1199ed4fe7a05c5b1beec1ccae60c626e5e507ee94` | `1d113c463200f3b209cc0909fc43463ccf3b83e1f8be7b1feec3dae5904aeae0` | `358901f6fac176e3c32ca729b05b03c6f7884df5bdb49d4926c898fc2eace2c6` | `76bccf5f5583879b7a6467e1813f96d3c41bdf4c57ec99525ec9d7a68c7538f0` |
| RK3 dt30 | `be009ac92251df14ed9bbb3f4314950d6a3881dcd503410bcabc364bcc652664` | `06fb20be64e2a9bd97268bdbdebf84e5f240910ba373f2c6743a8cefd5a845b4` | `a974c0399a331d50e52c4f045944075e7affaef28e268994bef705d95506896b` | `3953947a9d20e037c5d13b37ad3c65e7bc1e4a2d89e430664b14d03cd3dd45ee` |
| RK3 dt15 | `d23c6899450b7aa394924d639dfffb6efce00eb2ffd0f74e14f89d109af1c914` | `72d0d40dd24a7b427325fd5798631ab21bd8c78f509cf9113c0ef1d9d8d791a3` | `a79410b1fcc3160d637e335ac105b09e71c8bad7ca3ac2bf97ae7d6bc067ab4f` | `4548602109e85935e840328818f5cec24e9619668feb1723f0c53288fd07883a` |

The corresponding three `wrfout` SHA-256s per case are:

- PC2 dt60: `1bb59b0122997db6572e4aa9f2c2ebd3eb9a84e80e95b0c4b58a9fff89d733ca`; `799aa852b87030b59a1f88fd4ab51c9673c1663b504f56b24aaf7e3181d7fcce`; `67454c98010883e45a0fedce7d91fde1f027b72b98962de39c6c20c4da36542e`.
- PC2 dt30: `58b8a426426a135d238ac08d34d6027ae2ab5c2205b893996c2840318b3a9300`; `bc48b7ec9bb6f74673130c18ac5c9557704e2b32b313c0a80fd3af5b1ea32cc1`; `c51226b891ce007a5e6ae89f0467b0318c451ac1b43ee6107a6d4fa033b2b242`.
- PC2 dt15: `cd7ebcf61c4e6f56615d6bba491f8d7249f095d76ac9051ce20fa9a4e7f57776`; `00971183e5b09a9ce50f5b99d85dea5f2be9ae671e27d99d92c6beccde74fbb5`; `69dbf4dfd8b263b7e5a54adc5a1f9c95c40e24ab4dd6237c9ba2dcb6af6be444`.
- RK3 dt60: `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336`; `7504db09090ac14902ce3466b04acc8cd88b5a68fd825e963f82673ff567b884`; `fdb87e2c9cf778de5a67130f2fa129f4bb36fac80d022f16cb765270fd7fef97`.
- RK3 dt30: `58a75df32714fd8835c7253061f840e7e03ea1172fe9c10a754553ad7a1941c5`; `43dffce32f5ea0cc03ea0afd3b67b8b9326e05e595bdcdcab14785c3426f7baf`; `704c000d33436b7da47315d05f908daf56ec0eb03fb086d4d03b2705b8445b24`.
- RK3 dt15: `876890d91a71fcad7061ebd7a29e0e232320a6dba35fe6778f650e4168bf5854`; `764131cabce9e21e9d582a8dba0eedeaa13469ba48c9af8d29081178de6f4496`; `829f20ab7dd2f25ac3994aaa9efce90631ffdd527c1524e95a53859ad66fc689`.

The tight-tolerance failure case used namelist SHA-256 `c9c8a0f1a8a66318de05631f9afae666a6fc793598d6cccf0924ec27f98b9e0b`, with `rsl.error.0000` SHA-256 `66403c9ebebb6d17eb574148cf93cd0de68500c9ca8b829616ec99443d55c3bd` and `rsl.out.0000` SHA-256 `7d46a1f2a2200bb11d04d5dce340cdf8240979c6b6fc35c1bedc27d5518394d2`. Its partial `wrfout_d01_0001-01-01_00:00:00` has SHA-256 `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`.

All raw artifacts remain under `.validation/pr239-cleanbuild/t1_240s_20260926/`. This exploration does not establish temporal order, accuracy against an exact solution, or long-term stability. The current evidence instead shows nonmonotone self-differences and a strict-tolerance Stage 2 failure near the observed residual floor.
