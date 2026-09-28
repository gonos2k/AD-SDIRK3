# Option-2 V stress mass-basis correction and validation

Local timestamp: 2026-09-27 22:48:19 JST (+0900)

## Context and change

This work starts at merged feature source `fb025cd40dd7f3b288cce6a7d6a3249e8faabbc8` (PR #248 merge; its source tree is identical to PR #248 head `24fe3e6a18afd162ab57943b915f0bf2781cc5a4`). At report creation, it is isolated on local branch `agent/option2_v_mass_basis_step10` and remains uncommitted and unpushed.

For the fixed-mass V tendency path, WRF expects the primitive stress rate `(H+Z)/L_v`. `computeUnifiedRHS` later divides `rv_tend` by `M_v`, while option-2 V stress `H` and `Z` are source terms before that final division. The stress conversion now multiplies both components by `M_v/L_v`, so the final division returns `(H+Z)/L_v`. `L_v` is the canonical V layer mass, with the existing `c1h*mu_v+c2h` construction on the noncanonical fallback. The conversion is limited to option-2 stress diffusion. Option 1 and option-2 non-stress horizontal scaling keep their existing paths.

The final production diff contains no Step9/Step10 TLS capture hook. A private opt-in capture was used only in the isolated development build to test the exact Step10 `v`, `rho`, `Kv_mom`, `defor23`, `rdnw`, and raw `v_diff_v` passed through the actual RHS. The capture logs are retained as diagnostic receipts below; they are not required by the regression test. The permanent test checks the public ON/OFF RHS and extracted Fortran H/Z operators.

## Step10 identity gate and source checks

The corrected EOS-tuned actual-V fixture gives `rho=1` on both sides (C++ maximum EOS difference `1.192092896e-7`). During development, the Step9 captured operands and pre-scale H matched independent helper inputs/output exactly. Step10 captured input/output maximum errors were exactly zero for physical, packed, and map1 layouts:

- physical: 160 owned V cells; captured `v/rho/Kv/D23/rdnw/raw Z` errors all `0`;
- packed periodic: 112 owned V cells; all six errors `0`;
- map1: 160 owned V cells; all six errors `0`.

Those internal-local identities are development-only evidence. In the final hook-free CTest, C++ raw H/Z are checked against the source-extracted Fortran vertical/horizontal stress output, and actual public RHS ON/OFF differences are checked against the source primitive rate. The raw H/Z maximum errors are `1.49e-8/1.49e-8` physical, `2.24e-8/1.49e-8` packed, and `1.12e-8/1.49e-8` map1.

The ON/OFF comparison uses a predeclared FP32 error floor per owned cell:

`B = 4*eps32*max(|R_on|, |R_off|) + 2e-6*signal_phys + (err_H+err_Z)/L_min`.

The total actual-RHS primitive errors and floor maxima are `1.04e-10/6.39e-10` physical, `7.81e-11/6.74e-10` packed, and `4.39e-11/5.21e-10` map1. The maximum ON/OFF magnitudes are `1.329e-3/1.327e-3`, `1.403e-3/1.401e-3`, and `1.085e-3/1.083e-3`, respectively. The legacy `H/alpha_v + Z/M_v` mutant is rejected with residuals `4.385e-7`, `4.404e-7`, and `1.877e-7`, at least 359 times the corresponding floor. The source-side coupled operator self-check is within `1.49e-8`.

The standalone actual-V fixture uses uniform `M_v=80,000` and `L_v=92,000`, so it does not independently exercise a vertically varying V layer-mass ratio. The production scale is formed from the per-cell `level_mass_v`; the WRF comparison below provides a one-step, full-profile integration check, not a controlled isolated test of each vertical mass level.

An earlier broad full-RHS fixture was discarded: its Fortran side hard-coded `rho=1` while C++ derived density from an unmatched base state, producing a raw Step10 mismatch of `0.1300`. That result is a fixture mismatch, not evidence about V scaling. The corrected fixture and source-owned comparisons above supersede it. The preserved pre-fix dedicated actual-V receipt shows the real mass-basis residual before the correction.

## Validation and provenance

- CMake full build completed all 190 build steps. After the final unused-test-variable cleanup, the `test_scalar_diffusion_contract` target was rebuilt and linked before the final 107-test run.
- Targeted CTest passed 4/4 after final test cleanup: `Option2_Momentum_Geometry_Source_Parity`, `Option2_V_Actual_Rhs_Contract`, and U full-RHS map1/map1.25 controls. The final full-suite log includes these cases.
- Full CTest passed 107/107 in 236.89 s on the final C++ test source. Log: `.validation/v-step10/full_ctest_107_postcleanup.log`, SHA-256 `88cf4aac7fedb338c4c4acd6dc884127100701d5f6739c408015561134c9e16d`. Per-test output, including the V source receipt lines, is in `.validation/v-step10/LastTest_final_107.log`, SHA-256 `49057f530bc297ec9a0a7e231b9267e2123ed6bfda2047e3fddf05102a8c2059`.
- The production Make archive was invoked with `LIBTORCH_ROOT=/opt/homebrew`, `LIBTORCH_ABI=0`, and Homebrew clang. The raw Make compile transcript was not retained independently, so this records the build invocation rather than an independently audited ABI flag. The candidate archive linked into WRF and the candidate executable ran. `Core_Archive_MakeParity` passed 1/1; archive and CMake archive each match the 24-member manifest (the Make archive lists 25 entries including `__.SYMDEF`). Archive SHA-256: `596970bb4cc497279bcbcaaa5b9ebedf2c694a40b735e9fff92e354d3b151fd9`. Parity receipt SHA-256: `cd32fecc89b9944fc370079be22aeb62b33696aaa0919decb9d633215b15aa44`.
- Final C++ test executable SHA-256: `4dc6a312f6428e3c0b366c6a04b9eb83cf72bf5d17f2e6593f24a84469b4f435`.
- Final implementation SHA-256: `a34900fc4377edce9daa310a90afb638a5abf529ce005d7cd399de3d006550dd`. C++ test SHA-256: `e822b0245fbe5a0376d5181a88643de917b9947a87759c019c31dfd822d1437f`. Actual-RHS Python test SHA-256: `4a7f2db025d5165aad41e02f26817a73afec3996148ad4a4138152736eb3e8a9`. The CTest inventory and count claimants are updated to 107.
- The extracted Fortran routines come from `dyn_em/module_diffusion_em.F` SHA-256 `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea` and `dyn_em/module_em.F` SHA-256 `fb424486dbf9c903f77da6a15baf35e6772847e786b18d0297139eb48f0d586e`.
- Development-only capture stdout receipts are `.validation/v-step10/capture-physical.stdout` (`79d87a41ced63b56649fcae2c45993bac83b9b5faa529863bb96571c357a172c`), `capture-packed.stdout` (`ac1166a3b009c931a3c330c87a3be9f0a3f76a12834116df82860afbaf058073`), and `capture-map1.stdout` (`cad0a3cc521db1bfb929ab0df23761c028ccb8d450649bd01e8d9f2b21b7f32e`). The pre-fix dedicated source comparison is `fortran-actual-v-source-match.log` (`05b7a26ab9e2ca75d4d797b964e446454100f0e26d149ab3a9291b0e23c8d79e`); the post-fix diagnostic output-level comparison is `fortran-actual-v-after-scale.log` (`dd6ca9b255c9bced27a78a451884fb26c24e7afdc5b3fc59acca978670e025f4`).
- Graphify used the focused mirror `/private/tmp/sdirk3-v-massbasis-pr248-graph`, whose implementation, C++ test, Python tests, and Fortran sources match the final worktree hashes above. Refreshed graph SHA-256: `227c15e1c659a65b739f0a9def8ae5392732f1028f76b862f5e5ba979b207f4b`; report SHA-256: `c9a038af6995ff20e7e6df8cf38af1adc52b17cdd6ee21e9787e04e5f6fc921b`.

## WRF integration comparison

The Green team performed an incremental, candidate-first WRF relink against the preserved #246 clean WRF objects using the production archive built with the reported `LIBTORCH_ABI=0` Make invocation. No separate raw Make compile transcript was retained; the candidate linked and ran. Candidate executable SHA-256 is `693dd198ad7a0ec91f21fb774c8fde853a31ff539b876fa5034ae7ca49c687ee`. The later unused-variable cleanup touched only the standalone C++ test; the production implementation (`a34900fc...`) and archive hashes are unchanged, so the WRF executable contains the exact final production code. The complete tracked relink/input/output receipt is `docs/(202609272244)_option2_v_candidate_wrf_receipt.md`, SHA-256 `cb4ef23cf82e4284deed3637486e41f5039419a81c7ea8f0f7740064e5a878b8`; raw artifacts remain under `.validation/v-step10/wrf_candidate_final/`.

The one-rank comparison uses the same archived 17×17×17, 60-second setup (`diff_opt=2`, `km_opt=1`, `damp_opt=0`, `khdif=1000`, `kvdif=10`). Candidate PC2 and RK3 outputs are each byte-identical to the preserved clean #246/#247 reference outputs, with all 174 floating fields / 378,352 values finite. The separate K0 PC2 control (`khdif=kvdif=0`) is byte-identical to its clean reference. Candidate main-step timings are PC2 `0.51866 s`, RK3 `0.07971 s`, and K0 PC2 `0.48974 s`.

For the K>0 candidate endpoints, the same-input PC2-minus-RK3 float64 differences are:

| Field | RMS | Maximum absolute |
|---|---:|---:|
| U | 0.00209011943778 | 0.0106077194214 |
| V | 2.21139003568e-5 | 9.93933063e-5 |
| W | 0.000665487492008 | 0.00167457456701 |
| PH | 0.306797021925 | 0.94140625 |
| T | 0.0103997918347 | 0.0304565429688 |
| P | 0.039813248637 | 0.131130218506 |
| MU | 0.0415027193922 | 0.144721031189 |

These are one-step scheme differences, not errors against truth or a convergence-order estimate. No RK3 K0 run was requested. This WRF run is a narrowly scoped integration check and does not establish long-run forecast quality, MPI scaling, or adjoint behavior.
