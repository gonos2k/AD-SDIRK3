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

- **No preconditioner, Krylov budget, or objective alignment can fix stage 3.** They act on the
  implicit solve, which contributes 0.002% of the state stage 3 starts from.
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
