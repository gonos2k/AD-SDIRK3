# Option-2 SFS/NBA preflight

Local timestamp: 2026-09-29 01:35:27 JST (+0900)

## Context and change

The SDIRK3 Fortran bridge does not pass WRF's NBA stress arrays. The Fortran option-2 path calls `sfs_driver` when `sfs_opt>0` and forms cal_titau stresses from `rhoavg*mtau`; SDIRK3 instead evaluates the scalar-K strain path. This mismatch exists even when `khdif=kvdif=0`, because the SFS path selection does not depend on those scalars.

Added a fail-closed check in `dyn_em/module_implicit_sdirk3.F:init_implicit_sdirk3` for `diff_opt==2 && sfs_opt>0`. It is after the existing non-hydrostatic preflight but before the `sdirk3_initialized` return, tile allocations, configuration, or bridge setup. No K predicate, config option, ABI change, or diff-opt-1 rejection was added.

## Validation

`git diff --check` passed. The changed Fortran source compiled and the isolated WRF executable relinked with `J='-j 2' ./compile em_b_wave` in a copy of the configured PR #246 cleanbuild. The protected cleanbuild was left untouched. The copied build's WRF source was at `28f8a62`; WRF Fortran inputs otherwise matched PR #262, and only `module_implicit_sdirk3.F` was overlaid. The prebuilt SDIRK3 static library was reused (no ABI changed), so the WRF executable is a Fortran-preflight validation image rather than a full PR #262 C++ rebuild.

Four one-tile `test/em_b_wave` startup runs reused the same `input_jet`-generated `wrfinput_d01`; each namelist set `run_seconds=600`, `km_opt=1`, `khdif=kvdif=0`:

| `diff_opt` | `sfs_opt` | Result |
| --- | ---: | --- |
| 2 | 1 | Failed at initialization with `SDIRK3_OPTION2_SFS_NBA_UNSUPPORTED`, before solver allocation. |
| 2 | 2 | Failed at initialization with the same marker, before solver allocation. |
| 2 | 0 | Completed 600 s; 3 SDIRK stages, 0 failed steps. |
| 1 | 1 | Completed 600 s; the new guard did not trigger. |

The two negative logs show the fatal marker at source line 546 and no solver-allocation message before it. The positive runs are startup/safety controls, not forecast-quality validations. NetCDF checks found no NaN/Inf in any floating output variable: option-2/SFS-off scanned 161 variables / 11,037,008 values; diff-opt-1/SFS-on scanned 167 / 13,494,608. Their output SHA-256 values are `3b24b1bab5df70d651720cf196fa849c9b7a6556d10d932f5f6d42ba14c50a49` and `421b5a8dedc27869cff5f869fd9a8a9beb074cb65edec094f47718c147c590ba`. No same-setup RK3 field/runtime comparison was performed: the change rejects an unsupported option-2 SFS configuration and leaves accepted configurations' numerical operators unchanged; the available archived RK3 result does not use this 600 s, zero-K setup. No forecast-quality claim is made.

## Source and executable identity

Target code base: PR #262, commit `4bef7ab5875603f2ca0612583ef161d8e14ea21a`, plus this working-tree change.

Validation copy: `/private/tmp/sdirk3-sfs-option2-validation-20260929`.

| Artifact | SHA-256 |
| --- | --- |
| `dyn_em/module_implicit_sdirk3.F` | `4f6903102b53f0671de6d87458c5c20ef34b39688e267276b9296c0f4b1183d7` |
| relinked `main/wrf.exe` | `3acf2fb76ab36d92dbf86d580bec8057f547a2d0fdecfcf0a416720256294fac` |
| reused `libwrf_sdirk3_libtorch.a` | `b6ba5644d5cca5779b824cf17534a31fc71b2a2b652c50d0428aaebb83c3d55e` |
| `configure.wrf` | `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab` |
| `test/em_b_wave/input_jet` | `9f4cdc9e55c316df8164d6cde4dc65c91e55927e43339d0addcc1a938966aba6` |
| generated `wrfinput_d01` | `c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912` |

The four case namelist hashes, in table order, are `f6d9862edd23b3adc61a26ea64852b374c863fc015013484c01efa47fced1430`, `def65039026a6d54f598b19941fb63b76e9cadd58bdeb4d50e27383a07a850f6`, `c5bfc87041fa4da12a480cd71cd08ef70c3e4766b1da650670ed95109a8c5b70`, and `7148fc74ae87ec300cd622134051d5cea5fa0c4cd98b9400bc64e899e224227b`.

Case `rsl.out.0000` SHA-256 values in truth-table order:

- `d2sfs1_k0`: `e395a67afae028799086a45835db797b634853d22cb06007291f36cb9df9e1d3`
- `d2sfs2_k0`: `28b0dd2b8c163ac8fb5af598316a0506fcb3bb5aa13c7b8b1673236f6ae12c80`
- `d2sfs0_k0`: `fb8bdcee6f070d6dc5556a2968e2adea1184487ebf9340ccab0b51aba02b6c96`
- `d1sfs1`: `4bcc58edde301324d398a9b7e0dfc0741a9e6db3441eb490924bd5a6dd64d908`

Compiler stack: gfortran 15.2.0, Open MPI 5.0.9, WRF configured `DM_PARALLEL`, macOS arm64; configured file hash is listed above.

## Graphify and review

Graphify was built before and refreshed after editing. The targeted four-file Fortran corpus (`module_implicit_sdirk3.F`, `solve_em.F`, `module_diffusion_em.F`, `module_first_rk_step_part2.f90`) refreshed to 184 nodes / 1,154 edges; its current `graph.json` SHA-256 is `fa3efb613d9f3ec85ce5f47babe4694eaf51b033d38e822b83b603bb2ad8e6c8`. The PR #262 SDIRK3 C++ code graph was refreshed on the exact target worktree (3,570 nodes / 21,420 edges). Graphify relationships guided navigation; the Fortran callers and stress formulas were checked directly.

Red reviewed the guard placement and reported no blocker. Exact-hash review artifacts and case logs remain under `/private/tmp/sfs-option2-cases-20260929` and the isolated validation copy. No WRF run modified the protected cleanbuild.
