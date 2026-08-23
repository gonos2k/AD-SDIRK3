# R13.17 — checklist for the R13.9–R13.16 external review

Baseline: `main` merge `d41c80e` (PR #177 merged). Branch `agent/r13-17-metric-termination`.
Every status below is verified in the current tree, not assumed.

**The review's central finding, confirmed by reading the code:**

| | site | quantity |
|---|---|---|
| FGMRES **stops** on | `:2358–2363` (`error_for_stop < tol`) | ρ_D = ‖D⁻¹r‖ / ‖D⁻¹b‖ |
| FGMRES **reports success** on | `:2416–2418` (`rel_error_final < tol`) | ρ_S = ‖r‖ / ‖b‖ |

Both are *internally* consistent — `b_inner` is D-scaled at `:1254`, so ρ_D's denominator matches its
numerator, and there is no mixed-denominator artefact. The defect is that **one metric ends the
search and a different one decides whether the search succeeded**, collapsed into a single
`success` / `rel_error`. `InternalConvergenceStop` is exactly the state where they disagree.

---

## P0

- [x] **P0-1 — two "convergences", one success flag.** A solve can have ρ_D < η and ρ_S ≥ η: it
      stopped because its own objective was met, and is reported as a failed linear solve. That
      feeds total-failure recovery, zero-step handling and trust rejection, and it cannot separate
      "the forcing tolerance was loose" from "the D-objective is not aligned with the Newton merit"
      — the objective-mismatch this project has measured before. **Fix:** a per-solve
      `KrylovSolveReceipt` carrying ρ_D and ρ_S (initial and final), the tolerance actually applied,
      its **source**, the stopping metric, and per-metric reached flags. Production control flow
      unchanged (review's Option B): the D-stop stays a legitimate linear-solve termination and the
      Newton layer judges merit separately — but both are now *stated*.

- [x] **P0-2 — `KrylovForcingTermLimited` misses `InternalConvergenceStop`.** `met_tolerance` is
      `reason == ToleranceReached` only, so the one termination that *is* "met its own tolerance"
      is excluded, and lands back in `KrylovStagnated`: the exact misclassification the category was
      added to prevent. Folding it in naively is also wrong — the two reasons mean different things.
      **Fix:** split into `KrylovObjectiveMismatch` (D reached, S not) / `KrylovForcingTermLimited`
      (S reached but progress poor) / `KrylovBudgetLimited` (neither, budget exhausted) /
      `KrylovStagnated`. Plus: record the **tolerance source** (the layer string hardcodes
      Eisenstat–Walker while the value can come from a stage override or a ramp), and handle
      **near-worst ties** — worst is updated on strict `>`, so with η saturated at its cap two
      solves at the same ratio let whichever came first decide the category.

- [x] **P0-3 — "the failure moved outward" is a threshold statement, not an event.** The category
      changed because 0.8622 fell below the *chosen* 0.90 boundary; at 0.85 the same trajectory
      stays `krylov_stagnated`. The Newton loop's real exit reason is reconstructed from aggregates
      and never recorded. **Fix:** a typed `NewtonTerminationReason` written at the actual `break`
      sites, and the doc claim restated as what it is.

- [x] **P0-4 — provenance fails closed on mismatch, fail-OPEN on a missing stamp.** The predicate
      requires both stamps ≥ 0, so `signals_from_stage = -1` (the documented "not stamped" sentinel)
      skips the gate and the rest of the classifier runs. **Fix:** missing stamp is its own refusal.

## P1

- [x] **P1-1 — Taylor τ is a global packed L2.** One dominant block can hide a large relative
      Jacobian defect in a small one (`rw`, `ph`, `mu` are exactly the blocks at issue). **Fix:**
      per-block τ_b with a per-block floor, and τ_max as the reported verdict.
- [x] **P1-2 — no step-realization or roundoff-window gate.** At float32 a small step can fail to
      change the stored state at all, and `G(K+s) − G(K)` can be cancellation-dominated, while τ and
      the ratio still print plausible values. **Fix:** `realized_K_fraction`, `realized_U_fraction`,
      signal-to-roundoff, gated in the verdict.
- [x] **P1-3 — the A/B fingerprint calls the production preconditioner.** `M_fp_before = M_inv(v)`
      can trip the amplification guard's `fallback_locked` latch, which no counter restore can undo
      — and then before/after agree *because both are the identity*. **Fix:** fingerprint a pristine
      copy; read production state read-only.
- [x] **P1-4 — `failure_vs_r0` is a function-local static**, the same defect fixed for the
      no-progress threshold one commit earlier. **Fix:** object state, set at construction.

## Contract tests required by the review

`InternalConvergenceStop_Is_D_Tolerance_Reached` · `D_Converged_S_NotConverged_Is_ObjectiveMismatch`
· `Forcing_Category_Requires_Tolerance_Source` · `Forcing_Category_Handles_NearWorst_Ties` ·
`Worst_Solve_Records_Total_Arnoldi_Budget` · `Missing_Signals_From_Stage_Fails_Closed` ·
`Missing_Classifying_Stage_Fails_Closed` · `Newton_Exit_Reason_Is_Emitted` ·
`Taylor_Defect_Blockwise_Contract` · `Taylor_Defect_Step_Realization_Contract` ·
`Fingerprint_Does_Not_Call_Production_Preconditioner` ·
`FailureVsR0_Is_Solver_Config_Not_Function_Static`

The decisive synthetic case: **ρ_D = 0.85 < η = 0.9 with ρ_S = 0.99 > η** must classify as
`KrylovObjectiveMismatch` — neither `KrylovStagnated` nor `KrylovForcingTermLimited`.

## Accepted without change

The review's scope limits on the Taylor result (three accepted-step directions, global packed L2 —
*not* a global Jacobian certification), on the budget experiment (necessary, not sufficient), and
the standing NO-GOs (`dt=600` forward, one-step tangent/adjoint, exact 4D-Var, MPI production) are
correct as stated and are not contested here.


---

## Resolution — measured

All eight items implemented; `ctest 62/62`. Verified live on `em_b_wave` at dt=600.

### P0-1 / P0-2 — the two convergences, and the four reasons a solve shows no progress

`GMRESResult` now carries **both** readings (`rho_D_initial/final`, `rho_S_initial/final`), the
tolerance actually applied and per-metric reached flags. Production control flow is unchanged (the
review's Option B): the D-stop stays a legitimate termination, the Newton layer still judges merit
in its own coordinate — what changes is that the seam is *stated* rather than collapsed into one
`success`.

`met_tolerance` now includes `InternalConvergenceStop`, which is the one termination meaning "met
its own tolerance" and was excluded — sending exactly that state back to `KrylovStagnated`, the
misclassification the category was added to prevent. The classifier routes four ways:

| evidence | category | layer |
|---|---|---|
| D reached, S not | `KrylovObjectiveMismatch` | `krylov_objective_D_vs_newton_merit` |
| S reached, progress poor | `KrylovForcingTermLimited` | `eisenstat_walker_forcing_or_inner_budget` |
| neither, budget gone | `KrylovBudgetLimited` | `inner_krylov_budget` |
| neither, not cut off | `KrylovStagnated` | operator/preconditioner |

with the review's decisive synthetic case (ρ_D = 0.85 < η = 0.9, ρ_S = 0.99 > η) pinned to
`KrylovObjectiveMismatch`. The **tolerance source** is recorded (`worst_krylov_tol_source`) so a
category cannot name Eisenstat–Walker while the value came from a stage override, and a **tie at
the worst ratio** refuses the forcing/mismatch readings rather than letting arrival order pick the
layer.

### P0-3 — the Newton exit is recorded, not reconstructed

`NewtonTerminationReason` is written at the site that ends the loop. **The first verification run
immediately caught a defect in this fix**: the fallback said "nothing claimed it, so it was the
budget" and printed `newton_exit=budget_exhausted` for a loop that used **four of twelve**
iterations. Absence of a recorded reason is not evidence of one — the rule applied to every other
field all campaign, and applying it to the field added to *replace* a reconstruction would have
been a reconstruction with a confident name. The fallback now also requires the loop to have
reached its bound.

### P0-4 — provenance fails closed on absence

`StageSignalMissing` (layer `unstamped_signals_no_verdict`). The gate required both stamps ≥ 0, so
the documented "-1 = not stamped" sentinel skipped it entirely. Five fixtures were relying on that
fail-open and now stamp explicitly.

### P1-1 — blockwise τ, and the answer runs the *other* way

```
tau=0.1192 (packed)   tau_block_max=0.2008
tau_ru=0.2008   tau_ph=0.000476 (share 0.140)   tau_rw=0.0447 (share 0.000216)   tau_mu=0.0055
```

**The packed L2 was not hiding a defect in a small block.** The worst per-block τ is in `ru` — the
*dominant* block — and `ph`, `rw`, `mu` are all **better** than the packed reading (`ph` by 250×).
The concern was the right one to raise and the measurement answers it in the direction that
strengthens the Taylor result. The honest number to quote as the verdict is now **τ_max = 0.2008**,
not 0.119 — still ≪ 1, and 1.7× the packed value.

### P1-2 — the step was realized and the signal is far above roundoff

`realized_step_fraction = 1`, `signal_to_roundoff = 2.5e6`. Both gates (`StepNotRealized`,
`RoundoffLimited`) are now preconditions of the verdict, and this measurement clears them by four
orders of magnitude — so the earlier τ values were not cancellation artefacts.

### P1-3 / P1-4

The A/B fingerprint uses a **pristine copy**; calling production's `M_inv` could trip the
amplification guard's `fallback_locked`, and then "before" and "after" agree *because both are the
identity* — a latch no counter restore can undo. And `failure_vs_r0` moved to object state, the
same fix its sibling threshold received one commit earlier.

### Accepted without contest

The review's scope limits — the Taylor result covers three accepted-step directions (not a global
Jacobian certification), the budget experiment is necessary but not sufficient, and "the failure
moved outward" is a threshold statement — are correct, and the doc now says so in those words.

### What the typed exit reason found on its first honest run

```
category=krylov_stagnated  newton_iters=4  newton_budget=12  newton_exit=not_recorded
```

The dt=600 stage-2 Newton loop **exits through a path that records no reason** — it uses four of
twelve allowed iterations, does not converge, does not reach its bound, and none of the three sites
that now stamp a reason (converged / residual-stall / bound-reached) is the one that ended it.

That is the field doing its job. Before it, this run was classified from aggregates and the
classifier confidently reported a layer; the review's point was that nothing recorded *why* the
loop stopped. The answer turns out to be that the exit is **through an untyped path**, which no
amount of aggregate precedence could have revealed.

Two consequences, both stated rather than resolved here:

- The campaign's reading of the 12×-budget run — *"the failure moved outward to the Newton
  iteration"* — still rests on the 0.90 threshold **and** on an exit whose reason is unknown. It
  should be read as: at the chosen boundary the Krylov clause no longer fires, and the loop stops
  early for a reason not yet typed.
- Finding and typing that path is the concrete next step, and it is a **measurement**, not a design
  question: instrument the remaining exits (the total-failure zero-step path, the stage-abort
  break, the trust-region exhaustion) until `not_recorded` stops appearing at dt=600.

---

## The typed exit RETRACTS "the failure moved outward to Newton"

Instrumenting the three real Newton-loop exits (brace-depth tracking found them; the site I first
guessed at was an inner-scope `break` that does not end the loop) makes `not_recorded` disappear at
dt=600 — and the answer contradicts a standing campaign claim.

| | default budget | 12× budget |
|---|---|---|
| `worst_krylov_rel_vs_r0` | 0.9941 | 0.8622 |
| category | `krylov_stagnated` | **`newton_stagnated`** → layer `residual_floor_or_split` |
| **`newton_exit`** | `linear_solve_failure` | **`linear_solve_failure`** |
| solver's own message | `[Newton] GMRES total failure + zero update` | same |

**Both runs exit for the same reason.** Only the ratio moved, and it happened to cross the chosen
0.90 boundary — so the 12× run was classified `newton_stagnated`, whose layer is
`residual_floor_or_split`, i.e. **the split-explicit rebuild**, for a run whose loop broke at the
**zero-update guard**.

The campaign read that flip as *"the failure moved outward to the Newton iteration"*. **It did not
move.** The external review flagged this claim as HOLD on the grounds that the category change was a
threshold statement and the real exit was never recorded; the typed exit now shows the stronger
result — the exit is identical in both runs, and it is the linear solve.

**CORRECTED BY ROUND 7 (P0-B).** The paragraph above originally said the loop stopped "because the
linear solve produced nothing at all", and had the classifier honour that **whatever the progress
ratio reads**. Both were wrong, and the contradiction was on this same page: the 12× run's worst
solve removed **13.8 %** of its own residual — it did not produce nothing.

What the exit is actually gated by: `‖dK‖ < 1e-15` (the accepted update is numerically **zero**)
**and** `gmres_total_failure`, which under the default configuration is `raw > 1 || rel ≥ 0.999` on
**‖r‖/‖b‖**. That is the coordinate R13.12–R13.16 spent four rounds moving this classifier *off*,
and the `||` override reinstated it as a verdict that fires regardless of the r₀ evidence.

So the event is renamed for what it measures — `ZeroUpdateAfterTotalFailure`, layer
`zero_update_bnorm_rule_or_step_recovery`, naming **both** candidate causes — and it no longer
overrides the r₀ clause.

**What survives:** both runs break at the *same* code site, which is a real and useful measurement.
**What does not:** the inference that the failure did not move outward. The gate is the retracted
coordinate, so this evidence neither establishes nor refutes that claim. The standing claim is
therefore back to **HOLD**, which is where the external review had it — not retracted, and not
confirmed. What survives from the budget experiment is unchanged and still holds — a 12×
budget improves the inner solve 23× (0.59% → 13.8% of its own residual) and does not complete the
step. What does not survive is the inference that the *failure* relocated.

*Method note.* This is the fourth time in this campaign that a claim about **where** a failure lives
was produced by a threshold or a precedence over aggregates rather than by an event, and the fix was
the same each time: record what happened at the site where it happened. The first honest run of the
new field found a defect in the field itself (a fallback claiming budget exhaustion for a loop that
used 4 of 12), and the second found this.

---

## Self-review of R13.17 — three more defects, all in this batch's own work

Asked whether the typed-exit work had introduced further mistakes. It had.

1. **The GMRES exception exit was untyped, and `NewtonTerminationReason::Exception` had ZERO
   producers.** An enum value nothing writes is the same defect class as a field nothing reads —
   introduced by the very commit that added the enum to *end* reconstruction. Typed at the `catch`.

2. **The linear-failure test was inside `if (measured(worst))`.** So on a stage where no solve
   measured r₀ — the path with *no* Krylov evidence, and therefore the one most likely to fall
   through to a Newton category and name `residual_floor_or_split` — the misrouting the fix was
   written to close was still alive. Moved above the measured() branch.

3. **Mapping the exception to `KrylovDiverged` overclaimed.** Divergence is a *measured* behaviour
   (the residual grew); an exception establishes no such thing. New `KrylovSolveThrew`, layer
   `linear_solve_exception` — the same "don't name a mechanism you didn't measure" rule this
   campaign applies everywhere else, which I broke while applying it.

**And the retracted claim was chased down where it had spread**: `doc/R13_8_checklist.md` point 3
is struck through with the measurement that refutes it, and the **merged** PR #177 body carries a
retraction note at the top, since that body is a durable record a future reader will find.

73 checks, ctest 62/62.

---

## §13 — the raw physical numerical range, MEASURED

The review's remaining open item: every indefiniteness witness this campaign has is for `S⁻¹AS` or
`D⁻¹S⁻¹AS`, and the numerical range is **not** similarity-invariant — so whether `W(A)` straddles
the origin in the *physical* inner product was unestablished, and both campaign pivots rested on a
coordinate statement.

No new operator is needed. With `ṽ` a scaled direction, the physical direction is `v = S ṽ` and
`A v = S · gmres_op(ṽ)`, so `q_phys = ⟨S ṽ, S·gmres_op(ṽ)⟩ / ‖S ṽ‖²` is the Rayleigh quotient of the
raw `A` on a genuine physical direction, from matvecs already taken.

**Random directions, 24 samples, dt=600 stage 2:**

```
A (S coords):   q_min=+5615.98  q_max=+5865.77  neg=0/24
A (physical) :  q_min=-8043.13  q_max=-5232.09  neg=24/24
```

The sign of the quadratic form of **the same operator** flips with the metric — which is the
review's point, demonstrated rather than argued.

**But the random arm alone is a one-sided sample and would have been read wrongly.** All-negative
suggests *negative definite* — as good as positive definite for GMRES, which would have made the
"intrinsically indefinite" conclusion a coordinate artefact. Random directions in high dimensions
concentrate. The spanning check — one direction per variable block — settles it:

```
qphys_ru=+7198.81  qphys_rv=+7188.39  qphys_rw=+14285.89
qphys_ph=+1.000000  qphys_t=+7198.81  qphys_mu=-7196.82
phys_blocks_pos=5  phys_blocks_neg=1  phys_straddles_origin=1
```

**MEASURED: `W(A)` straddles the origin in the physical Euclidean inner product, and the witness is
the `mu` block.** The campaign's "intrinsically indefinite" therefore survives the coordinate
objection — it is a property of the operator, not of `S` or `D`.

Note the near-exact antisymmetry: `ru`/`t` at **+7198.81** against `mu` at **−7196.82**. That is the
signature of a skew-like coupling between the mass variable and the momentum/thermal ones — the
continuity coupling — and it is consistent with the long-standing record that `mu` dominates the
stage-2 residual.

**Two caveats, stated rather than buried.** (1) The block directions are uniform-fill, and this
project has already recorded that horizontally-uniform structured directions are a **null space** for
some blocks. `qphys_ph = 1.000000` *exactly* is that signature — `A = I − hγJ` acting as precisely the
identity, i.e. `J` has no component along it — so the **positive** readings may understate structure.
The negative witness is not degenerate (−7196.82, nowhere near 1) and is what carries the
conclusion. (2) A spanning sample of six directions is a witness for straddling, not a bound on the
range; "5 positive" is not a proof that only `mu` is negative.
