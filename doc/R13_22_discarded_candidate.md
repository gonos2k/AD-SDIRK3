# R13.22 — the discard rule, not the solver, was ending stage 3

**Baseline** `c63927f` (PR #181 merged) · **Independent review: NOT RUN**.

R13.21's terminal probe measured one instance: at the iteration that ends dt=600, the candidate the
total-failure rule discarded had τ = 0.0056 and would have reduced ‖R‖ by 12.5 %. One instance is
not a frequency, and a policy argument needs one. This is that measurement, and it went further
than expected.

## What the flag does to the candidate

`gmres_total_failure = true` removes the candidate from the trust region **entirely** — the trust
loop carries `!gmres_total_failure` in its condition (`newton_solver.cpp:11492` against
`:11331`), so the step is never offered to the mechanism whose job is to decide whether a step is
usable. Only the `-M⁻¹R` recovery is tried. Read from the source, not inferred.

## Every discarded candidate measured was a descent direction

Opt-in probe (`WRF_SDIRK3_DISCARDED_CANDIDATE`), one RHS evaluation per total-failure iteration,
diagnosis only (`state_mutated=0`):

| configuration | where | ρ_S | ρ vs r₀ | ‖R‖ → ‖R(K+dK)‖ | descent? |
|---|---|---|---|---|---|
| default budget | stage 2, iter 3 | 1.048 | 0.9941 | 4.77e8 → 4.173e8 (**−12.5 %**) | **yes** |
| `stage2_restart=192` | stage 3, iter 1 | **1585** | **0.6773** | 1.35e15 → 5.42e14 (**−60 %**) | **yes** |

The stage-3 row is the striking one: that solve **removed 32 % of its own initial residual**, and
the ‖b‖-normalised rule flagged it a total failure because ρ_S = 1585 — the residual is 1585× ‖b‖,
which at a warm start says nothing about progress.

## Switching the coordinate: stage 3 goes 8.7 % → 66.6 %

`WRF_SDIRK3_KRYLOV_FAILURE_VS_R0=1` is an **already-shipped opt-in** that judges the same predicate
against r₀ instead of ‖b‖. Single variable, `stage2_restart=192` held fixed:

| rule | category | newton_iters | steps accepted | R_first → R_last |
|---|---|---|---|---|
| `‖b‖` (default) | `zero_update_after_total_failure` | 2 | 1 | 0.9999 → 0.9128 (**−8.7 %**) |
| r₀ (opt-in) | `krylov_diverged` | **11** | **10** | 0.9999 → **0.3337** (**−66.6 %**) |

**The `‖r‖/‖b‖` discard rule was ending the stage-3 Newton loop after one step.** With the r₀
coordinate the loop runs eleven iterations and takes the residual down by two thirds.

## What this does NOT establish

- **The step still does not complete.** Both arms end `outcome=20`, zero timesteps. The wall moved;
  it did not vanish.
- The r₀ arm ends on `krylov_diverged` at iteration 11 with `worst_krylov_rel_vs_r0 = 1.003` — a
  genuine linear-solve divergence appears later, and it is a *different* failure from the one it
  replaced.
- **n = 1 per arm**, one stage of one timestep of one case, and only one discard per run — so
  "every discarded candidate was a descent direction" is 2 for 2, not a rate.
- An earlier version of this note would have said the r₀ rule "changes nothing": that was measured
  on the **default** budget, where `vs_r0 = 0.9941 ≥ 0.99` fires the r₀ rule too. Scoping a negative
  result to the configuration it was measured in is the whole point — at the budget that reaches
  stage 3 it changes a great deal.

## On the record

`discarded_candidates` and `discarded_were_descent` are emitted on `SDIRK3_FIRST_FAILURE`, so how
many usable steps a run threw away is a field rather than a probe you have to re-run. (Written as
counters first with no consumer — the class this campaign spent two self-review rounds closing —
and wired before shipping.)
