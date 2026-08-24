# R13.19 — checklist for the R13.18 precision review

Baseline: `main` merge `36d3506` (PR #179 merged). Branch `agent/r13-19-initial-stop-consistency`.

**Two of the review's sharpest claims verified before writing anything:**

1. **The near-worst fold is still order-dependent.** Replayed the reviewer's three-element chain
   `A(0.9985, not-met) · B(0.9994, met) · C(1.0000, met)` over all six permutations of my R13.18
   fold: **4 give `true`, 2 give `false`**, and the correct answer is `true` (A sits outside the
   final band `≥ 0.999`). "Near" is **non-transitive** — A~B and B~C but A≁C — and a running
   `(worst, all_met)` cannot retract A once C arrives. My R13.18 fix closed the 2-element case only.
2. **The contract test pins a wrong gate contract.** `ok_stage()` sets `gate_metric_ok = true`
   (`test_first_failure_classification.cpp:63`) and my fixture builds on it and asserts
   `stage_gate_metric_mismatch` — so **CI actively fixes a contract that reports the stage gate
   refused while the record says it passed**. The classifier's condition never reads
   `gate_metric_ok`.

Legend: **[DONE]** · **[OPEN]**

## P0

- [x] **P0-1 — `InitialConverged` merges the stop metric and the S metric back into one success.**
      The early return sets `success = true` unconditionally and stores `error_val` — which is
      ρ_D (or ρ_E under the WRMS experiment) — into `rel_error`, the **S-coordinate** field. So
      ρ_stop = 0.85, ρ_S = 0.99, η = 0.90 returns *success* where the normal finaliser would return
      failure and the classifier would say `KrylovObjectiveMismatch`. It also populates none of the
      new receipt. **This is a production path**: the value feeds `gmres_success`,
      `gmres_raw_rel_error`, the trust-region prediction, the total-failure rule and warm-start
      quality — not a diagnostic.
- [x] **P0-2 — `StageGateMetricMismatch` does not measure the stage gate.** `exit_rho_E` is the
      **StageEntry-weighted linear Krylov residual**; the real gate re-evaluates the **nonlinear**
      stage residual at `U_new` against `stage_gate_rel_threshold`, under one of three
      `gate_metric_mode`s. Different residual, denominator, weighting point and threshold. The
      classifier also never reads `gate_metric_ok`, and the fixture asserts the mismatch with the
      gate recorded OK. Causal order compounds it: the gate is evaluated **after** the Newton loop
      ended, so promoting it to the *first* refusal contradicts the taxonomy.
- [x] **P0-3 — the near-worst reducer is still order-dependent** (verified above), and separately
      the **mechanism** attribution is too: `worst_*` updates on strict `>`, so on an exact tie the
      first-arriving solve's D/S/source/budget receipt wins.
- [x] **P0-4 — the stopping-metric enum exists and the classifier still hardcodes D.** Under the
      WRMS experiment `error_tensor` is the `E⁻¹S` objective and is stored in `rho_D_final` /
      `D_tolerance_reached`; the category and layer then say `..._D_vs_newton_merit`.

## P1

- [x] **P1-1 — tolerance provenance is not provenance.** `tol_source` keys `EisenstatWalker` off
      `policy.ew_applied`, which is whether E–W changed the **restart budget**, not whether it set
      the tolerance. The INN ramp multiplies `krylov_tol_adaptive` and is not in the selector at
      all (`InnRamp` has no producer). And the emitter never calls `krylov_forcing_layer_for` — it
      prints the source-neutral category layer, so the "recorded source selects the layer" claim is
      not implemented end to end.
- [x] **P1-2 — `KrylovBudgetLimited` still overclaims.** Exhaustion is measured; "still descending
      when cut off" is not. A solve flat from the first restart classifies the same.
- [x] **P1-3 — the Taylor receipt fails OPEN on missing new fields**, and the contract test pins
      that. Needs receipt versioning so a v2 record must carry them.
- [x] **P1-4 — event and cause share one enum**, which is why the `ZeroUpdateAfterTotalFailure`
      branch both "outranks the reconstruction" and "must not override r₀ evidence" while the code
      does the former.

## Accepted / unchanged

A/B shared preconditioner instance stays HOLD. `dt=600` forward, one-step tangent/adjoint, exact
4D-Var and MPI production remain NO-GO.


---

## First batch — P0-1, P0-2, P0-3

Measured after, dt=600 stage 2 (verdict and values unchanged; only the **claims** are now accurate):

```
category=zero_update_after_total_failure  layer=zero_update_bnorm_rule_or_step_recovery
exit_krylov_iter=3  exit_rho_stop=0.9297  exit_rho_S=1.048  exit_rho_E_entry_linear=0.8618
krylov_solves_trivial=0  near_worst_all_met_tol=0
```

**P0-1 — the production bug.** `InitialConverged` set `success = true` unconditionally and stored
the **stop** metric into `rel_error`, the **S-coordinate** field. `success` is now the S question,
the same one the normal exit answers; `rel_error` always carries ρ_S; the stop objective keeps its
own field; and the full receipt (both ρ's, `tolerance_applied`, both reached flags, the stopping
metric, `arnoldi_spent/allowed = 0/allowed`) is populated. The matching rule moved with it: an
`InitialConverged` solve counts as **trivial** only when it met **both** metrics — one that met the
stop objective and not S is not a zero-work success, it is exactly the objective mismatch, and
excluding it hid the state the classifier exists to name.

**P0-2 — false gate authority, removed.** `exit_rho_E` is the **StageEntry-weighted linear Krylov
residual**; the stage gate re-evaluates the **nonlinear** stage residual at `U_new` against its own
threshold under one of three modes. Different residual, denominator, weighting point, threshold.
The category is renamed **`KrylovEntryMetricMismatch`** (layer
`krylov_entry_E_metric_vs_solver_metrics`), the emitted field to
`exit_rho_E_entry_linear`, and the clause now **requires `gate_metric_ok == false`** — because the
fixture had been asserting the mismatch on a record inheriting `gate_metric_ok = true` from
`ok_stage()`, i.e. **CI was pinning a contract that reported the gate refusing while the record said
it passed.** A new fixture pins the converse: with the gate recorded as passing, the seam may not be
reported as a refusal.

**P0-3 — the reducer was still order-dependent, verified before fixing.** Replaying the reviewer's
three-element chain over all six permutations of the R13.18 fold: **4 → `true`, 2 → `false`**, with
`true` correct. "Near" is **non-transitive** (A~B, B~C, A≁C) and a running `(worst, all_met)` cannot
retract A once C arrives — the R13.18 fix had closed the 2-element case only. The state is now **two
maxima** (`worst`, `worst_unmet`) with the predicate evaluated once at the end
(`near_worst_all_met`), which is order-independent by construction. All six permutations are
asserted to agree, and to agree on `true`.


---

## Second batch — P0-4, P1-1, P1-2, P1-3

```
category=zero_update_after_total_failure  layer=zero_update_bnorm_rule_or_step_recovery
specific_layer=n/a   exit_stop_metric=block_D   exit_tol_source=eisenstat_walker
exit_rho_E_entry_linear=0.8618   near_worst_all_met_tol=0
tau=0.1192  tau_excited_block_max=0.2008  tau_verdict=measured
```

**P0-4.** The objective-mismatch layer said `..._D_vs_newton_merit` while under the WRMS experiment
the metric actually satisfied is `E⁻¹S` at the Newton linearization point — the enum was recorded
and the layer ignored it. The category's layer is now metric-neutral
(`krylov_stop_metric_vs_newton_merit`) and `krylov_stopping_layer_for` derives the specific one,
with `stop_metric_unrecorded` when nothing was recorded.

**P1-1.** `tol_source` keyed `EisenstatWalker` off `policy.ew_applied`, which is whether E–W changed
the **restart budget** — so a run whose *tolerance* came from E–W with a budget multiplier of 1 was
recorded as `Base`. It now keys on `ew_eta_used`, and the **INN ramp** is in the selector, giving
`InnRamp` its first producer. **And the emitter now calls the helpers**: `specific_layer` is on the
record, so "the recorded source selects the layer" is implemented end to end rather than only in
the contract tests. It reads `n/a` here because this record's category is neither of the two that
derive a layer — which is the honest output, not a missing one.

**P1-2.** `KrylovBudgetLimited` → **`KrylovBudgetExhausted`**, layer
`inner_budget_or_unresolved_stagnation`. "Limited" asserted the residual was still descending when
the budget cut it off, and nothing measures that — a solve flat from its first restart reaches the
same branch. The category now states the fact it has and its layer names the ambiguity instead of
resolving it.

**P1-3.** The Taylor receipt is **versioned**. Every new gate fires only when its field was
measured, so a record missing them returned `Measured` — deliberate, for logs predating the fields,
but it made a *new* record that failed to populate them read as certified. A `receipt_version = 2`
record must carry all four preconditions or returns `ReceiptIncomplete`; v1 keeps the legacy
reading. The live emitter declares v2, and all four omissions are pinned as failing closed.

## Remaining

- A/B shared preconditioner instance stays **HOLD**; `dt=600` forward, one-step tangent/adjoint,
  exact 4D-Var and MPI production remain **NO-GO**.


---

## Third batch — P1-4, the event/cause split

The review's structural point: one enum answering two questions is *why* the
`ZeroUpdateAfterTotalFailure` branch could carry both "a recorded event outranks the aggregate
reconstruction" and "it must not override the r₀ evidence" — while **returning immediately**, which
overrides it. The comment was aspirational and the code was not.

Fixed **additively**, so the four rounds of fixtures built on `first_failure_of` keep their meaning:
it still returns the **primary event**, and `stage_diagnosis_of` reports the **metric attribution**
beside it — computed from the same clauses with the recorded exit removed, so the two answers cannot
drift apart. Measured:

```
category=zero_update_after_total_failure   layer=zero_update_bnorm_rule_or_step_recovery
attribution=krylov_stagnated
attribution_layer=operator_or_timestep_or_jvp_or_scaling_or_preconditioner_or_policy
attribution_measured=1
```

The record now says **both**: the loop ended at the zero-update guard (‖b‖-gated), *and* the r₀
metric evidence independently reads `krylov_stagnated` (worst = 0.9941 ≥ 0.90). Two lines of
evidence, neither discarding the other — which is what the comment claimed all along. And
`attribution_measured` reports **absence as absence** rather than as agreement with the event.

**Every item in the R13.18 precision review is now closed**, except the A/B shared preconditioner
instance, which stays HOLD as a stated limitation. `dt=600` forward, one-step tangent/adjoint, exact
4D-Var and MPI production remain NO-GO.

---

## Self-review of R13.19 — two defects in this batch's own work

Asked whether this batch had introduced mistakes. It had.

**1. `mixed_mechanism_in_band` had ZERO producers and ZERO consumers.** Declared on `NearWorstFold`
with the comment *"set by the caller's two-pass check"* — **a caller that does not exist.** The
recurring defect of this campaign, introduced by the very commit that fixed an instance of it
(P0-3). **Removed rather than wired**, because the thing it was reaching for is a *separate* open
item: the review's point that the **mechanism** attribution is still order-dependent — `worst_*`
updates on strict `>`, so on an exact tie the first-arriving solve's D/S/source/budget receipt wins.
That needs per-solve receipts and a two-pass reduction, not a boolean, and is now recorded as **open**
rather than papered over with a field nothing sets.

**2. `attribution` was named for something it is not.** `stage_diagnosis_of` computes it by clearing
`newton_termination` and re-running the classifier — but with the termination cleared, the clauses
that *consume* it (R13.17) fall back to the **aggregate reconstruction**, the thing this campaign
spent four rounds moving away from. So on a stage with no r₀ evidence the field is the old
precedence, not "what the metric evidence says". And the first `attribution_measured` merely
excluded two enum values, so a pure reconstruction naming an operator layer would have read as
*measured*.

The field is now documented as **"the classification with the recorded exit removed"**, and
`attribution_from_metric` says whether it rests on a real r₀/Krylov reading. Measured on the live
case: **`attribution_from_metric=1`** — the `krylov_stagnated` attribution there is genuine metric
evidence (worst = 0.9941), not a fallback. A fixture pins the converse.

### Checked and found sound (negative results)

- **ρ_S at `InitialConverged`** is computed from `r_true_inner`, which is **halo-zeroed at creation**
  (`:1189-1190`), and `bnorm_unscaled` uses the halo-zeroed `b` — so the new ρ_S is on the same
  quantity the normal finaliser reports. The two paths do not disagree.
- **The tolerance-source selector.** `ew_eta_used` is set only inside `apply_ew`, which returns
  early unless `budget_active && ew_enabled && !tol_overridden` — so `> 0` genuinely means E–W
  computed an η, and it is strictly better evidence than the old `ew_applied` (budget scaling).
  E–W and a stage override are mutually exclusive by construction, so the selector's ordering is
  harmless.

### Now open, stated rather than closed

**Mechanism attribution on an exact tie remains order-dependent.** The `all_met` predicate is
order-independent (two maxima); the D/S/source/budget receipt attached to `worst_*` is not, because
it updates on strict `>`. Fixing it properly is the review's per-solve-receipt + two-pass reduction,
which is its own increment.

---

## Second adversarial pass — round 8 (code) and a numerics referee (claims)

### Round 8, P0-B — my P0-1 fix produced the inversion again, the eighth time

Narrowing `trivial_solve` admitted the D-satisfied zero-work solve to the aggregate — correct — but
`met_tolerance` still excluded `InitialConverged`, so that solve was scored as an **unmet** solve at
the maximum. The tie refusal is the **first** clause of the four-way, so `KrylovObjectiveMismatch`
— **the category the fix exists to reach** — became unreachable for exactly the solve it was aiming
at, and the emitted layer was the operator/split. The row even carried
`worst_krylov_D_reached=1 worst_krylov_S_reached=0` beside `category=krylov_stagnated`: two
mutually exclusive readings on one line.

`InitialConverged` **did** meet a tolerance — the D objective; that branch is gated on
`error_tensor < tol` — so it now counts as such, and two fixtures pin both the fixed behaviour and
the broken one it replaces.

### Round 8, P0-D — P1-1 was not fixed for the default configuration

`stage_budget_forcing_eta` is written only inside the stage-budget resolver and only when
`budget_active`, which requires a stage knob to be set — **and they all default to 0**. Meanwhile
`use_adaptive_tolerances` defaults **true** and the E–W block writes `krylov_tol_adaptive`
directly. So **every solve in a stock run recorded `tol_source=base`** while E–W owned the
tolerance; my measured run only read `eisenstat_walker` because its namelist sets
`stage2_gmres_restart = 8`. A flag is now set where E–W actually writes the tolerance.

### Round 8, P1-B — a consumer left behind when its producer changed

`gmres_tolerance_reached` counted `InitialConverged` unconditionally — added when that return
reported *one* metric. R13.19 made it report two, so a solve meeting D and not S was being counted
as a finished solve: overstating exactly the case the objective-mismatch work exists to surface.

### Numerics referee — the physical-range claim was LOGICALLY INVERTED

The referee's sharpest finding, and it is right. I wrote that the straddle's witness is `mu` and
that the negative reading "carries the conclusion". **It does not.** The negative side was already
over-determined by 24 random directions (`neg = 24/24`); what the block scan **adds** is the
*positive* side, and **the straddle rests entirely on `ru`, `rv`, `rw`, `t`** — the four structured
readings my own caveat discounts as possibly degenerate.

The consequence is sharper than my original text: **in the physical metric the random evidence alone
reads negative definite**, which for GMRES is as good as positive definite. "Intrinsically
indefinite" survives the coordinate objection *only* through those four directions, so **the
positive side — not `mu` — is what a further measurement must attack.**

Also **withdrawn**: the "near-antisymmetry ⇒ skew continuity coupling" reading. Single-block
directions probe the **diagonal** blocks of the symmetric part and cannot see the `mu`↔`(ru,t)`
coupling at all.

### Open from round 8, not closed here

`P0-C` (`specific_layer` reads `exit_*` while the category it annotates can be decided by `worst_*`),
`P1-A` (undisclosed production side effects of P0-1), `P1-E` (`KrylovEntryMetricMismatch` may be
production-unreachable), `P1-F` (the measurement offered as evidence does not exercise the changed
path), `P1-G` (`exit_*` promoted from `last_*`, which are written only inside the r₀-measured guard,
so a stale receipt can be labelled "this solve's"). Recorded as open rather than claimed closed.

### Round 8, remaining P0/P1 — closed

**P1-A — an undisclosed PRODUCTION side effect of my own P0-1 fix, and it was unconditional.**
After that fix `rel_error` carries ρ_S, and `initial_rel_error` is `rn0/bn0` taken from the same
halo-zeroed `r` and the **same pre-scaling `b`** (`bn0` at `:1208`, the D-scaling at `:1254`) — *the
same two norms*. So their ratio is exactly 1, and the opt-in r₀ rule's
`raw >= 0.99 * r0_ref` is **unconditionally true for every `InitialConverged` solve**, putting all
of them on the recovery / zero-step path. A commit whose message claimed to change only what is
*reported* was changing what the solver *does*. A solve that converged on entry is now excluded from
both total-failure rules.

**P0-C — the layer described a different solve than the verdict.** `specific_layer` derived from
the **exit** solve's source/metric unconditionally, while the four-way clauses can be reached from
the **stage-worst** receipt — and `worst_krylov_tolerance_source` was produced, plumbed, printed and
read by nothing. `StageDiagnosis::decided_by_exit_receipt` now says which receipt decided the
category, the layer is derived from *that* one, and the record carries `layer_receipt=` so a reader
can see it. Measured: `layer_receipt=stage_worst`.

**P1-B — a consumer left behind.** `gmres_tolerance_reached` counted `InitialConverged`
unconditionally; with that return now reporting two metrics, a solve meeting D and not S was being
counted as finished.

### Round 8's negative results, worth as much

The reviewer tried and failed to break: the halo-zeroing of ρ_S at both `InitialConverged` sites
(it matches the normal finaliser); `bnorm_unscaled` as the denominator; `arnoldi_allowed`
(byte-identical to the normal finaliser); **the fold's order-independence** (checked equal values,
single solve, exact band edge, zero/negative/NaN progress and the three-element chain); the
`KrylovBudgetExhausted` rename (no stale references); the Taylor v2 receipt; `gate_metric_ok` as a
real measurement; the per-solve stats reset; and AD hygiene (no new `.item()`/`.detach()`
violations).

### Still open, stated

- **P1-E** — `KrylovEntryMetricMismatch` may be **production-unreachable by default**: D && S met
  ⇒ ρ_S < tol ≤ 0.9 ⇒ not a total failure ⇒ the `ZeroUpdateAfterTotalFailure` exit is never taken,
  so only fixtures reach it. Not yet confirmed by a run.
- **P1-F** — `krylov_solves_trivial = 0` in every measurement means the P0-1 path **was not
  exercised**, so "verdict and values unchanged" is *not* evidence that the change is safe. A run
  that actually takes the `InitialConverged` branch is the missing measurement.
- **P1-G** — `exit_*` is promoted from `last_*`, which are written only inside the r₀-measured
  guard, so a stale receipt from an earlier iteration can be labelled "this solve's".
