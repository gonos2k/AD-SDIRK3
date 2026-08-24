# R13.20 — red-team round 9 closeout

**Baseline** `agent/r13-19-initial-stop-consistency` @ `8d7636c` · **Reviewer** red-team round 9
(read-only, static; no build, no run) · **Independent review: NOT RUN.** `/code-review ultra` is
user-triggered only and its quota is exhausted. CI green is not a substitute and is not offered as
one.

Round 9 raised eleven findings (1 P0, 7 P1, 3 P2) and explicitly reported five attacks that came
back clean. Every item below was **re-verified in the tree before being acted on** — the discipline
this campaign lost exactly once, in round 8's claim 5A, and which R9-1 then caught.

---

## Ranked, as the reviewer ranked them

| # | Sev | Finding | Disposition |
|---|-----|---------|-------------|
| R9-1 | P0 | the ρ_E halo "fix" is a provable no-op; its premise is false; the commit walks into the trap it names | **DONE** in `8d7636c` — code removed, both checklists corrected, lesson recorded |
| R9-5 | P1 | the tolerance-source fix inverted: `Base` lost its producer, and iteration 0's fixed constant is labelled `eisenstat_walker` | **DONE** — source decided at the site |
| R9-2 | P1 | `attribution_from_metric` is 1 by construction in the case it was written to catch | **DONE** — reported by the clause that returns |
| R9-12 | P1 | §13's correction overstates in the mirror direction; three block readings identical to 6 figures with no locality verdict | **DONE** — doc corrected, probe instrumented, conclusion frozen pending measurement |
| R9-6 | P1 | `tolerance_source` in the tie-equality biases the refusal toward the operator layer | **DONE** — compares the implied category |
| R9-10 | P1 | `KrylovEntryMetricMismatch` is *provably*, not "may be", unreachable by default | **DONE** — the arithmetic is at the clause |
| R9-4 | P1 | the promotion-site comment asserts an invariant the doc says is false | **DONE** in `8d7636c` — stamp check + comment |
| R9-3 | P1 | dead ternary arm; `layer_receipt` printed on `n/a` rows | **DONE** |
| R9-7 | P1 | `R13_19_checklist.md` contradicts itself, refuted paragraph unmarked | **DONE** — struck |
| R9-8 | P2 | two clones per Newton iteration feeding the no-op | **DONE** in `8d7636c` — deleted with the calls |
| R9-9 | P2 | `build_halo_mask() is never called` is false | **DONE** |
| R9-11 | P2 | `receipt_version = 2` asserted unconditionally over conditional assignments | **DONE** — reworded to what the code does |

---

## R9-5 — the tolerance source is now decided where the tolerance is set

The E–W block had **three** levers and recorded one enum value for all of them:

| what produced the number | what was recorded | what a tolerance-limited verdict then routed to |
|---|---|---|
| `γ·(‖R_k‖/‖R_{k-1}‖)^α` | `eisenstat_walker` | `ew_gamma` / `ew_alpha` — correct |
| `max(krylov_tol, WRF_SDIRK3_EW_ETA_INITIAL)` at iteration 0 | `eisenstat_walker` | **wrong**: no residual ratio was read, and `ew_eta_updated_this_iter = false` three lines later is the code saying so |
| the `[ew_eta_min, ew_eta_max]` clamp on either arm | `eisenstat_walker` | **wrong**: the knob is `ew_eta_max`, and it saturates *exactly in the failing regime* |

`KrylovToleranceSource` now carries `EwEtaClamp` and `EwInitialFloor`, each decided **at the site**
by comparing the candidate values — so `Base` regains a producer whenever `options_.krylov_tol` is
the binding one, which the previous form could not express.

Two further corrections in the same selector:

- `stage_budget_forcing_eta > 0` is **dropped**. It is `policy.ew_eta_used`, and `apply_ew`
  (`wrf_sdirk3_stage_krylov_policy.h:114-134`) writes the restart **budget** and never `p.tol`.
  Verified: only `apply_stage2`/`apply_stage3` write the tolerance, and both set `tol_overridden`,
  which is the `StageOverride` arm above it. The disjunct could fire only with adaptive tolerances
  OFF and a stage budget knob set — labelling a plain `krylov_tol` solve `eisenstat_walker`. Same
  category error as the P1-1 defect it replaced, one level up.
- the flag is **declared inside the Newton loop**, so it cannot inherit the previous iteration's
  answer. The predecessor was declared outside and never reset — inert only because both arms
  happened to set it, which is not a property anyone should have to re-derive.

## R9-2 / R9-3 — the deciding clause now says what it decided on

`first_failure_of` takes an optional `StageDecisionBasis*` and each region that returns names its
evidence class: `KrylovR0Receipt`, `ExitReceipt`, `LegacyKrylovAggregate`, `NewtonExitReason`,
`AggregateReconstruction`, `StepAcceptance`, `NewtonResidualTrace`, `KrylovDivergedFlag`,
`Precondition`, `Postcondition`. Set at region tops rather than at all ~25 return sites, to keep
the classifier readable.

`attribution_from_metric` is now `attribution_basis == KrylovR0Receipt`. The old inferred form was
`measured(worst_vs_r0) || measured(best_vs_r0) || solves_measured > 0` — a test for *does an r0
reading exist*, not *did one decide*. The two come apart exactly where it matters: r0 measured and
the worst ratio **below** the threshold means the classifier falls through to the aggregate
reconstruction, and the flag read 1 because the branch it fell through was entered on
`measured(worst_vs_r0)`. A fixture pins the 0.

`decided_by_exit_receipt` is likewise `primary_event_basis == ExitReceipt`, and the emitter computes
`specific_layer` and `layer_receipt` **together** so `layer_receipt=n/a` on the ~13 categories whose
layer is `n/a` — it used to print `stage_worst`, the struct default rendered as a measurement, and a
commit message reported that as a finding. The `KrylovForcingTermLimited` exit arm is gone rather
than left as a live-looking alternative no input can select. `event_basis=` and `attribution_basis=`
are on the record.

## R9-6, and a P0 it uncovered

`same_mechanism_as` compared `tolerance_source`, which the classifier never branches on, so two
solves that both implied *objective mismatch* were declared ambiguous when one had the INN ramp
fire — and the refusal falls through to `KrylovStagnated`, i.e. **toward the split-explicit
rebuild**. It now compares the **implied category**, via one `krylov_mechanism_category()` that the
classifier's four-way also calls, so the two cannot drift.

Unifying them exposed a defect round 9 did not report:

> **The `!all_near_worst_met_tolerance → KrylovStagnated` guard preempted the four-way and made two
> of its four answers unreachable in production.**

`near_worst_all_met` compares `worst_unmet` against `worst`; when the strictly-worst solve is itself
unmet those are the same number, so the predicate is **false by construction** — and
`worst_krylov_met_tolerance` belongs to that same strictly-worst solve. Reaching the four-way
therefore implied `worst_krylov_met_tolerance == true`, which `S_reached || met_tolerance` always
accepted. **`KrylovBudgetExhausted` and the trailing `KrylovStagnated` had no producer.** The
fixture that pinned `krylov_budget_exhausted` reached it from `all_near_worst_met_tolerance = true`
(the struct default) with `worst_krylov_met_tolerance = false` — **a pair the solver cannot emit**,
so the category read as covered while never firing. That category was *renamed* in R13.19 with a
fixture, on a branch production never reached.

The guard was the round-6 approximation of tie disagreement; `near_worst_mechanism_ambiguous` —
order-independent, comparing exactly what the answer depends on — is the real thing. The
approximation is retired from the classifier and stays on the record as telemetry, and the fixtures
now express the refusal through the flag that carries it.

## R9-12 — §13 is frozen, not re-interpreted

The correction is corrected (see `doc/R13_17_checklist.md` §13): the caveat identifies degeneracy by
`q ≈ 1` and `ru`/`rv`/`rw`/`t` are 7188–14286, so "possibly degenerate" does not apply to them — the
straddle has a non-degenerate witness on **both** sides.

What remains open is the coincidence: `|q−1|` for `ru`, `t` and `mu` agree to ~1 part in 10⁵. The
review's proposed mechanism (directions not block-local) is **refuted by construction** — `e_b` is
exactly zero outside its block, so no other block can contribute to `q_b`. The probe now emits
`_vout` (must be 0), `_Avin`, `_Avout`, `phys_blocks_measured` and `phys_blocks_local`, so the
precondition carries a verdict. **§13 must not be built on again until those rows are read.**

---

## Found while fixing, not reported by the review

**Hard-constraint violation: `.item<double>()` outside `NoGradGuard`.** The block-direction scan in
the `SDIRK3_NUMRANGE` probe sits *after* the sample loop, so `ng_red` — declared inside that loop —
had already gone out of scope. Two unguarded calls. Round 9 checked the *Taylor* probe for exactly
this class and reported it sound; it did not check this one. Fixed, plus five further sites in
`wrf_sdirk3_newton_solver.cpp` (three where the tensor was built under a guard but the extraction
was not, two in the JVP auto-bench that were also GPU→CPU syncs) converted to `guarded_item<T>`.

**Open, measured, deliberately out of scope.** A brace-depth scan of `external/libtorch_wrf/sdirk3/`
reports ~123 apparently-unguarded `.item()` sites in `wrf_sdirk3_tile_unified_impl.cpp`. That count
is **noisy** — the scanner cannot see `/* */` doc blocks, test files, or tensors already detached —
and it is pre-existing rather than introduced by R13. It is recorded here as a candidate audit, not
as a defect count, and not fixed in a reporting-hardening change.

---

## Verification

`ctest` **62/62** in `build/sdirk3-core`; the classification contract is **116 checks** (was 103).
No new ctest target, so the suite-count ratchets do not move.

**Not run:** the model itself at dt=600 — the new `phys_blocks_*` and `event_basis` rows are emitted
but unread. Every claim above is static or contract-test evidence; none of it is a measurement of
the solver's behaviour on `em_b_wave`.

---

## Numerics-referee items, closed in the same increment

These were recorded in R13.19 and not acted on. Each premise was re-checked in the tree first.

**Claim 1B — what τ can and cannot see. CONFIRMED.** `As` is the AD JVP *of `compute_rhs`* and `dR`
is a finite difference *of the same `compute_rhs`*, so τ measures AD-tangent-vs-primal consistency.
**A defect in `compute_rhs` itself is invisible** — AD differentiates the wrong function faithfully
and τ → 0. That is not hypothetical: the campaign's own standing root-cause note (`Omega := rom =
mu*w` where WRF's Ω is `mu·dη/dt` from `calc_ww_cp`) is exactly such a defect and would leave every
τ row untouched. Recorded at the probe and in the printed conclusion, which now says "AD-vs-primal
Jacobian defect".

**Claim 1C — the linearity receipts are blind to this. CONFIRMED.** `e_repeat`, `e_hom`, `e_add` and
`linearity_residual` all test **linearity**, and a wrong-but-linear `A = J + E` passes every one
*exactly*. `R13_8_checklist.md`'s *"the receipt behind it is now one that a broken operator would
fail"* is **false for `linearity_residual`** and true only for `tau_alpha_over_tau`; the two are
printed side by side and the sentence covered both. Struck in the doc, and the caveat is at
`operator_linear`'s definition.

**Claim 1 (main) — the sample excludes the failing iteration BY CONSTRUCTION. CONFIRMED by brace
walk**: the probe is inside `if (step_accepted)` (`wrf_sdirk3_newton_solver.cpp:11709`), so it
cannot sample the dt=600 exit, which takes the zero-update break with no accepted step. **Every τ
this campaign has quoted came from a step that succeeded, and nothing said so.** The row now carries
`probe_gate=step_accepted`, `accepted_so_far` and `rejected_so_far`; `SDIRK3_FIRST_FAILURE` carries
`taylor_probe_iter` and `taylor_covers_last_newton_iter`.

**Claim 1D — the retracted τ wording was still standing. CONFIRMED, four sites.** R13.18 retracted
*"τ ≪ 1"* (τ_max = 0.2008) and the retraction was chased into R13.8's "moved outward" point but not
into the τ wording, which is older and more load-bearing — it survived at `R13_8_checklist.md:747,
752, 1204, 1325`, including the **premise sentence of the budget experiment**. All four corrected,
both R13.18 wording boxes ticked, and the worst excited block is now named: `ru`, the block the
campaign's own record says dominates the stage-3 entry norm.

**Claim 2 — two unearned labels. CONFIRMED, and renamed.** The probe used `vb = gmres_rhs/‖gmres_rhs‖`
and called it `*_krylov` on the strength of its own comment, *"at a cold start b/‖b‖ IS the first
Arnoldi vector"* — **a precondition it never measured**, on a record (stage 2, iteration 3) that is a
**warm start**, where the first Arnoldi vector is `r₀/‖r₀‖`. And even at a cold start it is Arnoldi
vector **#1**; the later vectors, where cancellation lives, are not measured, so the plural in "the
directions GMRES builds" was not earned either. Renamed `identity_frac_krylov` →
`identity_frac_rhs_dir` (same quantity; past logs' `*_krylov` rows are these), with
`rhs_dir_is_first_arnoldi` **measured** from `‖x₀‖`.

**Claim 4 — "12× budget → 23×". SUPPORTED as arithmetic, corrected as an attribution.** Both
endpoints are non-solves: ρ = 0.8622 is 0.064 decimal digits, and the run's own per-vector rate
extrapolates to ~1400 vectors for ρ = 0.1 and ~2800 for ρ = 0.01 — another 16–33× on top of the 12×.
More substantively the two arms are **not the same experiment**: the budget changed the Newton
trajectory (4 → 3 iterations, 4 → 3 solves, 3 → 2 accepted steps), so `worst_krylov_rel_vs_r0` is a
maximum over **different sets of different linear systems**. And `krylov_iters` is a stage aggregate
printed beside a per-solve extremum — the same mixing this document caught for `best_krylov_rel`
two hundred lines earlier. Written into `R13_8_checklist.md` with the two lesser confounds
(three knobs, and the persistent `stage2_hopeless_*` members).

**Claim 7.1 — an uncalibrated threshold doing load-bearing work. CONFIRMED and fixed.** The `1e-3`
excitation floor **selects which block the verdict names**: `share_rw = 2.16e-04` is 4.6× below it,
so `rw` is excluded and the headline reads 0.2008 (`ru`) — while `tauraw_rw = 0.207042` is *larger*.
The campaign fixed the floor-*normalisation* trap and missed the floor-*value* trap, which is the
same shape as the 0.90 no-progress boundary that got an override, an on-record value and its limits
written at the constant. The floor now gets all three (`kTauExcitationShare`,
`WRF_SDIRK3_TAU_EXCITATION_SHARE`, `tau_excitation_share` / `_observed` on the row) **plus** the
counterfactual: `tau_raw_block_max` and `tau_raw_block_max_name` are emitted beside the excited max,
so no future reader can quote the verdict without the number it suppressed.

**Claim 7.2 — n = 1 everywhere. ACCEPTED.** One case, one stage, one timestep, one Newton loop; no
dt ladder, no second configuration, no repeat of any physics run except the probe OFF/ON control.
The denominator is now on the scope paragraph in `R13_8_checklist.md` rather than appearing once and
nowhere in the conclusions.

**Claim 7.3 — the priority was inverted. ACCEPTED.** The outer reduction ratio **tracks the achieved
forcing term almost exactly** (η 0.55 → 0.89 → 0.955 against ratios 0.73, 0.83, 0.89) — the
inexact-Newton bound being attained. That is a stronger and more direct argument for "the inner
solve binds" than τ, it carries none of τ's selection caveats, and it was already on the record as a
supporting remark under a τ headline. Inverted, with the implication the table always carried: **at
ratio 0.89 even a perfect outer trajectory needs O(10²) Newton iterations against a budget of 12**,
so the target is a forcing term small enough to change the ratio, not "a better solve".

**Claim 7.4 — two quantities never put on the same axes. FIXED.** The frozen A/B ladder reports ρ_D;
the live worst solve reported only the r₀ coordinate, so the one comparison that says whether the
ladder is representative of the production solve could not be made from a log — although both
numbers were already computed. `worst_krylov_rho_D` and `worst_krylov_rho_S` are now emitted beside
`worst_krylov_rel_vs_r0`.

**Still open.** Claim 7.4's comparison itself: the fields are emitted but **unread** — it needs a
dt=600 run. Same for `phys_blocks_local`, `taylor_covers_last_newton_iter`, `tau_raw_block_max`,
`event_basis` and `rhs_dir_is_first_arnoldi`. Everything in this document is static or contract-test
evidence.

---

## Adversarial loop — findings the reviews did not report

Each iteration attacks this increment's own changes. The campaign's record is that nine consecutive
rounds found a defect *inside* the previous round's fix; these are mine.

### Iteration 1 — the tolerance outlived its iteration

R9-5 moved the tolerance *receipt* into the Newton loop and left the **value it describes** outside
it. Five writers, no cross-iteration role. Inert with adaptive tolerances on (the E-W block
reassigns before any read, so the default path is byte-identical) and **not** inert with them off:
the INN ramp is an in-place multiply and the policy round-trip passes the value through, so
iteration *k* started from *k−1*'s post-ramp value and the ramp compounded to γ^k. At γ = 0.5 over a
12-iteration budget that is 2.4e-4 × base — a tolerance no solve can reach — while the code calls
the ramp "conservative".

Checked systematically, not just this one: a brace-depth scan of every local declared between the
function head and the Newton loop, cross-referenced against assignments inside it, returns 14
names. Thirteen have one writer and are cross-iteration **by design** (`K`, `last_res_norm`,
`prev_iter_res_norm`, the stall/hopeless counters, and `newton_tol_adaptive` — a monotone ratchet
seeded from `init_R0_norm`, now commented as deliberate).

### Iteration 2 — an entire stage class was never classified, and a NaN was reported one stage late

A field-by-field audit of `StageFailureSignals` (65 fields) against every producer found **two with
no production writer at all**: `explicit_rhs_measured` and `explicit_rhs_finite`. Only the test
fixtures wrote them. Both default `false`, and they gate the explicit branch — so
`first_failure_of` returned `InsufficientEvidence` on its first line **every time**, and three
categories below it were unreachable.

~~Following that found the larger one: the explicit branch hard-set `last_stage_converged_ = true`
… so `handle_stage_gate` early-returned on every explicit stage … and a non-finite explicit
tendency passed silently into the next stage's state.~~ **RETRACTED in iteration 8 — see below.**
The shared `k_fast_all_finite` check that runs after *both* branches already sets
`last_stage_converged_ = false` on a non-finite tendency, which makes `stage_failed` true and the
gate fire. I wrote that claim without reading forty lines further down.

**What survives is the finding above, and it is the one that mattered:** the gate fired and the
classifier could not say *why*, because the two signals its explicit branch reads had no producer
outside the fixtures. `explicit_rhs_measured` is set where the tendency is evaluated (free — no
sync) and `explicit_rhs_finite` is fed from the shared reduction that was already computing it for
both branches.

**And the reachability is now on the record instead of implied.** From the single production call
site the explicit branch can return `InsufficientEvidence` or `ExplicitRhsNotFinite` and nothing
else: `ExplicitAdmissibilityRejected` needs a finite tendency with a bad metric, but a finite
tendency sets the metrics to 0 and the gate early-returns; `ExplicitPublishRejected` and the
explicit `None` need `state_published`, which that call site sets `false` unconditionally because it
is upstream of any publish. Both are kept for a publish-site call that does not exist yet, and the
header says so rather than leaving three live-looking categories.

CI gates run locally rather than push-and-see (`sdirk3-ci.yml` has four count ratchets):
`check_ratchets.sh` → `from_blob actual=70 == baseline=70`; the README/`CLAUDE.md` "N-test CTest"
claims both read 62 against a pinned inventory of 62; no new ctest target, so none of them move.

Verified sound while looking, and worth recording as negatives: `worst_krylov_rho_S` is
`gmres_result.rel_error`, the **unclamped** ρ_S (the clamp happens later, into `gmres_rel_error`),
so the warm-start value above 1 that this campaign measured is preserved rather than hidden;
`worst_krylov_rho_D` is `‖D⁻¹r‖/‖D⁻¹b‖` with a **consistent** denominator (`bnorm_safe` is taken
from the already-scaled `b_inner`), so it really is the ladder's coordinate and claim 7.4's
comparison is well posed; and `res_norm_detached`'s exit `.item()` is inside a `NoGradGuard`.

### Iteration 3 — a fixture that pins a reserved branch is not coverage, and nothing said which was which

CI on iteration 2's head: **all four checks green** (`fast-contracts`, `core-linux`,
`build-contract-negatives`, `required`).

Two systematic audits, both closing the *other* half of iteration 2's work.

**All 23 `StageFailure` enumerators have a return site** — no dead enum value. But reachability
from the single production call site is a different question, and iteration 2 answered it only for
the explicit branch. The implicit branch has the same hole and it is now written at the clause:
reaching the tail means `newton_converged` was true, and `handle_stage_gate` is entered only when
`stage_failed || gate_metric_bad`, so a converged stage there **necessarily** has
`gate_metric_ok == false` and answers `AdmissibilityRejected`. **`PublishRejected` and `None` are
unreachable from that site** — it sets `state_published = false` unconditionally, being upstream of
any publish.

**So four fixtures pin classifications no production signal combination can produce**
(`publish_rejected`, `explicit_admissibility_rejected`, `explicit_publish_rejected`, explicit
`none`). They are not wrong — they pin the classifier's *precedence*, which is real — but nothing
distinguished them from coverage, and **that is exactly how a dead branch survived two
increments**: the `krylov_budget_exhausted` fixture reached its clause from a signal pair the
solver cannot emit, so CI read the category as covered while it never fired, and it was even
*renamed* on a branch production never reached.

Fixed **structurally, not with a comment**: `check_reserved()` prints `ok(RESERVED)` in the test
output with the arithmetic for why, and a **reserved-branch ratchet (4/4)** sits beside the
case-count ratchet — so converting a reserved pin into a live one, or quietly adding another
unreachable branch, has to be an explicit edit.

**One dead field, recorded not removed.** An audit of all 80 `ConvergenceStats` members against
every reader and writer in the tree found exactly one with **neither**: `condition_number`. Its
option `compute_condition_number` (default false) has no consumer either. It predates this
campaign and the struct crosses the public API, so it is annotated in place rather than deleted.

### Iteration 4 — the `.item()` rule is now counted instead of reviewed

The rule CLAUDE.md states unconditionally, and the one the user has flagged as a repeated
regression, was being held by review. Review cannot hold it, for two measured reasons:
`NoGradGuard` is **RAII**, so one declared inside a loop stops protecting at the end of each
iteration while remaining visible a few dozen lines above (that is exactly how the numrange block
scan came to call `.item<double>()` twice unguarded, in a probe a review had just checked for this
class); and the volume is beyond eyeballing.

**Measured, with comments and string literals stripped and `.detach()`ed operands excluded: 129
sites**, 118 of them in one 29k-line file. A naïve scan reports 225 — most of it prose *about*
`.item()` inside doc comments — so the number is only meaningful with the stripping.

> **Name the denominator.** 129 was measured with the **four-line proximity** exclusion rule.
> Iteration 9 replaced that with a **named-and-near data-flow** rule and re-measured the *post-fix*
> tree at **74**. Those two numbers are not comparable as a before/after: 29 sites were fixed here
> and a further 26 turned out to be false positives the narrow window could not resolve. Every
> figure in this section is on the proximity rule; every figure in iteration 9 is on the shipped one.

Classified rather than dumped (proximity rule): **100 sit behind a `debug_level` / probe gate; 18
do not** and can run in a production step — NaN/Inf health checks and norm computations on the RHS path, i.e. the
hot path, which is where the rule matters most. All 18 are now `guarded_item<T>()`. The remaining
11 outside that file are guarded in place with an explicit `NoGradGuard` rather than a new include,
to avoid pulling `wrf_sdirk3_autograd_utils.h` into five more headers.

**And it is now a ratchet, not a one-time cleanup.** `.github/ci/lint_item_guard.py` does the
brace-depth scan; `check_ratchets.sh` gains a second baseline
(`tests/lint_item_guard_baseline.txt`, **100 → corrected to 74 in iteration 9**) with the same
three conditions as `from_blob`:
actual must equal the head baseline, the head baseline may never exceed the base branch's, and the
file must be one integer line. Fixing sites *requires* lowering the baseline; adding one fails.

**Negative-tested, because a receipt that cannot fail is not a receipt.** Appending one unguarded
`.item()` to a header takes it to `actual=101 head_baseline=100 → FAIL`; removing it restores
`OK`. Extending the script rather than the workflow keeps the YAML untouched — this repo has
rotted a CI-side counter four times — and the one YAML edit (the step's name, now naming both
ratchets) passes `actionlint` locally.

**What it does not prove**, stated: a guarded `.item()` can still be a GPU sync on a hot path, and
a detached operand says nothing about whether the sync belongs there. This bounds one failure mode.

### Iteration 5 — three constants under a comment that said "MEASURED, not asserted"

The receipt-struct audit, finished. `TaylorDefectInputs` (11 fields) is clean. `AbComparison` (23)
had **one field with a producer and no reader**: `shared_preconditioner_instance`, written `true`
at one site and consulted by nothing — `ab_attributable` does not gate on it, and the A/B row
printed **`shared_preconditioner_instance=1 fresh_wrapper_per_arm=1` as a compile-time literal**.
That is the exact defect the tree already recorded for `published` (`tile_unified_impl:12408`,
"a compile-time LITERAL at both call sites"), recurring one struct over: a change to
`fresh_wrapper_per_arm` would have left the row reading `1`. Both now print from the fields, which
gives `shared_preconditioner_instance` its first consumer, and the struct says **why** it is
deliberately not a gate: sharing the instance is admissible precisely because
`preconditioner_state_unchanged` measures that the object did not move between arms.

And the comment above the assignments — *"MEASURED, not asserted (external review P0-1/P0-2)"* —
stood above **three constants**. They are true, but true **by construction at that site** (one
`gmres_op` closure handed to every arm; `make_fresh_M()` copies the wrapper per row; the
preconditioner object is shared), not measured from anything. Relabelled rather than changed,
because a constant dressed as a precondition is what round 5 removed from `alpha_arm_measured`.
What *is* measured sits on the lines below, and the comment now says so.

**Swept for the same class rather than fixing one instance.** Every `field=0`/`field=1` literal in
a record line: `witness_confirmed=0` (inside the not-measured branch, correct — and its comment
already explains the padding), `probe_interfered=1` (inside `if (after != before)`, correct),
`accuracy_valid=0/1` (two branches, correct), `replaced_with=1`/`concurrent=1` (values, correct),
`fresh_solver_per_arm=0` (a true-by-construction limitation, left as is and noted). One more was
worth deriving: `tolerance_exit_disabled=1` sat beside fields read from real variables while the
arms' tolerance is a literal `0.0f` argument. It is now `kAbArmTol`, passed to the arms and read by
the record, so changing the argument moves the claim.

Both ratchets green (from_blob 70/70, item-guard 100/100); ctest 62/62.

### Iteration 6 — a coverage claim that had drifted, inside the gate that IS the coverage

**First, a decisive negative.** The `.item()` ratchet added in iteration 4 **ran in CI and passed**
(`fast-contracts` SUCCESS on `0615b5d`), so `python3` on `ubuntu-24.04` and the whole new step are
verified empirically, not assumed.

**Two more negatives, checked rather than trusted.** No offline parser exists for
`SDIRK3_FIRST_FAILURE`, `SDIRK3_TAYLOR_DEFECT`, `SDIRK3_NUMRANGE` or the A/B row — the two shell
harnesses parse `SDIRK3_STAGE_*` / `SDIRK3_RHS_*` markers only — so this increment's **mid-line
field insertions break nothing**, and the `identity_frac_krylov` → `_rhs_dir` rename has no
consumer to break. And the implicit path resets `last_stage_signals_` to a fresh struct *before*
its field-by-field copy, so the cross-stage staleness R13.10 fixed for the explicit path cannot
recur on the implicit one.

**The finding.** `sdirk3-ci.yml` carried a hand-written census of the stage-operand harness:
*"42 `gate` calls + 2 `skip` total"*, *"COVERED — 16 gates, lines 205-235"*, *"NOT COVERED — the 26
gates at lines 263-434"*, *"--self-test exits at 239"*. Measured: **95 gates, 3 skips**, self-test
block **187..474**, **56 exercised** against synthetic fixtures and **39 syntax-only**. Every number
had drifted, and this is the comment a reader uses to budget review effort — a coverage claim that
rots, sitting inside the gate that *is* the coverage. Same defect the README ctest-count ratchet
exists to stop.

Fixed the way this repo already fixed the core-manifest count — **derive it, do not restate it**.
`--self-test` prints the census computed from its own source, together with what the split does and
does not prove, and the YAML points at that output instead of carrying numbers. The census uses
`note`, not `gate`, so the harness's own exact-count ratchet is untouched; `bash -n` and
`actionlint` both pass. *(Iteration 7 then caught this replacement overstating — see below.)*

**One simplification.** The emit site called `first_failure_of(sig)` for `first` *and*
`stage_diagnosis_of(sig)` — which calls it again — for `primary_event`. Equal today because the
function is pure, but the emitter now pairs `first` with `diag.primary_event_basis`, and a layer
derived from one call annotating a category from another is the shape round 8's P0-C removed one
field over. One call now.

### Iteration 7 — the replacement census overstated, one iteration old

Attacking iteration 6's own fix first. Its census said **`56 inside --self-test → exercised …
in hosted CI`**. Measured: **12 of those 56 sit behind `if [ -x "$CBIN" ]`**, and the torch-free
hosted job does not build that binary — the harness's own `SELF_TEST_EXPECTED_NO_LIVE=44` says so
outright. So the line **overstated hosted-CI coverage by 12 gates**: a coverage claim larger than
what runs, which is the same defect as the stale census it replaced, one layer down and one
iteration old. Seventh consecutive round in which the fix contains the class it was written to
close, and this one is mine.

The census now leads with what it can never overstate — **`EXECUTED THIS RUN: $checks`**, the
harness's own counter — beside `contract binary: present / ABSENT`, with the live-only count
derived from `WITH_LIVE − NO_LIVE` rather than typed. Verified both ways locally: **44 / ABSENT**
torch-free, **56 / present** with the binary.

**And the same class again, in the step that consumes it.** The LIVE job grepped for
`SELF-TEST: ALL PASS (56/56)` — putting the expected count in **two** places, the YAML and
`SELF_TEST_EXPECTED_WITH_LIVE`, so adding a live gate needs two edits and the YAML lags. That is
precisely what the *neighbouring* step's comment warns about (*"No count ratchet in this YAML on
purpose … a YAML-side count has rotted here four times"*). Its own explanatory comment had already
rotted: it justified the number as *"40-vs-28 is itself the proof"*, a pair from an older ratchet.
The step now greps `contract binary: present` — a derived signal that proves what the step is
actually for, the live path being taken — plus `ALL PASS`, leaving the count owned by the script
alone. **Negative-tested:** it passes with the binary and fails without it.

Checked and clean: the sibling `run_decomposition_matrix.sh` makes no numeric coverage claim
(`SELF-TEST: assertion logic + manifest sane ($rows cases)` is derived), and no other workflow
comment carries a live count — the remaining numbers are historical statements about past fixes.

### Iteration 8 — I retract iteration 2's headline

Attacking my own iteration-2 claim, which asserted a behaviour change I had not verified: *"a retry
of an explicit stage re-evaluates the same RHS … acceptable"*, and above it, *"the gate could not
fire on an explicit stage AT ALL"*.

**Both were wrong, and reading forty lines further down was all it took.** A shared
`k_fast_all_finite` reduction runs after *both* the explicit and implicit branches (verified by
brace depth: same scope depth as the branch itself) and, on a non-finite tendency, already sets
`last_stage_converged_ = false` plus three gate metrics to infinity — converting it to
stage-failure semantics so `stage_fail_action` handles it. **So the gate did fire, and a NaN never
"passed silently into the next stage".** Iteration 2's headline is retracted.

What survives is iteration 2's *first* finding, unchanged and still the one that mattered: the gate
fired and `first_failure_of` answered `insufficient_evidence`, because `explicit_rhs_measured` and
`explicit_rhs_finite` had no producer outside the test fixtures.

**And the fix is now smaller than the claim was.** Iteration 2 added a second
`isfinite(k_fast).all().item<bool>()` on the explicit path — a duplicate GPU→CPU sync per timestep
for a fact already being measured thirty lines later, in a commit that argued for the sync's cost.
Removed. `explicit_rhs_measured` stays where the tendency is evaluated (free); `explicit_rhs_finite`
is fed from the shared reduction, one producer for both branches.

**One real improvement survives the retraction.** That shared handler set three of the five gate
metrics to infinity and left `wrms_norm_` / `wrms_growth_` — and `wrms_growth_` is what
`stage_gate_metric_value()` reads in the **default** mode 0. `converged_ = false` forces the gate to
fire regardless, so nothing was ever missed; but which metrics are infinite decides
`gate_metric_ok`, and therefore whether the record says *"the tendency is not finite"* or *"the
metric was fine"*. A runtime mode should not change what a NaN looks like on the record. All five
now, for both branches.

**Method note.** Two iterations in a row the defect was in my own previous iteration, both times
because I inferred a control-flow fact instead of reading it. The brace-depth check that settled
this took one command.

### Iteration 9 — auditing my own claims, and a gate that would have hung the job it guards

The method note from iteration 8 was *"I inferred a control-flow fact instead of reading it"*, twice.
So this iteration re-read every control-flow claim this increment has made.

**Four verified, none wrong.** (1) With adaptive tolerances on, every read of `krylov_tol_adaptive`
outside the E–W block (first at `:7388`) follows its writes at `:6582`/`:6620` — the byte-identical
claim holds, and after iteration 1 it holds with adaptive tolerances *off* too. (2) `ew_eta_max =
0.9f`, `ew_eta_min = 0.02f` and `total_failure_vs_b = raw > 1.0f || rel >= 0.999f`, read from the
source rather than copied from the reviewer — R9-10's arithmetic stands. (3) Every tolerance exit
is a strict `<`, so `kAbArmTol = 0.0f` disables it for any non-negative residual, and a NaN
residual does not exit either. (4) Iteration 4's *heuristic* gated/ungated split: all eight sites
whose nearest enclosing conditional is not a debug gate sit inside
`if (debug_level >= 2)` further out, so the "100 gated / 18 production-reachable" classification
holds.

**Then the lint's own exclusion rule, which I had shipped as a CI gate without auditing.** It
excluded a site when `.detach()` appeared within four lines — proximity, not data flow. Audited by
reading: all 12 exclusions in this tree were real links. But that is soundness *by local style*, so
I replaced it with a named data-flow link — and **measured the replacement instead of trusting it.**

Two failures, both caught by measuring:

- **A file-wide transitive closure returned ZERO violations where there are 100.** In 29k lines
  almost every name is reachable from some `.detach()`, so the "more principled" rule degenerated
  into excluding everything. A rule that cannot fire is not a rule — the campaign's own standard,
  applied to my own tool.
- **The first working version took over five minutes on one translation unit** and was moved to the
  background by the timeout. It re-ran a regex per (assignment, seed) pair each round. **A CI gate
  that hangs the job it guards** — shipped by me one iteration earlier had I not measured. Rewritten
  to tokenise each right-hand side once and close over set intersections: **1.4 s**.

The shipped rule now requires the link to be **named** (an identifier in the receiver expression
traces to a `.detach()` assignment) **and near** (within eight lines). Result: **74**, not 100 —
**26 of the original 100 were false positives**, real detach links the four-line window was too
narrow to see. Spot-checked by reading all 21 of the newly-excluded sites in the widened window:
every one is `X_cpu = Y.detach().to(kCPU)` followed by a reduction. Baseline lowered 100 → 74, which
is what the ratchet's "fixing violations REQUIRES lowering the baseline" clause is for, and
re-negative-tested: one added unguarded `.item()` gives `75 vs 74 → FAIL`.

### Iteration 10 — a record the campaign quotes as evidence could not be read by key

Final pass. Acceptance first, after a **force-clean rebuild** of every sdirk3 object (the
stale-object-on-header-change trap this project has recorded): **ctest 62/62**, both ratchets green
(`from_blob` 70/70, item-guard 74/74), `actionlint` clean, both harness self-tests pass
(`44/44`, `4 cases`), and `fast-contracts` **SUCCESS in CI on `4081db0`** — so the corrected lint
and its lowered baseline are verified on the runner, not just locally.

**The finding.** A duplicate-key check across the three emitted records: `SDIRK3_FIRST_FAILURE` has
69 fields and **no** duplicates; `SDIRK3_NUMRANGE` had **`q_min=` on all four arms and `neg=` on
three**, separated only by prose labels (`A:`, `| A_physical(raw, unscaled):`, `| AM^-1(production):`,
`| M^-1A:`). A grep for `neg=` on that row returns whichever arm came last — and **§13 of
`R13_17_checklist.md` quotes two different `neg=` values from that single line** as the evidence for
the physical-numerical-range conclusion, one of the two readings that justified a campaign pivot.
The emit site's own comment even argues that *"`neg=` carries a denominator on every arm so the
three are comparable"* — comparable to a human reading the prose, unreadable to anything else.

Keys are per-arm now (`A_q_min`/`A_neg`, `Aphys_*`, `AMinv_*`, `MinvA_*`), the prose labels stay for
a human reader, and §13 records that its quoted numbers came off the ambiguous form. Safe to rename
because iteration 6 established there is no offline parser for this record.

---

## Where this leaves the increment

Ten adversarial iterations. **Three of them found the defect inside my own previous iteration** —
the tolerance value left outside the loop after moving its receipt in (1), an overstated
control-flow claim about the explicit stage (8), and a census that overstated what runs (7) — plus
iteration 9, which found that the CI gate I had shipped over-counted by 26 and, in its first
"more principled" rewrite, both degenerated to zero and took over five minutes on one file.

**The one thing that has not changed: every new field is emitted and unread.** `phys_blocks_local`,
`taylor_covers_last_newton_iter`, `tau_raw_block_max`, `worst_krylov_rho_D`, `event_basis`,
`rhs_dir_is_first_arnoldi` and the per-arm numrange keys all need a dt=600 run. Nothing in this
increment measures the solver's behaviour on `em_b_wave`; it is static analysis, contract tests and
CI gates throughout. **Independent review: NOT RUN.**
