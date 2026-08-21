# R9 — what the corrected measurements say

All measurements: `em_b_wave`, WRFParity, `imex_split_mode=3`, dt=600,
`WRF_SDIRK3_STAGE2_GMRES_RESTART=600 WRF_SDIRK3_STAGE3_GMRES_RESTART=600`, single rank.
No independent review has been run on this work (`/code-review ultra` is user-only).

## 1. The three coordinate systems, confirmed in source

| system | vector | where |
|---|---|---|
| physical | `R` | what the stage gate judges |
| Krylov (S) | `r~ = S^-1 R` | `gmres_rhs = -(S_inv_diag_ * R)` |
| objective | `L r~` | whatever left weighting is installed |

`S` is **not** the identity: block-constant `S_q = max(||R_0,q|| / sqrt(n_q), floor_q)`,
rebuilt at Newton iter 0 and frozen for the stage.

## 2. Three defects, all confirmed, all fixed

1. `rho_unscaled = ||r~|| / ||D^-1 b~||` — unweighted numerator, weighted denominator.
   `b_inner` is scaled in place *before* `bnorm_safe` is taken.
2. the stage-WRMS objective was applied as `E^-1`; on Krylov vectors it must be `E^-1 S`.
3. the "physical residual share" was the share of `r~`, i.e. the (S) share.

Now one helper each (`wrf_sdirk3_krylov_metrics.h`), and a contract that encodes all three
(`Krylov_Metric_Coordinate_Contract`, 29 cases).

## 3. The 70-291x number is retracted; the HYPOTHESIS survives at 30x, against a different norm

Because the RHS cancels, `rho_D / rho_mixed = ||D^-1 r~|| / ||r~||` — a directional
amplification of `D^-1`, with no denominator in it. Correctly normalised, from the same `r`
and the same `b`:

| solve | rho_D | rho_S | rho_phys | spread | solver_error |
|---|---|---|---|---|---|
| 1 | 0.2196 | 0.1958 | 0.3086 | 1.58x | 0.2196 |
| 2 | 0.2735 | 0.3503 | 0.1768 | 1.98x | 0.2735 |
| 3 | 0.9412 | 0.9455 | 1.001 | 1.06x | 0.9412 |

`solver_error` is the solve's own convergence quantity and matches `rho_D` exactly in all three
— the helper and the solve agree about what is being minimised.

Against the RAW physical norm the objectives differ by under 2x, not 10^2. But the stage gate
does not judge the raw physical norm; it judges the WRMS. With the weights handed over
(`E^-1 S`, the fourth ratio):

| solve | rho_D | rho_S | rho_phys | **rho_wrms** | rho_wrms/rho_D |
|---|---|---|---|---|---|
| 1 (stage 2) | 0.2196 | 0.1958 | 0.3086 | **6.664** | 30x |
| 2 | 0.2735 | 0.3503 | 0.1768 | **3.432** | 13x |
| 3 (stage 3) | 0.9412 | 0.9455 | 1.001 | **2231** | **2370x** |

**Every one is above 1**: in the norm the gate actually uses, the residual EXCEEDS the
right-hand side — worse than not solving at all — while FGMRES reports 0.22-0.94 and, at
stage 2, "converged". At stage 3 the two objectives are 2370x apart.

So the inner/outer mismatch hypothesis SURVIVES, with different numbers and a different pair
than the retracted table claimed: not `D` against the raw physical norm (1.6x) but `D` against
`E^-1 S` (30x). The retracted measurement pointed the same direction for the wrong reason.

## 4. The block-share table named the wrong block

| block | s_krylov | s_physical | s_D |
|---|---|---|---|
| ru | 0.2015 | 0.0370 | 0.16667 |
| rv | 0.1991 | 2.7e-9 | 0.16667 |
| rw | 0.1998 | 8.9e-9 | 0.16667 |
| ph | 0.1998 | 0.0160 | 0.16667 |
| t | 0.1967 | 3.1e-5 | 0.16666 |
| **mu** | **0.0031** | **0.9470** | 0.16667 |

and the stage-WRMS share (`E^-1 S`) is more concentrated still:

| block | s_wrms |
|---|---|
| ru / rv / rw / t | ~5.7e-7 |
| ph | 1.5e-7 |
| **mu** | **0.999998** |

**This is the mechanism, stated in one line: FGMRES gives mu 1/6 of its objective; the stage
gate gives mu essentially all of it.**

- physical residual is **94.7% mu**, not "99.986% ph"
- in (S) coordinates it is near-uniform across five blocks — S equalises by construction
- `s_D = 1/6` exactly is an identity of `D_q = 1/||r~_q||`, not a finding
- `kappa(D)` = weight dynamic range = **8.1 / 11.1**, not 2.07e6

## 5. Stage-3 entry: 99.8% base state, 0.18% predictor

Holding `Y_3` fixed and varying only the predictor:

| predictor | raw L2 | wrms | ||K0|| | ru | next |
|---|---|---|---|---|---|
| zero (`= ||F_I(Y_3)||`) | 7.381e14 | 4.64e6 | 0 | 7.381e14 | ph 2.2e11 |
| prev_stage | 7.394e14 | 4.88e6 | 0.85 | 7.394e14 | ph 2.2e11 |
| production | 7.394e14 | 4.88e6 | 0.85 | 7.394e14 | ph 2.2e11 |
| picard | inf | inf | 7.4e14 | inf | — |

**ru is 99.99% of it.** `||Y_3|| = 1.251e10` vs `||Y_2|| = 2.100e6` — **5957x** — while
`||U_n|| ~ ||Y_2||`, so the growth is in the accepted stage-2 K.

Probe soundness: the `production` row reuses the `F` the Newton loop already computed while
`prev_stage` recomputes it from the same K0; they agree in every block to 6 digits, which is
what makes `compute_rhs` demonstrably a pure function of its argument and the rows comparable.

## 6. Where the mismatch actually is

`[STAGE METRIC TELEMETRY] stage=2 converged=1 gate_metric_used=wrms_growth
gate_metric_value=0.01302 rho_newton_scaled_exit=0.06674 rho_gate_current=10.8
metric_mismatch=10.73 rho_actual=5.189e4 rho_ratio=7.775e5 r0_unscaled_l2=8.742e8`

(`r0_unscaled_l2` matches the entry ledger's stage-2 raw L2 exactly.)

Three numbers describe one exit and disagree by up to **7.8e5**: the Newton scaled-RMS
criterion (0.067, PASS), the gate's `wrms_growth` (0.013, PASS), and the actual relative
residual (5.19e4). The code already computes and prints the discrepancy as `metric_mismatch`.

So an objective mismatch is real — at the **stage-acceptance** layer, not the Krylov layer the
retracted PR measured. Stage 2 and stage 3 also fail in different blocks: stage 2's residual is
97.3% mu (raw) / 94.7% (physical); stage 3's is 99.99% ru.

## 6a. That inference was WRONG, and the term-by-term decomposition says what is right

`SDIRK3_STAGE_EXIT stage=2 converged=1 newton_iters=3 final_res=0.06674 K_norm=4806
dt_gamma_K_norm=1.257e6` — the accepted stage-2 K contributes ~1.3e6, the same order as
`||U_n|| = 1.88e6`. **It cannot supply 1.25e10.** So the chain in §6 was wrong about the
mechanism, and the ARK stage base had to be decomposed term by term:

```
Y_3 = U_n                                                    1.884e6
    + h a^E_31 k_slow[1]   (a^E = 0.5276,  ||k|| = 1771)      5.606e5
    + h a^I_31 k_fast[1]   (a^I = 0.2576,  ||k|| = 0.85)      1.307e2
    + h a^E_32 k_slow[2]   (a^E = 0.07241, ||k|| = 2.879e8)   1.251e10   <- 99.98%
    + h a^I_32 k_fast[2]   (a^I = -0.09351,||k|| = 4806)      2.696e5
    = 1.251e10
```

(`600 * 0.07241 * 2.879e8 = 1.2508e10`, matching the assembled norm.)

**ONE term is 99.98% of Y_3: the EXPLICIT slow tendency from stage 2.** The two implicit terms
together are 2.2e-5 of it. `||k_slow||` went 1771 -> 2.879e8, a factor of **1.6e5**, while the
state it was evaluated at moved only 1.884e6 -> 2.100e6 (11%).

### What this settles

Stage 3 is handed a state 6640x `||U_n||` by the explicit channel BEFORE any implicit solve
runs. `F_I(Y_3)` is then 7.4e14 and **99.99% ru** — the u-momentum block, matching
Wall-2 (the explicit u-momentum cascade). So:

- ~~**No preconditioner, Krylov budget, or objective alignment can fix stage 3.**~~
  **RETRACTED 2026-08-21 (review R10, refuted by measurement).** The argument only bounded the
  DIRECT implicit contribution to `Y_3`. But `k_slow[2] = F_E(U_conv)` with
  `U_conv = U_stage_2 + h a_22 K_2`, so the explicit tendency is evaluated at a state the
  IMPLICIT solve moved to — by 60% of `||U_stage_2||`. Measured, `||F_E(U_conv)||` varies
  **7.5x** with the stage-2 Krylov budget alone. See §R10 below.
- The stage-acceptance mismatch in §6 is real but is NOT the stage-3 mechanism; stage 2's
  accepted K is small and roughly the right size.
- This is the same wall the split-explicit rebuild was chosen to address, now measured at the
  ARK tableau term rather than inferred from a per-component tendency comparison.

## 7. The stage-3 budget experiment was never single-variable

Two sites, same shape:

- budget resolution runs stage-2 knobs -> EW scaling -> stage-3 knobs, so an unset stage-3
  knob means "stage 2's value, scaled" (600 -> 450) and an explicit one means "stands unscaled"
  (600). Setting the knob to the value already in effect **changes the budget**.
- the aggressive early-stop gates read the **stage-2** knobs at `stage_id >= 2`, stage 3
  included, so setting only `stage3_*` leaves those gates off.

Both are now resolved by one pure function (`wrf_sdirk3_stage_krylov_policy.h`), default
`ShippedOrder` (unchanged), with `WRF_SDIRK3_STAGE_KNOB_FIRST=1` selecting single-variable
resolution. `Stage_Krylov_Policy_Contract`, 26 cases, pins both orderings and the confound.

## 8. Not reproducible on this head

The review's §10.2 (mu-Schur `reduction_applied=true` under the HEVI identity bypass) does not
reproduce: `last_mu_schur_reduction_used_ = !hevi_mu_identity` and `solve_path = 2` already
record the bypass, and `reduction_applied` no longer exists as a field. Two comments still
naming it have been corrected.


## 9. Aligning the objective is measured, and it is strictly WORSE

`WRF_SDIRK3_KRYLOV_WRMS_METRIC=1` installs `E^-1 S` as the Krylov left weight. The FIRST solve
of both arms runs on an identical `(A, b)` — nothing before it differs — so this is a
controlled comparison, not a run-level A/B.

| objective | kappa(L) | rho at restart 0 | stage-2 outcome |
|---|---|---|---|
| `D` block-constant | 8.1 | 0.2196 | CONVERGED, 3 Newton iters |
| `E^-1 S` stage-WRMS | **1.63e8** | **1.000** (zero progress) | FAILED, stalled at iter 1 |

`kappa(E^-1 S) = 1.63e8` (max 1.476e10, min 90.49) — the retracted `kappa(E) = 3.98e9` was
both the wrong operator and 24x off. Under this arm `s_D == s_wrms` exactly, confirming the
weighting installed is the one reported.

Mechanism: `E^-1 S` puts **0.999998** of the objective on mu, so the Krylov space is built
almost entirely from mu directions and the other five blocks are invisible to the minimiser.
All four ratios read 1.000 at restart 0 — GMRES made no progress at all.

**So the mismatch is real (2370x at stage 3) but removing it is not a fix.** It is a symptom of
mu being physically dominant while numerically tiny, not the cause of the stage-3 failure —
and §6a already attributes that failure to the explicit ARK term, which no Krylov objective
touches.

---

# R9 remainder — F1, I1, H1..H5

Same configuration unless stated. No independent review has been run.

## F1 — the nonlinear ledger separates the two failure modes, and stage 3 is mode (a)

`WRF_SDIRK3_NONLINEAR_LEDGER=1`. Predicted and actual come from the SAME trial; the prediction
is a read, not an operator call, via `R + a A dK = (1-a) R - a r_g` (b = -R, r_g = b - A dK).
E is frozen at STAGE ENTRY so the rows are comparable to each other — a per-iteration recapture
made the first version's rows silently incomparable, and the chained identity
`R_actual(iter 0) == R_before(iter 1)` is what shows the fix took.

| stage | iter | R_before | R_pred_linear | R_actual | actual/pred | gmres_rel_error |
|---|---|---|---|---|---|---|
| 2 | 0 | 4.891e5 | 3.260e6 | 1.187e6 | 0.364 | 0.196 |
| 2 | 1 | 1.187e6 | 7.329e5 | 3.497e5 | 0.477 | 0.350 |
| **3** | 0 | 4.883e6 | 7.657e9 | 7.617e9 | **0.995** | 0.948 |

Every row above carries `step_is_multiple_of_dK=1`, `step_over_dK=1`, `alpha_eff=1`,
`pred_valid=1`. That is not decoration: the identity holds only for the step `a dK`, and
production applies `alpha * dK_scaled`, which the trust region can shrink, the total-failure
path zeroes, and the fallback path replaces with a DIFFERENT vector (`dK_recovery`). Without
the guard the ledger could print a prediction for a step the run never took. The applied step
is now projected onto the dK **the solve returned**, snapshotted before the two post-solve
mutations: `apply_halo_zeroing(dK)` always runs, and the direct-U override replaces the whole
`ru` block with `-R_u` when enabled. Projecting onto the LIVE dK would have compared the step
against a vector `r_g` never saw and certified a mismatched pair — `pred_valid=1` would have
been trivially true. Parallel (full step, any trust shrink, or the zero step, where the
prediction correctly collapses to `R`) uses the effective `alpha*c`; non-parallel, or no
snapshot, reports `pred_valid=0` and no number.

Measured against the snapshot, `step_over_dK = 1` exactly, so in this configuration both
mutations are inert — the halo mask is the identity on the 1D packed vector and
`direct_u_solve_thresh = 0`. The rows stand as published, now on a check that could have
failed.

**Stage 3 fails in mode (a): the linear system is unsolved.** The linearization is FAITHFUL to
0.5% — the model predicts a 1568x residual growth and the nonlinear map delivers exactly that.
So the step is bad and reality agrees; nothing is wrong with the local linear model.

At stage 2 the nonlinearity HELPS (actual is 0.36-0.48 of predicted).

## I1 — the explicit cascade scales as dt^2.4, and needs dt ~ 15 s to stop dominating

`||k_slow[1]|| = 1770.82` at every dt (it is `F_slow(U_n)`, dt-independent — a free control).

| dt | `\|\|Y_2 - U_n\|\|` term | `\|\|k_slow[2]\|\| = \|\|F_slow(Y_2)\|\|` | `h a^E_32 k_slow[2]` |
|---|---|---|---|
| 600 | 9.262e5 | 2.879e8 | 1.251e10 |
| 300 | 4.631e5 | 1.091e8 | 2.371e9 |
| — | 0 | 1770.82 (exact) | — |

`k_slow` scales as `delta^1.400`, and `delta ∝ dt`, so the stage-3 explicit term scales as
**dt^2.400** (2.400 measured directly from the two term values, not assumed).

Fit: `||F_slow(U_n + delta)|| = 1.277 delta^1.400` reproduces 2.879e8 at dt=600.

**EXTRAPOLATED** (2 measured points + 1 exact anchor, over a 39x extrapolation — not measured):
the explicit term falls to `||U_n||` only at **dt ~ 15 s**, i.e. ~39x below the operational 600.
That is the order of an acoustic sub-step, which is what a split-explicit sub-cycle supplies.

**Why it could not be verified by running:** at dt=120 stage 2 STALLS (Newton exits at iter 3,
`final_res = 0.2146` vs tol 0.2) even with the iteration cap raised to 8, so stage 3 is never
reached and `k_slow[2]` does not exist. Stage 2 converges at dt=600 and dt=300 and stalls at
dt=120 — **non-monotonic in dt**, matching the recorded small-dt phenomenology.

## H1 — M does not model the operator, and the defect is SHAPE, not gain

`WRF_SDIRK3_APINV_DEFECT=1`, stage 2, first three Krylov directions:

| krylov_iter | eps_krylov | shape_k | cos_k | gain_p | cos_p |
|---|---|---|---|---|---|
| 0 | 8.085e4 | 7.556e4 (93%) | 0.356 | -187.5 | -0.0012 |
| 1 | 6.897e4 | 4.479e4 (65%) | 0.760 | 6424 | 0.230 |
| 2 | 5.511e4 | 4.396e4 (80%) | **-0.603** | -8623 | -0.252 |

`shape` is 65-93% of the whole defect, so **no scalar gain can fix it**, and `cos` swings from
+0.76 to -0.60 across three directions. Judged the way the record says to judge it
(`||A M^-1 v - v||`, never the gain), M is not close to modelling `A`.

Per-block, defect per unit of probe direction (`SDIRK3_APINV_V0_BLOCKS`, first direction):

| block | v | d | d/v |
|---|---|---|---|
| ru | 338.3 | 2.663e8 | 7.9e5 |
| rv | 338.7 | 4.299e10 | 1.3e8 |
| **rw** | 335.7 | **5.702e11** | **1.7e9** |
| ph | 175.2 | 2.745e6 | 1.6e4 |
| t | 338.3 | 1.369e7 | 4.0e4 |
| **mu** | 3.602e6 | 3.493e9 | **9.7e2** |

**This does not reproduce the recorded "M annihilates the ph/mu rows".** Under WRFParity with
the corrected Omega, mu has the SMALLEST per-unit defect and **rw the largest, by six orders**.
The recorded claim was measured on the legacy operator and its re-measurement was open; this is
it. Caveat: three structured directions at one stage, and structured directions can lie in a
null space — a claim about the operator needs more than this.

## H2 — the acoustic-gravity block is the worst-modelled row, and re-deriving it cannot reach dt=600

Reachability decided by measurement rather than by re-derivation, as the checklist required.
Two facts point opposite ways and both are measured:

- **For**: `rw` carries the largest per-unit preconditioner defect of any block, 6 orders above
  `mu`. The W-phi block IS where M is worst.
- **Against**: the W-phi 2x2 refinement was already implemented, measured WORSE and reverted
  (the over-damped W diagonal was load-bearing); and stage 3's state is 99.98% one explicit
  term, so any M acts on 0.002% of what stage 3 is handed.

**Verdict: worth doing for implicit-solve quality, cannot address dt=600.** Not re-derived here
— doing so would be building on the second fact being wrong, and it is measured.

## H3 — stage 2 converges at 102 Arnoldi, one fifth of the 510 on record

Single-variable (`WRF_SDIRK3_STAGE_KNOB_FIRST=1`), reporting Arnoldi USED, not requested:

| requested | Arnoldi USED | converged | rho_newton_scaled_exit |
|---|---|---|---|
| 60 | 51 | 0 | 0.2758 |
| **120** | **102** | **1** | 0.1800 |
| 200 | 170 | 1 | 0.1293 |
| 300 | 255 | 1 | 0.1035 |
| 600 | 510 | 1 | 0.0667 |

`newton_tol = 0.2`, so 102 is marginal (0.180) and 170 is comfortable. The recorded "~510
needed" was measured before the policy confound was removed and without reporting the used
count. Every arm here scales by exactly 0.85 (51/60, 102/120, 170/200, 255/300, 510/600),
which is what makes it single-variable.

## H4 — stage-3 ceiling, shown rather than asserted

With stage 2 CONVERGED (`converged=1`, `final_res=0.0667`) and stage 3 given its own 600-vector
budget, stage 3 reaches `gmres_rel_error = 0.948` and its step multiplies the residual by 1568x
— faithfully, per F1. Across this session stage 3 failed at every budget tried (51, 128, 255,
383, 510, 600) and at every objective tried (`D`, `E^-1 S`). It is not a budget or an objective.

## H5 — dt=600 full step: where it stops, with the term named

It stops at **stage 3 of the first ARK sweep**. The state stage 3 is handed is `1.251e10`,
6640x `||U_n||`, of which **99.98% is the single explicit term `h a^E_32 k_slow[2]`**. `F_I` of
that state is `7.4e14` and **99.99% `ru`**. No implicit-side lever reaches it. The measured
scaling says the term stops dominating only near dt ~ 15 s.

---

# R10 — the implicit solve IS in the causal path; the explicit response is strongly nonlinear

> (Section title corrected 2026-08-21: "non-differentiable" was not established — see the
> retraction below and doc/R11_checklist.md.)

No independent review has been run (`/code-review ultra` is user-only).

## R0. Retraction

Published and **not established**: *"No preconditioner, Krylov budget or objective alignment
can fix stage 3."*

The argument bounded only the DIRECT implicit contribution to `Y_3` (`h a^I_32 k_fast[2]`,
2.7e5 against 1.251e10). It never tested the INDIRECT path, and the indirect path is where the
term lives:

```
k_slow[i] = F_E(U_conv),   U_conv = U_stage_i + h a_ii K_i        (tile_unified_impl :9381-9394)
```

`F_E` is evaluated at the state the implicit solve moved to. At stage 2 that displacement is
`h gamma ||K_2|| = 1.257e6` against `||U_stage_2|| = 2.10e6` — **60%**, not a perturbation.

## A. Stage-2 solve accuracy changes the dominant term by 7.5x

Single-variable (`WRF_SDIRK3_STAGE_KNOB_FIRST=1`), Arnoldi USED, `||F_E||` read at the same
production site in every arm:

| stage-2 budget | Arnoldi | converged | final_res | `\|\|K_2\|\|` | **`\|\|F_E(U_conv)\|\|`** |
|---|---|---|---|---|---|
| 60 | 51 | 0 | 0.2758 | 2986 | — (sweep aborts before the stage-2 slow evaluation) |
| 120 | 102 | 1 | 0.1800 | 2346 | **3.83e7** |
| 300 | 255 | 1 | 0.1035 | 3569 | **2.074e8** |
| 600 | 510 | 1 | 0.0667 | 4806 | **2.879e8** |

**Verdict A2: the stage-2 solver is IN the causal path.** And monotonically in the direction
that makes it worse: converging stage 2 harder produces a LARGER explicit tendency for stage 3.

Not a power law — `||F_E||` rises 5.4x over one 1.52x increase in displacement (exponent 4.0)
and only 1.39x over the next 1.35x (exponent 1.1). Something switches between them, which is
what experiment D was run to find.

## B. The slow RHS is bit-exactly dt-invariant

`WRF_SDIRK3_RHS_DT_INVARIANCE=1` — same state, same halo, only `dt_stage_` moves:

| stage | dt_ref | dt_alt | norm_ratio | **vector_rel_diff** |
|---|---|---|---|---|
| 1 | 600 | 300 | 1 | **0** |
| 1 | 600 | 1200 | 1 | **0** |
| 2 | 600 | 300 | 1 | **0** |
| 2 | 600 | 1200 | 1 | **0** |

**Verdict B2: no hidden dt, no tendency/increment mixing, no dt double-application.** Of the
review's two candidate explanations for the ~h^2.4 growth, the unit-contract one is refuted
bit-exactly. (`grid_info_->dt = dt_stage_` at `:12039` is written but measurably not read by
any term the explicit tendency depends on.)

## D. The blow-up is a NON-DIFFERENTIABLE response, not a cascade

Walking the segment the implicit solve traversed, `U(lambda) = U_stage_2 + lambda (U_conv - U_stage_2)`,
evaluating the production slow RHS at each point:

| lambda | displacement | `\|\|F_E\|\|` | vs previous |
|---|---|---|---|
| 0 | 0 | 1.124e6 | — |
| **0.125** | 7.5% | **6.731e7** | **59.9x** |
| 0.25 | 15.0% | 1.248e8 | 1.85x |
| 0.375 | 22.4% | 1.736e8 | 1.39x |
| 0.5 | 29.9% | 2.138e8 | 1.23x |
| 0.625 | 37.4% | 2.452e8 | 1.15x |
| 0.75 | 44.9% | 2.680e8 | 1.09x |
| 0.875 | 52.4% | 2.822e8 | 1.05x |
| 1.0 | 59.8% | 2.879e8 | 1.02x |

**60x of the growth happens in the first 12.5% of the displacement**, then it saturates.
`F_E ~ C lambda^0.70` fits the tail (at lambda=0.125 the fit gives 6.68e7 against 6.73e7
measured).

~~**An exponent below 1 means the derivative is unbounded at lambda = 0.**~~

**RETRACTED 2026-08-21 (review R11).** The fit is on the wrong function. `||F_E(U_0)|| = 1.124e6`,
not 0, while `C lambda^0.70 -> 0` as `lambda -> 0`, so that form cannot represent the function
near the origin at all. An exponent read off `lambda in [0.125, 1]` is a finite-interval slope
of a NORM; it does not bound a derivative at 0, and a scalar norm cannot decide
differentiability of a vector field regardless — two fields can rotate while the norm moves
smoothly, and term cancellation can produce any exponent.

What the data supports: **strong nonlinear, precision-sensitive growth along `Y_1 -> Y_2`.**
The test that decides it is the Taylor remainder against the true JVP.

The state itself moves smoothly across the whole segment (`U_min` -1.06e4 -> -2.54e4,
`U_absmax` 1.99e4 -> 2.54e4, monotone) and there are **zero** non-finite values anywhere, so
this is not overflow and not a positivity trip visible in the state norms.

**Validity of the continuation:** at `lambda = 1` it reproduces the production `k_slow[2]`
exactly (2.879e8), which is what establishes it is walking the same code path rather than a
second implementation.

**And it starts before the implicit solve.** `||F_E(U_n)|| = 1770.81`, `||F_E(U_stage_2)|| =
1.124e6` — a **635x** jump across the pure ARK explicit assembly step, before any implicit
displacement, followed by a further 256x across the implicit displacement. Both stages of the
growth are the same sub-linear response to displacement.

## What this leaves

The dominant term is explicit, but the state it is evaluated at is set by the implicit solve,
and the response to displacement is strongly nonlinear near the base state. The next measurement
is therefore the review's P0-4: decompose `F_E` into its operator terms at
`lambda in {0, 0.125}` and identify which term carries the 60x — that is where a `sqrt`/`abs`/
floor/denominator would show itself.

## C. The ARK assembly is linear in h, arithmetically

`term_E = dt * |a^E_32| * ||k_slow[2]||`: `600 * 0.07241 * 2.879e8 = 1.2508e10` against the
assembled `1.251e10`. The assembly applies exactly one factor of `h` to a tendency that B shows
carries none, so the outer multiplier is not a second source of `h`.

## P0-3a. The primal split is exact

`WRF_SDIRK3_SPLIT_IDENTITY=1`, at the state the stage actually evaluates, through the same
entry point:

```
stage=1  F_full=1770.81  F_explicit=1770.81  F_implicit=0.845571
         defect_abs=3.78e-05  defect_rel=2.13e-08  defect_absmax=1.90e-06
```

`F_full = F_E + F_I` to **2.1e-8 relative** — float32 round-off over ~1e6 accumulated terms.
So no term is double-counted across the partition and none is dropped, and attributing the
blow-up to the explicit partition is meaningful.

## Still open

- `P0-3b/c/d` — JVP, VJP and transpose-consistency split identities (the primal one is closed;
  the derivative ones need the JVP/VJP plumbed per partition)
- `P0-4a/b/c` — operator-level decomposition of `F_E` and state admissibility at
  `lambda in {0, 0.125}`. **This is the next measurement**: it is where the 60x lives and where
  a `sqrt`/`abs`/floor/denominator would be identified.
- `P0-5` — `CFL` and `rho(h J_E)` via JVP power iteration, to separate a real explicit stability
  limit from a localized state defect

## P0-4a. The term is ADVECTION, and it carries the whole jump

`WRF_SDIRK3_UTERMS_TRACE=1` alongside the continuation. The trace snapshots the accumulating
`ru_tend` at named sites inside the PRODUCTION assembly — it is not a second implementation of
the RHS, which is what makes it admissible here.

| lambda | `\|\|F_E\|\|` | **adv `\|dR\|`** | adv `max\|dR\|` | coriolis `\|dR\|` |
|---|---|---|---|---|
| 0 | 1.124e6 | 2.069e8 | 4.731e6 | 4019 |
| **0.125** | 6.324e7 | **1.146e10** | 2.724e8 | 3871 |
| 0.25 | 1.171e8 | 2.029e10 | — | 3693 |
| 0.5 | 1.998e8 | 3.320e10 | — | 3394 |
| 1.0 | 2.671e8 | 5.025e10 | 2.332e9 | 2978 |

- **`adv` jumps 55.4x in the first 12.5%**, matching the 56x jump in `||F_E||`. It is the term.
- **Coriolis DECREASES** monotonically (4019 -> 2978) and is 6-7 orders smaller throughout.
- `entry` and `final` deltas are exactly 0, so on this path `ru_tend` is advection plus
  Coriolis and nothing else contributes.
- `max|dR|` grows 493x against `|dR|`'s 243x, so the growth CONCENTRATES as it grows — the
  later states are less smooth, not merely larger.

**Scope, stated rather than assumed:** this build's trace has four sites
(`entry`, `adv`, `coriolis`, `final`). There is no `adv_x` / `adv_y` / `adv_z` split here, so
the previously recorded attribution to VERTICAL advection is **not re-confirmed by this
measurement** — it stands on the earlier probe, not on this one. Separating the three
directions is the next step, and it is what would connect this to a specific operator.

## P0-4a (direction) + P0-4c. It is VERTICAL advection, and the theta violation is downstream

The three directions were already captured independently in the production assembly
(`ucap.terms.advection.{x,y,vertical}`); only their norms were missing from the stream.

| lambda | adv_x | adv_y | **adv_z** | z/horiz | mu_min | t_min |
|---|---|---|---|---|---|---|
| 0 | 6556 | 5.999e4 | **2.069e8** | **3444** | -10.89 | -41.16 |
| **0.125** | 4.548e7 | 5.557e4 | **1.146e10** | 252 | -745.9 | -85.11 |
| 0.25 | 9.626e7 | 5.407e4 | 2.029e10 | 210.7 | -1481 | -313 |
| 0.5 | 2.177e8 | 6.036e4 | 3.320e10 | 152.5 | -2951 | -768.7 |
| 1.0 | 5.592e8 | 1.001e5 | **5.024e10** | 89.8 | -5891 | -1680 |

- **`adv_z` is 3444x the horizontal sum at the base state**, and it carries the jump: 2.069e8
  -> 1.146e10 is **55.4x**, matching the 56x in `||F_E||` exactly. **This RE-CONFIRMS the
  recorded attribution to VERTICAL advection**, which the previous (lumped-`adv`) measurement
  could not separate and which I had therefore marked unconfirmed.
- `adv_y` is essentially FLAT across the whole continuation (5.4e4 - 1.0e5). The jet is
  x-directed, so the cross-stream advection barely participates.
- `adv_x` grows **85,000x** (6556 -> 5.592e8) — the fastest-growing term by far, though still
  90x below `adv_z` at lambda=1. `z/horiz` falls 3444 -> 90 for that reason, not because
  `adv_z` weakens.

### The theta violation is a CONSEQUENCE, not the trigger

`t` is the potential-temperature PERTURBATION (t0 = 300), so absolute theta is `300 + t_min`:

| lambda | t_min | absolute theta_min |
|---|---|---|
| 0 | -41.16 | 259 K — physical |
| 0.125 | -85.11 | 215 K — physical |
| 0.25 | -313 | **-13 K — unphysical** |
| 1.0 | -1680 | -1380 K |

~~Absolute theta crosses zero near `lambda ~ 0.24`.~~

**RETRACTED 2026-08-21 (R11 A1-A4).** That used `300 + t_min`, but `th_base_` **already carries
`t0`** — so the full field is `th_base_ + t` and I subtracted `t0` twice. Measured through the
production reconstruction, absolute theta stays **240 K -> 192.3 K** across the whole
continuation and never approaches zero. The state does NOT go unphysical.

**Not claimed:** `mu` is likewise a perturbation (`mu' `, full mass `mu' + mub`), and `mub` was
not measured here, so the `mu_min` / `mu_nonpos` columns are the perturbation's trend and
**not** a statement that column mass went non-positive. Establishing that needs `mub` on the
same record.

## P0-5. The spectral-radius probe is INVALID as built, and the eps sweep is what shows it

Power iteration with a finite-difference matvec at `U_n`, stage 1:

| eps_rel | rho(J_E) | vs previous | `\|\|dF\|\|/\|\|F\|\|` |
|---|---|---|---|
| 1e-2 | 648417 | — | 6.90e6 |
| 1e-3 | 60367 | /10.7 | 6.42e4 |
| 1e-4 | 6573 | /9.2 | 699.5 |
| 1e-5 | 630 | /10.4 | 6.70 |

**`rho` scales as `eps^+1`.** Neither hypothesis survives that:

- a smooth operator gives an FD quotient that is **eps-independent** once eps is below the
  curvature scale and above round-off — a plateau, which is absent;
- the measured `lambda^0.70` response predicts `rho ~ eps^-0.30`, i.e. **rising** 2x per decade
  of eps reduction — the opposite sign.

`rho ~ eps` means `||F(U + eps v) - F(U)|| ~ eps^2`: no linear term is being detected at all.

**The defect is in the instrument, not the operator.** Power iteration assumes a LINEAR map, and
`(F(U + eps v) - F(U))/eps` is linear in `v` only as `eps -> 0`. At finite `eps` against a
strongly nonlinear `F`, iterating it is not power iteration on `J_E`, and the iterate walks into
whatever direction maximises the quadratic response instead of an eigenvector.

**So `h rho = 3.8e6` and `outside=1` are NOT reported as findings.** No claim is made here about
whether the explicit partition sits outside the RK3 stability region — that question is still
open, and answering it needs a true JVP (forward-mode AD on the explicit RHS, which the implicit
side already has) plus a verified-linearity check before any Arnoldi or power iteration is run
on it.

What the sweep does establish, and it is not nothing: **a single `eps` would have published
3.8e6 with a confident verdict attached.** The slope is the discriminator; one point cannot
show a slope.

## P0-3b/c/d. The DERIVATIVE split identities hold, at machine precision

The primal split being exact does not imply the tangent or adjoint ones: a term double-counted
in one mode and cancelled in the other, or a piece of graph only one mode retains, is invisible
to the primal check and fatal to an adjoint model. Measured at the state the stage evaluates,
through the production entry point, with a random direction:

| identity | relative error |
|---|---|
| `J_full v == J_E v + J_I v` | **1.83e-10** |
| `J_full^T w == J_E^T w + J_I^T w` | **4.52e-08** |
| `<J v, w> == <v, J^T w>` | **1.60e-08** (-315466 vs -315466) |

`jvp_fd_fallback = 0` — the JVP ran on the **true forward-mode dual**, the same helper the
Newton matvec uses. Had it fallen back to a finite difference the 1.83e-10 would have measured
the fallback rather than the AD, which is why the flag is on the record beside the number.
`vjp_available = 1`: reverse mode ran on all three modes.

The VJP is taken on a **detached leaf**, so the probe's `backward()` cannot push spurious
contributions into the live state's `.grad()` or free buffers a real adjoint would still need.

**This closes the review's concern directly:** a pressure/acoustic term double-counted across
the partition, or dropped from one side, would let each operator pass its own test while the
assembled stage cascaded. It is measurably not happening — the slow/fast split is exact in the
primal, the tangent AND the adjoint.

## P0-5b. ANSWERED: the explicit partition is 605x outside the RK3 stability region

The FD probe was replaced with the **true forward-mode dual** — the exact directional
derivative, so the map is linear by construction. But "by construction" is the same kind of
claim that failed the first time, so linearity is **measured** before any iterate is trusted,
and the verdict is emitted only when that check passes:

| quantity | value |
|---|---|
| `jvp_fd_fallback` | **0** (true dual, not a finite difference) |
| `homogeneity_rel` — `J(a v) vs a J(v)` | 9.37e-08 |
| `additivity_rel` — `J(v1+v2) vs J(v1)+J(v2)` | 9.43e-08 |
| **`linear_verified`** | **1** |
| **`rho(J_E)`** | **1.746 s^-1** |
| `h` | 600 |
| **`h rho`** | **1047.7** |
| RK3 limits | 2.5 real / 1.73 imaginary |

**The true spectral radius is 1.746, not the 6364 the FD estimate gave — off by 3600x.**

`h rho = 1047.7` against an imaginary-axis limit of 1.73: **outside by 605x**. The implied
maximum stable step for the explicit partition is

    h_max = 1.73 / 1.746 = 0.99 s

and this is measured at `U_n` — the smooth balanced initial condition, before anything has gone
wrong. **The explicit partition is unstable at dt=600 from the very first stage.**

That is consistent with everything measured: `||F_E||` goes 1770.81 -> 1.124e6 (635x) across the
pure ARK explicit assembly step, which is what an operator 605x outside its stability region
does to a state in one step.

`1/rho = 0.57 s` is the acoustic timescale for this grid, not an advective one — worth noting,
not yet explained, and the natural next check is whether `rho` survives ablating `adv_z`.

### Two independent estimates of the required dt, and they disagree

| estimate | value | what it measures |
|---|---|---|
| `dt^2.4` extrapolation | ~15 s | when the term stops DOMINATING `Y_3` |
| `h rho <= 1.73` | ~1 s | when the explicit partition is STABLE |

They differ 15x because they answer different questions, and the stability one is the binding
constraint. The earlier extrapolation is superseded as a design target.

## Follow-up: the largest term is NOT the stiff term, and the stiff mode is NOT acoustic

Two A/B results that each correct something I wrote earlier.

### 1. Ablating `adv_z` leaves `rho` unchanged — **RETRACTED 2026-08-21, the ablation removed NOTHING**

| `WRF_SDIRK3_ABLATE_ADV_Z` | `rho(J_E)` | `h rho` |
|---|---|---|
| 0 | 1.74762 | 1048.6 |
| 1 | 1.74542 | 1047.3 |
| difference | **0.13%** | — |

`adv_z` dominates the **norm** of `F_E` (3444x the horizontal sum) and carries the entire 55.4x
jump along the continuation — and contributes **nothing** to the **spectrum**. Those are
different questions about the same term, and it answers them oppositely.

~~So the stability violation is not caused by the term that makes `F_E` large.~~

**RETRACTED.** The A/B was never valid. The spectrum probe runs at `stage_id = 1`, i.e. at
`U_conv_1 = U_stage_1 + h a_11 K_1` with `||K_1|| = 0.845` — essentially `U_n`, the balanced
initial state, where `w ~ 0` and hence `omega ~ 0`. Vertical advection contributes **nothing**
there, so ablating it removes nothing and `rho` being unchanged is a tautology.

The probe now emits `F_E_at_U0` on the same line as `rho`, from the same `U0`, and it is
**identical across every arm**:

| ablation | `F_E_at_U0` | `rho(J_E)` |
|---|---|---|
| baseline | 1770.81 | 1.74627 |
| `ADV_Z` | **1770.81** | 1.74501 |
| `T_COMPRESS` | **1770.81** | 1.74813 |
| `T_ADV_Z` | **1770.81** | 1.74649 |
| `T_DIFF_V` | **1770.81** | 1.74724 |

An arm whose `F_E` norm equals the baseline's removed nothing, and its `rho` says nothing. All
five term ablations are void, and the theta-term table below with them.

**What survives, because it does not depend on any ablation:** `rho = 1.746` with
`linear_verified = 1`, `h rho = 1048` (605x outside), and the eigenvector decomposition.

**The corrected measurement** is the same A/B at a state where the terms are ACTIVE — stage 2's
`U_conv`, where `adv_z = 5.02e10` — with `F_E_at_U0` proving each arm removed something.

### 2. The stiff mode is thermal-momentum, not acoustic

The converged power-iteration vector IS the dominant eigendirection. Decomposed per variable:

| block | share of the eigenvector |
|---|---|
| **t (theta)** | **0.458** |
| ru | 0.310 |
| rw | 0.119 |
| rv | 0.113 |
| **ph** | **0 exactly** |
| **mu** | **0 exactly** |

**`ph` and `mu` are exactly zero.** An acoustic mode cannot exist without geopotential and
column mass participating, so `1/rho = 0.57 s` matching the grid's acoustic time is a
**coincidence** — my reading of it as an acoustic signature is **retracted**. The stiff mode is
a theta perturbation coupled to all three momentum components.

That `ph` and `mu` are *identically* zero is also a clean confirmation that the explicit
partition genuinely does not touch the acoustic variables — consistent with the split
identities holding to machine precision.

### What this makes the next measurement

The stiff mode is 46% theta, and the u-terms trace only ever watched `ru`. The question is
which term in the **theta** explicit tendency carries a 0.57 s timescale — ablating candidates
one at a time against `rho` is the same A/B that just settled `adv_z`.

## The corrected A/B, at a state where the terms are ACTIVE

Re-run at stage 2's `U_conv` — where `F_E_at_U0` **differs between arms**, which is what makes
the comparison mean anything.

| arm | `F_E_at_U0` | removed | ru | rw | t | `rho` | `h rho` |
|---|---|---|---|---|---|---|---|
| baseline | 3.830e7 | — | **0.9979** | 0.0011 | 0.0010 | **205.8** | 1.235e5 |
| **ADV_Z** | 3.821e7 | 0.23% | **2.0e-9** | **0.5122** | **0.4878** | 222.5 | 1.335e5 |
| T_COMPRESS | 3.809e7 | 0.55% | 0.9979 | 0.0011 | 0.0010 | 210.5 | 1.263e5 |
| T_ADV_Z | 3.830e7 | 0.00% | 0.9990 | 0.0010 | 1.1e-11 | 220.9 | 1.326e5 |

(`ph` and `mu` are identically 0 in every arm, as at stage 1.)

### 1. The stiffness is STATE-DEPENDENT, and the implicit displacement causes it

| state | `rho` | `h rho` | max stable `h` |
|---|---|---|---|
| `U_n` (stage 1) | 1.746 | 1048 | 0.99 s |
| `U_conv` (stage 2) | **205.8** | **1.235e5** | **0.0084 s** |

**118x stiffer, and 71,000x outside the RK3 imaginary-axis limit.** The explicit partition at
the stage-2 state would need `dt ~ 8 milliseconds`.

This is the causal link the review asked for, now measured on a verified-linear operator: the
implicit solve moves the state to where the explicit operator is two orders of magnitude
stiffer. Both facts — that the implicit solve is causal, and that the instability is explicit —
are true at once.

### 2. `ru_adv_z` DOES carry the dominant mode

Ablating it takes `ru` from **0.9979 to 2.0e-9** in the eigenvector — the dominant mode is
removed outright. (The earlier stage-1 A/B could not see this: it removed nothing there.)

### 3. But removing it does NOT reduce the stiffness

`rho` goes 205.8 -> **222.5**, i.e. slightly UP. Killing the top mode exposes a second one of
comparable magnitude underneath — `rw 0.512 + t 0.488`, a vertical-velocity/theta pair.

**So the stiffness is not one term.** Sub-cycling or repartitioning vertical advection alone
would remove the leading eigenvector and leave `h rho` where it is. That is a stronger and more
useful statement than either of the two things I previously wrote about `adv_z`, and it is the
first version of the claim that rests on a validated A/B.

### 4. What the other arms say

`T_COMPRESS` removes 0.55% of `F_E` and changes neither the eigenvector nor `rho` — not the
carrier. `T_ADV_Z` (theta's vertical advection) removes **0.00%** of `F_E` at this state, so
that arm is uninformative here, exactly as all five were at stage 1.

## The Ritz spectrum, and what it does to every earlier stability claim

Eight ablation arms left `rho` in a 9% band while the eigenvector composition moved from
`(ru 0.998)` to `(rw 0.515, t 0.485)` to `(rw 1.000)`. Power iteration returns only the dominant
modulus, so an invariant `rho` under a changing eigenvector is exactly the case where ablation
cannot say more. Arnoldi (m=24) on the same verified-linear JVP measures the spectrum instead.

### Stage 1 — the spectrum is REAL, and it straddles the origin

```
ritz0=(+1.76012, 0)   ritz4=(+1.40081, 0)
ritz1=(+1.72355, 0)   ritz5=(+1.33615, 0)
ritz2=(+1.67453, 0)   ritz6=(-1.32198, 0)
ritz3=(+1.58499, 0)   ritz7=(-1.29036, 0)
near_top_count=4      top_real_frac=1.0
```

Every imaginary part is exactly 0. **So the imaginary-axis limit 1.73 was the wrong comparison,
and `max stable h = 1.73/rho = 0.99 s` is RETRACTED.**

### Stage 2 — complex pairs, 12 near the top, dominant real part POSITIVE

```
ritz0=(+118.6, +-189.5)|223.6|    ritz4=(+85.34, +-198.1)|215.7|
ritz2=(+102.5, +-193.8)|219.3|    ritz6=(-49.26, +-201.7)|207.7|
near_top_count=12                 top_real_frac=0.53
```

### Evaluating the actual RK3 stability function instead of comparing to a scalar limit

`R(z) = 1 + z + z^2/2 + z^3/6`, on the measured eigenvalues:

| stage | `lambda` | `\|R(600 lambda)\|` | **max stable `h`** |
|---|---|---|---|
| 1 | **+1.760** | 1.97e8 | **0** |
| 1 | -1.322 | 8.29e7 | 1.90 s |
| 2 | **+118.6 +- 189.5i** | 4.02e14 | **0** |
| 2 | -49.3 +- 201.7i | 3.22e14 | 0.0117 s |

**Eigenvalues with `Re(lambda) > 0` have NO stable timestep** — not a small one, zero. Near the
origin `R(z) ~ 1 + z`, so `|R| ~ 1 + h Re(lambda) > 1` for every `h > 0`.

### Sub-cycling cannot recover it, and the reason is not the scheme

`N` sub-steps of `h/N` for the dominant `lambda`:

| N | `log10\|amp\|` |
|---|---|
| 1 | +14.6 |
| 10 | +116 |
| 100 | +860 |
| 1000 | +5610 |
| N -> inf | **+3.09e4** = `h Re(lambda)/ln 10` |

Sub-cycling makes it **worse**, converging to the exact exponential `exp(h Re lambda)`. That is
the point: `Re(lambda) > 0` means the LINEARIZED operator genuinely grows, and no time
integrator makes a growing mode not grow. An A-stable implicit step is *bounded*
(`|1/(1-z)| = 7.5e-6`) but that is the scheme damping a mode the true linearized solution
amplifies.

### What this retracts and what it establishes

**Retracted:** "max stable h = 0.99 s" and "h_max ~ 8.4 ms" — both divided a scalar limit by
`rho`, presuming an imaginary spectrum. The spectrum is not imaginary and the dominant
eigenvalues are in the right half-plane.

**Established, numerically:** an `m=24` Arnoldi projection of the explicit Jacobian, from one
random start with a single Gram-Schmidt pass, produced Ritz values with large positive real
parts at both measured states.

**NOT established (review R11); the stronger wording is retracted.** No Ritz residuals, no `m`
study, no reorthogonalization and one seed, so convergence of those pairs is unknown. The start
vector is a raw packed random vector, so the operator sampled is `J_E` and not the
active-domain `P J_E P` — halo, boundary and staggered-endpoint degrees of freedom are in it.
And `rho(J_E)` does not govern the step: the one-step tangent is `D(Phi_h)`, and `J_E` and
`J_I` do not commute. A right-half-plane eigenvalue of the frozen linearized explicit operator
is also not by itself a numerical defect — the exact solution of `y' = lambda y` with
`Re lambda > 0` grows too.

**Caveat, stated not buried:** Ritz values from a 24-dimensional Krylov space approximate the
OUTER spectrum and their convergence is not established here; and `exp(h Re lambda)` describes
the frozen linearized operator, not the nonlinear system.

## Decomposing the right-half-plane eigenvalues by term — a NEGATIVE result, with its coverage stated

The quantity an ablation must move is not `rho`. `|lambda|` cannot separate a left-half-plane
eigenvalue (a `dt` problem) from a right-half-plane one (not a `dt` problem), which is why eight
arms left `rho` in a 9% band and said nothing. `n_rhp` and `max_re` are what decide it.

`F_E_at_U0` is printed at 14 digits. At the default stream precision it read `3.83e+07` for an
arm that removed 0.24% and for one that removed 1e-8 — a validity field that cannot resolve the
difference it is checking is not a check.

| arm | `F_E_at_U0` | relative change | fired | `n_rhp` | `max_re` | `min_re` |
|---|---|---|---|---|---|---|
| baseline | 38296542.293877 | — | — | 13/24 | 143.0 | -120.7 |
| **ADV_Z** (u vertical adv) | 38205633.485272 | **-2.37e-03** | yes | **15/24** | **167.6** | -111.4 |
| ADV_H (u horizontal adv) | 38296541.915904 | -9.87e-09 | yes, negligibly | 13/24 | 140.2 | -120.8 |
| **T_ADV_H** (theta horiz adv) | 38213309.137881 | **-2.17e-03** | yes | 13/24 | 140.6 | -120.8 |
| T_DIFF_V (theta vert diff) | 38296542.293877 | **+0.00e+00** | **no — bit-identical** | 13/24 | 144.0 | -120.7 |

### The answer

**No term tested produces the right-half-plane eigenvalues.** `n_rhp = 13 of 24` in every arm,
including the baseline, and at BOTH stages. Removing vertical u-advection makes it **worse** —
`n_rhp` 13 -> 15 and `max_re` 143 -> 168.

### The coverage limit, which bounds what that answer is worth

The arms that changed anything account for **0.45% of `||F_E||` combined**. So this is not a
decomposition of the operator; it is a test of four terms that together are half a percent of
it. "No single term among these carries the RHP part" is what the data supports. "The RHP part
is not localized in any term" is **not** — 99.5% of the operator was never varied.

Two gates also turned out not to be usable controls, and that is a property of the wiring, not
a result: `WRF_SDIRK3_ABLATE_RU_SLOW` is wired to `ru_slow` in the split-explicit driver, a path
the JVP of `computeUnifiedRHS(ExplicitOnly)` does not traverse, so it cannot ablate this
operator at all; `T_DIFF_V` left `F_E` **bit-identical**, so theta's vertical diffusion is
either not reached or identically zero at this state.

### What would actually decompose it

Ablation covers a term only if a gate exists for it, and gates exist for four. The operator's
own structure is the alternative: `J_E` is a sum of per-term Jacobians, so the RHP content
could be attributed by measuring the spectrum of each `J_term` directly — the same Arnoldi, run
on a JVP restricted to one term — rather than by subtracting terms from the total and hoping
the remainder shifts.

---

# R11 — differentiability, measured on the right function

## D1/D2. The Taylor remainder, and why FP32 cannot settle it

`r1(e) = ||F(U+e d) - F(U) - e J d|| / max(||dF||, e||J d||)` with `J d` from the verified
forward-mode dual (`jvp_fd_fallback = 0` on every row). Differentiable => `r1 = O(e)`.

`realized_frac = ||(U + e d) - U|| / (e ||d||)` is on every row because the state is FP32: it
says how much of the intended perturbation actually landed.

| stage / dir | eps | realized | dF/(e Jd) | r1 | r2 |
|---|---|---|---|---|---|
| 1 / random | 0.25 | **1.016** | 1.000 | **0.0149** | 0.00087 |
| 1 / random | 0.0625 | **0.952** | 1.000 | **0.0049** | 0.0025 |
| 1 / random | 0.0156 | 0.913 | 1.000 | 0.0127 | 0.0099 |
| 1 / random | 1.5e-5 | 0.668 | 0.938 | 0.347 | 0.347 |
| 2 / implicit_step | 0.25 | **1.008** | 1.043 | **0.249** | 0.170 |
| 2 / implicit_step | 0.0625 | **0.982** | 1.411 | **0.823** | 0.684 |
| 2 / implicit_step | 0.0156 | **1.013** | 2.182 | **0.876** | 1.632 |
| 2 / implicit_step | 9.8e-4 | 0.019 | 0.098 | 1.005 | 1.001 |
| 2 / implicit_step | 1.5e-5 | **0.001** | 2.1e-4 | 1.000 | 1.000 |
| 2 / random | 0.25 | 1.016 | 1.001 | 0.0242 | 0.015 |
| 2 / random | 1.5e-5 | 0.652 | 14.23 | 0.997 | 9.44 |

### `r1 -> 1` at small eps is an ARTEFACT, and the algebra says so

`r1 = ||dF - e Jd|| / max(||dF||, e||Jd||)`. If the perturbation does not land, `dF -> 0` and
the ratio tends to `||e Jd|| / ||e Jd|| = 1` **identically** — independent of any property of
the operator. Every row with `realized_frac << 1` reads `r1 = 1.00` for that reason and carries
no information. Without `realized_frac` those rows would have looked like a clean demonstration
of non-differentiability.

### The only rows that mean anything

`realized_frac` within 5% of 1 holds on `eps in [0.0156, 0.25]` — **1.2 decades**:

| dir | eps 0.25 -> smaller | r1 |
|---|---|---|
| stage 1 / random | 4x smaller | 0.0149 -> 0.0049, **falls 3.0x** — consistent with `O(eps)` |
| stage 2 / implicit_step | 16x smaller | 0.249 -> 0.876, **RISES 3.5x** — not `O(eps)` |

### Verdict (D2)

**INCONCLUSIVE at FP32 state precision, and the reason is measured rather than argued.** The
asymptotic regime that decides differentiability needs `eps -> 0`, and below `eps ~ 1e-2` in the
implicit-step direction FP32 has already destroyed the perturbation (`realized_frac` 1.013 ->
0.019 -> 0.001). One direction behaves like a differentiable field over the clean window and one
does not, over 1.2 decades — that is suggestive, not asymptotic.

**So "non-differentiable" is not re-established by the correct test either.** What is
established: the `lambda^0.70` argument was wrong (it fit a function that does not vanish at 0),
and settling the question requires an FP64 state path, not merely FP64 accumulation — the binding
constraint is the state's own precision, not the reduction's.

## S1–S5. The Arnoldi measurement, hardened — and the split does NOT create the RHP modes

The review's objections to the `m=24` single-pass run were all actionable, and all four are now
addressed. `WRF_SDIRK3_ARNOLDI_M` selects `m`; the Gram-Schmidt is doubled; per-pair Ritz
residuals and `||V^T V - I||` are reported; and the operator is selectable
(`WRF_SDIRK3_SPECTRUM_MODE=explicit|full`).

### S1–S3: convergence, at stage 2

| m | `\|lambda_0\|` | top-pair Ritz residual (relative) | `orth_loss` |
|---|---|---|---|
| 24 | 223.6 | 0.049 | 1.0e-07 |
| 48 | 224.1 | **0.0073** | 9.2e-08 |
| 96 | 224.4 | 0.0112 | 9.9e-08 |

`|lambda_0|` moves **0.4% over m = 24 -> 96**, the top-pair residual falls from 5% to ~1%, and
double Gram-Schmidt holds orthogonality at 1e-7 (FP32 vectors, FP64 Gram matrix). The dominant
pair is converged; the `m=24` single-pass run was not wrong, but it could not have shown that.

### S4: it is NOT a boundary mode

Edge-energy fraction of the converged eigenvector (first and last index along each dimension)
against the uniform-random baseline `1 - (1-2/40)(1-2/64)(1-2/80) = 0.1027` for this grid
(`e_we=41, e_sn=81, e_vert=65`):

| m | `edge_ru` | ratio to baseline |
|---|---|---|
| 24 | 0.1534 | 1.49 |
| 48 | 0.1240 | 1.21 |
| **96** | **0.1047** | **1.02** |

At convergence the mode's edge fraction is what a uniformly distributed vector would have. The
mild edge bias at `m=24` was truncation contamination and it disappears as `m` grows — so the
RHP mode is **not** an artefact of sampling halo / staggered-endpoint degrees of freedom, and
`P J_E P` versus `J_E` is not what is producing it.

### S5 (substance): `J_full` has LARGER right-half-plane eigenvalues than `J_E`

The objection was that `rho(J_E)` cannot decide the additive method because `J_E` and `J_I` do
not commute and the implicit coupling might cancel the mode. Measured, same Arnoldi, `m=48`:

| stage | operator | `n_rhp` | `max_re` | `min_re` | `ritz0` | rel. residual |
|---|---|---|---|---|---|---|
| 1 | `J_E` | 26/48 | 1.764 | -1.327 | (+1.764, 0) | 0.0029 |
| 1 | `J_full` | 27/48 | **73.09** | -1.341 | (+73.09, 0) | 0.205 (**not converged**) |
| 2 | `J_E` | 28/48 | 169.8 | -121.0 | (+119.4, -189.7) | 0.0082 |
| 2 | **`J_full`** | 25/48 | **470.8** | -450.7 | **(+470.8, 0)** | 0.0098 |

**The implicit part does not cancel the right-half-plane content — it increases it**, 169.8 ->
470.8 at stage 2 on a converged pair. Roughly half the Ritz values are right-half-plane for
both operators at both stages (25–28 of 48).

So **the RHP modes are not created by the explicit/implicit split.** They are present in the
full linearized RHS at these states, and more strongly. That removes "the partition manufactures
the instability" as an explanation and points the question back at the STATE — which the
stage-2 solver determines, the causal path already established.

**Still open, and not claimed:** this is `J_full`, the full RHS Jacobian, not `D(Phi_h)`, the
one-step tangent through the implicit solves. A growing mode of the linearized dynamics is not
by itself a scheme defect — an A-stable implicit step gives bounded amplification of a mode the
true linearized solution amplifies. The stage-1 `J_full` value (+73.09) has a 20% Ritz residual
and one isolated near-top value, so it is reported but not relied on.

## T1/T2. The dt authorities inside the explicit RHS, enumerated and reachability-checked

The fixed-state test varied `dt_stage_` and found the RHS bit-identical, and I read that as "no
hidden dt anywhere". The review's objection — that other dt authorities exist and the test does
not touch them — is correct, and enumerating them gives a better answer than the black-box test
could.

`computeUnifiedRHS` spans **10,627 lines**. Non-comment lines mentioning a timestep: **six.**

| line | text | status |
|---|---|---|
| 12711 | `grid_info_->dt = dt_stage_;` | a WRITE; no `->dt` read exists anywhere in the tree |
| 16221 | `float gamma_dt = 261.52f;` | **hardcoded `600 * gamma`** — drives a clamp |
| 16278 | `float gamma_dt = 261.52f;` | same constant, inside `debug_level >= 2` — print only |
| 17252 | a string literal in a debug message | not a use |
| 19811 | `float dt = dt_stage_;` | live read |
| 19932 | passes that `dt` to the W-damping call | the only consumer |

### The hardcoded constant is real, and it is NOT on the executed path

`mu_tend_threshold = 1e4 / gamma_dt` with `gamma_dt` frozen at `261.52 = 600 * 0.43586652`,
feeding `mu_tend = mu_tend.clamp(-38, +38)`. By the code's own stated intent
(`Delta mu' < 1e4 Pa per Newton step`) that threshold must scale as `1/dt` — 76.5 at dt=300,
382 at dt=60 — so at smaller `dt` it would clamp up to an order of magnitude harder than
intended. A clamp is also a genuine non-differentiability: zero derivative outside the bounds.

**But it is inside `if (!mu_tend_fortran_parity)`, and `mu_tend_fortran_parity` defaults to
true** ("Disable mean-subtract and clamp for mu_tend, default true for parity"). Confirmed at
runtime: `WRF_SDIRK3_MU_CLAMP_TRACE=1` emits **nothing** — the block is never entered.

So this is a **latent** defect, not an active one. It would bite the moment that flag is turned
off, and it is exactly the shape the review predicted: a dt authority that is frozen rather than
absent.

### Why the invariance test could not have found it

**A constant is trivially `dt`-invariant.** Varying `dt_stage_` and getting a bit-identical RHS
shows there is no dt-VARYING path; it cannot distinguish that from a dt path frozen at the wrong
value. The corrected statement of the earlier result is:

> the explicit slow RHS has no dt-VARYING dependence in this configuration

and the reachability enumeration above is what upgrades it to:

> the only live dt read (`:19811`) feeds W-damping, which is off under parity; the one frozen dt
> constant is on a disabled branch. So the RHS has no live dt dependence here — established by
> enumeration and reachability, not by a black-box invariance check.

## V1. `block_energy_shares` now fails closed in the PRODUCTION path

The validators existed and no caller invoked them, so a malformed layout still produced shares
summing to 1. The helper now returns `BlockShares { shares, valid, reason }` and rejects:
non-partition layout (overlap / gap / uncovered tail / empty block), weight length mismatch,
device/dtype mismatch, non-positive or non-finite weights, and zero weighted energy. The Newton
solver's objective-share emit prints `-1` plus a `SDIRK3_OBJECTIVE_SHARE_INVALID` line carrying
each reason, instead of plausible numbers.

Returning a bare vector was what made the gap possible: the function had no way to say "these
numbers mean nothing" other than by returning numbers. Contract: 51 cases (from 46), including
that an overlapping layout is now REJECTED rather than renormalised.

## A1–A4. Physical admissibility, through the PRODUCTION reconstruction

The earlier rows reported `mu_min`, `t_min`, `ph_min` — all **perturbations** — and were read as
an admissibility check. Full fields need the base state, and pressure and density need the same
EOS the model uses (`acoustic::diag_p_al`), not an inline `rd*theta/p` the code's own comments
record as 87% off at the top level.

| lambda | `mu_full_min` | `mu<=0` | `th_full_min` | `dz_min` | `dz<=0` | `p_full_min` | `rho_min` |
|---|---|---|---|---|---|---|---|
| 0 | 8.907e4 | **0** | **240.0 K** | 215.1 m | **0** | 1.024e4 Pa | 7.69 |
| 0.25 | 8.881e4 | 0 | 244.3 K | 215.2 m | 0 | 7538 Pa | 3.18 |
| 0.5 | 8.855e4 | 0 | 247.7 K | 215.3 m | 0 | 5131 Pa | 1.70 |
| 0.75 | 8.829e4 | 0 | 244.5 K | 215.3 m | 0 | 2726 Pa | 1.09 |
| 1 | 8.803e4 | **0** | **192.3 K** | 205.7 m | **0** | 679.5 Pa | 0.804 |

**The state stays physical across the entire continuation.** Column mass never goes non-positive,
no layer inverts, pressure stays positive down to the model top, and absolute theta falls from
240 K to 192.3 K without approaching zero.

### This retracts the "state goes unphysical" finding

I reported absolute theta crossing zero near `lambda ~ 0.24` from `300 + t_min`. **`th_base_`
already carries `t0`**, so the full field is `th_base_ + t` and `t0` was subtracted twice. The
correct minimum is **192.3 K, not -13 K**. Nothing about the continuation is inadmissible, and
the "violation follows the blow-up" framing goes with it — there is no violation.

### One anomaly, flagged rather than explained

`rho_max` reaches 4.2e6 kg/m^3 while `rho_min` is a plausible 0.80-7.7. So `al` has near-zero
entries in a few cells. The code's own comment at the `diag_p_al` call site records that this
routine builds `al` **geometrically** and is 35% off on the gradient versus WRF's
`calc_p_rho_phi` form, because `p' = p0*(R(t0+th)/(p0*alpha))^(cp/cv) - pb` cancels two ~1e5 Pa
terms down to ~11 Pa. Whether these cells are that known artefact or a real near-singularity is
**not** established here.

## F2. No two large terms are cancelling in the u-advection decomposition

Norms alone cannot see cancellation, so the cosine of each direction with the total advective
tendency is now on the record (stage-2 state):

| term | norm | cosine with the total |
|---|---|---|
| `adv_x` | 4.918e7 | **0.0009** — orthogonal |
| `adv_y` | 6.584e4 | -0.020 |
| **`adv_z`** | **1.725e10** | **1.000** |
| total | 1.725e10 | — |
| `sum_of_norms / total` | **1.003** | |

The sum of the parts' norms is 1.003x the total, so **nothing cancels**: `adv_z` IS the total,
`adv_x` is orthogonal to it (4.9e7 that contributes nothing to the magnitude), and `adv_y` is
negligible. A norm-ranked decomposition is not misleading here — which had to be measured,
because it is exactly where one would be.

## D3. The large Taylor remainder is DIRECTIONAL, and not a boundary effect

Clean rows only (`realized_frac > 0.94`), stage 2:

| direction | `r1` at eps=0.25 | at 0.0625 | at 0.0156 |
|---|---|---|---|
| `implicit_step` | 0.893 | 0.961 | 0.942 |
| **`random`** | **0.116** | **0.132** | — |
| `edge_only` | 0.925 | 0.925 | — |
| `interior_only` | 0.979 | 0.981 | 0.996 |

**Restricting to the interior does not improve the remainder at all** (0.979 vs 0.893 for the
whole step), and neither does restricting to the edges. So the large remainder is **not** a
boundary / halo / staggered-endpoint effect — the review's `P J_E P` concern does not explain
it, consistent with the eigenvector's edge fraction sitting at the uniform baseline.

Along a **random** direction the remainder is **8x smaller** (0.12 vs 0.89-0.98). That
dissociation reproduces across runs: at `restart=600` it was 0.024 vs 0.249 (10x), here at
`restart=120` it is 0.116 vs 0.893 (7.7x). The absolute values differ between runs because the
stage-2 solution differs; the ratio does not.

**So the roughness is a property of the implicit-step DIRECTION**, not of the operator
everywhere and not of the boundary. It is still measured over 1.2 decades of `eps`, so it
remains short of a differentiability verdict — see the FP32 limit above.

## V2. The runtime behaviour manifest — the sweep WAS single-variable, now demonstrated

`policy_fields_that_differ()` compares the pure policy, and the pure policy is not everything
that changes what a run does: hopeless-mode caps, early-stop streaks, the warm-start latch, the
trust radius and the preconditioner mode are all stateful and all alter the solve. Two arms are
separate processes, so there is no in-process baseline to fail closed against; what makes a
sweep verifiable is that every behaviour-bearing value is on the record at the same point in
both arms.

`WRF_SDIRK3_POLICY_MANIFEST=1` emits 26 such fields at stage 2, iteration 0. Diffing the two
arms of the stage-2 budget sweep (`restart` 120 vs 300):

```
DIFFERS  restart   102 -> 255
identical fields: 25 of 26
```

**Only the knob differs.** The earlier "single-variable" claim for that sweep is no longer an
assumption about the resolver — it is a measured property of every behaviour-bearing runtime
field, including the stateful ones the pure policy does not contain.

## S6. The probes are TILE-LOCAL, and now say so instead of producing comparable-looking numbers

The JVP, the Arnoldi basis and the continuation all run on this rank's packed state. Under
`np > 1` each rank holds a subdomain with an exchanged halo, so a spectrum or a Taylor remainder
computed there is a property of the LOCAL operator — not the global one, and not comparable
rank-to-rank or against a single-rank run. Emitting them anyway would produce numbers that look
like an np-equivalence check and are not one.

All six RHS-re-entering probes now fail closed on any topology other than one rank, with
`SDIRK3_PROBE_SKIPPED probe=<name> reason="tile-local operator under np>1..."`. Same judgment as
the existing stage-operand diagnostic; these are opt-in diagnostics, so they skip with a stated
reason rather than aborting. Verified at `np=1`: the probes still fire.

**An np=1,2,4 equivalence check for these quantities therefore needs a formulation that is
global by construction, not a rerun of these probes.** That is not done.

## F1. Term-observer coverage, stated exactly

| variable | term observer | status |
|---|---|---|
| `ru` | 9 capture sites; 4 fire on the ExplicitOnly path (Entry, Advection, Coriolis, Final) | advection further splits x/y/z with cosines |
| `rv`, `rw`, `ph`, `t`, `mu` | **none** | **not covered** |

For `ru` the coverage is closed on the executed path: `post_capture_tail status=PASS |dR|=0`
confirms nothing modifies `ru_tend` after the last capture, so the captured sites account for
the whole tendency. The advection closure check is fail-closed by design and correctly reports
`INVALID reason=missing_adv_x` at an evaluation where the advection tensors were never populated
(the all-zero first call) rather than inventing a pass.

The stage-2 dominant eigenvector is 99.8% `ru`, so the one covered variable is the one that
carries the stiffness — but that is a reason to have prioritised it, **not** a substitute for
the other five. `F_E = F_adv + F_pressure/metric + F_buoyancy + F_coriolis + F_diffusion +
F_damping + F_boundary + F_source` across all six variables is **not** measured, and the review's
P0-5 stays open on that basis.
