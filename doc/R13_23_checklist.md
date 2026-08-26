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

- [x] **1.1** One canonical candidate-merit evaluator, used by trust, recovery, and the probes.
- [x] **1.2** A total-failure signal routes the candidate to globalization when the merit improves,
      instead of discarding it unevaluated. Keep the signal as a *warning*, not a veto.
- [x] **1.3** Fixtures: `TotalFailureCandidate_WithNonlinearDecrease_IsGlobalized`,
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
- [x] **6.2** Runtime provenance manifest: compiled default, Registry default, namelist request,
      effective value, authority — for the knobs that change control flow.
- [x] **6.3** Boundary receipt after the setter runs (`raw_*` vs `effective_*`).

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

**Phase 1 — arbitration, and it declines.** A total-failure signal is a statement about the LINEAR
residual ratio, and using it to discard the step removes the candidate from the trust region
entirely. When armed (`WRF_SDIRK3_CANDIDATE_ARBITRATION`), the candidate is measured in the norm
the trust region actually minimises and only a genuine improvement clears the flag — handing the
step to ordinary globalization to accept or reject, not auto-accepting it.

**Measured behaviour-neutral on the records that motivated it**, which is the point:

```
arb=off  cat=zero_update_after_total_failure  nit=4  acc=3  R_last=0.4578  outcome=20
arb=on   cat=zero_update_after_total_failure  nit=4  acc=3  R_last=0.4578  outcome=20
         R_raw=4.77e+08 -> 4.173e+08   R_S=476 -> 487.7   rescued=0
```

The arbitration evaluates, finds the raw L₂ better and the trust norm worse, and **correctly
declines**. Same outcome as before — reached by measurement instead of by assumption. Opt-in
because it costs one RHS evaluation per total-failure iteration for a rescue that (measured) does
not fire.

**Phase 6.2 — the authority manifest, and it immediately paid.** `[CONFIG AUTHORITY]` prints the
compiled default beside the effective value for the knobs that change control flow, flagging
disagreement. On the live run **three of five are overridden**:

```
nk_trust_region           compiled=true  effective=false  <-- OVERRIDDEN
nk_line_search            compiled=false effective=true   <-- OVERRIDDEN
use_autograd              compiled=false effective=true   <-- OVERRIDDEN
hevi_split                compiled=false effective=false
stage_require_convergence compiled=false effective=false
```

So `nk_trust_region` was not an isolated case: `nk_line_search` and `use_autograd` also run
opposite to their compiled defaults, and nothing said so. **Writing that table from memory got four
of five wrong on the first attempt** — they were corrected by reading `wrf_sdirk3_config.h`, and
the line numbers are cited in the source. A manifest that prints a false `compiled_default` is
worse than none, because it looks authoritative.

**Phase 1.3 — the rule, not just the code path.** The arbitration decision is a pure function in
`wrf_sdirk3_first_failure.h`, so a fixture can reject its negation. Five checks pin it, and three
carry the **live dt=600 numbers** rather than synthetic ones: the stage-2 candidate (raw L₂ −12.5 %,
trust norm 476 → 487.7) and the stage-3 one (raw −60 %, trust norm 46× worse) must both be
**refused**. Anyone who moves the judgement back to the raw norm — the mistake R13.22 made in
prose — breaks those two immediately. The remaining checks pin the two ways a rescue must not
happen: an unmeasured S norm is fail-closed, and a tie is not an improvement.

**Phase 6.3 — the receipt, and it resolves a standing discrepancy.** `raw_*` (what Fortran passed)
and `config_flags_*` (what this rank uses) are different objects: `refreshProcessAwareBoundaryFlags_`
masks each symmetric/open **edge** flag by whether the rank owns that edge, while periodicity passes
through unprojected. The projection rule is now a fixtured pure function that production
**consumes**, so it cannot drift from what the tests pin — eight checks, including that an interior
rank of a 3×1 decomposition has effective `open_xs = false` against a raw `true`, and that
periodicity is never masked (doing so would silently un-wrap the domain on every rank but two).

The receipt is emitted from the **refresh**, not the setter: three of the refresh's four callers are
proc-grid updates that change the projection *without* the setter running, and emitting at the
setter would have left those silent. The `g_sdirk3_config` sync moved ahead of the refresh — it
reads only `raw_flags_*` and `config_flags_polar_`, which the refresh does not touch, so the move is
behaviour-identical — and that lets the receipt compare a **synced** gcfg instead of the previous
value.

**Live dt=600 record**, which settles the `periodic_x` question the summary carried:

```
[BC RECEIPT] setter_call=1 proc_grid=1x1 my=(0,0) projection_is_identity=1 gcfg_matches_effective=1
[BC RECEIPT] periodic     raw_x=1 raw_y=0 | eff_x=1 eff_y=0  (unprojected: global-domain metadata)
[BC RECEIPT] symmetric    raw=0011 | eff=0011  (xs,xe,ys,ye)
[BC RECEIPT] open         raw=0000 | eff=0000  (xs,xe,ys,ye)
```

Fortran passed exactly what `namelist.input` declares (`periodic_x=.true., periodic_y=.false.`, with
symmetric `ys/ye` — a channel), and the solver uses it. **So the values were never wrong.** The
`periodic_x=0` seen in the log is a *lifecycle* artefact, confirmed by ORDER rather than inferred:
`rsl.error.0000:138` and `:145` print at **construction**, the receipt is `:283`, and the setter's
own log at `:289` reads `periodic_x=1`. Line 145's title was literally `Boundary conditions:` — a
pre-authority printout that reads as authoritative, the same trap 6.2 found in the config dump. Both
constructor dumps are now labelled `PRE-SETTER`.

One asymmetry is recorded rather than left to be rediscovered: the per-rank consumers
(`grid_info_`, `newton_solver_`) receive the **effective** flags while the global `g_sdirk3_config`
receives the **raw** ones. Those diverge exactly when the projection bites, so it is latent at np=1
— and np>1 refuses to start at `dyn_em/module_implicit_sdirk3.F:925`
(`SDIRK3_MPI_STAGE_HALO_UNSUPPORTED`), **read in the tree, not assumed**, which is the discipline
Phase 0 was written about. `gcfg_matches_effective` is the field that will say so the moment that
refusal is lifted.

**Checklist complete: 24/24.** ctest 62/62; ratchets green (`from_blob` 70/70, item-guard 74/74);
classification fixtures 157 → **170**. The dt=600 run still fails to converge
(`category=newton_budget_exhausted`) — unchanged and not claimed otherwise; note this run carries no
probe env, so it is not the same configuration as the arbitration A/B above. Its
`exit_receipt_complete=0` with `exit_attribution=none` is the Phase 4 gate working: a Newton-budget
exit has no complete Krylov exit receipt, and the record declines to invent a subtype.

**Independent review: NOT RUN.**

---

## Self-review after the checklist closed

Four claims from the two closing phases were attacked. Three held; the fourth turned up a defect
that neither the external review nor the checklist had asked about.

**Held — "the gcfg sync move is behaviour-identical."** The two consumers that now run *after* the
sync instead of before it read `g_sdirk3_config` only for `debug_level`
(`update_boundary_periodicity`, `newton_solver.cpp:13797`); the `grid_info_` assignment reads no
global at all. The claim was checked rather than repeated.

**Held — "`measured()` is not weaker than the `std::isfinite` it replaced."** `measured(v)` is
`v == v && v >= 0 && v < inf`: it rejects NaN and infinity like `isfinite`, and additionally
rejects the −1.0 sentinel. The extraction strengthened the guard rather than loosening it.

**Held — the changed log strings have no consumer.** The `initial_converged` lesson says a wire
string with a reader must not be renamed. `Boundary conditions:` and the `periodic_x=` lines were
grepped across `tests/`, `.github/` and the contract suite: no test or CI job keys on either, so
the `PRE-SETTER` labels are safe. (One correction to my own reasoning along the way: a first
brace-depth scan said the line-search block sat inside the trust-OFF branch. Recomputed properly,
that block closes at `newton_solver.cpp:11416` and the line search is on the trust-ON path.)

**FAILED — "the rescued candidate goes to the line search."** It does not, and the reason is worse
than the mistake. `nk_line_search` is fully wired (Registry → Fortran bridge → C++ config → env →
`options_.use_line_search`) and the authority manifest reports it **effective=true**. But the guard
in front of the Armijo block is a closure:

```cpp
bool skip = !step_accepted;                          // (A) covers not-accepted
if (step_accepted || rhs_budget <= 0) skip = true;   // (B) covers accepted
```

(A) and (B) together cover both values of `step_accepted`, so `skip` is **unconditionally true**:
the dK-magnitude refinement under it is dead, and the line search cannot run on any input. This is
the *coarse guard preempts its refinement* shape — (A) was the original skip, (B) was added later
for a different reason, and together they closed the door. It is also the campaign's signature
class: **a knob wired end to end whose consumer is unreachable**, reported as ON.

Deliberately **not repaired.** Reopening a globalization strategy inside a solver whose convergence
is under measurement is a numerics change that must be opt-in and measured, not a drive-by fix. So
the finding is pinned instead: the guard moved into `wrf_sdirk3_first_failure.h`, **production
consumes it** (so it cannot drift from the pins), and five fixtures assert it is true on all four
`(step_accepted × rhs_budget)` corners. If anyone reopens the path, the pins fail and say why.

And the manifest is corrected, because **a value is not an effect** — printing `effective=true` for
a knob that cannot change anything is the same trap 6.2 was written to close, one level up:

```
[CONFIG AUTHORITY] nk_line_search compiled_default=false effective=true  <-- OVERRIDDEN
  [NO REACHABLE CONSUMER: the Armijo guard is closed -- see line_search_skipped()]
```

**Verified on the live dt=600 run**, not just compiled — the manifest note, both `PRE-SETTER`
labels and the receipt all appear in `rsl.error.0000`:

```
  periodic_x=0, periodic_y=0  (PRE-SETTER defaults)
  Boundary conditions (PRE-SETTER defaults; see [BC RECEIPT] for effective):
[BC RECEIPT] setter_call=1 proc_grid=1x1 projection_is_identity=1 gcfg_matches_effective=1
```

ctest 62/62; ratchets green; fixtures 170 → **175**. **Independent review: NOT RUN.**

---

## Second self-review round — the suspicion was wrong, and that is the finding

The sentence attacked was my own: *"the rescue hands the step to the ordinary globalization path to
accept or reject on its own terms."* On the shipped configuration `nk_trust_region` is **false**, so
the direct-accept shortcut is what runs — and by the time the arbitration fires, that shortcut has
already been skipped. If nothing downstream judged the candidate, clearing the veto would leave
**neither a step nor a signal**: precisely the zero-update loop the shortcut's own comment warns
about, reintroduced by the mechanism meant to be more careful.

**Measured by reading the control flow, not the flag name: the claim holds.** Every block open
between the shortcut and the trust loop was enumerated; **none tests `nk_trust_region`**. Only the
shortcut is gated on it. `max_trust_attempts` is `3` unconditionally. So the loop's own condition is
the gate, and a rescued candidate enters a real globalizer on **every** configuration.

**But the check turned up something the flag name hides.** `nk_trust_region = false` does **not**
disable the trust region — it installs a direct-accept shortcut in front of it, and the loop still
runs up to three attempts when that shortcut does not accept. The manifest said `effective=false`,
which reads as *no trust region*. That is the **same class as the line-search finding, in the
opposite direction**: one knob reads ON and cannot act, the other reads OFF and still does.

```
[CONFIG AUTHORITY] nk_trust_region  compiled_default=true effective=false  <-- OVERRIDDEN
  [false = DIRECT-ACCEPT SHORTCUT FIRST, not 'no trust region': the trust loop is not gated
   on this flag and still runs up to 3 attempts]
```

The loop's condition is now a fixtured rule that **production consumes**, so the state a rescue
produces is pinned to enter it. Five checks: the rescue state enters; a standing signal does not
(the veto still bites when not rescued); an accepted step does not re-enter; exhausted attempts and
an exhausted RHS budget each stop it. The fourth, silent outcome — veto cleared, step not taken,
loop not entered — is now unrepresentable.

Both manifest rows verified on the live dt=600 run. ctest 62/62; ratchets green; fixtures 175 → **180**.

**Independent review: NOT RUN.**
