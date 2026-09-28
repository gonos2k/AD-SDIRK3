# Reviewed-stack integration and remaining checklist

Local timestamp: 2026-09-29 03:16:08 JST (Asia/Tokyo).

## Context and changes

The integration worktree combines the reviewed feature-base history with the W actual-RHS/native option-2 stack (#253, #260–#264), the T1 retry/evidence stack (#254–#259), and the separate T1 model-coordinate audit (#252). It also records ancestry from `adsdirk3/main` at `c8a135a1b2ade8b1382f712a38b066a81eacb348`. The W and T1 heads had a conflict-free merge tree `d65487ccccdd9cb93bbe674d35b14e856cb03cfb`; merging current main changed no file content. The integration contains reviewed production changes as well as tests and reports, but none of the local VJP-gradient fallback experiments described in the T1 reports.

This integration does not itself fix the remaining strict Stage-2 T1 stall. The last 15-second `em_b_wave` evidence still stops before a forecast even with dyadic trust retry. The reported VJP fallback and realized-displacement model comparisons were disposable experiments, not merged solver paths.

## Validation at this checkpoint

The merged source configured and built 217 CMake targets with AppleClang 21 and Homebrew PyTorch 2.10 (`Torch_DIR=/opt/homebrew/share/cmake/Torch`). The full 109-test CTest run initially passed 108 tests; `Core_Archive_MakeParity` failed solely because its required production Make archive had not yet been built. After building that archive from this same source with Homebrew clang and `LIBTORCH_ABI=0`, the single failed test passed on rerun. Thus every registered test has passed against this merged source; the full 109-test command was not rerun after the archive preparation. The subsequent #252 merge added only one report and did not change code or test registration.

CMake archive SHA-256: `244040a58e41b11f8da240e25b17ff3803e002cc50491dfd844a57ee2cd4e1ab`. Make archive SHA-256: `b73dc1330d514dd8ee52cc02160dd4dbab1f77abd4f8384f0aeca3283cb39818`. The parity contract compares exact archive member names and the shared 24-source manifest, not byte-identical compiler output. Full-step test executable SHA-256: `3b437d5e589ee760db2d5af01db1b6d93351d65da9418cfd212e155d00f20f5c`; scalar diffusion test executable SHA-256: `1a7f4726491f6c2243ed5ad27bc8a4ab33aa299c2ea7a362860480a7b5fbcd0c`.

No integrated full-WRF rebuild or new `test/em_b_wave` forecast was performed at this checkpoint. Consequently there is no new same-setup archived RK3 field or runtime comparison. The prior individual PR reports retain their narrower WRF evidence; they do not establish a forecast for the merged tree. The standalone Apple libc++ build does not validate a WRF ABI0-linked executable.

## Remaining checklist

| Item | Status and next closure evidence |
| --- | --- |
| Reviewed W/T1 stacks and #252 on one source tree | Locally combined and standalone build/test checked; exact-head remote integration CI and PR still pending. |
| K1 native option-2 W RHS PH/MU derivative | Closed for the single `ExplicitOnly`, fixed-scalar-K, dry physical-tile contract in #264; not a whole-step claim. |
| K1 native W whole-step active diffusion derivative | Open. Existing ON−OFF whole-step test uses a legacy/fallback option-2 path with `smagorinsky_opt=0`; test an accepted native-gate W PH/MU direction with an increment FD/VJP budget. |
| L34 broader operator | Open for spatially supplied K, remaining U/V/W/scalar interactions, boundary ownership, non-unit maps, and MPI production parity. Do not infer these from the current single-tile contracts. |
| T1 strict 15-second convergence | Open. Stage 2 still stalls; no successful forecast or same-accuracy PC0/PC2/RK3 comparison for this configuration. |
| G1 physical budget and time accuracy | Open for true layer mass/area and stage flux/source budgets, followed by timestep refinement above solver error. |
| A1 active whole-step and full-call adjoint | Open beyond bounded single-tile checks, especially actual WRF packing, physical boundaries, halo transpose, and state-dependent coefficients. |

Next actions are to finish native W whole-step derivative closure, validate that amended exact tree, run integration CI, and create the integration PR. A full WRF/reproducible RK3 comparison remains a separate acceptance gate for numerical completion.
