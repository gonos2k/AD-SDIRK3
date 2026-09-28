# T1 iter22 VJP half/quarter attainability probe

Local timestamp: 2026-09-29 03:41:28 JST (+0900)

## Measurement and result

At the repeated strict Stage-2 iter22 base, this output-only probe evaluated the full clipped VJP fallback candidate plus exactly two predeclared subscales: `alpha=0.5` and `0.25` of that already clipped direction. The candidates used the canonical FP32 stage equation:

```text
K_alpha = fl32(K + alpha*dK_gradient)
U_alpha = fl32(U_stage + h*K_alpha)
R_alpha = K_alpha - F_implicit(U_alpha)
```

Each sidecar trust prediction used the unchanged actual-ΔU linear model
`R_linear = R + (K_alpha-K) - J_F(U_eval)*(U_alpha-U_eval)`. The actual-reduction and rho gate stayed at the existing `0.25`; sidecar candidates did not enter solver selection or alter acceptance. The full candidate in the actual-model run was uphill and rejected. Both predeclared smaller canonical trials would pass the same actual-ΔU gate:

| Scale of clipped VJP direction | Measured actual reduction | Actual-ΔU predicted reduction | Actual-ΔU rho | Smooth predicted reduction / rho |
| ---: | ---: | ---: | ---: | ---: |
| 1.00 (solver candidate) | `-6.955e-11` | `-6.952e-11` | undefined | `1.808e-9` / `-0.03848` |
| 0.50 | `+1.364e-9` | `+1.364e-9` | `1.0` | `9.425e-10` / `1.447` |
| 0.25 | `+1.13e-9` | `+1.13e-9` | `1.0` | `4.809e-10` / `2.349` |

No finite-difference JVP fallback occurred. The base invariants `U_eval == fl32(U_stage+h*K)` and `R == K-F` both passed bitwise. Thus a smaller step along this same source-defined direction is an attainable descent for this captured residual map. This falsifies a no-descent claim for the full clipped direction at this base; it does not establish convergence or exclude other stage-history issues.

The nominal and realized displacements still differ materially in FP32. For alpha 0.5, packed block U (`ru`) actual/nominal displacement L2 is `3.513e-7 / 2.875e-8` (max difference `2.38e-7`, 4,896 changed cells, max 14.06 ULP); W (`rw`) is `3.084e-6 / 3.084e-6` (difference `2.503e-9`, max `3.618e-10`, 4,896 cells, max 97.5 ULP); PH is `2.271e-3 / 6.695e-7` (difference max `4.883e-4`, 4,900 cells, max 32 ULP). At alpha 0.25, the corresponding max ULP counts are 31.34, 176.8, and 4. These counts include packed halo cells, not only owned interior cells.

The full strict run still ends at Stage-2 `ZeroStepStall` on iter24 with residual `5.61148e-7` against `1e-7`; the sidecar trials were not adopted. There is no forecast step, so no forecast or RK3 comparison was performed.

## Stage-history diagnostic caveat

A separate attempt to enable the existing `WRF_SDIRK3_STAGE_OPERAND_DIAG=1` fails its diagnostic gate at Stage-2 entry, before Newton, with `hist_rel=1.574367e-2`, `hist_max_abs=2.440828e-4`, and `hist_max_rel=69.64121`. A follow-up isolated fail-marker measurement found `fp32_replay_exact=1`; the maximum relative history error is at flattened PH index 18381 (PH-local 3676), where actual history is zero and `hdiff=-2.129521890e-4`. The unconditional `hist_max_rel>0.1` gate therefore rejects a source history whose production FP32 recurrence replay is exact: this marker is a zero-net PH rounding false positive, not evidence of a missing stage source. The diagnostic failed before its separate per-source applied-delta authority, so source-by-source forcing attribution and physical-boundary consistency remain untested. The iter22 candidate pair was run with this fail-closed history diagnostic off; it used the production Stage-2 RHS closure.

## Controls and provenance

This was a disposable experiment in `/private/tmp/sdirk3-t1-iter22-halved-vjp-probe-20260929`, based on PR #255 `c6ce9e80ba72c31676ab7f63ecb99abb32456911` plus the previously reviewed local VJP fallback and actual-ΔU selector. Only the local Newton source contains the added output-only scale probe. Graphify matched the pre-instrument source and was refreshed afterward; it reported 3,564 nodes / 29,273 extracted edges. `git diff --check` passed.

The one-rank env-off and actual-model env-on pair used the same executable, `OMP_NUM_THREADS=1`, RHS counting, namelist, diagnostics, and input. Their first 515 ordered RHS digests match. The sidecar adds exactly four RHS records (two canonical candidate RHS calls and two actual-ΔU JVP calls); deleting totals `12425–12428` restores the complete previous 15,843-entry actual-model stream. The terminal iter24 snapshot’s 31 tensors are bitwise equal to the previous run. The fallback decision and terminal residual are unchanged. Both partial `wrfout` files have the same SHA; neither run completed a forecast step.

- Diagnostic Newton source: `e47303c29cb6849e9f08d78cf69ccb4e2ffe82170758a14d077f9691290e8108`
- Newton object: `1531e30d4c572ac042f1c1f9b2209f6a0aa3d026f90a2ff2874a61c400e8ad93`
- LibTorch archive: `04c481b4a32890c00b3bf92495a0017e76cbc9714b58db97f56c7d6f11faca2c`
- Shared executable: `eb4e258a1ab81742f38cf007193ce0d68b107470e06a5d815854d99f95b38540`
- `wrfinput_d01`: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`
- `namelist.input`: `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`
- `diagnostics.txt`: `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`
- Env-off / actual-model `rsl.error.0000`: `9b77ef4404d918acc283a61d9649bcf9c3b28578f52cb36f44e81111565ac3d0` / `c6a6b6c3344f1df893940ab39a3e63d8625ae12873b26c5d2fd6e7da20b496e2`
- Actual-model terminal iter24 snapshot: `7549f03c2d9b40c3801b863da0f5cb9643ce7d37c35530b05d7882bda19d5142` (31 tensors; bitwise equal to prior run)
- Env-off / actual-model partial `wrfout`: both `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`
- Stage-operand diagnostic failure log: `984c25370d835f792eef00f64beda23f30e9e3890b50fd7d5fba37f1b400428d`

This report is documentation-only on a branch based on integrated checklist HEAD `0ec2f963d5296aa7a44ea4c6b395d4cbaae79324`. No production change or PR is proposed from this experiment.
