# AD-SDIRK3 numerical-contract PR validation

Recorded: 2026-09-19T10:47:46+09:00

## Source and scope

Base: `38f2953083901f6d52f288f9a0d4c334772579da` (PR #197).
Validated code: `d35d4d89efdab0f8af08de51d6c4208172dc6dfb`.
This report and AGENTS.md are documentation-only additions after that code revision; numerical checks were reused, not rerun for documentation changes.

The branch corrects Newton/Krylov/trust-model contracts, WRF staggered and hybrid-coordinate dynamics, cache publication/lifecycle, ARK stage composition and native trajectory derivatives. It exposes the bounded fixed-trajectory begin/pullback/close API to C and Fortran callers. See the component README and committed contract tests for supported configurations and reproducible entry points.

## Validation tied to the code revision

| Check | Observed result | Scope |
|---|---|---|
| Clean C++ build | Completed | Fresh core and tests, matching current headers |
| CTest | 101/101: 95 non-MPI and 6 MPI | Includes MPI contract cases at 1/2/4 ranks; does not establish full MPI adjoint support |
| Static ratchets | 85 rules passed | No baseline relaxation |
| Fortran bridge | Authoritative module compiled and linked | Updated module inserted into a copy of the archived Fortran library and linked to the fresh C++ archive |
| Deferred trajectory and C ABI probes | Passed | First publication, finite pullback, rejected oversized cotangent and output-sentinel preservation |
| WRF `em_b_wave` | Exit 0; 161 floating fields finite and bit-identical at two frames | Serial PC0, dt=60 s, T=240 s, trajectory recording off; comparison is against archived same-profile SDIRK3 |

Recorded toolchain: AppleClang 21.0.0.21000334, GNU Fortran 15.2.0, Command Line Tools SDK 26.5, local arm64 LibTorch and MPI. This was a clean C++ rebuild plus the stated Fortran module/archive replacement, not a clean rebuild of the entire WRF tree. At PR preparation, the configured installation reports LibTorch 2.10.0, Open MPI 5.0.9 and netCDF 4.9.2.

Artifact SHA-256:

- Core archive: `f351b08c10909b294c930eceb6541d2974fa75a67d47f1bc35c447dbb0f0a493`.
- WRF executable: `03ca31deab19d151c0be42d59bf95aa29d247fdad40172bd1cc55e9c181bd1c3`.
- Output and archived SDIRK3 reference: `a31482322334e1a6074439de0ad38132366d440f262eac7aba8a209eeb0ccb6c`.
- Reviewed pre-commit code patch over `3644a30`: `9990affd45ae546da048f8dba696fd2fea64ac58d21ff3a8f6e14e60dddd91cb`.

Detailed logs remain in the local, untracked evidence directory `.validation/continuation-1611/trajectory-bridge-current-20260919/`. They are not bundled in this PR. This report records their results and identifiers without implying that remote CI has run on this PR.

## Open gates

- V01: actual production temporal third-order convergence remains unproven. A private FP64 working-state refinement experiment had mixed field slopes; it is not this code revision's order proof.
- V02: operational timestep and long-duration stability claims remain bounded. Earlier runtime evidence must not be attributed to the newer trajectory bridge executable.
- V03: native fixed-trajectory derivatives do not complete the whole WRF caller transpose, Taylor validation, or 4D-Var adjoint.
- V04: the archived same-setup split-explicit RK3 reference is unavailable, so no RK3 forecast/runtime comparison or speedup claim is made.
- L34: horizontal diffusion remains open. Independent review found option dispatch, W coefficient ownership, full caller map normalization, and missing scalar terrain terms. Private candidates and test-oracle repairs are excluded from this PR. A private normalization A/B still failed; exact-source scalar comparisons also remain failing. None is production completion evidence.
- CUDA and full-model MPS validation remain outside the demonstrated CPU scope; component-specific MPS evidence must not be generalized.

No new model run was performed while preparing this PR. Green/Red Luna-high reviews checked the branch scope and bounded evidence. The PR is a draft for review; these results do not establish completion of the full numerical project.
