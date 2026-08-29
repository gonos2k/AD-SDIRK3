# R13.26 — one lifecycle receipt, stamped where the decision happens

**Baseline** `main@18d70ad` (PR #185 merged) · **Source** external deep review of R13.25 ·
**Independent review: NOT RUN** (`/code-review ultra` is user-triggered; quota exhausted). CI green
is not offered as a substitute.

R13.25 separated signal from outcome **as types**. The review shows the producers, the statistics
and the classifier do not yet consume that separation — and that my own Phase 2 introduced a
regression in the recovery gate.

---

## Verification of the review's premises

Re-read in the tree at `bc7c009` before being accepted. **All seven hold.**

| # | Claim | Verified? | Evidence |
|---|---|---|---|
| **§3 P1-high** | the canonical-S recovery path bypasses `fallback_accept_ratio` | **YES — my own regression** | `:11809` `canonical_ok = rec_s_ok ? s_merit_improved : legacy` — the S arm is a *strict decrease*, so a configured `trust_fallback_ratio` is ignored whenever the S merit exists, while the log still prints `ratio_gate=` |
| §3.3 | an *admission* predicate became a *state-mutating acceptance* predicate | **YES** | `candidate_arbitration_rescues` grants entry to a trial at the arbitration site; at `:11828` its result sets `step_accepted = true` directly |
| **§4.1** | an ordinary trust rejection records `ZeroUpdate` | **YES** | `:12406` derives the rejected branch from `arbitration_admitted` alone |
| **§4.2/4.3** | `RejectedRecovery` and `Vetoed` have no producer | **YES** | both appear **only** in the counter comparison at `:12743-12744`; zero assignments |
| §4.4 | `globalization_rejections` therefore undercounts | **YES** | it counts exactly the two outcomes above, one of which is never produced |
| **§5** | the classifier consumes a *resolved* signal as stagnation evidence | **YES** | `:1561` reads `linear_total_failure_signals`, which increments on the signal regardless of the outcome that overruled it |
| §6 | `EntryMetricMismatch` means two different things | **YES** | the enum says "stop metric met, S tolerance not"; the producer at `:11591` requires *additionally* that the nonlinear check failed |
| **§7** | completeness passes a one-sided stamp | **YES** | `:789` `if (receipt_iter >= 0 && exit_iter >= 0 && ...)` — `exit_iter = -1` skips the test |
| §7.1 | **my fixture cannot fail** | **YES** | `check(!complete(v) \|\| v.exit_iter < 0)` with `v.exit_iter = -1` makes the right disjunct always true |
| §8 | the lint accepts a call whose result is unused | **YES** | `candidate_disposition`'s result is stored and never read; the lint sees the call |

---

## Checklist

### Phase 1 — §3: restore the recovery ratio gate (P1-high, mine)

- [x] **1.1** The canonical-S recovery acceptance must apply `fallback_accept_ratio`, not a bare
      strict decrease. A configured 0.5 must mean 0.5.
- [x] **1.2** Fail closed when the S merit is unavailable **or** the halo mask was required and
      missing — do not fall back to the raw predicate implicitly.
- [x] **1.3** Fixtures: a 0.999 ratio under a 0.98 gate is REJECTED; 0.97 is accepted; the config
      value demonstrably changes the verdict.

### Phase 2 — §4: stamp the outcome at each decision site

- [x] **2.1** Trust rejection stamps `RejectedTrust` whether or not the arbitration admitted it.
- [x] **2.2** Recovery stamps `AcceptedRecovery` / `RejectedRecovery` — the latter currently has no
      producer at all.
- [x] **2.3** `ZeroUpdate` narrows to "no globalizer evaluated the candidate".
- [x] **2.4** `globalization_rejections` then counts what its name says.

### Phase 3 — §5: the classifier must read an UNRESOLVED failure

- [x] **3.1** Add `unresolved_linear_failures` (signal raised AND not overruled by any acceptance)
      and make the legacy classifier branch read it.
- [x] **3.2** Keep the signal count as telemetry.
- [x] **3.3** Fixtures: a resolved signal is NOT stagnation evidence; an unresolved one is.

### Phase 4 — §6: one meaning per signal

- [x] **4.1** `EntryMetricMismatch` becomes the SIGNAL (option A): raised on
      `gmres_objective_mismatch_on_entry`, with acceptance/rejection carried by the outcome.

### Phase 5 — §7: completeness rejects a one-sided stamp

- [x] **5.1** Both stamped or neither; a mismatch is incomplete.
- [x] **5.2** Replace the fixture that cannot fail with one that can.

### Phase 6 — §8: the lint must see an unused result

- [x] **6.1** `[[nodiscard]]` on the decision rules, so a call whose result is dropped is a
      compiler diagnostic rather than something the lint counts as consumption.

---

## Deferred, with reasons

**The Armijo state machine (§9).** The review's own conclusion is to keep it forced OFF. Wiring the
helper into a dead block does not repair the apply path (`alpha` stays 1 and is applied;
`accepted_residual` keeps the α=1 value). A `LineSearchResult` rebuild is its own increment.

**§10's single `CandidateLifecycle` struct.** Phases 2–4 give each field a real producer first;
collapsing them into one receipt afterwards is a refactor with no behaviour to verify, and doing it
before the producers are right would just move the same gaps.

---

## Progress

**Phase 1 — the recovery gate, and it was my regression.** R13.25 routed recovery acceptance
through `candidate_arbitration_rescues`, which asks only `after < before`. That helper is an
**admission** rule — passing it grants entry to a trust trial that judges again — and reusing it at
a site that sets `step_accepted = true` turned an entry ticket into a final verdict. So
`fallback_accept_ratio` stopped being consulted whenever the S merit existed: a one-ULP improvement
was adopted **while the log still printed `ratio_gate=0.98`**. `recovery_step_is_acceptable()`
applies the configured ratio and fails closed on a missing merit or an unusable halo mask; `ru` is
an AND guard inside the rule rather than a separate `&&` a future edit can drop.

**Phase 2 — the outcome is stamped where the decision happens.** R13.25 inferred the rejected
branch from `arbitration_admitted` alone, so an ordinary trust rejection recorded `ZeroUpdate`, and
`RejectedRecovery` and `Vetoed` had **no producer at all** — they existed only in the counter
comparison. `trust_attempted` and `recovery_attempted` are set where those mechanisms actually run,
`ZeroUpdate` now means "no globalizer evaluated the candidate", and `globalization_rejections`
counts what its name says.

**Phase 3 — the classifier reads an UNRESOLVED failure.** The legacy branch read the signal
*count*, which increments whether or not a globalizer went on to accept the step — so an iteration
whose recovery step was accepted (residual down, state advanced) could still make the stage report
`KrylovStagnated`, contradicting this file's own lifecycle rule. `unresolved_linear_failures` is
the evidence; the raw count stays as telemetry; older records fall back through both.

**Phase 4 — one meaning per signal.** `EntryMetricMismatch` was raised on
`entry_mismatch_step_rejected` — the mismatch **and** the nonlinear check failing — so a mismatch
that the check then accepted was recorded as `None`. It now tracks
`gmres_objective_mismatch_on_entry`; acceptance or rejection is the outcome's job.

**Phase 5 — completeness rejects a one-sided stamp, and my fixture could not fail.** The guard
required *both* stamps present before comparing, so `exit_iter = -1` skipped it entirely. And the
pin I wrote — `check(!complete(v) || v.exit_iter < 0)` with `v.exit_iter = -1` — had an always-true
right disjunct: the rule could return anything. Both fixed, with the mirror case added.

**Three existing fixtures broke when the rule tightened**, all through
`give_complete_exit_receipt()` — a helper *named* for completeness that never stamped
`newton_exit_event_iter`. The rule working, not the fixtures being wrong.

**Phase 6 — `[[nodiscard]]` on the decision rules**, so a call whose result is dropped is a
compiler diagnostic rather than something the lint counts as consumption.

**And the ratchet I added last round caught my own edit.** Adding `[[nodiscard]]` made the lint's
pattern stop matching seven rules — `40 → 33` — and the coverage ratchet failed the gate. Not a
deletion: the rules were still there and would have been **silently exempt**. That is the failure
mode the ratchet exists for, firing on the person who wrote it. The pattern now tolerates
attributes; 42 rules, all consumed.

**Measured at dt=600**: every `SDIRK3_*` telemetry line is **byte-identical**. ctest 62/62;
ratchets green; fixtures 220 → **231**. **Independent review: NOT RUN.**

## Deferred, with reasons

**The Armijo state machine (§9).** The review's own conclusion is to keep it forced OFF. Wiring the
helper into a dead block does not repair the apply path — `alpha` stays 1 and is applied, and
`accepted_residual` keeps the α=1 value — so a `LineSearchResult` rebuild is its own increment.

**§10's single `CandidateLifecycle` struct.** Phases 2–4 gave each field a real producer first;
collapsing them into one receipt is a refactor with no behaviour to verify, and doing it before the
producers were right would have moved the same gaps into a new shape.

---

## Self-review — the counter was fixed and the runtime flag was not

Phase 3 made the classifier read `unresolved_linear_failures`. But `gmres_total_failure` is raised
at `:11610`, **before** the recovery site at `:11856`, and a successful recovery never cleared it.
So a recovery that took the step — residual down, state advanced, `step_accepted = true` — still
hit the **unconditional** counter at `:12747` (`gmres_total_failures++`) and still read as a
failure to every later consumer of the flag. I fixed the classifier's *counter* while the runtime
*flag* kept the old answer: the producer/consumer split again, one field over, in the increment
whose title is about exactly that.

The flag is now cleared where recovery overrules the signal; `gmres_total_failure_candidate` is
deliberately untouched, because whether the *linear* solve signalled is a property of that solve
and the signal statistics read it. `runtime_failure_flag_consistent()` states the invariant — an
accepted step and a standing failure flag cannot both be true — production evaluates it and speaks
only on divergence, and four fixtures pin it including the negative direction (a refused or vetoed
candidate must *keep* the flag).

**What this run can and cannot show.** dt=600 telemetry is byte-identical and
`SDIRK3_LIFECYCLE_FLAG_MISMATCH` fires 0 times — but so does `recovered by fallback`: the recovery
path does not execute in this configuration, so the contract's *firing* was not measured here.
The fixtures pin the rule; the production check is the net for a path this run does not reach.

ctest 62/62; ratchets green; fixtures 231 → **235**. **Independent review: NOT RUN.**
