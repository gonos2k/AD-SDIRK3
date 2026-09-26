# PR #244 clean full-WRF validation

Recorded: 2026-09-27T00:28:45+09:00 JST

Provenance correction recorded: 2026-09-27T00:42:17+09:00 JST

## Source and clean build

- Exact source: `e19760eeedeef25480d841cb6458eab88619adb3`, branch `agent/t1-pr244-final`.
- Pinned MYNN submodule: `90f36c25259ec1960b24325f5b29ac7c5adeac73`.
- New isolated worktree: `/Users/yhlee/SDIRK3-pr244-cleanbuild`; it began with zero `.o` and `.mod` files. No existing shared worktree or validation artifact was cleaned.
- `./clean -a` returned 0; after cleanup the worktree again contained zero `.o` and `.mod` files. Configure selected WRF option 37 / nesting 1 and reported “Configuration successful.”
- Final build command: `./compile em_b_wave -j 8`, with `SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk`, `NETCDF=/opt/homebrew`, `LIBTORCH_ROOT=/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch`, and `LIBTORCH_ABI=0`. It completed at 00:19:51 JST with “Executables successfully built.”
- Stack: macOS 26.7 arm64, Apple clang 21.0.0, GNU Fortran 15.2.0, Open MPI 5.0.9, netCDF 4.9.3, netCDF-Fortran 4.6.2, and Homebrew LibTorch 2.10.0. The production compile log confirms `_GLIBCXX_USE_CXX11_ABI=0`.
- Both required executables were built: `main/wrf.exe` SHA-256 `ec9ab9c9cbd7f8e75753e45185d0ef1ad6e5ca62874ecc0717187ba1646c8934`; `main/ideal.exe` SHA-256 `e254176e24c118b512875d3e106e273bf9885cd504270d60aeab9a79b3c9bf35`.
- Production archive SHA-256: `01c4faf1dbd58951032d855d8661128352a16d2adae8a72aa7079ef1fe61eade`. `configure.wrf` SHA-256: `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab`.
- Full compile log SHA-256: `916e66aec992ccb43e4bbdea35290ae3676bc4e477a8bf01e8e84edc9f817a5f`; configure log: `ee09805e9c1e8c52a4f6bfc66ab24ffe0ded6a98028c41f2efea9dfcd477190f`; clean log: `b6295e8f807bc9368ad4cd8f176b1490031bdfa3bde585d22c5d18afaff51192`.
- Linker emitted nonfatal duplicate-library and section-alignment warnings. No compile or link failure occurred.

Graphify was refreshed in the isolated worktree to commit `e19760ee`: `dyn_em` has 1,766 nodes / 2,567 edges; the SDIRK3 C++ component has 3,542 nodes / 6,577 edges. Code extraction completed. Semantic extraction for documentation was not run because no LLM API key was available. The code graph is a navigation aid; source inspection confirmed the Fortran bridge and production archive manifest.

## One-step PC2/RK3 pair

The clean executable ran the archived one-rank, 17×17×17, 60-second PC2/RK3 setup with `diff_opt=2`, `khdif=1000`, `kvdif=10`. Both used the archived initial input SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9` and diagnostics SHA-256 `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`. PC2 used scheme 6 and RK3 scheme 0; namelist SHA-256 values were `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850` and `86d1ee65708470aa4c1683a34131f6e249d03a574678f5c7f6ca03ef55bca8f2`.

Both exited 0 and reached 00:01:00 with `SUCCESS COMPLETE WRF`. PC2 reported `Tile 1: Steps=3, Failed=0, Last dt=60.0000` in `rsl.error.0000`; this is the reported solver-step count, without interpreting it as stage count. Its WRF main-step time was 0.47756 s and external wall time 0.97 s; RK3 was 0.07879 s internally and 0.56 s wall. Each output contains two frames, 174 floating variables, and 378,352 floating values; all values are finite. Selected U/V/W/PH/T/P/MU initial-frame fields match exactly between PC2 and RK3.

Endpoint PC2-minus-RK3 RMS / maximum absolute differences:

| Field | RMS | Maximum absolute |
|---|---:|---:|
| U (m s⁻¹) | 0.00209012907 | 0.0106077194 |
| V (m s⁻¹) | 2.22597646e-5 | 0.000113779679 |
| W (m s⁻¹) | 0.000630030176 | 0.00156310399 |
| PH (m² s⁻²) | 0.291640013 | 0.831054688 |
| T (K) | 0.00995724089 | 0.0338745117 |
| P (Pa) | 0.0367478989 | 0.190734863 |
| MU (Pa) | 0.0415023901 | 0.144718170 |

The actual #243 integrated candidate is preserved under `/private/tmp/sdirk3-v-on-w-integration/.validation/integrated-final`. It is source commit `a1d5f6a3c8c64e66510c81d5fbe98ce568ed180f`; executable SHA-256 `2e84a03096d101c1f8ecf828b9e2ec2bab98c3bdfadbf49e2769fad9fa8cad22`; the linked production archive `/private/tmp/sdirk3-v-on-w-make/libwrf_sdirk3_libtorch.a` SHA-256 `05654e5272efd1c0aad19996f0ff7ace34be853b05f81f66e48b3e46cfe03178`; and the archived input SHA matches `e71b730a...`. The exact #243 integrated PC2 comparator is `.../run_pc2/wrfout_d01_0001-01-01_00:00:00` (SHA-256 `187e6e7e9331be5089c5aa730490d2171cf1ff8766bbce902f6e46215d6c40c4`). The matching RK3 reference is `.../run_rk3/wrfout_d01_0001-01-01_00:00:00` (SHA-256 `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336`). The clean RK3 output is byte-identical to that integrated RK3 reference. The clean PC2 output SHA-256 is `3679f6674a1fdf384bc42f9fb85f887c6f09c17536d418cd3f92313f0c691d32`; its endpoint field differences from the exact #243 integrated PC2 comparator are:

| Field | RMS | Maximum absolute |
|---|---:|---:|
| U | 5.87699e-9 | 2.38419e-7 |
| V | 8.21223e-10 | 2.32831e-8 |
| W | 5.79808e-8 | 3.04915e-6 |
| PH | 1.04674e-5 | 4.88281e-4 |
| T | 0 | 0 |
| MU | 8.42937e-8 | 9.53674e-7 |
| P | 2.32831e-5 | 0.00149012 |

Correction note: a similarly named `option2_vertical_final_20260926` result from the older vertical-fix worktree was previously attributed as the #243 integrated candidate. It is not the #243 integrated comparator and is not used in this corrected comparison. These remain same-setup one-step comparisons; they do not establish forecast quality or long-run behavior.

## Strict dt15 snapshot OFF/ON

I ran the archived strict PC2 `dt=15 s`, 240-second setup from the same e71b730a input, with `sdirk3_newton_tol=1e-7` and `sdirk3_krylov_tol=1e-7`. The two namelists differ only in `sdirk3_stage2_rejection_snapshot_diag` (`.false.` OFF / `.true.` ON). Both exited 1 at Stage 2 in the first timestep with the same failure: scaled residual 1.3034e-6, 7 Newton iterations, 301 Krylov iterations, then zero-step stagnation and fail-closed WRF abort. Each partial output has one time and 174 finite floating variables; OFF and ON `wrfout` files are byte-identical (SHA-256 `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`).

The ON run captured the first common trust-decision rejection at timestep 0, Stage 2, Newton iteration 1, attempt 0. Metadata records `rho=-0.11448053743280608`, actual reduction `-8.2438306048531479e-9`, predicted reduction `7.2010760865722119e-8`, and threshold 0.25. It records observed FP32 operands and no RHS replay. The run later reached the same terminal failure as OFF. Snapshot tensor archive SHA-256 `caf31f34597f3ec0632a8c1c1284a82630ca79b36c77f67edb754df0d91b1078`; metadata SHA-256 `4bf3f864bdf94ea5d9b5b0d5cc74cc243e70abf99cb0ffb5e41871e5e55ff66a`.

The OFF/ON namelist SHA-256 values are `246a148486b8f5bc8d3e22fe03ca7a1ff7090fdf5d89a471ebf8e35642e84d54` and `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`. Their `rsl.error.0000` hashes are `4324394a64192459ff566b8d20aeac371f138ca37237aa7b9c60041e5ca52eab` and `52c12ace0e12d85693f7ee0fcc81566dba856d05578e5bd64a5cb730d0eebd99`; `rsl.out.0000` hashes are `daee50fecd2b052b58413338d858d006c72e672e1254fa1153df386e04c61f1b` and `463240e84910925d3cb6417dc6487794196ee03e915e986dc5d20f5ff631f94d`.

## Artifact hashes

| Case | `wrfout` SHA-256 | `rsl.error.0000` SHA-256 | `time.log` SHA-256 |
|---|---|---|---|
| PC2 | `3679f6674a1fdf384bc42f9fb85f887c6f09c17536d418cd3f92313f0c691d32` | `c8af622bd009309ac9e9448ba6e69bccc5bd953571c30b4f2af22ccf22378080` | `0000d88d40eba29aa0c4ccc84cf3fa1ba6b0ad5cc87c59cad70cb904c01c1ae5` |
| RK3 | `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336` | `398628035a2c600af055e8a194521f57f5b29e0712482964f0b5f622a0d2f6b7` | `3c1edb00fd31c49310a7f01b1f0ff5bda60afb6b08f16f25468cd7488e42c1e7` |

Detailed raw logs and outputs remain under `.validation/pr244-clean/`. The worktree is isolated and has no tracked source diff after restoring generated tracked files; Graphify artifacts and one generated `.f90` remain untracked and were not included in the report commit. Graphify code graphs match `e19760ee`; graph extraction for documentation was skipped because no semantic-extraction API key was available. No full CTest run, long forecast, or convergence proof was performed here.
