# R13.18 — checklist for the R13.17 deep review

Baseline: `main` merge `a5aaaae` (PR #178 merged). Branch `agent/r13-18-exit-solve-receipts`,
which first **rescues two orphaned commits** — the physical numerical-range measurement and the
round-7 withdrawal — that were pushed after #178 merged. Tenth occurrence of that race.

## First: reconciling this review with red-team round 7

The two disagree on the retraction, and the disagreement is **resolvable by separating two claims**:

| claim | verdict | by whom |
|---|---|---|
| "the failure moved **outward** to Newton" | **retracted — the exit event is identical in both runs** | both agree (round 7: *"the measured part is sound and survives: both runs break at the same code site"*) |
| "…therefore the first failure **is the linear solve**" | **not supported** — the gate is `‖dK‖<1e-15` **and** a ‖r‖/‖b‖ predicate | round 7 only |

So the retraction stands; what does **not** follow is the positive attribution. My previous wording
("back to HOLD, neither confirmed nor retracted") over-corrected and is itself fixed below.

Legend: **[DONE]** · **[OPEN]**

## P0

- [x] **P0-1 — the stopping metric may be `D` *or* `E`, and the receipt always calls it `D`.**
      `WRF_SDIRK3_KRYLOV_WRMS_METRIC=1` replaces the block-constant `D⁻¹` with `E⁻¹S`, so the
      internal objective becomes ρ_E, while the fields still read `rho_D_final` /
      `D_tolerance_reached`. Worse, the **stage gate** accepts/rejects on `‖E⁻¹R‖`, and neither the
      receipt nor the classifier carries ρ_E — so ρ_D < η, ρ_S < η, ρ_E ≥ η is unclassifiable.
- [x] **P0-2 — tolerance *source* is produced and emitted, and the classifier never reads it**,
      while the layer string hardcodes `eisenstat_walker_...`. A record can read
      `tol_source=stage_override` beside a layer naming Eisenstat–Walker. Same class, again.
- [x] **P0-3 — the near-worst tie reducer is ORDER-DEPENDENT.** Streaming update never resets the
      tie state when a strictly larger worst arrives. Reviewer's counterexample: `A(0.90, not-met)`
      then `B(0.99, met)` → `false`; `B` then `A` → `true`. Same solve set, different verdict.
- [x] **P0-4 — the terminal exit is subtyped by the STAGE-WORST solve, not the EXIT solve.** The
      `worst_*` receipts belong to the largest-ratio solve in the stage, which need not be the one
      that ended the loop.
- [x] **P0-5 — `KrylovBudgetLimited` claims more than it measures.** `reason == MaxBudget` is the
      resolver's *default* when nothing else is chosen, and coexists with the message "early exit
      before max restarts" — so it does not establish `spent == allowed`. And "still descending when
      cut off" has no tail-slope evidence.

## P1

- [x] **P1-1 — the `rw` Taylor reading is not what the doc claims.** `share_rw = 0.000216 < 1e-3`,
      the excitation floor, so `tau_rw = 0.0447` is normalised by the **floor**, not by the block's
      own `‖A s‖`. Raw ≈ 0.207. **My claim that `rw` is "better than the packed reading" is wrong**
      — the correct statement is that this step direction barely excites `rw`, so it constrains the
      `rw` Jacobian hardly at all.
- [x] **P1-2 — `tau_block_max` is written and never read by the verdict**, and the realization /
      roundoff gates only fire when *measured*, so a record missing them still returns `Measured`.
- [x] **P1-3 — `K` realization is checked, `U_eval = U_stage + hγK` realization is not.** A step can
      survive in `K` and be quantized away when added to a large background.
- [x] **P1-4 — `rho_D_initial` has no producer; early returns carry an incomplete receipt.**
- [x] **P1-5 — enum inventory ≠ producer set**: `TrustRejected` and `NonfiniteResidual` have no
      producers, and a comment claims a site is "NOT a Newton-loop exit" — verify against the code.
- [ ] **P1-6 — the A/B fingerprint still shares the underlying preconditioner instance.** Accepted
      as a stated limitation; the OFF/ON trajectory control stands for the current configuration.

## Scientific wording to correct

- [ ] τ_max = 0.2008 must not be called "≪ 1". The honest statement: no dominant first-order
      Jacobian defect in the measured directions, but the **full-step nonlinear remainder is ~20 %
      of the linear response in the worst excited block**.
- [ ] `rw` must be reported as **not constrained** by this direction, not as accurate.

---

## Measured after the first batch

```
tau=0.1192  tau_excited_block_max=0.2008  tau_verdict=measured
tau_ru=0.200793  tauraw_ru=0.200793  excited_ru=1
tau_ph=0.000476  tauraw_ph=0.000476  share_ph=0.139789  excited_ph=1
tau_rw=0.044653  tauraw_rw=0.207042  share_rw=0.000216  excited_rw=0
category=zero_update_after_total_failure  newton_exit=zero_update_after_total_failure
```

**P1-1 confirmed, to four digits.** The reviewer back-computed `rw`'s raw ratio as
`0.0447 × (1e-3 / 2.16e-4) ≈ 0.207`; measured, **`tauraw_rw = 0.207042`**. So `rw`'s raw defect is
the *largest* of the three — marginally above `ru`'s 0.2008 — and my earlier claim that `rw` was
"better than the packed reading" had it exactly backwards. It is **not constrained** by this
direction (`excited_rw = 0`, 0.02 % of ‖A s‖), which is a different statement from accurate, and the
record now carries `tauraw_`, `share_` and `excited_` per block so the two cannot be conflated.

`ph` is genuinely excited (share 0.14) and its raw and floor-normalised values coincide, so its
small τ **is** meaningful. `ru` likewise.

**P0-3 closed and pinned.** The near-worst fold is a pure function
(`near_worst_accumulate`) with both permutations of the reviewer's counterexample asserted equal,
plus the replace/join/ignore cases and the single-solve case.

**P0-5 closed.** `budget_exhausted` is now `arnoldi_spent >= arnoldi_allowed`, measured at the
solve, instead of `termination_reason == MaxBudget` — which is the resolver's *default* and coexists
with the message "early exit before max restarts", so it never established exhaustion.

**P1-2 closed.** `tau_excited_block_max` is read by the verdict (`BlockDefect` above 1.0); a record
without the field still classifies as before.


---

## Second batch — P0-1, P0-2, P0-4 closed

```
category=zero_update_after_total_failure  layer=zero_update_bnorm_rule_or_step_recovery
newton_exit=zero_update_after_total_failure
exit_krylov_iter=3  exit_D_reached=0  exit_S_reached=0
exit_stop_metric=block_D  exit_tol_source=eisenstat_walker
worst_krylov_tol_source=eisenstat_walker  near_worst_all_met_tol=0
```

**P0-1.** The stopping metric is now **typed and recorded** (`KrylovStoppingMetric`:
`identity_S` / `block_D` / `stage_wrms_E`) instead of being called `D` unconditionally.
`WRF_SDIRK3_KRYLOV_WRMS_METRIC` swaps `D⁻¹` for `E⁻¹S`, and the receipt now says which one ran —
measured here as `block_D`, which is what the default configuration should give. *(The ρ_E
**values** and the stage-gate seam the review also raises remain open; naming the metric is the
prerequisite, not the whole of it.)*

**P0-2.** The forcing category's layer is **source-neutral**
(`krylov_tolerance_policy_or_inner_budget`) and the specific layer is **derived** from the recorded
source through `krylov_forcing_layer_for` — `stage_tolerance_override`, `eisenstat_walker_forcing`,
`inn_tolerance_ramp`, `base_inner_tolerance`, and `inner_tolerance_source_unrecorded` when nothing
was recorded. It used to name Eisenstat–Walker while the value could have come from a stage
override, with the source produced, emitted and read by nothing.

**P0-4.** The terminal event is subtyped by the **exit solve's own receipt** (`exit_krylov_iter=3`),
not the stage's largest-ratio solve. Measured, the exit solve met **neither** tolerance
(`exit_D_reached=0`, `exit_S_reached=0`), so the event correctly keeps its own name rather than
being read as an objective mismatch. Three fixtures pin it: exit-met-neither keeps the name,
exit-met-D-not-S earns the mismatch, and **no exit receipt may not borrow the stage-worst one**.

## Still open, carried forward

- **P0-1 remainder**: ρ_E *values* on the receipt, and the **stage-gate** seam (the gate accepts on
  `‖E⁻¹R‖`, so ρ_D < η, ρ_S < η, ρ_E ≥ η is still unclassifiable).
- **P1-3**: `U_eval = U_stage + hγK` realization (only `K` realization is checked).
- **P1-4**: `rho_D_initial` has no producer; early returns carry an incomplete receipt.
- **P1-5**: `TrustRejected` / `NonfiniteResidual` have no producers — enum inventory ≠ producer set.
- **P1-6**: the A/B fingerprint still shares the underlying preconditioner instance (stated
  limitation; the OFF/ON trajectory control stands for the current configuration).


---

## Third batch — P1-3, P1-4, P1-5 closed

```
tau=0.1192  tau_excited_block_max=0.2008  tau_verdict=measured
realized_step_fraction=1  realized_U_fraction=1  signal_to_roundoff=2.527e+06
```

**P1-3.** `U_eval = U_stage + hγK` realization is now measured and gated, not just `K`. The float32
loss happens **at the addition to the background**, not when `K` is stored, so checking only `K`
certified the wrong sum. Measured `realized_U_fraction = 1`, so the earlier τ values clear the
stricter gate too.

**P1-4.** `rho_D_initial` had **zero producers** while its header comment promised "both readings
(initial and final)" — so the P0-1 headline *"both readings are on the record"* was true of no
record. It is captured in both solvers beside its S sibling, from the same initial residual.

**P1-5.** Two defects, both mine.

- A comment read *"NOT a Newton-loop exit — stamping it here would attribute the loop's exit to a
  site that does not end it"* — **ten lines above the `break` that ends the Newton loop, and above
  the stamp**. I wrote it believing the stall detector and that `break` were two sites; they are
  one. A comment denying what the code under it does is the same class as a field nothing reads.
- `TrustRejected` and `NonfiniteResidual` had **zero producers**, and neither corresponds to a real
  exit: the trust region's "all attempts rejected" path keeps `K` and *continues*, and no
  non-finite-residual site breaks the loop. **Removed** — keeping them made the inventory look more
  complete than the instrumentation was. If such an exit is added, the value returns *with* its
  producer.

## Remaining open, unchanged

ρ_E *values* and the **stage-gate seam** (the gate accepts on `‖E⁻¹R‖`, so ρ_D < η, ρ_S < η,
ρ_E ≥ η is still unclassifiable), and the A/B fingerprint's shared preconditioner instance — a
stated limitation, with the OFF/ON trajectory control standing for the current configuration.


---

## Fourth batch — the ρ_E seam closed, and what it immediately showed

`rho_E` is computed per solve from the stage weights (one `E` per stage, so the sequence is
comparable across iterations) with the residual mapped back to physical coordinates first, since
`E` is a physical weighting. New category `StageGateMetricMismatch`, layer
`stage_gate_E_metric_vs_solver_metrics`, for the state the receipt previously could not express:
both metrics the solver was steered by satisfied, and the **gate's** metric not.

**Measured, dt=600 stage 2 — the exit solve's three metric readings.**

*Corrected by the numerics referee (claim 5).* This said "three readings of the **SAME** residual",
and that was **false as implemented**: ρ_D and ρ_S were computed on halo-zeroed copies while ρ_E
used `gmres_result.r_true`, the **raw** residual, and normalised by a different `b`. So part of the
spread below was halo content and denominator choice rather than metric disagreement. The identical
halo check was run for ρ_S at `InitialConverged` and **recorded as a negative result** — and never
run for ρ_E. The code now halo-zeroes both sides of ρ_E; the numbers below are the pre-fix reading
and are retained as the record of what was measured when the claim was made.

```
exit_rho_stop = 0.9297   (block_D — the metric the loop actually stopped on)
exit_rho_S    = 1.0480   (the metric `success` is judged by)
exit_rho_E    = 0.8618   (the metric the STAGE GATE accepts on)
```

They span **0.86 to 1.05 on one residual — a 22 % spread**, and the ordering matters:

- **ρ_S = 1.048 is the only one above 1**, and `> 1` is precisely what makes `total_failure_vs_b`
  fire — the flag that gates the `ZeroUpdateAfterTotalFailure` exit.
- ~~**ρ_E = 0.8618** says that in the gate's own metric this residual came down **14 %**.~~
  **WITHDRAWN — a baseline error, and the fifth appearance of a class this campaign has caught four
  times before.** ρ_E is `‖E⁻¹Sr‖ / ‖E⁻¹Sb‖` — normalised by **b**, not by **r₀** — so it cannot say
  how far the residual "came down". This document's own table settles it: ‖r‖/‖b‖ asks *is the step
  predicted to reduce the nonlinear residual*, ‖r‖/‖r₀‖ asks *did the solve move*, and **they
  coincide only on a cold start**. The exit solve is Newton iteration 3, warm-started, with
  `r₀/‖b‖ = 1.054` measured on this very case.

~~So the solve that the ‖b‖ rule called a *total failure* had, in the metric the stage gate actually
judges by, made real progress.~~ **UNSUPPORTED** — progress is not what ρ_E measures, and (per
R13.19 P0-2) ρ_E is not the stage gate's metric either. What survives is the weaker and still
useful statement: **the three metrics disagree materially on the failing solve, and ρ_S is the only
one above 1** — which is what makes it, and only it, trip the ‖b‖ total-failure rule. That is round 7's P0-B as a measurement rather than an argument, and
it is the concrete reason the phrase "the linear solve produced nothing" had to be withdrawn.

**What this does and does not establish.** It shows the three metrics disagree materially on the
failing solve, with ρ_S the outlier. It does **not** by itself say which metric should govern the
total-failure flag — that is a design decision about what the solver is being asked to achieve, and
production behaviour is unchanged here. What has changed is that all three are on the record, so the
question can be argued from data instead of from whichever one a predicate happened to read.

All checklist items from the R13.17 deep review are now closed except the A/B fingerprint's shared
preconditioner instance, which is accepted and stated as a limitation with the OFF/ON trajectory
control standing for the current configuration.
