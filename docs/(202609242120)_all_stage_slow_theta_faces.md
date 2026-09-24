# Accepted slow theta face fluxes at all four ARK stages

Timestamp: 2026-09-24 21:20:12 JST (+0900)

## Context and change

This candidate starts from PR #221 head `dd5d7adb78c767fb65e436ff6577b48c9651a34e`. #220 had already captured the actual theta advection X/Y/Z and horizontal diffusion X/Y producer faces at the first ESDIRK stage. The later slow stages are also evaluated once each, after the corresponding implicit stage is accepted, at `U_conv=U_stage+dt*a_ii*K_fast`; their producer faces can therefore be associated with the accepted `k_slow` without choosing among Newton iterations.

The default-off `ArkBudgetTrace` now stores one detached theta-face snapshot for each of the four slow RHS evaluations. Capture is armed immediately around `compute_k_slow(U_conv)` and reset before the next RHS call. The test reconstructs five signed face-divergence components with the production map factors and compares their sum cell by cell with `L·theta'_slow + theta'·c1h·MU_slow`, where `L=c1h(MUB+MU)+c2h`. Its separate full-theta budget still accounts for the constant 300 K through the mass change. Each direction must have a resolved signal, so a zeroed component cannot silently pass because another dominates. A trace request on the full-halo path fails explicitly: owned faces and halo correction fluxes are not assigned the same meaning here.

The supported fixture is one dry, closed, nonuniform-hybrid, nonunit-map tile in ordinary ARK mode, with fixed positive horizontal diffusion, `kvdif=0`, zero caller-supplied physics tendency, no Rayleigh damping, and no AD halo exchange. This change is observation-only under a test flag. It does **not** reconcile stage 2–4 fast Newton `F/K/R` faces, vertical scalar diffusion H3, nonzero internal/external sources, or partial-tile/MPI ownership. Accordingly, S1b-θall remains open.

## Direct validation

The final local test reports accepted slow face-to-tendency maximum discrepancies:

| Stage | Maximum error | FP32 operation-scale budget | Smallest of five component signals |
|---:|---:|---:|---:|
| 2 | `1.04453e-4` | `4.38703e-4` | `35.8156` |
| 3 | `1.02021e-4` | `4.38413e-4` | `35.5390` |
| 4 | `1.56742e-4` | `4.38840e-4` | `35.9462` |

Stage 1 retains its direct face checks from #220. A copied-source negative control deliberately omitted capture for stage 3 while leaving the numerical RHS intact. It compiled, and the test rejected the missing face inventory with `accepted slow stage lacks theta producer faces: 3`. The authoritative source was restored and rebuilt. The direct final-run log SHA-256 is `19a3c0e6f191562e47b129c845ef375f97e872fa30fc578f18c2625be31c06cc`; the mutation log is `f9f7630afe13fdfba485fae70fdcc7bdaf6fb704be0a4883a22c339d875e54a0`. Final-source `Full_Tile_*` CTest passed 5/5 in 117.28 s, transcript SHA-256 `05f0e39a2738ce9ebc51dfcb67d530e17dee7d28ec0461438d130548b14e7c53`.

Source SHA-256: `wrf_sdirk3_tile_unified.h` `2866a1df1b66345ec3e3ccb918a6de6579fd6c53f26cfd410419baad66abacb7`, `wrf_sdirk3_tile_unified_impl.cpp` `e609bb7143766f079d7834ad1d992d4a34562b98dd2594e88a80c602ae7def6d`, `tests/test_full_tile_step.cpp` `7b83d0b2821232324720ebbf70d41079ab7e04eb54498b9aae1a6ca7c209a286`; rebuilt test executable `828be464dbe7b30f9ffb4166fcacd31c4090fa49d1f53f3cdd5065ed6d38cb0e`. The focused Graphify source corpus was refreshed after changes, but all numerical claims here follow the actual producer formulas and executions.

## Short WRF and RK3 context

The rebuilt C++ archive SHA-256 is `e29fcebbb580671f94ad5c14950198783278c1b09f4cbc8951f497dc2c3b1807`; it was incrementally relinked against archived WRF objects/configuration, **not** a clean full WRF build. The executable SHA-256 is `0ad22bf597f0dba5db46906e891c69a3e48f1cc142def552f885405dcac9097b`. Same-input one-rank/one-thread `em_b_wave` PC2 diffusion OFF (60 s × 4) and enabled diffusion (`diff_opt=2`, `khdif=1000`, `kvdif=0`, 60 s × 1) runs both completed. Outputs were byte-identical to the previous #220 results: OFF `feddafb618397f279ece672110c13d67312b8836fd360b24728edaf8391b8d23`, ON `33ef0a3ca62f68b9b851ed9d757668ca9257ee8731c24dbe81621aebdb429b75`. This supports default-off observation parity, not general model validation.

No new RK3 run or repeated runtime comparison was performed. The archived same-setup RK3 comparison from the #220 report remains the field-error context: U RMS `0.002088916`, V `0.0000220964`, W `0.0000872781`, PH `0.0325392`, T `0.0000309467`, MU `0.0414933`. A short one-step field difference does not establish third-order time accuracy or same-accuracy speed.

## Next actions

Validate this exact candidate head in no-cost CI. For S1b-θall, tie fast face records to the coherent accepted Newton residual evaluation and measure any postsolve K−F transform; then add vertical diffusion H3 and explicit source buckets with nonzero controls. Keep the quadratic ARK endpoint mass×theta defect separate from a stage RHS budget. Follow with the independently open hybrid/terrain Fortran comparison, staggered momentum budgets, MPI/tile boundaries, active time-order and whole-WRF adjoint items in the remaining checklist.
