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
