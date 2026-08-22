# First-failure classification: em_b_wave, dt=600 — the measurement

The classifier (`wrf_sdirk3_first_failure.h`) exists to answer one question the campaign
could not: **"the step did not complete" holds seven distinguishable outcomes, and a dt sweep
is the right experiment for exactly one of them — which one is occurring?**

Full build in an isolated worktree at `main` = `60bd856` + the R13.3 loop commit.
`test/em_b_wave`, `sdirk3_imex_split_mode = 3` (ARK324), `time_step = 600`, np=1.

---

## The ladder

Single-variable: `WRF_SDIRK3_MAX_NEWTON_ITER`, nothing else touched.

| budget | category | layer | newton_iters | R_first → R_last | best_krylov_rel | gmres_fail | rejected |
|---|---|---|---|---|---|---|---|
| 3 (namelist) | `newton_budget_exhausted` | newton_iteration_budget | 3 | 1 → 0.4855 | 0.5526 | 0 | 0 |
| 12 | `krylov_stagnated` | preconditioner_or_operator | 4 | 1 → 0.4578 | 0.5526 | 1 | 1 |
| 40 | `krylov_stagnated` | preconditioner_or_operator | 4 | 1 → 0.4578 | 0.5526 | 1 | 1 |

All three abort at **stage 2**, then `outcome=20`, then the Fortran fatal gate. One
`SDIRK3_FIRST_FAILURE` record per run — the classifier reports exactly once, as designed.

---

## What is MEASURED

1. **The first refusal at an adequate Newton budget is the LINEAR solve.**
   `gmres_total_failures=1` at Newton iteration 4, `category=krylov_stagnated`.

2. **It is not budget-limited.** Budgets 12 and 40 produce **bit-identical records in every
   field**. Newton stops at 4 iterations either way. Whatever the wall is, more outer
   iterations do not move it.

3. **The linear solve never gets below 0.55 relative error**, in any of the three runs.
   `best_krylov_rel = 0.5526`, invariant under the Newton budget — so it is a property of the
   linear system, not of how often it is asked.

4. **Newton is converging, not diverging, right up to the wall.** 1 → 0.4855 (budget 3), 1 →
   0.4578 (budget 12/40). Monotone in both.

## What is EXCLUDED, in all three runs

| hypothesis | refuting field |
|---|---|
| state / EOS / metric / boundary | `entry_finite=1` |
| the RHS operator itself | `R0_finite=1` |
| **divergence — i.e. dt** | the residual **falls**, monotonically |
| trust-region / line-search policy | `steps_rejected=0` at budget 3 |
| the admissibility threshold | `gate_metric_ok=1` |

**The dt sweep was the wrong experiment.** Nothing here diverges. dt at 600, 300, 120, 60 and
20 all returned "did not complete" because the phrase covered a linear-solve failure that dt
does not address.

## What is NOT established

**Whether the Krylov failure is the preconditioner or the operator.** The layer label is
`preconditioner_or_operator` — a deliberate disjunction. This record cannot separate them, and
nothing here should be read as choosing one.

---

## A correction, recorded because the mistake is instructive

The **first** record (budget 3) was read as excluding the preconditioner: `best_krylov_rel =
0.5526` and `gmres_total_failures = 0` say the linear solve made progress and did not fail.

That reading was wrong, and the very next point on the ladder refuted it. At budget 3, Newton
was cut off **before reaching the iteration where GMRES fails**. The exclusion was an artifact
of the budget, not a property of the solve.

**A single record's exclusions are valid only for that configuration.** One point does not
make an exclusion; a ladder does. The classifier made the error cheap to find — the second run
cost one command and no rebuild — but it did not prevent it, and nothing in the record marked
the reading as under-budgeted.

## A correction to the taxonomy, from the same run

The first run classified as `newton_stagnated` → layer `residual_floor_or_split`, while the
residual had fallen monotonically 1 → 0.4855 with every step accepted and the linear solve
working. Newton had not stalled: it spent its third and last iteration short of
`newton_tol = 0.2`.

`NewtonBudgetExhausted` was missing. "Ran out of iterations" and "stopped moving" are the same
observation — `converged=0`, iteration ended — and they point at opposite work. They are now
separated by `newton_iterations >= newton_iteration_budget` **and** a residual still falling.
The measured record is pinned as a fixture in
`First_Failure_Classification_Contract`, along with the two cases that bracket it: the same
residual history with budget left (a stall), and a flat residual that also exhausted its
budget (also a stall — spending every iteration on nothing is not the budget's fault).

---

## The next measurement

The disjunction `preconditioner_or_operator` is what to split next, and the campaign already
has the instrument: a preconditioner-**off** run discriminates them. If the linear solve still
floors at ~0.55 with `M = I`, the limit is the operator; if it improves, it is `M`.

That is one run, single-variable, and it is the next thing to do — not another dt.
