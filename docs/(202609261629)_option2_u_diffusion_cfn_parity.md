# Option-2 U diffusion and top extrapolation parity

Local timestamp: 2026-09-26 16:29:00 JST

## Context

This increment was developed in an isolated worktree at PR #239 HEAD `1c5c828470b858146ad6b8f8e539a1ee79dade46`. The focused Graphify corpus was rebuilt from byte-for-byte copies of the target sources: 406 nodes and 2803 edges. Graph relationships were used for navigation; all claims below were checked against source and executable results.

## Changes

The compiled Fortran oracle now extracts and runs `compute_diff_metrics`, `cal_deform_and_div`, the U stress routines, `vertical_diffusion_u_2`, `horizontal_diffusion_u_2`, and `rk_addtend_dry`. It compares the complete owned U column on a shared terrain-following state. The fixture uses one complete periodic single tile, dry `km_opt=1` behavior, constant `khdif=2/3` and `kvdif=4/3`, `xkmh=2/3`, `xkmv=4/3`, `xkhh=2`, `xkhv=4`, `rho=1`, map factor 1.25, `dx=dz=1000 m`, and `dn=dnw=-1/4`. The interior profile is `U=[0,1,0,0]*(1+0.1*cos(x))`; a separate top pulse checks upper extrapolation.

The source-extracted driver computes metrics and deformation, prepares the needed periodic-X and Y-constant halo values analytically, then runs vertical U before horizontal U and passes their sum through the exact U loop in `rk_addtend_dry`. It does not execute WRF's `set_physical_bc3d`, MPI halo exchange, the enclosing `vertical_diffusion_2` / `horizontal_diffusion_2` wrappers, or a full `computeUnifiedRHS` call. On the C++ side the test composes private vertical-U and horizontal-U helpers, then applies the same map-factor division. This is composite U diffusion operator parity, not full-RHS parity.

The production change factors top extrapolation coefficient calculation into `refreshTopExtrapolationCoefficients()`. A WRF-supplied `fnm/fnp` profile remains authoritative; each zero-copy RHS update refreshes only `cfn/cfn1` from current `rdnw/rdn`. It leaves the internal generated-weight epoch state unchanged while external weights are active.

## Validation

The source-extracted Fortran oracle compiled in FP32 and REAL64 at `-O0` and `-O2`. For each precision/build, the owned U column was compared at all mass levels. The declared budget is `2e-6 * max(1e-12, signal)`.

| State | Quantity | FP32 signal | FP32 max error / budget | REAL64 signal | REAL64 max error / budget |
|---|---|---:|---:|---:|---:|
| Interior pulse | Composite raw U | 0.115609595 | 1.16e-9 / 2.31e-7 | 0.115609587 | 1.98e-8 / 2.31e-7 |
| Interior pulse | Post-`rk_addtend_dry` U | 0.092487678 | 5.05e-9 / 1.85e-7 | 0.092487669 | 1.58e-8 / 1.85e-7 |
| Top pulse | Composite raw U | 0.056452403 | 5.15e-9 / 1.13e-7 | 0.056452395 | 8.24e-9 / 1.13e-7 |
| Top pulse | Post-`rk_addtend_dry` U | 0.045161922 | 5.24e-9 / 9.03e-8 | 0.045161916 | 6.59e-9 / 9.03e-8 |

The external-weight zero-copy regression advanced with supplied half/half `fnm/fnp`, then changed only the top `rdnw` from 4 to 2 while `rdn` stayed 4. It observed `cfn/cfn1` change from `1.5/-0.5` to `2/-1`, while the external weights remained unchanged. The wrong-K control produced a 0.0560 U vertical-tendency difference; the cached-stage-metric control produced a 0.00207 horizontal-U difference against a 9.52e-9 budget. No tolerance was adjusted.

The first top-pulse failure was a fixture-ordering error: the C++ horizontal helper ran before the test set external `fnm/fnp` and matching eta metrics, so it used stale `cfn/cfn1`. That invalid setup differed from Fortran by 2.06e-3 in horizontal U at `(j=3,k=3,i=4)`, while vertical U differed by only 3.76e-9. After pinning `rdnw=rdn=4` and setting interpolation weights before both helpers, C++ reports `cfn/cfn1=1.5/-0.5`; top-pulse D11 and the composite U column match. This earlier result is retained here as a test-fixture diagnostic, not as a production defect.

The standalone CMake tree was fully built, then all 102 CTests passed (264.02 s). The separate production Make archive check passed with 24 manifest members. CMake used AppleClang 21 / LibTorch 2.13; the Make archive used Homebrew Clang, `_GLIBCXX_USE_CXX11_ABI=0`, and the macOS SDK.

For the WRF one-step check, the corrected `.validation/cfn-refresh-corrected/wrf.exe` was linked from the unchanged PR #239 cleanbuild Fortran objects and the candidate Make archive. The existing cleanbuild executable and archive were left untouched. One 60 s `em_b_wave` PC2 run (`time_integration_scheme=6`) and one RK3 run (`time_integration_scheme=0`) used `diff_opt=2`, `km_opt=1`, `khdif=1000`, `kvdif=10`, and the same `wrfinput_d01`. Both corrected candidate runs completed successfully.

| Field | PC2 shape | Finite | Candidate vs clean max error | RK3 shape | Finite | Candidate vs clean max error |
|---|---:|---:|---:|---:|---:|---:|
| U | 16×16×17 | 4352/4352 | 0 | 16×16×17 | 4352/4352 | 0 |
| V | 16×17×16 | 4352/4352 | 0 | 16×17×16 | 4352/4352 | 0 |
| W | 17×16×16 | 4352/4352 | 0 | 17×16×16 | 4352/4352 | 0 |
| PH | 17×16×16 | 4352/4352 | 0 | 17×16×16 | 4352/4352 | 0 |
| T | 16×16×16 | 4096/4096 | 0 | 16×16×16 | 4096/4096 | 0 |
| MU | 16×16 | 256/256 | 0 | 16×16 | 256/256 | 0 |

The PC2/RK3 reference differences at 60 s had max/RMS absolute values: U `0.01061/0.00209`, V `1.14e-4/2.23e-5`, W `0.00156/0.00063`, PH `0.8311/0.2916`, T `0.03387/0.00996`, MU `0.1447/0.0415`. The corrected candidate and clean outputs were bitwise identical for both integrators. Runtime was 2.78 s (PC2) and 0.68 s (RK3) for the corrected candidate, versus 0.89 s and 0.51 s in the clean reference. These single short runs are noisy; no performance claim is made.

The first candidate relink was invalid because the cleanbuild archive was selected from an earlier `-L/-l` pair; its output comparison is retracted. The corrected executable was relinked from the cleanbuild's unchanged `wrf.o`, `module_wrf_top.o`, and `libwrflib.a` with the candidate Make archive in both the library search path and explicit archive position. It is not a clean full WRF rebuild. `nm -gU | c++filt` confirms that it defines `TileSDIRK3UnifiedSolver::refreshTopExtrapolationCoefficients()`. The exact argv and reproducible dry-run recipe are saved in `.validation/cfn-refresh-corrected/link_command.txt` (SHA-256 `0f335ff3b4f11d4edb93e85f800aa986d1b1c1c28b770107424f282eba1aea23`). PC2 and RK3 each reached `SUCCESS COMPLETE WRF`.

No longer-duration `test/em_b_wave` forecast comparison or timestep-stability sweep was performed.

## Provenance

- Target base revision: `1c5c828470b858146ad6b8f8e539a1ee79dade46`. Final source/test candidate HEAD before the documentation-only commit: `d0881bcbeb77adf33f93730af73e9f37ceee2a7f`.
- Commits on `agent/option2-u-diffusion-cfn-parity`: production fix `12bc22a3bbd7c31dbbd64aa7096456ba4e691cd9`; test contracts `d0881bcbeb77adf33f93730af73f9e37ceee2a7f`.
- Fortran source hashes: `module_diffusion_em.F` `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea`; authoritative RK caller `module_first_rk_step_part2.F` `9c8c06b246c1cb1bb1632c25e5683a0cfd8545e6832df6eeaa1c744524325738`; generated RK caller `module_first_rk_step_part2.f90` `5e3886a49f9674c3894e73fb12518a292e8c5a0c79b5992ff9bd38b56e590312`; normalizer `module_em.F` `fb424486dbf9c903f77da6a15baf35e6772847e786b18d0297139eb48f0d586e`.
- Concatenated exact extracted routine hash: `a9c4a2572dcaacc99941fcbfd3b5f8cd091bfc1b5c6a87103520eed6013d6f37`.
- C++ source hashes: implementation `b4a16f19e50e71b2bb5076f36a1f07f268d5ea6b07d9b583de9c130650d0bdc4`; header `27fc0d18cae7e6187f57acc7b2716b60fb09df7f349a1d4f86d7a29deb84d4df`.
- Test hashes: C++ `1feaaebc74e203041be075b8604c61d9148423ed0dcfa000fcd2c1e40b2cd900`; Python `9d09b944459c3d2d061fda074364252d97978e5b13ff3c78f5da81a4f3e3a924`.
- Standalone test executable SHA-256: `2e448e06acdd87ddeeb2b9075555367047345fbaa66d7e0c9300b0fef80fe419`; candidate Make archive: `6c83086ec4434b3cac5deb83bfe024a1a8e60dd76e1edc3a939ce381964f915d`.
- Cleanbuild code revision: `ca736343ec40786d611b9aa216559188dfd4188d`, a docs-only descendant of the target base; clean executable SHA-256 `edeeefd1e5c16d2600a8e604186572232665c4aef0f3d99de89ee020a57cf9e3`; corrected candidate `.validation` executable SHA-256 `6354abcec80de9b4a5c6fac6c597ef2ed15ffb9b9b3a9f921eb3c1901bb4124a`.
- One-step input hashes: `wrfinput_d01` `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`; PC2 namelist `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850`; RK3 namelist `86d1ee65708470aa4c1683a34131f6e249d03a574678f5c7f6ca03ef55bca8f2`.
- One-step run artifacts (clean and candidate outputs have the same SHA-256):

| Artifact | PC2 SHA-256 | RK3 SHA-256 |
|---|---|---|
| Namelist | `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850` | `86d1ee65708470aa4c1683a34131f6e249d03a574678f5c7f6ca03ef55bca8f2` |
| Corrected candidate `run.log` | `cfa912c29cb665b91f87af71d4067844fef2c803a2d0988dd2657f8ff85c3682` | `f2a2b5dfa8fc169e57e07fe4e962dbaf1ac59e227ad3b73f7a4f3d4cdf6635f1` |
| Corrected candidate `rsl.error.0000` | `b41c441e7fd5e2afeca85c5b559873de36ffaee035dd57ad0171b25ea4679f0d` | `bea8d08f7cdf6b556478f436f4f49bb4f397df4e6216a4edc8f2b09f0a87cb28` |
| Corrected candidate `rsl.out.0000` | `0868601c429a7648722f3d1100b6a2da3dc497747345e000736ea659cef827d2` | `610a64603f2915d0ec74efd2d4b3207d76159b0ca5418bd826c4200f18291be1` |
| Clean `rsl.error.0000` | `34bbecdf588fba7ef0292a4130fcba3d28c68c4e69fa748e1f01a1806e86ee2e` | `efb2d35a3023b696293baf27b224b9c77ea430eac74bfa1cf8e4a25124396d5f` |
| Clean `rsl.out.0000` | `acce4c5ea60af671cb64309a7917258792b8adc4e341a1fa331f4cf626ec5660` | `5aa7f282c8ff21253dde7fcb90452a7f1b0904a55b4c031b649c635d8ee3bbc9` |
| `wrfout_d01_..._00:00:00` | `1bb59b0122997db6572e4aa9f2c2ebd3eb9a84e80e95b0c4b58a9fff89d733ca` | `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336` |

The clean reference logs are retained under `/Users/yhlee/SDIRK3-pr239-cleanbuild/.validation/pr239-cleanbuild/runs/{pc2,rk3}`. Full CTest output is `.validation/cfn-refresh/ctest_final.log`, SHA-256 `adbb2e798396678258e9bfb130694858f7677f3ed758346c9d83921365d66cc8` (102/102 passed). The corrected candidate artifacts are in `.validation/cfn-refresh-corrected/`; the earlier `.validation/cfn-refresh/wrf.exe` is superseded and must not be used as evidence.
- Standalone Fortran compiler: GNU Fortran 15.2.0; production Make C++ compiler: Homebrew Clang 22.1.4.

## Remaining scope

The option-2 native guard and one-step WRF run cover the supported dry single-tile, constant-coefficient case. The standalone C++ U parity oracle still composes private operators rather than invoking the complete `computeUnifiedRHS`; V/W/scalar diffusion parity, actual WRF halo/BC execution, MPI decomposition, and longer forecast quality/performance comparisons remain open. External→internal interpolation-weight source switching was not included in this cfn-only fix.
