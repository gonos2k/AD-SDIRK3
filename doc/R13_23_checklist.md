# R13.23 — candidate-merit arbitration, receipt authority, and runtime provenance

**Baseline** `c63927f` (PR #181 merged) · **Source** deep review of R13.21 ·
**Independent review: NOT RUN.**

Premises verified in the tree before being accepted. One of them **retracts a claim I merged**.

---

## Verification of the review's premises

| # | Claim | Verified? | Evidence |
|---|---|---|---|
| **§8** | C++ default, Registry default and effective runtime disagree on `nk_trust_region` | **YES — and it retracts my own merged claim** | `wrf_sdirk3_config.h:880` = `true`; `Registry.EM_SDIRK3_OPTIMIZATIONS:60` = **`.false.`**; `em_b_wave` does not set it; the live log records `nk_trust_region = false` and `[TRUST OFF]` |
| P0-1 | total-failure is a hard veto — the candidate is discarded with no nonlinear trial | **YES** | `gmres_total_failure = true` at `:11331`; the trust loop at `:11492` carries `!gmres_total_failure` |
| P0-1b | changing the denominator does not fix it — both rules flagged the live candidate | **PARTLY — scope matters** | true on the **default** budget (`vs_r0 = 0.9941 ≥ 0.99`); **false** at `stage2_restart=192`, where R13.22 measured `vs_r0 = 0.6773`, the r₀ rule not firing, and stage 3 going 8.7 % → 66.6 % |
| P0-2 | the terminal probe measures raw packed L₂, not the production merit | **YES** | probe uses `R.norm()`; trust/stage metrics are S-weighted / WRMS / gate |
| P0-3 | the `dK` the probe evaluates may not be the `dK` GMRES produced | **YES structurally; delta measured 0 here** | post-solve halo/direct-U/clamp stages exist between them |
| P0-4 | `exit_receipt_complete` is emitted but does not gate attribution; `Unknown` metric passes | **YES** | `krylov_exit_attribution_of` never consults it; `krylov_receipt_complete` tests `stopping_metric >= 0` and `Unknown == 0` |
| P0-5 | the NaN early return pairs `x = 0` with the *current* residual | **YES** | the return builds `zeros_like(x0)` while `r_true` is the live residual; R13.21 then stamped `rho = 1.0` on top |
| §12 | `state_mutated=0` overclaims | **YES** | the probe calls `apply_jacobian` and `compute_rhs`; `NoGradGuard` bounds the graph, not caches or counters |

---

## Checklist

### Phase 0 — the retraction (do first; it changes what P0-1 means)

- [x] **0.1** Correct `doc/R13_21_checklist.md` and the code comment at `newton_solver.cpp:11296`:
      the trust-off path is **live in `em_b_wave`**, not latent.
- [x] **0.2** Record the lesson: I read the C++ struct default and **inferred** the effective value
      instead of reading the log I already had — the `read-the-control-flow-dont-infer-it` class,
      applied to configuration.

### Phase 1 — P0-1: no hard veto without a merit trial

- [ ] **1.1** One canonical candidate-merit evaluator, used by trust, recovery, and the probes.
- [ ] **1.2** A total-failure signal routes the candidate to globalization when the merit improves,
      instead of discarding it unevaluated. Keep the signal as a *warning*, not a veto.
- [ ] **1.3** Fixtures: `TotalFailureCandidate_WithNonlinearDecrease_IsGlobalized`,
      `TotalFailureVsBAndVsR0_CannotSkipMeritTrial`.

### Phase 2 — P0-2: measure the merit the production path uses

- [x] **2.1** Verify what the trust region and the stage gate actually minimise.
- [x] **2.2** Terminal probe v2 reports raw and S-weighted, with `gate_metric_evaluated=0` stated
      rather than implied. WRMS/worst-block deferred — see the note below.
- [x] **2.3** Correct every place that says the discarded candidate "reduces the merit function" —
      what was measured is raw packed L₂.

### Phase 3 — P0-3: candidate provenance

- [x] **3.1** Verify whether post-solve processing can change `dK` between the solve and the probe.
- [x] **3.2** Emit `candidate_source`, digests, and the relative delta; only call it "the GMRES
      candidate" when that delta is 0.

### Phase 4 — P0-4: receipt authority

- [x] **4.1** `Unknown` stopping metric fails the completeness rule.
- [x] **4.2** Add the consistency conditions: reached flags vs ρ/tolerance, `spent ≤ allowed`,
      receipt iteration == exit iteration.
- [x] **4.3** `krylov_exit_attribution_of` returns *unavailable* on an incomplete receipt.
- [x] **4.4** Fixtures: `IncompleteExitReceipt_CannotAttribute`, `UnknownStoppingMetric_FailsClosed`,
      `ReachedFlagsMustMatchRhoAndTolerance`.

### Phase 5 — P0-5: NaN return consistency

- [x] **5.1** Return a solution and a residual that belong together.
- [x] **5.2** Stop stamping `rho = 1.0`; fail closed with sentinels when nothing was measured.
- [x] **5.3** Fixtures: `NanRetry_ReturnedXMatchesReturnedResidual`, `NanRetry_DoesNotFabricateRhoOne`.

### Phase 6 — provenance and honesty of the probes

- [x] **6.1** Rename `state_mutated` → `K_mutated`; do not claim non-interference that was not
      measured.
- [ ] **6.2** Runtime provenance manifest: compiled default, Registry default, namelist request,
      effective value, authority — for the knobs that change control flow.
- [ ] **6.3** Boundary receipt after the setter runs (`raw_*` vs `effective_*`).

---

## Accepted without argument

The NO-GO list for `dt=600` forward completion, full-step tangent/adjoint, exact 4D-Var and MPI
production — none of those was claimed. The stage-3 A/B fail-close is correct and stays.


---

## Progress

**Phase 0 — the retraction.** Three authorities disagree on `nk_trust_region`: C++ `true`, Registry
**`.false.`**, `em_b_wave` unset, effective runtime **`false`** (`[TRUST OFF]` in the live log). My
R13.21 note said the shipped configuration never takes that path — wrong, and corrected in the code
comment and the R13.21 doc. **P0-1 was live.** I read a struct default and inferred the effective
value instead of reading the log already on disk.

**Phase 2 — and it broke my own headline.** The trust region minimises `‖S⁻¹R‖` — its own comment
says so. The discarded candidates measured in that norm:

| candidate | raw L₂ | **S-weighted** |
|---|---|---|
| stage 2, iter 3 | −12.5 % | **+2.5 %** |
| stage 3, iter 1 | −60 % | **46× worse** |

Both are descent directions **only in the raw L₂**. R13.22's "the discard rule threw away usable
steps" is withdrawn; by the production merit both discards are defensible. One canonical evaluator
now reports both norms at both probes, and `gate_metric_evaluated=0` says plainly that the stage
gate metric was **not** evaluated — inventing it at a candidate would be the fabrication this work
is closing elsewhere. WRMS and worst-block are deferred rather than guessed: the stage weights are
a stage-level object and wiring them into a candidate probe is a separate change.

**Phase 4 — the receipt now earns the attribution.** `Unknown` (= 0) passed a `>= 0` test, so an
unstamped metric read as complete. The rule now rejects it, plus `spent > allowed`, reached flags
that contradict ρ against the tolerance, a tolerance never applied but claimed reached, and a
receipt stamped for another iteration. `krylov_exit_attribution_of` is gated on it, and the emitter
uses the **same** view so the flag and the gate cannot judge different things.

**Three fixtures failed when the gate went in** — they were asserting a subtype from a receipt
carrying only the two reached flags. That is the rule working; they now supply complete receipts,
and a new fixture pins that an incomplete one yields **no** subtype.

**Phase 6.1** — `state_mutated` → `K_mutated`. The probe calls `apply_jacobian` and `compute_rhs`;
`NoGradGuard` bounds the graph, not caches, counters or latches.

**Live record**: `exit_receipt_complete=1`, `exit_attribution=none`, `category=zero_update_after_total_failure`.

**Phase 5 — the NaN return now hands back one solve.** It returned `x = 0` beside the *current
iterate's* residual, so `r_true ≠ b − A(x)` and one result carried two solves — and downstream that
residual feeds trust prediction, per-block analysis and the exit receipt, none of which correspond
to the `x` handed back. Option B: keep the zero solution (which is what `NanRetryExhausted` means)
and return the residual that belongs to it, `b` — exactly what this file already writes at `:1154`
and `:2592` for the same situation. **A consequence worth naming: with `x = 0` the ratios ρ_D =
ρ_S = 1 are now arithmetically true**, where R13.21 stamped `1.0` on top of a mismatched pair and
the review correctly called that fabricated. A pure rule pins it, with the pre-R13.23 pairing as
the negative case.

**Phase 3 — provenance measured, not argued.** The tree's own comment names two post-solve
mutations of `dK` (halo zeroing and the direct-U override of the `ru` block), so what a probe
evaluates need not be what GMRES returned. Captured after the S-scaling — a coordinate change, not
a different candidate — and before those mutations, only when a probe is armed. **Measured on the
live record: `candidate_delta_vs_solve = 0` at both probes**, so on this configuration the probed
candidate *is* the solve's. The concern is a real path that did not fire here, and the record now
says so instead of the question going unasked.
