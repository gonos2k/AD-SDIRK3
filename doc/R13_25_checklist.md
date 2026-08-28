# R13.25 — the final lifecycle: a signal is not an outcome

**Baseline** `main@4f973b9` (PR #184 merged) · **Source** external deep review of R13.24 / PR #184 ·
**Independent review: NOT RUN** (`/code-review ultra` is user-triggered; quota exhausted). CI green
is not offered as a substitute.

R13.24 closed "the original veto re-rejects an admitted candidate inside trust". The review shows
the defect **moved one transition later**: the candidate is now admitted and genuinely evaluated,
and when trust refuses it, nothing updates the runtime failure state.

---

## Verification of the review's premises

Every premise re-read in the tree at `4f973b9` before being accepted. **All nine hold.**

| # | Claim | Verified? | Evidence |
|---|---|---|---|
| **P0-1** | after admission + trust rejection the runtime failure state is never recomputed | **YES** | `gmres_total_failure` has exactly two assignments — `:11529` (decl `false`) and `:11532` (`true`) — both **before** the trust loop; the zero-update break at `:12909` reads it |
| §4.2 | statistics and runtime therefore disagree on the same iteration | **YES** | the post-hoc correction moves the count to `gmres_total_failures` while the control flow still sees `false` |
| **§10** | `line_search_alpha_is_trustworthy` has no production consumer | **YES** | **0** occurrences in `newton_solver.cpp` — a rule with fixtures and no caller, the class I fixed for another rule in the same increment |
| §5 | recovery acceptance uses raw L₂ / raw `ru`, not the canonical merit | **YES** | `recovery_ratio` / `recovery_ru_ratio` are plain floats from unscaled norms; `S_inv_diag_` never enters |
| §6 | `gmres_total_failures` conflates a linear failure with an entry mismatch | **YES** | `:11530-11532` sets it on `effective_veto \|\| entry_mismatch_step_rejected` — so my comment "this counts whether the linear solve failed" is false for the second arm |
| §7 | the halo-mask state never reaches the admission decision | **YES** | `CandidateArbitration` carries only `s_merit_measured`, `s_before`, `s_after` |
| §8 | the exit-iteration fallback launders a missing stamp | **YES** | I wrote `exit_iter = (newton_exit_event_iter >= 0) ? ... : exit_krylov_iter` — the comparison then passes by construction |
| §9 | the manifest and the tile disagree on the same knob | **YES** | `[CONFIG AUTHORITY] nk_line_search effective=true` and `[CONFIG UNSUPPORTED] forcing effective=false` in one run |
| §11 | candidate and trust merits differ in reduction precision | **YES** | candidate `.to(kFloat64).norm()`; trust `guarded_item<float>(R_scaled.norm())` |

---

## Checklist

### Phase 1 — P0: the final lifecycle (a signal is not an outcome)

- [x] **1.1** `LinearSignal` and `TrialOutcome` as separate types; the existing
      `CandidateDisposition` describes admission only and must stop pretending to be a disposition.
- [x] **1.2** Recompute the runtime failure state **after** the trial: admitted-then-rejected must
      reach the typed exit, not a generic stall. Declare the policy explicitly (A: immediate typed
      exit).
- [x] **1.3** One lifecycle receipt read by statistics, Newton termination, the stage diagnostic
      and the classifier — no post-hoc counter correction.
- [x] **1.4** Solver-level transition fixtures (not pure-helper compositions).

### Phase 2 — P1-high: recovery must use the canonical merit

- [x] **2.1** Recovery acceptance judged by the canonical S merit; `ru` becomes an **AND** guard,
      never an **OR** escape.
- [x] **2.2** Measure at dt=600 — does the recovery path execute at all in this configuration?

### Phase 3 — §6: counters that mean one thing each

- [x] **3.1** Separate `linear_total_failure_signals` from `entry_metric_mismatch_events` and from
      globalization rejections; the classifier's legacy `gmres_total_failures > 0` branch must read
      the linear one.

### Phase 4 — §7: the mask state belongs in the decision

- [x] **4.1** `HaloMaskStatus { NotRequired, Applied, RequiredButUnavailable }` in the merit
      contract; admission fails closed on `RequiredButUnavailable`.

### Phase 5 — §8: stop laundering the missing stamp

- [x] **5.1** Pass `newton_exit_event_iter` through unchanged; a missing stamp leaves the receipt
      **incomplete** rather than satisfying the comparison with a copy.

### Phase 6 — §9: requested ≠ supported ≠ effective

- [x] **6.1** Three named values in the manifest instead of one `effective` that contradicts the
      tile.

### Phase 7 — §10: the rule I left without a consumer

- [x] **7.1** Wire `line_search_alpha_is_trustworthy` into the Armijo accept decision — without
      opening the guard — so reopening the path cannot bypass it.

### Phase 8 — §11: one reduction, not two

- [x] **8.1** Candidate and trust merits must agree in dtype and reduction, or the "canonical"
      claim is false at the boundary that decides admission.

---

## Deferred, with reasons

**§12 full-step pre-veto (send a scaled trial).** Still correct, still a redesign of the admission
mechanism rather than a repair. It becomes tractable once the lifecycle above gives admission a
real outcome to report.

---

## Progress

**Phase 1 — a signal is not an outcome (P0).** `gmres_total_failure` had exactly two assignments,
both **before** the trial, and was never recomputed. So an admitted candidate that trust refused
left it `false`: the zero-update break skipped its typed exit, the loop kept iterating on an
unchanged K, and the stage drifted into a generic stall — while a post-hoc counter correction
recorded the same iteration as a linear total failure. **One iteration, two contradictory records**,
differing in Newton iteration count, RHS/JVP calls, trust-radius trajectory, stage exit reason,
exit receipt and classifier verdict.

The cause was `CandidateDisposition` describing **admission** and being read as a **fate**.
`LinearSignal` and `TrialOutcome` are now separate types, and **Policy A is chosen explicitly**: the
linear solve signalled, one nonlinear mechanism was given a chance to overrule it and declined, so
the signal stands and the stage takes the typed exit. Reaching a generic stall because a boolean
happened to be false is the absence of a policy, not a policy. (Policy B — a bounded retry on a
changed radius — is a different design needing its own budget and exhaustion category.)

**Phase 3 — counters that mean one thing each.** R13.24's predicate fired on
`effective_veto || entry_mismatch_step_rejected`, so `gmres_total_failures` mixed a linear
residual-ratio failure with a **nonlinear** check failing after a solve that signalled nothing —
and the legacy classifier reads that counter as Krylov-stagnation evidence. My own comment claiming
it counts "did the linear solve fail" was false for the second arm. Three counters replace the
post-hoc arithmetic; the legacy pair is untouched so archived records stay comparable.

**Phase 2 — recovery uses the canonical merit.** This site **mutates the state** and accepted on
raw packed L₂, or via an **OR escape** on the raw `ru` block alone, while every other acceptance
had been moved to `‖S⁻¹R‖`. The repository has measured candidates that fall in raw L₂ (−12.5 %,
−60 %) while the S merit worsens (+2.5 %, 46×); nothing makes a recovery vector immune. `ru` is now
an **AND guard**, never an OR escape. Fail-closed: when the S merit is unavailable the legacy raw
predicate remains, so this cannot become *more* permissive than before.

**Phase 4 — the mask state reaches the decision.** R13.24 recorded `s_halo_masked` in a log line
while the admission contract carried only "was an S merit measured". A merit that needed the halo
mask and could not apply it was handed to the decision and read as the trust merit. The claim was
true of the prose and not of the code; `HaloMaskStatus::RequiredButUnavailable` now fails closed.

**Phase 5 — stop laundering the missing stamp.** My R13.24 fallback copied `exit_krylov_iter` into
`exit_iter` when the event stamp was absent. That does not "skip the comparison" — it makes the
comparison pass by construction, restoring the identity the split was made to remove. Both
authorities now pass through unchanged.

**Phase 6 — requested ≠ supported ≠ effective.** One run printed
`[CONFIG AUTHORITY] nk_line_search effective=true` and `[CONFIG UNSUPPORTED] forcing effective=false`.
`effective` was in fact the *requested* value. Three named values now:

```
[CONFIG AUTHORITY] nk_line_search compiled_default=false requested=true supported=false solver_effective=false
```

**Phase 7 — the rule I left without a consumer, again.** `line_search_alpha_is_trustworthy` had
fixtures and **zero** callers — the "rule with no consumer is a comment with a test" shape,
committed by me in the increment where I fixed it for another rule. It is now the Armijo accept
decision, so reopening the guard cannot bypass it, and the max-arms branch no longer adopts the
last α tried: exhausting the arms is a **reject**.

**Phase 8 — one reduction.** The candidate merit reduced in FP64 while trust acceptance takes the
norm in the tensor's own dtype. Same formula, different arithmetic, and at the strict
`after < before` boundary a roundoff-level disagreement decides differently — sharpest on the
entry-mismatch path, which trust never re-checks. The candidate now reduces as trust does.

**Measured at dt=600.** Every `SDIRK3_*` telemetry line is **byte-identical** to the pre-R13.25
run, and `SDIRK3_RECOVERY_MERIT` / `SDIRK3_CANDIDATE_LIFECYCLE` fire **0** times — the changed
paths do not execute in this configuration, which is what the A/B confirms rather than assumes.

ctest 62/62; ratchets green; fixtures 206 → **217**. **Independent review: NOT RUN.**

---

## Self-review of this increment — I folded the type back down

The first thing attacked was the type I had just introduced. `TrialOutcome` exists to tell three
acceptance paths apart, and I derived it from a single boolean:

```cpp
step_accepted ? TrialOutcome::AcceptedTrust : ...
```

`step_accepted` becomes true at **three** places — the direct-accept shortcut (`:11474`), the
recovery fallback (`:11791`) and a trust attempt (`:12291`). All three were labelled
`AcceptedTrust`. The recovery path's own marker, `recovered_with_fallback`, is block-local and dead
by the time the outcome is derived (its block closes at `:11826`), so the information was not
merely unused — it was **unrecoverable** at the point that needed it.

Under today's policy this changes nothing: all three overrule the signal. That is exactly the
trap. R13.24's `CandidateDisposition` was also harmless when written, and became a P0 once a later
consumer needed the state it could not express. **A label that is false is believed by whoever
reads it next.**

Each acceptance site now stamps its own outcome, and three fixtures pin that the paths stay
distinguishable — including the current equivalence stated **per path**, so a future policy change
has to face each one instead of inheriting a single verdict for all three.

**Also checked, and clean.** All three consumers of `s_halo_status` read it immediately after their
own `candidate_merits` call with no intervening call (`:11460→:11466`, `:11526→:11535`,
`:11772→:11778`), and the lambda resets it on entry — so no decision can inherit a previous call's
mask state. The fourth caller (`:11607`) is diagnostic only and guards on `s_measured`.

dt=600 re-run: `SDIRK3_*` telemetry **byte-identical**. ctest 62/62; ratchets green; fixtures
217 → **220**. **Independent review: NOT RUN.**

---

## Second self-review — I split the counters and left the consumer reading the old one

Phase 3 created `linear_total_failure_signals`, `entry_metric_mismatch_events` and
`globalization_rejections`. A census of their readers:

```
linear_total_failure_signals : writes=1  readers_elsewhere=0
entry_metric_mismatch_events : writes=1  readers_elsewhere=0
globalization_rejections     : writes=1  readers_elsewhere=0
```

**Zero consumers — the third recurrence of this shape in as many increments.** And it is worse than
the usual version: the review's §6.2 named the consumer whose misreading was the entire point —
`first_failure.h:1548`, `if (s.gmres_total_failures > 0) return KrylovStagnated` — and that line
went on reading the **mixed** counter. So an entry mismatch (a *nonlinear* decrease check failing
after a solve that raised no total-failure signal) still arrived as evidence that **Krylov
stagnated**, sending the next investigation at the wrong layer. Splitting the producer while the
misreading consumer is untouched fixes nothing.

The counters are now carried into `StageFailureSignals` and the classifier reads the linear one.
A record predating the split reports `-1` — *"this record cannot answer"*, not *"no failures"* — and
keeps the legacy reading, so archived logs classify exactly as before.

**The fixture was verified to reject the defect, not merely to pass.** With the fix reverted
in-place, exactly one check fails — the entry-mismatch one — and restoring it passes again. Two
companion checks pin that a genuine linear failure still reaches `krylov_stagnated` (the repair
must not blunt the signal it protects) and that a pre-split record is unchanged.

**Also checked, and clean.** `reset_per_solve` is `s = ConvergenceStats{}` — value-initialised, so
the new fields reset with the rest and cannot accumulate across stages; the field-by-field list
that fell behind the struct for three increments is already gone.

dt=600 re-run: `SDIRK3_*` telemetry **byte-identical**, `category=newton_budget_exhausted`
unchanged. ctest 62/62; ratchets green; fixtures 220 → **223**. **Independent review: NOT RUN.**
