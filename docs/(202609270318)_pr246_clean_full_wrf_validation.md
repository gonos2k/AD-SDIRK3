# PR #246 exact-head clean WRF validation

Recorded: 2026-09-27T03:18:38+09:00 JST

## Scope and source

This report validates the exact draft PR #246 source at `ca051d47c1a1ad379dbae8d3ea9e967647254595` in the isolated worktree `/Users/yhlee/SDIRK3-pr246-cleanbuild`. The worktree was created detached at that commit; the validation report is being committed on `agent/pr246-clean-full-wrf-validation`. The root `/Users/yhlee/SDIRK3` and prior validation worktrees were not cleaned or modified.

Pinned submodule `phys/MYNN-EDMF` is `90f36c25259ec1960b24325f5b29ac7c5adeac73`. After the build, tracked generated Fortran files were restored to the target commit; the tracked source diff is empty. Executable provenance is anchored to the exact source commit and the final linked archive below.

## Clean build and stack

Initial `.o`/`.mod` count was zero. `./clean -a` returned 0; it printed two harmless `cd build: No such file or directory` messages from cleanup and left no stale Fortran objects. Configuration used `NETCDF=/opt/homebrew`, `SDKROOT=/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk`, and `printf '37\n1\n' | ./configure`, selecting the SDIRK3 LibTorch dmpar setup and basic nesting. Configure succeeded.

The selected stack recorded in `configure.wrf` and the link command was GNU Fortran 15.2.0 (`gfortran`), GCC 15.2.0, Open MPI 5.0.9 (`mpif90`/`mpicc`), Homebrew netCDF-C 4.9.3 and netCDF-Fortran 4.6.2, Apple Silicon macOS SDK, Homebrew LLVM clang++ 22.1.4 for the C++ archive, and Homebrew LibTorch 2.10.0 under `/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch`. The archive build and WRF link use `_GLIBCXX_USE_CXX11_ABI=0`; the final WRF link resolves LibTorch, MPI, netCDF and GNU Fortran libraries through the selected Homebrew paths.

One cleanup gap was caught before accepting the build: WRF's nested `sdirk3_libtorch` target initially said “nothing to do here” because `./clean -a` had not removed the ignored C++ archive. I ran `make clean` in `external/libtorch_wrf/sdirk3`, which removed the archive, all 24 source objects and dependency files, then rebuilt it explicitly from zero objects with `LIBTORCH_ROOT=/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch`, `LIBTORCH_ABI=0`, and `/opt/homebrew/opt/llvm/bin/clang++`. This completed before the WRF executable link. The fresh archive is SHA-256 `b6ba5644d5cca5779b824cf17534a31fc71b2a2b652c50d0428aaebb83c3d55e` and contains 24 `.o` members plus the macOS archive index. The link lines in the full compile log point at this worktree archive.

The full command was `./compile em_b_wave -j 8` with `SDKROOT`, `NETCDF`, `LIBTORCH_ROOT`, `LIBTORCH_ABI=0`, and `/opt/homebrew/bin` first in `PATH`. It exited 0, with build start 02:48:33 JST and completion 03:10:20 JST, and built both executables. The linker emitted a section-alignment reduction warning and duplicate-library warnings; these were nonfatal. No compile or link errors occurred.

| Artifact | SHA-256 |
|---|---|
| `configure.wrf` | `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab` |
| clean log | `b6295e8f807bc9368ad4cd8f176b1490031bdfa3bde585d22c5d18afaff51192` |
| configure log | `ee09805e9c1e8c52a4f6bfc66ab24ffe0ded6a98028c41f2efea9dfcd477190f` |
| full WRF compile/link log | `e0265bd31a6659e544cf91001b8f7211e3c2678f171d7862cdcf934f6ac7c05c` |
| clean LibTorch archive build log | `9481918d50c49e13c87c9ee31c4d2ac7113b5a151181fece1a72030d6c0d32c7` |
| fresh `main/wrf.exe` | `1ed66f384917cb22ea60d8decdb4bbe9e756da423d75ad12e9bf6f7a6ca042af` |
| fresh `main/ideal.exe` | `642bcd9504596ffae00ebe2f97627e1ab3f7f6fc43afc66ff90d3c02e1058a45` |
| fresh `libwrf_sdirk3_libtorch.a` | `b6ba5644d5cca5779b824cf17534a31fc71b2a2b652c50d0428aaebb83c3d55e` |

## Graphify

Both code corpora were updated in this exact worktree at `ca051d47`. `dyn_em` contains 1,766 nodes / 2,567 edges / 98 communities; `external/libtorch_wrf/sdirk3` contains 3,545 nodes / 6,583 edges / 214 communities. Structural extraction reports 100% extracted for the Fortran corpus and 97% extracted with 218 inferred edges in C++; semantic extraction for documentation was not run because no LLM API key was available. These graphs are navigation aids, not evidence of numerical correctness. Graph outputs remain uncommitted under their respective `graphify-out/` directories.

## Archived 60-second PC2/RK3 pair

The one-rank `em_b_wave` comparison used the archived 17×17×17 setup, `run_seconds=60`, `time_step=60`, `diff_opt=2`, `km_opt=1`, `damp_opt=0`, `khdif=1000`, and `kvdif=10`. PC2 used `time_integration_scheme=6`; RK3 used scheme 0. The namelists were byte-for-byte the archived namelists and differ only in the integration scheme. Both used the same archived `wrfinput_d01` SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, `diagnostics.txt` SHA-256 `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`, and `input_jet` SHA-256 `9f4cdc9e55c316df8164d6cde4dc65c91e55927e43339d0addcc1a938966aba6`. PC2/RK3 namelist SHA-256 values are `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850` and `86d1ee65708470aa4c1683a34131f6e249d03a574678f5c7f6ca03ef55bca8f2`.

Both runs exited 0, reached 00:01:00 and reported `SUCCESS COMPLETE WRF`. Each output has 174 floating variables and 378,352 floating values across two frames; all values are finite. All 174 initial-frame floating variables match exactly between PC2 and RK3. WRF reported main-step elapsed times of 0.48716 s for PC2 and 0.07846 s for RK3; these are descriptive timings for this one step. The endpoint files are:

| Case | `wrfout` SHA-256 | `rsl.error.0000` SHA-256 | `rsl.out.0000` SHA-256 |
|---|---|---|---|
| PC2 | `1a4d68818597764938777d70290bd9a847a56d8003ece43b1fd07eec5649b144` | `139355bc1dc99facdb5322c66c74dcadd62ff98cb0c037052f933059936bc9c5` | `e069a1de93d7f029c780c8c6d494f94a32a742bb46c980b10a508afb21893e41` |
| RK3 | `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336` | `e5f93c36c765cdf7381e22a8f29d9b77e710fe8569acd7dab58a4e2b92f9e8c1` | `d3eb79ae991ca5f9a9c6525fabceb554f1ea44001530a34cc7c664d6cb527cb0` |

The clean #246 RK3 output is byte-identical to the actual #243 integrated RK3 output at `/private/tmp/sdirk3-v-on-w-integration/.validation/integrated-final/run_rk3/wrfout_d01_0001-01-01_00:00:00` (SHA-256 `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336`). That #243 comparator was built from source `a1d5f6a3c8c64e66510c81d5fbe98ce568ed180f`; its executable SHA-256 was `2e84a03096d101c1f8ecf828b9e2ec2bab98c3bdfadbf49e2769fad9fa8cad22`.

For PC2, the corrected clean #244 reference is `/Users/yhlee/SDIRK3-pr244-cleanbuild/.validation/pr244-clean/wrf_fresh/pc2/wrfout_d01_0001-01-01_00:00:00`, SHA-256 `3679f6674a1fdf384bc42f9fb85f887c6f09c17536d418cd3f92313f0c691d32`; it came from source `e19760eeedeef25480d841cb6458eab88619adb3` and executable SHA-256 `ec9ab9c9cbd7f8e75753e45185d0ef1ad6e5ca62874ecc0717187ba1646c8934`. The #245 scalar-mixed comparator is `/private/tmp/sdirk3-scalar-on-pr244/.validation/integrated-scalar/wrf_fresh/pc2/wrfout_d01_0001-01-01_00:00:00`, SHA-256 `8d9e012fa0062c9696df904b72d5add4f05c7b2a73cdb59a81a0be0df5e482e9`; its source is `9147bbdca8e34059847c908d6e633d9053dd13a4`, relinked executable SHA-256 `7cd81308091f7b517c595f150d162d648f6fd4f2bf6b43614978dc7a4368e634`.

Endpoint clean #246 PC2 minus each comparator (RMS / maximum absolute difference, float64 calculation over each variable's endpoint frame):

| Field | vs clean #244 RMS | vs clean #244 max | vs #245 mixed RMS | vs #245 mixed max |
|---|---:|---:|---:|---:|
| U | 1.09849920511e-6 | 3.33786010742e-6 | 2.75796226703e-9 | 1.19209289551e-7 |
| V | 2.32001265251e-6 | 1.83884985745e-5 | 1.5130681382e-10 | 1.86264514923e-9 |
| W | 0.0012785369572 | 0.00308190099895 | 3.57104174788e-10 | 2.32830643654e-9 |
| PH | 0.592652843232 | 1.46923828125 | 0 | 0 |
| T | 0.0202799565477 | 0.0643310546875 | 0 | 0 |
| P | 0.0608744077683 | 0.119209289551 | 0 | 0 |
| MU | 6.64444766012e-6 | 1.81198120117e-5 | 2.98023223877e-8 | 4.76837158203e-7 |

Full per-variable RMS/max comparisons cover all 174 floating variables against both PC2 references. The inline table below is generated from endpoint variables, requires equal shapes, and records finite-pair count; its source CSV is `.validation/pr246-clean/pc2_all_174_field_comparisons.csv` (SHA-256 `51329add64f66d9f8f8b53b50f5e3d26a2dedbea2633e5c0ce2bdca0c3d66d55`). This field comparison is evidence for numerical difference only; it does not establish forecast acceptance or temporal accuracy.

| Comparator | Variable | Values | Finite pairs | RMS | Max absolute |
|---|---|---:|---:|---:|---:|
| clean244_pc2 | `XLAT` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `XLONG` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `LU_INDEX` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `ZNU` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `ZNW` | 17 | 17 | 0 | 0 |
| clean244_pc2 | `ZS` | 5 | 5 | 0 | 0 |
| clean244_pc2 | `DZS` | 5 | 5 | 0 | 0 |
| clean244_pc2 | `VAR_SSO` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `U` | 4352 | 4352 | 1.09849920511e-06 | 3.33786010742e-06 |
| clean244_pc2 | `V` | 4352 | 4352 | 2.32001265251e-06 | 1.83884985745e-05 |
| clean244_pc2 | `W` | 4352 | 4352 | 0.0012785369572 | 0.00308190099895 |
| clean244_pc2 | `ACOUSTIC_GRAD_U` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `ACOUSTIC_GRAD_V` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `ACOUSTIC_GRAD_W` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `ACOUSTIC_GRAD_P` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `PH` | 4352 | 4352 | 0.592652843232 | 1.46923828125 |
| clean244_pc2 | `PHB` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `T` | 4096 | 4096 | 0.0202799565477 | 0.0643310546875 |
| clean244_pc2 | `THM` | 4096 | 4096 | 0.0202806411744 | 0.0643157958984 |
| clean244_pc2 | `HFX_FORCE` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `LH_FORCE` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `TSK_FORCE` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `HFX_FORCE_TEND` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `LH_FORCE_TEND` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `TSK_FORCE_TEND` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `MU` | 256 | 256 | 6.64444766012e-06 | 1.81198120117e-05 |
| clean244_pc2 | `MUB` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `ACOUSTIC_CFL_MAX` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `ACOUSTIC_DT_FACTOR` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `NEST_POS` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `P` | 4096 | 4096 | 0.0608744077683 | 0.119209289551 |
| clean244_pc2 | `ZX` | 4624 | 4624 | 0 | 0 |
| clean244_pc2 | `ZY` | 4624 | 4624 | 0 | 0 |
| clean244_pc2 | `PB` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `FNM` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `FNP` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `RDNW` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `RDN` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `DNW` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `DN` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `CFN` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `CFN1` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `P_HYD` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `Q2` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `T2` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `TH2` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `PSFC` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `U10` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `V10` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `RDX` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `RDY` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `AREA2D` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `DX2D` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `RESM` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `ZETATOP` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `CF1` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `CF2` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `CF3` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `XTIME` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `QVAPOR` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `SHDMAX` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SHDMIN` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SHDAVG` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SNOALB` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `TSLB` | 1280 | 1280 | 0 | 0 |
| clean244_pc2 | `SMOIS` | 1280 | 1280 | 0 | 0 |
| clean244_pc2 | `SH2O` | 1280 | 1280 | 0 | 0 |
| clean244_pc2 | `SEAICE` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `XICEM` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SFROFF` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `UDROFF` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `VEGFRA` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `GRDFLX` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `ACGRDFLX` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `ACSNOM` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SNOW` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SNOWH` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `CANWAT` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SSTSK` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `WATER_DEPTH` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `COSZEN` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `LAI` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `VAR` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `O3_GFS_DU` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `MAPFAC_M` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `MAPFAC_U` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `MAPFAC_V` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `MAPFAC_MX` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `MAPFAC_MY` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `MAPFAC_UX` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `MAPFAC_UY` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `MAPFAC_VX` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `MF_VX_INV` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `MAPFAC_VY` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `F` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `E` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SINALPHA` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `COSALPHA` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `HGT` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `TSK` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `P_TOP` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `T00` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `P00` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `TLP` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `TISO` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `TLP_STRAT` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `P_STRAT` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `MAX_MSFTX` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `MAX_MSFTY` | 1 | 1 | 0 | 0 |
| clean244_pc2 | `RAINC` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `RAINSH` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `RAINNC` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SNOWNC` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `GRAUPELNC` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `HAILNC` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `CLDFRA` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `SWDOWN` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `GLW` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SWNORM` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `OLR` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `XLAT_U` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `XLONG_U` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `XLAT_V` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `XLONG_V` | 272 | 272 | 0 | 0 |
| clean244_pc2 | `ALBEDO` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `CLAT` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `ALBBCK` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `EMISS` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `NOAHRES` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `TMN` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `XLAND` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `UST` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `PBLH` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `HFX` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `QFX` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `LH` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `ACHFX` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `ACLHF` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SNOWC` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SR` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `DEFOR11` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `DEFOR22` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `DEFOR12` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `DEFOR33` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `DEFOR13` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `DEFOR23` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `XKMV` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `XKMH` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `XKHV` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `XKHH` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `DIV` | 4096 | 4096 | 0 | 0 |
| clean244_pc2 | `K1_U` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `K1_V` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `K1_W` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `K2_U` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `K2_V` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `K2_W` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `K3_U` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `K3_V` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `K3_W` | 4352 | 4352 | 0 | 0 |
| clean244_pc2 | `C1H` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `C2H` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `C1F` | 17 | 17 | 0 | 0 |
| clean244_pc2 | `C2F` | 17 | 17 | 0 | 0 |
| clean244_pc2 | `C3H` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `C4H` | 16 | 16 | 0 | 0 |
| clean244_pc2 | `C3F` | 17 | 17 | 0 | 0 |
| clean244_pc2 | `C4F` | 17 | 17 | 0 | 0 |
| clean244_pc2 | `PCB` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `PC` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `LANDMASK` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `LAKEMASK` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SST` | 256 | 256 | 0 | 0 |
| clean244_pc2 | `SST_INPUT` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `XLAT` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `XLONG` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `LU_INDEX` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `ZNU` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `ZNW` | 17 | 17 | 0 | 0 |
| candidate245_pc2 | `ZS` | 5 | 5 | 0 | 0 |
| candidate245_pc2 | `DZS` | 5 | 5 | 0 | 0 |
| candidate245_pc2 | `VAR_SSO` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `U` | 4352 | 4352 | 2.75796226703e-09 | 1.19209289551e-07 |
| candidate245_pc2 | `V` | 4352 | 4352 | 1.5130681382e-10 | 1.86264514923e-09 |
| candidate245_pc2 | `W` | 4352 | 4352 | 3.57104174788e-10 | 2.32830643654e-09 |
| candidate245_pc2 | `ACOUSTIC_GRAD_U` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `ACOUSTIC_GRAD_V` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `ACOUSTIC_GRAD_W` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `ACOUSTIC_GRAD_P` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `PH` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `PHB` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `T` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `THM` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `HFX_FORCE` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `LH_FORCE` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `TSK_FORCE` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `HFX_FORCE_TEND` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `LH_FORCE_TEND` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `TSK_FORCE_TEND` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `MU` | 256 | 256 | 2.98023223877e-08 | 4.76837158203e-07 |
| candidate245_pc2 | `MUB` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `ACOUSTIC_CFL_MAX` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `ACOUSTIC_DT_FACTOR` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `NEST_POS` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `P` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `ZX` | 4624 | 4624 | 0 | 0 |
| candidate245_pc2 | `ZY` | 4624 | 4624 | 0 | 0 |
| candidate245_pc2 | `PB` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `FNM` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `FNP` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `RDNW` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `RDN` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `DNW` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `DN` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `CFN` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `CFN1` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `P_HYD` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `Q2` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `T2` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `TH2` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `PSFC` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `U10` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `V10` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `RDX` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `RDY` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `AREA2D` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `DX2D` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `RESM` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `ZETATOP` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `CF1` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `CF2` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `CF3` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `XTIME` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `QVAPOR` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `SHDMAX` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SHDMIN` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SHDAVG` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SNOALB` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `TSLB` | 1280 | 1280 | 0 | 0 |
| candidate245_pc2 | `SMOIS` | 1280 | 1280 | 0 | 0 |
| candidate245_pc2 | `SH2O` | 1280 | 1280 | 0 | 0 |
| candidate245_pc2 | `SEAICE` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `XICEM` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SFROFF` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `UDROFF` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `VEGFRA` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `GRDFLX` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `ACGRDFLX` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `ACSNOM` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SNOW` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SNOWH` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `CANWAT` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SSTSK` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `WATER_DEPTH` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `COSZEN` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `LAI` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `VAR` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `O3_GFS_DU` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `MAPFAC_M` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `MAPFAC_U` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `MAPFAC_V` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `MAPFAC_MX` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `MAPFAC_MY` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `MAPFAC_UX` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `MAPFAC_UY` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `MAPFAC_VX` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `MF_VX_INV` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `MAPFAC_VY` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `F` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `E` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SINALPHA` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `COSALPHA` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `HGT` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `TSK` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `P_TOP` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `T00` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `P00` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `TLP` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `TISO` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `TLP_STRAT` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `P_STRAT` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `MAX_MSFTX` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `MAX_MSFTY` | 1 | 1 | 0 | 0 |
| candidate245_pc2 | `RAINC` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `RAINSH` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `RAINNC` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SNOWNC` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `GRAUPELNC` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `HAILNC` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `CLDFRA` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `SWDOWN` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `GLW` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SWNORM` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `OLR` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `XLAT_U` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `XLONG_U` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `XLAT_V` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `XLONG_V` | 272 | 272 | 0 | 0 |
| candidate245_pc2 | `ALBEDO` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `CLAT` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `ALBBCK` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `EMISS` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `NOAHRES` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `TMN` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `XLAND` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `UST` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `PBLH` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `HFX` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `QFX` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `LH` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `ACHFX` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `ACLHF` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SNOWC` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SR` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `DEFOR11` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `DEFOR22` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `DEFOR12` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `DEFOR33` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `DEFOR13` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `DEFOR23` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `XKMV` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `XKMH` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `XKHV` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `XKHH` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `DIV` | 4096 | 4096 | 0 | 0 |
| candidate245_pc2 | `K1_U` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `K1_V` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `K1_W` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `K2_U` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `K2_V` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `K2_W` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `K3_U` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `K3_V` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `K3_W` | 4352 | 4352 | 0 | 0 |
| candidate245_pc2 | `C1H` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `C2H` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `C1F` | 17 | 17 | 0 | 0 |
| candidate245_pc2 | `C2F` | 17 | 17 | 0 | 0 |
| candidate245_pc2 | `C3H` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `C4H` | 16 | 16 | 0 | 0 |
| candidate245_pc2 | `C3F` | 17 | 17 | 0 | 0 |
| candidate245_pc2 | `C4F` | 17 | 17 | 0 | 0 |
| candidate245_pc2 | `PCB` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `PC` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `LANDMASK` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `LAKEMASK` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SST` | 256 | 256 | 0 | 0 |
| candidate245_pc2 | `SST_INPUT` | 256 | 256 | 0 | 0 |

## Strict `dt=15` snapshot OFF/ON

The same clean #246 executable was run with the archived strict PC2 setup for `run_seconds=240`, `time_step=15`, `diff_opt=2`, `khdif=1000`, `kvdif=10`, `sdirk3_newton_tol=1e-7`, and `sdirk3_krylov_tol=1e-7`. Both cases used the same e71b730a input and diagnostics sidecar; the two namelists differ only in `sdirk3_stage2_rejection_snapshot_diag` (`.false.` OFF / `.true.` ON). Their namelist hashes are `127b113239fc554bafe03092a60399627a04f00a595fc8f1ca3f0c68746d888b` and `4fef985474a6451aeec83b4fc8256cbc1b3f684bccd127e425aeb4bc3c19a9b1`.

Both runs exited 1 at the first timestep with the same Stage 2 fail-closed abort. The solver ended at scaled residual `1.36145e-6`, after 7 Newton and 301 Krylov iterations; telemetry records `newton_exit=zero_step_stall`, `first_rejection_iter=4`, and terminal stall iteration 6. Both partial outputs have one frame, 174 finite floating variables, and are byte-identical: SHA-256 `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`. The diagnostic snapshots record observed FP32 operands only and no RHS replay.

The ON run wrote both required artifacts. The first common-path rejection was at timestep 0, Stage 2, Newton iter 1, attempt 0 (`rho=-0.041891571554478126`, actual reduction `-3.3205876948731177e-9`, predicted reduction `7.9266247878880393e-8`). The terminal artifact captured Newton iter 6, attempt 1; metadata confirms `candidate_newton_iter=6`, `terminal_stall_iter=6`, and stagnation count 3 (`rho=-0.049757664347082765`, actual reduction `-2.5774131304183244e-10`, predicted reduction `5.1799319044392307e-9`). Snapshot PT/JSON SHA-256 values:

| Artifact | SHA-256 |
|---|---|
| first rejection tensor | `2a74d8249b180cb630b2010b0e8536ca41483a76f642729f2081b1bfe54fb921` |
| first rejection metadata | `6c178666250f88a924393e933b73ef6c61eac744938f28c033985d6d2840e6e8` |
| terminal stall tensor | `7ce7d471ac61f20797cc585e18afc729184870ae15671f6869c6043814085d26` |
| terminal stall metadata | `382d531ec86105601f98163e1c3eb00c1e87e844c5fc53351c18051a20d093ad` |

OFF/ON `rsl.error.0000` SHA-256 values are `81ab739785e2873d2b037652ea4b1b3877505eb6bf34d2487b3cb0eea2563fd8` and `0a4a65a9e1833fdc618f865477cf93a820720f24e1dbc2578bc14a9fe4e0bdcc`; `rsl.out.0000` hashes are `252f800cee0afb84f4e16ca7c4d175d7e7f5c8a3b2f1cc5a89e8af852a4cbec5` and `6df4394663a3a64f96846f5ee2e5c18dc8a9e7788bdec7f9c871fd467e462655`. The matching failure evidence supports only this setup and executable; it does not diagnose the underlying convergence cause.

## Limits and preserved evidence

The runs validate a one-step PC2/RK3 comparison and reproduce the strict `dt=15` failure while verifying snapshot contents. They do not establish forecast quality, long-run stability, convergence order, MPI scaling, or adjoint behavior. `ideal.exe` was built but not run. No full CTest suite was run. Raw build and model logs, executables, inputs, namelists, outputs, snapshots, Graphify outputs, and the complete all-field CSV remain in this isolated worktree under `.validation/pr246-clean/` and `graphify-out/`; no generated binaries or build outputs are part of the documentation commit.
