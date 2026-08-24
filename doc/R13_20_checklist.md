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

Following that found the larger one. The explicit branch hard-set `last_stage_converged_ = true`
and all three gate metrics to `0`, so `handle_stage_gate` **early-returned on every explicit
stage** — nothing classified it, and a non-finite explicit tendency passed silently into the next
stage's state, where `entry_state_finite` caught it. **One stage late, attributed to the wrong
stage**: exactly the "first, not worst" misattribution this classifier exists to prevent. This is
live on `em_b_wave`/ARK324, where stage 1 is explicit on every timestep.

Both fixed. The signals are produced where the tendency is evaluated (one GPU→CPU sync per
timestep inside a `NoGradGuard`, the same shape and justification as `last_stage_entry_finite_`),
and a non-finite tendency now sets `converged = false` **and** all three metrics to infinity — all
three, because otherwise the `gate_metric_mode` chosen at runtime decides whether a NaN is caught.

**Behaviour change, stated plainly.** On any step whose explicit tendency is finite this is
byte-identical: finite → `converged = true`, metrics 0, gate early-returns, exactly as before. It
differs *only* when the tendency is non-finite, where the previous behaviour was to advance a NaN.

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

Classified rather than dumped: **100 sit behind a `debug_level` / probe gate; 18 do not** and can
run in a production step — NaN/Inf health checks and norm computations on the RHS path, i.e. the
hot path, which is where the rule matters most. All 18 are now `guarded_item<T>()`. The remaining
11 outside that file are guarded in place with an explicit `NoGradGuard` rather than a new include,
to avoid pulling `wrf_sdirk3_autograd_utils.h` into five more headers.

**And it is now a ratchet, not a one-time cleanup.** `.github/ci/lint_item_guard.py` does the
brace-depth scan; `check_ratchets.sh` gains a second baseline
(`tests/lint_item_guard_baseline.txt`, at **100**) with the same three conditions as `from_blob`:
actual must equal the head baseline, the head baseline may never exceed the base branch's, and the
file must be one integer line. Fixing sites *requires* lowering the baseline; adding one fails.

**Negative-tested, because a receipt that cannot fail is not a receipt.** Appending one unguarded
`.item()` to a header takes it to `actual=101 head_baseline=100 → FAIL`; removing it restores
`OK`. Extending the script rather than the workflow keeps the YAML untouched — this repo has
rotted a CI-side counter four times — and the one YAML edit (the step's name, now naming both
ratchets) passes `actionlint` locally.

**What it does not prove**, stated: a guarded `.item()` can still be a GPU sync on a hot path, and
a detached operand says nothing about whether the sync belongs there. This bounds one failure mode.
