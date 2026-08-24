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

- [x] **2.1** The zero-update branch returns `ZeroUpdateAfterTotalFailure` as `primary_event`,
      always.
- [x] **2.2** Add `exit_attribution` to `StageDiagnosis`, carrying the metric subtype the branch
      used to return.
- [x] **2.3** Emitter prints the event and the exit attribution as separate fields.
- [x] **2.4** Update the fixtures that currently *pin* the substitution — they encode the defect.
- [x] **2.5** Fixtures: `ZeroUpdate_RemainsPrimaryEvent`,
      `ExitObjectiveMismatch_IsSeparateAttribution`.

**Phase 2 result.** `first_failure_of`'s zero-update branch returns the EVENT unconditionally; the
subtype it used to return is `StageDiagnosis::exit_attribution`, computed by the pure
`krylov_exit_attribution_of`. **Three fixtures were pinning the substitution** — they asserted the
subtype through `name_of(s)`, i.e. as the thing that ended the stage — and are rewritten to check
the event and the attribution separately. The emitter prints `exit_attribution` and
`exit_attribution_layer`, the latter derived from `exit_stopping_metric`, the receipt it belongs to.
The `KrylovObjectiveMismatch` arm of `specific_layer` loses its exit alternative, which after this
change no input can select.

**Verified on the live dt=600 record**, not just in fixtures:

```
category=zero_update_after_total_failure   event_basis=exit_receipt
attribution=krylov_budget_exhausted        attribution_basis=krylov_r0_receipt
exit_attribution=none                      exit_attribution_layer=n/a
```

Three distinct answers on one row. `exit_attribution=none` is the honest reading here — this exit
solve had `exit_D_reached=0`, so its receipt cannot support a subtype, and the record says so
rather than inventing one.

### Phase 3 — P0-3: an action-stable reducer

- [x] **3.1** Add `stopping_metric` to the per-solve `KrylovSolveMechanism` receipt.
- [x] **3.2** Compare near-worst solves on the full **action key** — category + stopping metric +
      tolerance source — not category alone.
- [x] **3.3** Emit `attribution_specific_layer` beside the primary one.
- [x] **3.4** Add `worst_krylov_stopping_metric` (P1-1) so a stage-worst objective mismatch can name
      its layer instead of `stop_metric_unrecorded_for_worst_solve`.
- [x] **3.5** Fixtures: `NearWorst_SameCategoryDifferentToleranceSource_IsActionAmbiguous`,
      `NearWorst_SameCategoryDifferentStoppingMetric_IsActionAmbiguous`,
      `SpecificLayer_IsPermutationInvariant`, `AttributionSpecificLayer_IsEmitted`.

**Phase 3 result.** The tie check compares the **derived layer**, not the raw provenance fields.
R13.20 narrowed it from field equality to *category* equality on the ground that the classifier does
not branch on `tolerance_source` — right about the category, wrong about the consequence:
`specific_layer` is derived from the tolerance source (forcing) or the stopping metric (objective
mismatch), and the stage-worst receipt updates on a strict `>`, so on an exact tie the **first
arrival's** provenance is what the emitter reads.

Comparing the layer keeps R13.20's gain rather than reverting it: two tied solves that both imply
`KrylovStagnated` derive **no** specific layer, so differing provenance does not make them
ambiguous and the refusal still does not fire toward the operator/split layer for a difference that
cannot change the answer. Fixtures pin both directions.

`worst_krylov_stopping_metric` (P1-1) is added as the stage-worst twin of `exit_stopping_metric`,
so a four-way objective mismatch names its layer instead of `stop_metric_unrecorded_for_worst_solve`
— fail-closed before, actionable now. And `attribution_specific_layer` is emitted beside the
primary one, so a row whose *attribution* is forcing-limited no longer loses the source-specific
place to work.

### Phase 4 — P1-2: one finalizer for every return path

- [x] **4.1** Route all six `GMRESResult` return sites through one finalizer that fills the metric,
      budget and termination receipt.
- [x] **4.2** Fixtures: `NanRetry_ReturnHasCompleteReceipt`, `AllKrylovReturnsShareOneFinalizer`.

**Phase 4 result, and a deliberate scope choice.** The review proposes routing all six returns
through one `finalize_krylov_result`. That is a restructure of two 1000-line functions, and this
project's own rule is to prefer a small reversible mechanism over a risky monolithic rewrite. What
the contract actually needs is that **every** return produce a complete receipt — so the rule is
written as a pure predicate, `krylov_receipt_complete`, and the two returns that failed it (the
`NanRetryExhausted` early exits, one per solver) now fill the metric, budget and stopping-metric
fields. The other four already did.

The ratios are stamped `1.0` there rather than left at a sentinel: the residual is non-finite by
construction on that path, so "no reduction" is the honest reading and a sentinel would read
"never measured". Fixtures pin the complete case, **each field's absence separately** (a rule that
cannot fire is not a rule), and the pre-R13.21 receipt reconstructed — which the rule rejects,
which is why a terminal solve ending there fell back to the generic event.

### Phase 5 — §8: the threshold's sensitivity on the record

- [x] **5.1** Emit `threshold_value`, `threshold_distance`, `threshold_sensitive`.
- [x] **5.2** A threshold-sensitive attribution must not route to a specific layer.
- [x] **5.3** Fixtures: `ThresholdDistance_IsEmitted`,
      `ThresholdSensitiveAttribution_DoesNotRouteToSpecificLayer`.

**Phase 5 result.** `threshold_value`, `threshold_distance` (signed, so the side is readable),
`threshold_measured` and `threshold_sensitive` are on the record, computed with the **same
threshold resolution the classifier applies** — so the row cannot report a distance from a boundary
other than the one used. A verdict inside the ±0.02 band no longer issues a specific layer:
`specific_layer` and `attribution_specific_layer` both read
`threshold_sensitive_no_specific_layer`, because a work order that a 1 % change in the ratio would
reverse is not a work order. Absence of an r0 measurement is explicitly **not** reported as
near-boundary.

Fixtures use this campaign's own numbers: 0.907 and 0.8993 from the stage-3 sweep are both inside
the band and both refused a layer, while 0.9941 from the dt=600 central record is not.

**Verified live:** that record now reads `threshold_value=0.9 threshold_distance=0.09413
threshold_measured=1 threshold_sensitive=0` — far outside the band, layer retained.

### Phase 6 — the experiments (after Phases 1–5 land)

- [ ] **6.1** Stage-3 Newton iteration 0 on a **frozen** system, early-stop OFF, fresh
      preconditioner, Arnoldi 32/64/128/256/512.
- [ ] **6.2** Stage-3 iteration 1 from a **common** first step, so the second solve is comparable
      across arms.
- [x] **6.3** Terminal-candidate Taylor probe on the raw `dK` before zeroing, diagnosis-only, so the
      zero-update iteration is measured for the first time.

**6.3 result — the terminal iteration, measured for the first time.**

The Taylor probe sits inside `if (step_accepted)`, so the iteration that *ends* the loop is
structurally invisible to it — `taylor_covers_last_newton_iter = 0` at every rung of both budget
ladders. The raw candidate survives that exit (`dK_scaled` is zeroed; `dK` is not), so it can be
evaluated without advancing anything. `dt=600`, `max_newton_iter=12`, stage 2, the iteration that
broke the loop:

```
SDIRK3_TERMINAL_TAYLOR stage=2 newton_iter=3 alpha=0.3333
  tau_exit=0.005558   As_norm=2.448e+07   dK_raw_norm=387.8   state_mutated=0
  R_norm=4.77e+08  R_trial_norm(a=1/3)=4.567e+08  R_full_step_norm=4.173e+08
  candidate_alpha_would_reduce=1   candidate_full_would_reduce=1
```

**The linear model at the terminal candidate is faithful to 0.56 %** — better than any *accepted*
step in the same run (0.1192, 0.0645, 0.0182). **And the discarded full step would have reduced the
nonlinear residual by 12.5 %** (4.77e8 → 4.173e8); the α = 1/3 arm by 4.3 %.

So of the four outcomes the review's §10.3 lists for this exit, the measured one is the fourth:
**a usable candidate that the total-failure rule discarded** — on a `‖r‖/‖b‖` predicate, the
coordinate this campaign has repeatedly recorded as the wrong denominator. The zero-update exit at
dt=600 is not a linear-solve failure in any useful sense: the solve produced a step that is
accurately modelled *and* decreases the merit function, and the discard rule rejected it.

**Limits, stated.** n = 1 — one stage, one iteration, one timestep, one configuration. A 12.5 %
decrease is not convergence, and taking it would not obviously complete the step; what this
establishes is that at that point the **rule**, not the **solve**, is what stopped the loop.

---

## Standing caveats

The review's NO-GO list for `dt=600` forward completion, full-step tangent/adjoint, exact 4D-Var and
MPI production is **accepted without argument** — none of those was claimed.
