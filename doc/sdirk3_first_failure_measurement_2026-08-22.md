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

## Splitting the disjunction: preconditioner off

Single-variable: `WRF_SDIRK3_PRECOND_TYPE=0`, everything else fixed. Confirmed in the log by
`[PRECOND SELECTION] No preconditioner (precond_type=0)` — **not** by the `[C++ CONFIG DEBUG]
Setting precond_type = 2` line, which is the namelist being applied before the env override
and which a first, careless grep read as "the override did not take". The contradiction
between that line and two clearly different records is what exposed the bad probe.

| M | budget | newton_iters | R_last | **best_krylov_rel** | krylov_iters | gmres_fail |
|---|---|---|---|---|---|---|
| type 2 | 12 **and** 40 | 4 | 0.4578 | **0.5526** | 28 | 1 |
| I (off) | 12 **and** 40 | 2 | 0.5502 | **0.3853** | 14 | 1 |

Both arms are budget-invariant — 12 and 40 are bit-identical within each — so the comparison
is between two stable operators, not two samples.

### MEASURED

1. **The preconditioner makes the Krylov residual WORSE.** `best_krylov_rel` 0.5526 with `M`,
   **0.3853 with `M = I`**. Removing the preconditioner improves the linear solve by ~30%.

2. **Removing it does not fix the wall.** `gmres_total_failures=1` and
   `category=krylov_stagnated` in both arms. `M = I` floors at 0.3853, which is nowhere near
   a solved system.

So the disjunction resolves: **the preconditioner is not the cause of the wall, and is
separately net-harmful; the wall is in the OPERATOR.**

This reaches, from an independent channel, the same place as the earlier stage-2 budget study
(`M = I` better at both stages) — that one was a Krylov-budget experiment, this one is a
first-failure classification, and they agree.

### NOT established

That the operator is *indefinite*. That earlier conclusion was measured on the pre-Ω operator,
and its re-measurement under the corrected Ω / WRF-parity operator has never been done. This
ladder says the limit is the operator; it does not say **which property** of the operator, and
inheriting the old spectral verdict would be assuming exactly what is unmeasured.

## The next measurement

**Ritz spectrum of the operator GMRES actually iterates, on the current (Ω-corrected)
operator, with the preconditioner OFF.** Preconditioner-off because the measurement above
makes `M` a confound with no upside, and the Arnoldi Hessenberg GMRES already builds gives the
spectrum for free.

That is the outstanding question, and it has been outstanding since the Ω fix landed. Not
another dt, and not another preconditioner variant.

---

# The next measurement, run: the numerical range

Both probes in ONE run, preconditioner OFF, `WRF_SDIRK3_MAX_NEWTON_ITER=12`, stage 2.
`WRF_SDIRK3_STAGE2_GMRES_RESTART` is the only variable.

`SDIRK3_NUMERICAL_RANGE_UNPRECOND` is the symmetric part of the Hessenberg GMRES builds —
i.e. `V^T H(A) V` on the Krylov basis the solver **actually iterates**, `V` orthonormal.

| restart | m | min_eig_sym (1st) | n_neg | (2nd) | n_neg | random 24-sample |
|---|---|---|---|---|---|---|
| 8 | 7 | −953.7 | 4/7 | −2164 | 4/7 | `q_min=+5616, neg=0` |
| 24 | 20 | −976.8 | 11/20 | −1913 | 12/20 | `q_min=+5616, neg=0` |
| 48 | 41 | **−978.8** | 22/41 | **−1917** | 24/41 | `q_min=+5616, neg=0` |

## MEASURED

1. **The numerical range of the operator GMRES iterates straddles the origin.**
   `definite=0` at every m, both restarts, every budget.

2. **`min_eig_sym` is m-CONVERGED.** 7 → 20 → 41 moves it −953.7 → −976.8 → −978.8 (2.4%,
   then 0.2%); the second restart −1913 → −1917 (0.2%). This is an operator property, not a
   basis-size artifact — which the earlier Ritz study could not say of *its* extremes.

3. **The negative fraction is roughly m-invariant**, 0.54–0.60 across a 6× change in m. Also
   unlike the earlier study, where the count scaled with m.

4. **On the current Ω-corrected operator, with the preconditioner OFF.** This is the
   measurement that had been outstanding since the Ω fix.

## Why the ladders look the way they do

**Corrected (referee C4b):** an indefinite numerical range removes the Elman sufficient
condition and supplies nothing in its place. It is **consistent with** the budget-invariant
floor; it does not explain it. A definite operator of norm 10³ has an Elman rate flat for
thousands of steps, and `0 ∈ W(B)` is neither necessary nor sufficient for stagnation (the
actual `r₀` is demonstrably not a stagnating direction). The witness stands; the causal
sentence does not.

## The methodological finding, and it is the sharper one

**Random sampling of the quadratic form found NO negative curvature — 24 directions,
`neg=0`, in every single run — on the same operator, in the same process, whose Krylov basis
is more than half negative.**

The probe's own comment had warned that `neg==0` proves nothing, because "random vectors
concentrate, and a narrow negative cone is easy to miss". That is now demonstrated rather than
asserted, and there is a reason it is not bad luck:

> **GMRES's basis is adversarially selected.** It is built from the residual, so it goes
> exactly where the operator behaves badly. Random directions sample the whole space and
> concentrate; the Krylov basis is the worst case, by construction.

So a definiteness test must project onto the basis the solver actually builds. A random-vector
survey of `⟨v, Av⟩` is not a weaker version of that test — on this operator it returns the
opposite answer, with a tight cluster (q_max/q_min = 1.044) that reads as *reassuring*.

## Next

Not another spectrum. The operator's numerical range is indefinite in the space GMRES
iterates, measured and m-converged, so the remaining questions are about the **formulation**:
which terms put the negative part there, and whether a different split (or an
indefinite-aware method) removes it. The per-block observers already in the tree are the
instrument for the first half.

---

# RETRACTION (external review of R13.4, 2026-08-23)

Three conclusions above are **withdrawn**. All three review findings were confirmed in the
code before writing this.

## 1. "the preconditioner is net-harmful" — RETRACTED

The A/B was **not single-variable**. Production branches:

```cpp
auto gmres_result = gmres_M_inv
    ? krylov_methods::solve_fgmres(...)   // M on
    : krylov_methods::solve_gmres(...);   // M off
```
`wrf_sdirk3_newton_solver.cpp:8072/8108` — and the comment there says it outright:
*"Unpreconditioned solves keep solve_gmres bit-for-bit."*

So `WRF_SDIRK3_PRECOND_TYPE=0` changes the preconditioner **and the Krylov implementation**.
FGMRES with an identity preconditioner spans the same Krylov space as GMRES in exact
arithmetic, but two separate implementations are not guaranteed equal in early-stop, restart,
residual recomputation, breakdown handling or diagnostics — and none of that was checked.

Worse, the two arms ran **different Newton counts** (4 vs 2). `best_krylov_rel_error` is a
minimum over the stage, so the comparison was

  min over n=0..3 of ρ(A_n^M, b_n^M)   against   min over n=0..1 of ρ(A_n^I, b_n^I)

with `A_n = I − hγ J_F(U_eval,n)` and `U_eval,n` differing between arms. **Not the same linear
systems.** Adaptive (Eisenstat–Walker) forcing makes the effective tolerances differ too, so
"everything else fixed" was true of the namelist and false of the effective solver policy.

**What survives:** removing the production preconditioner **does not make the step complete**,
so `M` is not the sole necessary cause. Nothing about its sign or magnitude survives. "≈30%
worse" is void.

## 2. "the wall is the operator" — RETRACTED

It followed from (1). It also never separated the mathematical operator from **solver policy**:
`GMRESResult::termination_reason` already distinguishes `arnoldi_stagnation`,
`mid_budget_hopeless`, `restart_stagnation_threshold`, `nan_retry_exhausted`, `max_budget`,
`happy_breakdown` and true-residual divergence — and `StageFailureSignals` discarded every one
of them, collapsing them all into `krylov_stagnated`. An early-stop policy and an indefinite
operator arrived as the same category, and I read the category as the operator.

The layer is now the honest disjunction:
`operator_or_timestep_or_jvp_or_scaling_or_preconditioner_or_policy`.

## 3. "dt was never the question" — RETRACTED

`A_h = I − h·γ·J_F(Y_s + hγK)`. The timestep enters the Krylov operator, its field of values,
its non-normality, the right-hand side and the stage state **directly**. A falling *outer*
Newton residual shows the failure is not outer divergence; it says nothing about whether `h`
drives the *inner* conditioning.

The correct statement is **"outer Newton blow-up at dt=600 is excluded"**. And the old
600/300/120/60/20 sweep never recorded category, Krylov floor or termination reason per dt, so
it does not establish dt-independence either.

## Also corrected

- **The exclusion table over-claimed.** `entry_finite=1` excludes a NaN/Inf entry tensor — not
  a wrong EOS value, a stagger or metric sign error, a stale halo, or a finite-but-unphysical
  state. `R0_finite=1` excludes a NaN/Inf first residual — not a wrong RHS, a wrong Jacobian,
  a bad scale, or a JVP inconsistency. Renamed accordingly.
- **Self-contradiction in my own table.** It shows `rejected=1` at budgets 12 and 40 while the
  text claimed the trust region was excluded "in all three runs". It is excluded at budget 3
  only.
- **`R0_finite` could report an unmeasured residual as finite.** It was derived from
  `isfinite(initial_unscaled_residual)`, a member initialising to `0.0` — finite. A solve that
  never evaluated R0 printed `R0_finite=1`. This is my own "absence of a measurement must not
  become a finding" rule, broken in the code that states it. Now `R0_measured` gates it and the
  classifier returns `insufficient_evidence`.
- **Two budget authorities.** The loop bounds on `options_.max_newton_iter`; the record read
  `g_sdirk3_config.max_newton_iter`. The loop's value is now recorded from the loop.
- **`fresh_solver_per_arm` was never read.** R13.1 added it, the caller sets it `false`
  (snapshot/restore is not a fresh solver) and `certify_stage_reference` ignored it — so
  arms sharing a preconditioner and its caches could still certify. **Fifth occurrence of
  "a rule computed and its consumer reading something else", and the first where I wrote both
  halves in one commit.**

## What still stands

- Newton budget 3 is insufficient at dt=600 stage 2 (3/3 iterations, residual still falling).
- Budgets 12 and 40 give bit-identical summaries — the outer budget is not the binding
  constraint beyond 4 iterations.
- With an adequate outer budget the **inner Krylov path** is where the stage stops.
- Removing the production preconditioner does not complete the step.
- The **numerical range straddles the origin** — a negative eigenvalue of `Vᵀ H(A) V` with `V`
  orthonormal is a genuine witness, and only the converse (positive ⇒ definite) is invalid,
  which was never claimed. Note this does not by itself separate operator from policy either.

## The next experiment — corrected

**Not** another spectrum, and **not** another preconditioner variant. Freeze one linear system
at one Newton iteration — `(A, b, x₀, D, tol, restart, Arnoldi budget)`, digests recorded —
and run **M against I through the SAME FGMRES path** (identity closure as the preconditioner),
with early-stop **off**, comparing true-residual trajectories `ρ_j` at equal `j`.

That is the experiment that can attribute anything to `M`. It has not been run.

---

# The frozen linear-system A/B — the experiment the retraction called for

One `(A, b, x₀)` captured at **stage 2, Newton iteration 0**. Both arms run the **same**
`solve_fgmres` on it, `max_restarts=1` so `restart` *is* `j`, `tol=0` so neither arm can exit
on tolerance. The only difference is the closure passed as `M_inv`: production, or the
identity. `ρ` is the **true** residual `‖b − A·x‖/‖b‖`, recomputed by the probe rather than
taken from either arm's own bookkeeping.

```
SDIRK3_FROZEN_AB_SYSTEM stage=2 newton_iter=0
  b_digest=717053  x0_digest=0  b_norm=1039.47
```

| j | ρ (M = production) | ρ (M = I) | ratio M/I | termination |
|---|---|---|---|---|
| 4 | 0.7505 | 0.5469 | 1.37 | MaxBudget / MaxBudget |
| 8 | 0.5525 | 0.3639 | 1.52 | MaxBudget / MaxBudget |
| 16 | 0.4799 | 0.3095 | 1.55 | MaxBudget / MaxBudget |
| 32 | 0.4223 | 0.2962 | 1.43 | MaxBudget / MaxBudget |
| 48 | 0.3604 | 0.2853 | 1.26 | MaxBudget / MaxBudget |

`iters == j` in every row and both arms terminate on `MaxBudget`, so **equal j is equal work** —
the clause that is easiest to lose and that the earlier comparison never established.

## MEASURED

**The identity outperforms the production preconditioner at every Arnoldi budget**, by 26–55%
in true relative residual, on one frozen system through one code path.

## On the retraction

The earlier claim was withdrawn because **its evidence did not support it** — not because the
direction was known to be wrong. A controlled experiment now gives the same direction on
evidence that does support it. Those are different statements and the distinction is the
point: R13.4's number (0.5526 vs 0.3853) was uninterpretable, and it happened to point the
same way.

Attribution, against the rule in `wrf_sdirk3_probe_validity.h`:

| clause | how it is met |
|---|---|
| same operator | one closure object, both arms |
| same rhs / x₀ | digests emitted once, shared by every row |
| same solver path | both `solve_fgmres` — the clause R13.4 failed |
| same budget | `iters == j` in both arms |
| early stop disabled | `tol = 0`; every row terminates `MaxBudget` |
| same termination | `MaxBudget` in both arms at every j |

## Limits, stated

- **This is Newton iteration 0**, not the iteration where GMRES actually fails (≈4). It
  measures `M` on *this* system's conditioning, not on the failing one.
- **Neither arm converges.** Both are still descending at j=48 (0.3604, 0.2853), so this says
  `M` is worse, not that `I` is adequate. The wall is not removed by dropping the
  preconditioner — consistent with the earlier observation that the step does not complete
  either way.
- **It does not separate the operator from solver policy.** `termination_reason` is now
  emitted per row, and it is uniform here; plumbing it into `StageFailureSignals` for the
  production path is still open.
