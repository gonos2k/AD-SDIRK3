# R13.21 — initial-objective-mismatch control and action-stable stage diagnosis

**Baseline** `607874d` (PR #180 merged) · **Source** external review of R13.19–R13.20 ·
**Independent review: NOT RUN** (`/code-review ultra` is user-triggered; quota exhausted). CI green
is not offered as a substitute.

Every finding below was **re-verified in the tree before being accepted**. That discipline exists
because this campaign once shipped a "correction" worse than the overstatement it replaced — the one
time a reviewer's premise was taken on trust.

---

## Verification of the review's premises

| # | Claim | Verified? | Evidence |
|---|---|---|---|
| P0-1 | `gmres_converged_on_entry` conflates true initial convergence with initial objective mismatch | **YES** | `newton_solver.cpp:9850` sets it from `termination_reason == InitialConverged` alone; `:11115` / `:11126` exempt both total-failure rules on it |
| P0-2 | `primary_event` is replaced by a cause in the zero-update branch | **YES** | the zero-update block in `first_failure.h` returns `KrylovObjectiveMismatch` / `KrylovEntryMetricMismatch` instead of the event |
| P0-3 | the actionable layer is order-dependent on exact ties | **YES** | strict `if (progress > worst)` at `:10105` keeps the first arrival's `worst_krylov_tolerance_source`; `implies_same_category_as` compares category only |
| P1-1 | no stage-worst stopping metric | **YES** | `worst_krylov_stopping_metric` occurs **0** times; `exit_stopping_metric` exists |
| P1-2 | early returns leave the new receipt at sentinels | **YES** | the `NanRetryExhausted` returns (`:1552`, `:3395`) fill only the ctor fields + `initial_rel_error`; 6 `return res;` sites in total |
| §8 | the 0.90 boundary flips the category on a 0.8 % ratio difference | **YES — our own measurement** | stage-3 sweep: `vs_r0` 0.907 → `krylov_stagnated`, 0.8993 → `newton_stagnated` |
| §9.2 | "stage 3 is inherited from stage 2" is not established | **ACCEPTED** | our own writeup already says the arms differ in Newton trajectory; the review states the consequence more precisely |

**One qualification.** §3.2 says an objective-mismatch entry "can be accepted as a full Newton step
without a decrease check" on the trust-off path. The *conflation* is real and is P0-1. Whether that
particular acceptance path is reachable in the shipped configuration is a separate question, and
item 1.3 measures it rather than assuming it.

---

## Checklist

### Phase 1 — P0-1: the initial-stop consumer

- [x] **1.1** Split `gmres_converged_on_entry` into `S_reached_on_entry` and
      `objective_mismatch_on_entry`, both derived from the **receipt** (`S_tolerance_reached`),
      not from the termination label.
- [x] **1.2** Exempt the two total-failure rules on `S_reached_on_entry` only.
- [x] **1.3** Measure whether the trust-off acceptance path is reachable, **then** choose what an
      `objective_mismatch_on_entry` does (fail-closed vs require-decrease). Do not assume.
- [x] **1.4** Rename the reason to `InitialStopMetricReached`, **or** record at its definition why
      the name is kept. `InitialConverged` reads as S/Newton convergence, which it is not.
- [x] **1.5** Fixtures: `InitialStop_ObjectiveMismatch_IsNotSConvergedOnEntry`,
      `InitialStop_ObjectiveMismatch_DoesNotBypassFailurePolicy`.

**Phase 1 result.** The chain is verified end to end: `gmres_converged_on_entry` (label) → exempts
both total-failure rules → `gmres_total_failure_candidate = false` → under `!nk_trust_region` the
`else` branch computes `R_trial`, **stores it, and never compares it with `R`** before
`step_accepted = true`. The other two acceptance sites in that function both test a decrease. So
the review's §3.2 is confirmed — with one qualification it did not have: **`nk_trust_region`
defaults to `true` and `em_b_wave` does not override it**, so the shipped configuration never takes
that path. Latent, not live.

Fixed: the flag is split and both halves come from the receipt; only `S_reached_on_entry` exempts;
an objective mismatch on the trust-off path must show a measured nonlinear decrease, and on failure
routes to the **same** total-failure handling rather than becoming a silent no-step (which the
block's own comment warns creates zero-update loops). The rule is a pure function in
`wrf_sdirk3_first_failure.h` so a fixture can reject its negation — the pattern already used for
`near_worst_all_met`. The reason is renamed `InitialStopMetricReached`; **the emitted string stays
`initial_converged`**, because `tests/krylov_early_stop_ablation.py` and every archived log key on
it and renaming the wire format buys nothing.

### Phase 2 — P0-2: event and cause fully separated

- [ ] **2.1** The zero-update branch returns `ZeroUpdateAfterTotalFailure` as `primary_event`,
      always.
- [ ] **2.2** Add `exit_attribution` to `StageDiagnosis`, carrying the metric subtype the branch
      used to return.
- [ ] **2.3** Emitter prints the event and the exit attribution as separate fields.
- [ ] **2.4** Update the fixtures that currently *pin* the substitution — they encode the defect.
- [ ] **2.5** Fixtures: `ZeroUpdate_RemainsPrimaryEvent`,
      `ExitObjectiveMismatch_IsSeparateAttribution`.

### Phase 3 — P0-3: an action-stable reducer

- [ ] **3.1** Add `stopping_metric` to the per-solve `KrylovSolveMechanism` receipt.
- [ ] **3.2** Compare near-worst solves on the full **action key** — category + stopping metric +
      tolerance source — not category alone.
- [ ] **3.3** Emit `attribution_specific_layer` beside the primary one.
- [ ] **3.4** Add `worst_krylov_stopping_metric` (P1-1) so a stage-worst objective mismatch can name
      its layer instead of `stop_metric_unrecorded_for_worst_solve`.
- [ ] **3.5** Fixtures: `NearWorst_SameCategoryDifferentToleranceSource_IsActionAmbiguous`,
      `NearWorst_SameCategoryDifferentStoppingMetric_IsActionAmbiguous`,
      `SpecificLayer_IsPermutationInvariant`, `AttributionSpecificLayer_IsEmitted`.

### Phase 4 — P1-2: one finalizer for every return path

- [ ] **4.1** Route all six `GMRESResult` return sites through one finalizer that fills the metric,
      budget and termination receipt.
- [ ] **4.2** Fixtures: `NanRetry_ReturnHasCompleteReceipt`, `AllKrylovReturnsShareOneFinalizer`.

### Phase 5 — §8: the threshold's sensitivity on the record

- [ ] **5.1** Emit `threshold_value`, `threshold_distance`, `threshold_sensitive`.
- [ ] **5.2** A threshold-sensitive attribution must not route to a specific layer.
- [ ] **5.3** Fixtures: `ThresholdDistance_IsEmitted`,
      `ThresholdSensitiveAttribution_DoesNotRouteToSpecificLayer`.

### Phase 6 — the experiments (after Phases 1–5 land)

- [ ] **6.1** Stage-3 Newton iteration 0 on a **frozen** system, early-stop OFF, fresh
      preconditioner, Arnoldi 32/64/128/256/512.
- [ ] **6.2** Stage-3 iteration 1 from a **common** first step, so the second solve is comparable
      across arms.
- [ ] **6.3** Terminal-candidate Taylor probe on the raw `dK` before zeroing, diagnosis-only, so the
      zero-update iteration is measured for the first time.

---

## Standing caveats

The review's NO-GO list for `dt=600` forward completion, full-step tangent/adjoint, exact 4D-Var and
MPI production is **accepted without argument** — none of those was claimed.
