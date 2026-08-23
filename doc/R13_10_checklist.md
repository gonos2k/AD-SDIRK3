# R13.10 — checklist for the R13.8/R13.9 external review

Review baseline: `main` merge `68cd8cb` (PR #176). **That is before PR #177**, which carries three
adversarial rounds (R13.9–R13.14). Several items below were already closed there; each is marked
with its verified status in the current tree, not assumed.

Legend: **[DONE]** closed and verified · **[PARTIAL]** partly closed, remainder stated ·
**[OPEN]** not started.

---

## P0 — invalidates a conclusion

- [x] **P0-1 `fresh_preconditioner_per_arm=true` is hardcoded.** `make_fresh_M()` copies the mutable
      wrapper (`fallback_locked`, closure-local state) but every arm still calls into the *same*
      `UnifiedPreconditioner` instance, which updates caches and member diagnostics that later
      branches read. **[OPEN]** — verified hardcoded at `wrf_sdirk3_newton_solver.cpp:8905`.
      Fix: name it honestly (`fresh_wrapper_per_arm` + `shared_preconditioner_instance`) **and**
      make the verdict rest on an exact pre/post fingerprint of the shared object.

- [x] **P0-2 `fresh_operator_per_arm=true` is hardcoded.** All arms share one `gmres_op` closure —
      which is the *correct* A/B design; the defect is the contract name. **[OPEN]** — verified at
      `:8904`. Fix: replace with `same_frozen_operator` + `operator_repeatable` + `operator_linear`
      + `operator_state_unchanged`, none of them asserted.

- [x] **P0-3 arm-order verdict consumes only ρ_S.** `worst_order_delta` compares `rows[i].rho`
      alone (`:8825–8843`), while the headline uses ρ_D, ρ_phys, Msel and per-block. Termination is
      also collapsed to `termination_a=0 / termination_b=terms_equal?0:1` (`:8912–8913`) and
      `terms_equal` compares within a three-arm group, never forward vs reverse for the same
      `(arm,j)`. **[OPEN]**. Fix: typed per-row receipt, pairwise forward/reverse comparison over
      every headline metric, and split `mi_valid` / `msel_valid`.

- [ ] **P0-4 `probe_noninterfering=1` measures too little.** **[PARTIAL]** — round 5 widened it
      from one counter to four quantities *and* added restores: `precond_fallback_count_`,
      `precond_total_calls_`, `g_jvp_fd_fallback_count`, five per-iteration JVP counters, and
      `*variable_pc_event` (which feeds `stage3_hopeless_streak_`, a member that persists across
      timesteps). **Remaining**: the review's full contract — probe OFF vs ON compared on the
      production Newton residual sequence, A/M call sequences, termination sequence, solver outcome
      and published-state bytes.

---

## P1 — the record can mislead

- [x] **P1-1 Msel receipt is not consumed, and not reset per row.** `msel_engaged` is a shared_ptr
      printed per row (`:8729`) but never read by `ab_attributable`; an early-exit row can inherit
      the previous row's `true`. **[OPEN]**.

- [x] **P1-2 `d_weighted` is printed, not enforced.** No check that every arm used the same `D`;
      an empty `d_inv_used` cannot distinguish "D = I" from "D transfer failed", and `rho0_D` from
      the first row is reused as the baseline for all rows with no per-row equality receipt.
      **[OPEN]**.

- [x] **P1-3 numerical-range probe mixes CPU and device tensors.** `torch::eye` / `torch::zeros`
      / `linalg_eigh` outputs are CPU (`:2182`, `:2185`, `:3925`) while `Vm` keeps its device.
      Fails on CUDA/MPS; CI is pinned CPU so it cannot catch this. **[OPEN]**.

- [x] **P1-4 stage provenance is printed, not fail-closed.** `signals_from_stage` is emitted
      (`tile_unified_impl.cpp:9204`) but a mismatch still yields a category. Explicit ARK stages
      also need their own categories (`explicit_rhs_not_finite`, etc.) instead of collapsing to
      `insufficient_evidence`. **[OPEN]**.

- [x] **P1-5 `InitialConverged` receipts.** Two parts. (a) `initial_rel_error` not set on that
      return — **[DONE]**, fixed in PR #177 (`:1306`), and the review independently reached the
      same finding. (b) `gmres_tolerance_reached` does not count `InitialConverged`, which *did*
      satisfy the tolerance — **[OPEN]** (`:9280`).

- [x] **P1-6 the digest is a collision-prone scalar.** `‖x‖₂ + 7Σxᵢ + 13Σi·xᵢ`; the review supplies
      an explicit collision. **[PARTIAL]** — round 5 fixed the *conversion* (double→unsigned was
      UB and truncating; now bit-compared), but the scalar itself is unchanged. **Remaining**: an
      index-aware wide hash including shape/dtype/device.

---

## Science / scope items

- [x] **§14 warm-start-off experiment.** **[DONE]** — measured in PR #177: from x₀ = 0 the identity
      arm does *worse* (ρ_D 0.885 at j=8 vs 0.568 warm-started), so the warm start is refuted as a
      contributor rather than confirmed as one.
- [x] **§14 extended ladder j = 96, 192.** **[DONE]** — measured: ρ_D M/I 1.99 (48) → 2.01 (96) →
      1.53 (192); the identity plateaus at ρ_D ≈ 0.32 while M still descends. "Identity beats M" is
      therefore an **operational** claim at the production budget, which the review asks for.
- [ ] **§13 generalized numerical range in a physical energy metric** `W_H(A) = {xᵀHAx / xᵀHx}`.
      **[OPEN]** — the current witness is for `S⁻¹AS` / `D⁻¹S⁻¹AS`, and numerical range is not
      similarity-invariant, so raw physical `A` remains unestablished. Agreed and not claimed.
- [x] **§14 scope of the preconditioner conclusion.** **[DONE]** — PR #177 already states it as
      operational-at-the-production-budget with the crossing signature named.

---

## Items PR #177 closed that this review predates

Recorded so the two documents can be reconciled rather than re-litigated:

- ρ_S / ρ_phys / ρ_D separation, live `ab_attributable`, warm-start divergence baseline, per-solve
  stats reset, direct Rayleigh-quotient witness — the review's §2 credits; all still hold.
- **Three further P0s the review predates**: the `_vs_r0` field that laundered ‖r‖/‖b‖ via a
  fallback reference of 1.0; the max over solves that scored a zero-work `InitialConverged` as a
  total stall; and `linearity_residual = 0` as a **tautology** at a dyadic α.
- The np-equivalence emitter's `published` literal, and the probe's unrestored JVP counters and
  `*variable_pc_event`.


---

## Resolution — measured, `65ccc19`..HEAD

All P0 and P1 items are closed except the two stated below. Verified live on `em_b_wave` at dt=600,
one A/B firing with every precondition met:

```
ab_valid=1 ab_reason=ok   msel_valid=1 msel_reason=ok
operator_state_unchanged=1 precond_state_unchanged=1
counters_restored=1 pc_event_moved=0 probe_noninterfering=1
worst_order_delta=0 worst_order_metric=none discrete_order_invariant=1
d_same_across_rows=1 d_all_valid=1 b_digests_agree=1 x0_digests_agree=1
arm=I j=48 ru_rho=0.1735 rv_rho=0.9936 rw_rho=0.2385 ...   (reproduces digit for digit)
```

**P0-1/P0-2 — freshness replaced by measurement.** Both claims were hardcoded `true` and neither
was true as named. `make_fresh_M()` copies the mutable *wrapper*; the `UnifiedPreconditioner`
underneath is one instance for every arm. And sharing one `gmres_op` closure is the *correct* A/B
design — the arms must differ only in M — so "fresh operator per arm" was not merely hardcoded, it
was the wrong requirement. The contract now reads `same_frozen_operator` + `fresh_wrapper_per_arm`
+ `shared_preconditioner_instance` (stated, not hidden) + **`operator_state_unchanged`** and
**`preconditioner_state_unchanged`**, the last two measured as *behavioural fingerprints*: apply
each shared object to one fixed probe vector before and after the ladder and compare bit-exactly.
A state change that could alter a result is caught; one that could not is ignored — the right
sensitivity for this question, and strictly stronger evidence than the naming it replaces.

**P0-3 — order invariance is metric-complete.** It compared ρ_S alone while the headline is ρ_D,
ρ_phys, Msel and per-block. Now every metric is compared forward-vs-reverse for the same `(arm, j)`
— ρ_S, ρ_phys, ρ_D within tolerance, and **iterations, termination reason, `d_weighted`,
`msel_engaged`, A-apply count and the input digests bit-identical** — with the worst offender named
on the record (`worst_order_metric`). The Msel conclusion is a **separate verdict**
(`msel_attributable`): M-vs-I does not depend on the projection having engaged, and the Msel claim
does.

**P1-1** `msel_engaged` is now reset per row (a shared_ptr that an early-exit row inherited) and
consumed by `msel_valid`. **P1-2** every arm's `D` is digested and must agree, and a `D` the config
*requested* but did not receive is a **transfer failure**, not "D = I". **P1-3** the numerical-range
probe builds `eye`/`zeros` with `Vm.options()` and keeps the tiny eigenproblem on the CPU, moving
only the eigenvector — it threw on CUDA/MPS and CI's pinned CPU Torch could not catch it.
**P1-4** stage provenance now *fails closed* (`stage_signal_mismatch`) instead of printing beside a
confident wrong layer, and explicit ARK stages have their own categories
(`explicit_rhs_not_finite`, `explicit_admissibility_rejected`, `explicit_publish_rejected`) rather
than collapsing into `insufficient_evidence`. **P1-5** `InitialConverged` now counts toward
`gmres_tolerance_reached` — it *did* satisfy the tolerance, and excluding it undercounted finished
solves precisely on the iterations where Newton was closest. **P1-6** the digest is an index-aware
64-bit hash over the bytes with an avalanche step, including shape/dtype/device; the review's
explicit collision `(1,−2,1,0)` vs `(0,1,−2,1)` separates under it, and no number of extra signed
projections could have fixed the old three-moment scalar.

**A defect this batch introduced and caught.** The first version of the fingerprint took its
"after" applies *after* the counter restores, so the interference check reported
`probe_interfered=1` **against the fingerprint itself**. Fixing the order also exposed that
restore-then-compare made two clauses vacuous. The check is now split into two honest claims:
**`noninterfering`** — state that must not have moved at all (fallback counts, the shared pc event)
— and **`counters_restored`** — state that moves by design and must be put back. A restore makes
the *run* safe; it does not make the arms comparable.

### Still open, stated rather than claimed

- **P0-4 remainder (review Phase 4) — MEASURED, and it closes.** See below.
- **§13 energy-metric numerical range.** The witness is for `S⁻¹AS` / `D⁻¹S⁻¹AS`, and numerical
  range is not similarity-invariant, so indefiniteness of the raw physical `A` remains
  **unestablished** — as the review says. `W_H(A) = {xᵀHAx / xᵀHx}` under a physical energy metric
  is the measurement that would settle it, and it is not claimed here.


---

## P0-4 Phase 4 — probe OFF vs ON on the production trajectory, with a control

The review's contract for non-interference is not a counter but the run itself: the production
Newton residual sequence, call sequences, termination sequence, outcome and published state must be
the same with the probe on as with it off. Measured on `em_b_wave` at dt=600, 12-iteration budget,
taking every production line the solver emits and excluding only the probe's own records:

```
probe OFF: 39 trajectory lines
probe ON : 39 trajectory lines
diff      : 4 lines, ALL of the form
            [Newton] JVP calls: 8, total JVP time: 480 ms, avg: 60 ms/call
            [Newton] JVP calls: 8, total JVP time: 474.7 ms, avg: 59.34 ms/call
first-failure record: identical in every field --
  category=krylov_stagnated newton_iters=4 krylov_iters=28 steps_accepted=3 steps_rejected=1
  gmres_tolerance_reached=0 worst_krylov_rel_vs_r0=0.9941
```

**The JVP call counts are identical** (8, 9, 11, 11 in both), which is the direct evidence that the
probe's thousands of matvecs no longer leak into production's counters. The only differing numbers
are wall-clock milliseconds.

**And the control, because a difference is not attributable without one.** Two runs of the *same*
configuration — probe OFF both times:

```
diff: the SAME 4 lines, the same fields
      total JVP time: 446.3 ms  vs  451.9 ms
      total JVP time: 476.4 ms  vs  497 ms
```

Run-to-run wall-clock variation of the same binary, in exactly the lines that separated OFF from
ON. With wall-clock masked, **probe OFF vs probe ON is IDENTICAL** across all 39 lines.

So the honest statement is now the strong one the review asked for and the earlier wording could
not support: *the probe does not change the production run* — measured on the trajectory, not
inferred from a counter, and with a same-configuration baseline establishing what "identical" means
for this binary. What remains outside this evidence is the published-state **bytes**, which at
dt=600 do not exist: the step aborts and publishes `U_n` back to itself, so there is no advanced
state to compare. That gap closes when a step completes, not before.
