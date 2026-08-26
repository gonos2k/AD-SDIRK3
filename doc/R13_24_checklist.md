# R13.24 — one veto, one merit, one disposition

**Baseline** PR #183 (head at review time `1fdd792`; the branch has since advanced to `986f7f1`) ·
**Source** external deep review of R13.23 / PR #183 ·
**Independent review: NOT RUN** (`/code-review ultra` is user-triggered; quota exhausted). CI green
is not offered as a substitute.

The headline finding **retracts my own writeup**, and it is the campaign's signature defect one
level deeper than where I fixed it.

---

## Verification of the review's premises

Every premise re-read in the tree before being accepted.

| # | Claim | Verified? | Evidence |
|---|---|---|---|
| **P0-1** | trust acceptance still reads the ORIGINAL veto, so a rescued candidate can never be accepted | **YES — and it refutes my own text** | `newton_solver.cpp:12049` `else if (gmres_total_failure_candidate)` → `:12053 accept_step = false`, unconditional. `arbitration_rescued` is not consulted there, and the candidate flag is `const` |
| P0-2 | `nk_line_search=true` with no reachable consumer is a config-contract violation | **YES** | already measured in R13.23: the Armijo guard is a closure over both values of `step_accepted` |
| P0-3 | the entry objective-mismatch acceptance uses raw L₂ | **YES** | `:11397-11400` `base_norm = R.norm()`, `trial_norm = R_trial.norm()`, `accept_full_step = trial_norm < base_norm` |
| P1-1 | `candidate_merits` and the trust merit are different quantities (halo mask) | **YES** | `candidate_merits` (`:5749`) computes `(S_inv_diag_ * R).norm()` with **no** mask; the trust merit applies `halo_mask_` before its norm |
| P1-2 | the arbitration is a full-step pre-veto, so a good `α<1` step is never seen | **YES, by construction** | it evaluates `K + dK` only |
| P1-3 | the receipt-vs-exit iteration comparison is an identity in production | **YES** | `first_failure.h:969-970` fill `receipt_iter` and `exit_iter` from the same `s.exit_krylov_iter` |
| P1-4 | the NaN return synthesises ρ_D from ρ_S | **YES** | `:1579/:1581` both take `initial_rel_error_gmres`, while the normal path (`:2524-2525`) uses two different sources |
| §4.3 | an admitted-but-rejected candidate is miscounted as a non-total-failure | **YES** | the statistics read `gmres_total_failure`, which the arbitration clears |

**One qualification, stated precisely.** The review calls P0-3 *live*. The **path** is indeed the
shipped one (`nk_trust_region` is effectively false). The **branch** additionally requires
`gmres_objective_mismatch_on_entry`, and at dt=600 with `debug_level=1` its message
(`"entry met the stop metric but not S"`) prints **0 times** — so it is reachable-but-unexercised
in the measured configuration, not observed. Recording it this way because R13.23 Phase 0 was a
retraction for calling a live path latent; the discipline cuts both ways.

---

## Checklist

### Phase 1 — P0-1: one veto, one disposition (mine to fix)

- [x] **1.1** `arbitration_rescued` → `arbitration_admitted`. "Rescued" claims an outcome the
      mechanism cannot deliver; admission to a trial is what it actually does.
- [x] **1.2** A single `effective_total_failure_veto` consumed by **every** downstream reader —
      the `gmres_total_failure` predicate *and* trust acceptance.
- [x] **1.3** Statistics: an admitted candidate that trust then rejects must still count as a
      linear total-failure **signal**, not as a non-total-failure.
- [x] **1.4** The termination category must not silently change from `ZeroUpdateAfterTotalFailure`
      to a stall because the veto was cleared.
- [x] **1.5** Fixtures: `ArbitrationAdmission_ClearsTrustAcceptanceVeto`,
      `ArbitrationTrustReject_RemainsTypedTotalFailureSignal`,
      `ArbitrationReject_DoesNotBecomeNonTotalFailure`, and the end-to-end composition
      admission → loop entry → acceptance → `step_accepted`.

### Phase 2 — P0-3: the entry mismatch must judge in the merit that flagged it

- [x] **2.1** An objective mismatch is *by definition* an S-coordinate disagreement; accepting it
      on raw L₂ answers a different question. Use the canonical merit.
- [x] **2.2** Measure at dt=600 before and after — the branch is unexercised there, so the
      expectation is byte-identical telemetry. Confirm rather than assume.

### Phase 3 — P1-1: one canonical merit evaluator

- [x] **3.1** `candidate_merits` must apply the **same halo mask** the trust merit applies, or
      report that it could not and refuse to be compared.
- [x] **3.2** Carry the mask state in the receipt (`halo_masked`) so a merit that skipped it cannot
      be read as the trust merit.
- [x] **3.3** Fixtures: `CandidateMerit_MatchesTrustMerit_WithHaloMask`.

### Phase 4 — P0-2: the line-search knob must fail closed

- [x] **4.1** Reporting `effective=true` beside "no reachable consumer" is honest telemetry and a
      broken feature contract. Force the effective value to false and mark it unsupported.
- [x] **4.2** Fixtures: `LineSearchEnabled_HasReachableConsumer` — as a *negative* pin until the
      state machine is rebuilt.

### Phase 5 — P1-3: an independent exit iteration

- [x] **5.1** Split `exit_krylov_iter` from `newton_exit_event_iter` so the completeness rule
      compares two real authorities instead of a field with itself.
- [x] **5.2** Fixture: the production view can actually produce a mismatch.

### Phase 6 — P1-4: NaN metric provenance

- [x] **6.1** Stop copying `initial_rel_error_gmres` into `rho_D_initial`; fail closed with a
      sentinel when the D coordinate was never measured.

### Phase 7 — the line-search landmines (§11), pinned not opened

- [x] **7.1** Pin as fixtures, without reopening the path: budget exhaustion leaves `alpha = 1`;
      max-iterations uses the last α regardless of Armijo; `accepted_residual` is not updated to
      the α actually applied.

---

## Deliberately NOT done, and why

**P1-2 (arbitration should send a scaled trial, not a full step).** Correct in principle. It is a
redesign of the admission mechanism, not a repair, and it changes what the solver evaluates. It
belongs after Phase 1 makes admission mean something.

**Reopening the line search.** §11 shows three defects that fire the moment the guard is flipped.
Pinning them is the safe move; rebuilding the state machine is its own increment.

---

## Progress

**Phase 1 — one veto (P0-1), and it was mine.** R13.23 cleared the DERIVED flag
`gmres_total_failure` on admission and concluded the candidate was "handed to the globalizer to
accept or reject on its own terms". Trust acceptance reads the ORIGINAL
`gmres_total_failure_candidate` — `const`, never touched by the arbitration — and rejects on it
unconditionally at `:12049`. So an admitted candidate entered the loop and lost every attempt to
the signal its admission had just reconsidered. **Admission, not rescue**, exactly as the review
says.

That is this campaign's signature defect committed one level below where R13.23 fixed it: producer
and first consumer agreed while a *later* consumer still read the old quantity. The correction is
not another flag but `effective_total_failure_veto()`, which every consumer calls — and the name
`arbitration_rescued` became `arbitration_admitted`, because "rescued" asserted an outcome the
mechanism cannot deliver.

**Not every reader was converted, and that mattered.** Three sites keep reading the raw candidate
deliberately: `:11321` (first-Krylov-failure statistic), `:11353` (the shortcut's skip branch,
which runs *before* the arbitration exists) and `:11573/:11579/:11586` (stage-specific probes on
the linear signal). Those ask "did the linear solve fail?", which no globalizer's later decision
can change. Converting them wholesale would have replaced one conflation with another.

**Statistics (§4.3).** An admitted candidate silently moved from `gmres_total_failures` to
`gmres_non_total_failures` — the linear signal vanishing from the record because a nonlinear
mechanism was given a chance to look. `counts_as_linear_total_failure()` restores it.

**Phase 2 — the entry mismatch now judges in the coordinate that raised the objection (P0-3).** An
objective mismatch *is* an S-coordinate disagreement; accepting it because raw packed L₂ fell
answers a different question. R13.23 measured two candidates that fall in raw L₂ (−12.5 %, −60 %)
while the S merit worsens (+2.5 %, 46×) — precisely the shape this branch would have waved through.
Fail-closed without the S coordinate. Stated precisely: the **path** is the shipped one, but the
**branch** needs `gmres_objective_mismatch_on_entry`, whose message prints **0** times at dt=600
with `debug_level=1` — reachable-but-unexercised here, which is why the telemetry below is
unchanged.

**Phase 3 — one merit (P1-1).** `candidate_merits` computed `‖S⁻¹R‖` **unmasked** while trust
acceptance zeroes the halo first: two different numbers under one name, so a candidate could pass
one and fail the other. Now masked identically, with `s_halo_masked` travelling in the telemetry so
an unmasked value can never be read as the trust merit. Invisible at np=1; it would appear the
moment this runs multi-tile.

**Phase 4 — the line-search knob fails closed (P0-2).** Reporting `effective=true` beside "no
reachable consumer" was honest telemetry and still a broken feature contract. The solver option is
forced to `false` with a `[CONFIG UNSUPPORTED]` notice. **Not** a repair — §11's three defects fire
the moment the guard is flipped, and they are pinned instead (Phase 7).

**Phase 5 — the exit-iteration comparison is no longer an identity (P1-3).** The view filled
`receipt_iter` and `exit_iter` from the same `exit_krylov_iter`, so the completeness rule's
mismatch test could only ever fail in a fixture — the "verifying a state the producer cannot emit"
shape. `newton_exit_event_iter` is now stamped independently at the Newton exit site and wired
through `ConvergenceStats` → `StageFailureSignals`.

**Phase 6 — the NaN return stops synthesising ρ_D (P1-4).** It copied the S ratio into the D slot,
making them equal by construction; the normal path fills them from two different sources. A D
coordinate that was never measured is now the named sentinel `kMetricNotMeasured`.

**Phase 7 — §11 pinned, not opened.** Three defects would fire on a guard flip: budget exhaustion
leaves `alpha = 1.0` and applies the step Armijo just refused (budget is 5, the loop assumes 9
arms, so neither assignment site runs); max-iterations takes the last α regardless of Armijo; and a
successful backtrack never updates `accepted_residual`, so the ledger describes α=1 while α<1 was
applied. `line_search_alpha_is_trustworthy()` states the contract a rebuild must satisfy.

**Measured at dt=600.** Every `SDIRK3_*` telemetry line is **byte-identical** to the pre-R13.24
run, and the new `[CONFIG UNSUPPORTED]` notice fires once. The behaviour-affecting changes are all
on branches this configuration does not take, which is what the A/B confirms rather than assumes.

ctest 62/62; ratchets green; fixtures 193 → **206**. **Independent review: NOT RUN.**

## Deferred, with reasons

**P1-2 (send a scaled trial, not a full step).** Correct in principle: `m(1) ≥ m(0)` does not imply
`m(α) ≥ m(0)` for α < 1, so a full-step pre-veto can discard a direction that a trust-clamped step
would accept. It is a redesign of the admission mechanism rather than a repair, and it only becomes
meaningful now that admission actually reaches acceptance. Next increment.

**The line-search state machine.** Phase 4 closes the contract; rebuilding the search is its own
increment, with §11's three defects as its acceptance criteria.
