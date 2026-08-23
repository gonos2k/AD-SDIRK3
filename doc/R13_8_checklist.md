# R13.8 — non-interfering frozen A/B authority

Source: the deep static review of R13.7 (`b53b394`). Every item re-checked in the code before
being written down.

**All confirmed.** And the headline is uncomfortable: R13.7 *added* `ab_attributable()` — the
rule whose whole purpose is to stop an unattributable A/B being read as a result — and then
**did not call it from the emitter that produces the A/B.** That is the sixth occurrence of
"a rule computed and its consumer reading something else", committed in the same PR that
introduced the rule against it.

Legend: `[ ]` open · `[x]` closed · `[~]` partial · `[B]` blocked upstream.

---

## The two that invalidate the R13.7 conclusion

- [x] **A1 — the probe mutates the production preconditioner.** (review P0-1)
      CONFIRMED at two sites, both `mutable` lambdas:
      - `precond_func = [this, variable_pc_event, fallback_locked = false](…) mutable`
        (`newton_solver.cpp:6888`) — a latch; once set, every later call returns identity.
      - `gmres_M_inv = [… call_count = 0, refinement_active = true](…) mutable` (`:7469`) —
        the defect gate evaluates **only** on `call_count == 0` and can set
        `refinement_active = false` permanently.

      The frozen A/B calls `gmres_M_inv` five times (m = 4, 8, 16, 32, 48) **before**
      production's own solve uses the same object, and `solve_fgmres` takes it by
      `const std::function&` — which does not prevent a `mutable` target from changing.

      Two consequences: the M arms are **not independent of each other** (only the m=4 arm ever
      sees `call_count == 0`), and **the diagnostic changes the run it observes**. In this
      particular run `refinement=1`, so the defect wrapper was not installed — but
      `fallback_locked` is live regardless, and the structural defect is unconditional.

      FIX: a fresh preconditioner per arm. Failing that, snapshot/restore + exact-equality
      check on `fallback_locked`, `refinement_active`, `call_count`, `variable_pc_event`,
      `precond_fallback_count`, and fail the record closed when any moved.

- [x] **A2 — the live emitter does not consume `ab_attributable()`.** (review P0-4)
      CONFIRMED: `grep -c ab_attributable wrf_sdirk3_newton_solver.cpp` → **0**. The rule
      exists, its synthetic contract passes, and nothing in the emitting path calls it. The
      record carries `stage / arm / j / rho_true / rel_reported / iters / termination` and no
      verdict.
      FIX: build the `AbComparison` at the emit site, emit `ab_valid` + `ab_reason`, and
      suppress every conclusion-shaped field when `ab_valid = 0`.

---

## Contract defects in what R13.7 claimed

- [x] **A3 — `tol = 0` does not disable early stop.** (review P0-2) CONFIRMED. Arnoldi
      stagnation, the mid-budget hopeless probe and the restart-stagnation guard are gated on
      `no_early_stop`, not on `tol`. Every observed row *did* terminate `MaxBudget`, so the
      measurement is not contaminated — but the **contract** was written as
      `early_stop_disabled` *because* `tol=0`, which is false. Set `no_early_stop` for the
      probe and report `tolerance_exit_disabled` and `observed_termination` separately.

- [x] **A4 — "equal j = equal work" is wrong.** (review P0-3) CONFIRMED. Equal `j` is equal
      **Arnoldi dimension**. The M arm additionally performs `M⁻¹v` per direction, plus the
      preconditioner's internal solve, plus (when refinement is on) an extra `A` application.
      Correct phrasing: *equal Arnoldi dimension and equal outer matvec budget.* Add
      `A_apply_count`, `M_apply_count`.

- [x] **A5 — the attribution rule is itself too weak.** (review P0-5) CONFIRMED: two arms that
      failed the **same** way (`NanRetryExhausted` / `NanRetryExhausted`, or both on an FD
      fallback operator) satisfy `termination_a == termination_b`. Same-wrongness is not
      attribution. Needs per-arm validity, freshness flags, finite-ρ flags, and a termination
      **allow-list** (`MaxBudget` / `ToleranceReached`) rather than mere equality.

- [x] **A6 — the A/B does not certify which Jacobian it compared.** (review P0-6) CONFIRMED:
      the probe runs under `torch::NoGradGuard ng_ab` and records nothing about
      `jvp_method`, `fd_fallback`, or tangent semantics. An FD matvec is not guaranteed linear
      (block-dependent ε), and FGMRES presumes a linear operator. Record the JVP receipt and
      measure repeatability / homogeneity / additivity.

- [x] **A7 — `rho_true` is not the physical residual.** (review P0-7) CONFIRMED: with scaling
      on, the operator is `S⁻¹AS` and FGMRES minimises a further `D⁻¹`-weighted norm. What is
      reported is the unweighted Euclidean residual **in the S-Krylov coordinates**. Permitted
      conclusion: *identity was lower in ρ_S.* Not established: the solver objective ρ_D, the
      physical residual, or the stage-gate WRMS.

- [x] **A8 — five budgets are five separate solves, not one nested trajectory.**
      (review P0-8) CONFIRMED, and it compounds A1: with a stateful `M`, the first four
      vectors of the m=8 solve need not equal those of the m=4 solve. One run per arm to
      j=48 with checkpoints, plus an arm-order-reversed repetition.

---

## Carried from the classifier

- [x] **B1 — an unmeasured final residual is classified as divergence.** (review §11.1)
      CONFIRMED: `residual_last = -1.0` is the not-measured sentinel and
      `if (!measured(residual_last)) return NewtonDiverged;` maps it to divergence alongside
      NaN/Inf. Needs `final_residual_measured` and an `insufficient_evidence` route — the same
      defect R13.6 fixed for `R0` and left here.

- [x] **B2 — `gmres_successes` does not count successes.** (review §11.2) CONFIRMED: it is
      incremented whenever the solve was not a *total* failure, so a solve that ended at
      ρ = 0.5 counts as a success. Rename to what it counts, and count convergence separately.

- [x] **B3 — the numerical-range record does not name its coordinates.** (review §10)
      CONFIRMED: `UNPRECOND` means `M = I`, not "unscaled". With block scaling the projected
      operator is `D⁻¹S⁻¹AS`. The negative witness stands **for the operator GMRES iterated**;
      it does not transfer to raw physical `A` without a coordinate receipt. Add
      `operator_coordinates`, `block_scaled`, `S_digest`, `D_digest`, and the Arnoldi-relation
      and orthogonality defects.

- [x] **B4 — A/B records lack identity.** (review §12) CONFIRMED: no `global_timestep`,
      `rank`, `tile`, `solver_id`, `probe_seq`. And `abs().sum()` as a digest cannot separate
      `v` from `−v` or from a permutation — the same weakness fixed in
      `carried_state_digest` and reintroduced here.

---

## What survives R13.7 unchanged

- Freezing `(A, b, x₀)` and routing both arms through the **same** `solve_fgmres` is a real
  advance over the run-level comparison, and the reason the review's verdict is scoped rather
  than a full retraction.
- On that one Newton-0 system, in ρ_S, the identity arm was lower at every measured `j`.
- Removing the production preconditioner still does not complete the step.
- `fresh_solver_per_arm` now blocks false stage-reference certification.

## What must NOT be said until R13.8 closes

- that the production preconditioner is causally harmful (A1: the arms were not independent
  and the probe perturbs production);
- that the diagnostic is an observer (A1);
- that the identity wins in the solver objective or the stage-gate metric (A7);
- that raw physical `A` is indefinite (B3).


---

## Closeout

ctest **60/60**.

### Closed

- **A1** — `pristine_M` is copied before any arm runs (nothing has called `gmres_M_inv` at
  that point), each row takes its own copy, and production is restored from it.
  Non-interference is **measured** via `precond_fallback_count_`, not assumed, and reported as
  `probe_noninterfering`.
- **A2** — the emitter builds an `AbComparison` and emits `ab_valid` / `ab_reason`. Every row
  carries `ab_valid`, and the record states in words that no comparison may be drawn when it
  is 0.
- **A3** — `early_stop_disabled` now comes from `no_early_stop_enabled()`, the real accessor.
  `tolerance_exit_disabled` is reported separately, because `tol=0` only closes that one exit.
- **A5** — the rule gained per-arm freshness, non-interference, JVP authority, finite-ρ, and a
  termination **allow-list** (`MaxBudget` / `ToleranceReached`) instead of bare equality.
- **A8** — solved differently and, for this purpose, better: rather than one nested trajectory,
  **every row gets a fresh preconditioner**, which makes rows independent of each other
  directly. Arm order is additionally reversed in a second pass and `worst_order_delta` is
  reported — if the numbers move with the order, something is still stateful.
- **B1** — `final_residual_measured` separates the `-1` sentinel from NaN/Inf. R13.6 fixed this
  for `R0` and left it here; the sentinel was being reported as `newton_diverged`.
- **B2** — `gmres_successes` → `gmres_non_total_failures` (what it counts) plus
  `gmres_tolerance_reached` (what the old name implied).

### Closed in the second pass

- **A4** — `A_applies` is counted per row via a counting wrapper on the operator, and the
  claim reads *equal Arnoldi dimension* everywhere. The M arm's extra `M⁻¹` work is now
  visible rather than implied.
- **A6** — **the operator's linearity is measured, not assumed.** `e_repeat`, `e_homogeneity`
  and `e_additivity` are computed on the same operator the arms use, and
  `jvp_authoritative` is now `fd_fallback_free AND operator_linear`. FGMRES presumes a linear
  operator; an FD matvec with a block-dependent ε is not one, and the failure is silent —
  the Arnoldi relation simply stops describing what was computed.
- **B4** — the digest is norm + sum + first moment, so it separates `v` from `−v` and from a
  permutation. `abs().sum()` could do neither.

### Partial, and why

- **A7** — `rho_phys` (the `S`-weighted physical residual) now sits beside `rho_S` on every
  row, so the conclusion no longer rests on one norm. **ρ_D** (FGMRES's own `D⁻¹`-weighted
  objective) and **ρ_WRMS** (the stage gate's) are still not emitted: both live inside
  `solve_fgmres` and would need it to report them.
- **B3** — the record now names its coordinates (`operator_coordinates`, `block_scaled`,
  `right_precond=identity`), so "UNPRECOND" can no longer be read as "unscaled". The
  **Arnoldi-relation and basis-orthogonality defects** are still not emitted; they need the
  basis `V` retained, which the capture does not currently keep.

---

# The re-run, with the record judging itself

`WRF_SDIRK3_FROZEN_MI_AB=1 WRF_SDIRK3_NO_EARLY_STOP=1 WRF_SDIRK3_MAX_NEWTON_ITER=12`

```
ab_valid=1 ab_reason=ok
  early_stop_disabled=1        (the real accessor, not inferred from tol=0)
  jvp_fd_fallback_free=1
  operator_linear=1   e_repeat=0  e_homogeneity=4.277e-07  e_additivity=3.017e-07
  probe_noninterfering=1       (precond_fallback_count unchanged — measured)
  worst_order_delta=0          (arm order reversed: bit-identical)
```

| j | ρ_S (M) | ρ_S (I) | **ρ_phys (M)** | **ρ_phys (I)** | phys ratio | A_applies |
|---|---|---|---|---|---|---|
| 4 | 0.7505 | 0.5469 | 0.8793 | 0.5590 | 1.57 | 5 / 5 |
| 8 | 0.5525 | 0.3639 | 0.7329 | 0.2976 | 2.46 | 9 / 9 |
| 16 | 0.4799 | 0.3095 | 0.6832 | **0.1134** | **6.02** | 17 / 17 |
| 32 | 0.4223 | 0.2962 | 0.6175 | **0.0955** | **6.47** | 33 / 33 |
| 48 | 0.3604 | 0.2853 | 0.4842 | **0.0956** | **5.06** | 49 / 49 |

## The physical norm changes the size of the finding

In ρ_S the identity is 1.26–1.55× better. **In the physical residual it is 5–6.5× better**, and
the shape differs too: the identity arm reaches ρ_phys ≈ 0.095 by j=32 and plateaus, while the
production preconditioner is still at 0.48–0.62.

That is exactly why A7 mattered. The metric the earlier record reported was the one in which
the difference is *smallest*.

## Operator linearity — measured, not assumed

`e_repeat = 0` (bit-exact under repetition), `e_homogeneity = 4.3e-07`,
`e_additivity = 3.0e-07`. Both are at float32 epsilon (1.2e-07), so **the operator FGMRES was
handed is linear to working precision** and its premise holds. This was an assumption in every
previous measurement in this campaign.

## The numbers are unchanged from R13.7 — and that is worth stating

ρ_S is identical to R13.7's, digit for digit. R13.7's method was unsound and its numbers were
nonetheless right: the staleness hazard was structurally real and did not bite in that run
(`refinement=1`, so the defect wrapper was never installed, and the fallback latch never
fired).

A defect that has not yet bitten is not the same as no defect — it bites the moment one
setting changes, and retroactively casts doubt on every measurement taken before. What is new
is not the number but `worst_order_delta=0` and `probe_noninterfering=1`: **evidence** that it
did not bite, where before there was none.

## Still not established

- **ρ_D** — FGMRES's own `D⁻¹`-weighted objective. The open question this raises is sharp:
  `M` may be helping the norm the solver actually minimises while hurting both ρ_S and ρ_phys.
  That would make it a preconditioner for the wrong objective rather than a bad one, and it
  matches the earlier finding that aligning the Krylov objective made convergence worse.
- **Newton iteration 0**, not the iteration where GMRES fails (≈4).
- **ρ_WRMS**, the stage gate's own metric.


---

# Third pass — the two remaining halves (R13.9)

- **A7 closed.** `solve_fgmres` now *publishes* the left weight it minimised under
  (`d_inv_out`), and the probe forms ρ_D from it. Not rebuilt from the same rule — a second
  copy of a convention is how the two drift apart.
- **B3 closed.** The numerical-range record emits `e_orthogonality` (`‖VᵀV − I‖_F`) and
  `e_arnoldi` (`‖BV_m − V_{m+1}H̄_m‖_F / ‖BV_m‖_F`), which decide whether `H_m` is a projection
  at all — and then **re-evaluates the witness independently**: `v_min = V·y_min`,
  `q = ⟨v_min, B·v_min⟩/⟨v_min, v_min⟩` on the full operator, by a path that never touches the
  Hessenberg's arithmetic. `witness_confirmed=1` only if `q < 0`.

Measurements pending the rebuild; results appended below.

## Third-pass measurements

### ρ_D — the "wrong objective" hypothesis is REFUTED

| j | ρ_D (M) | ρ_D (I) | D ratio | ρ_S ratio | ρ_phys ratio |
|---|---|---|---|---|---|
| 4 | 0.7750 | 0.5497 | 1.41 | 1.37 | 1.57 |
| 8 | 0.5879 | 0.3534 | 1.66 | 1.52 | 2.46 |
| 16 | 0.5205 | 0.2853 | 1.82 | 1.55 | 6.02 |
| 32 | 0.4617 | 0.2721 | 1.70 | 1.43 | 6.47 |
| 48 | 0.3843 | 0.2623 | 1.47 | 1.26 | 5.06 |

The identity beats the production preconditioner **in all three norms** — the solver's own
objective included. `M` is not "a preconditioner for the wrong objective"; it loses in the
norm it is optimised against. The hypothesis raised at the end of R13.8 is closed, by
measurement, in the negative. `ab_valid=1` throughout; `e_repeat=0`, `e_homogeneity=4.5e-07`,
`e_additivity=3.0e-07`.

### Numerical range — the witness is confirmed by an independent path

```
m=20  min_eig_sym=-976.8   q_min_direct=-976.8   witness_confirmed=1
      e_orthogonality=3.3e-04   e_arnoldi=9.5e-08
m=20  min_eig_sym=-1913    q_min_direct=-1913    witness_confirmed=1
      e_orthogonality=4.6e-04   e_arnoldi=9.0e-08
```

`q_min_direct` — `⟨v_min, B·v_min⟩/⟨v_min, v_min⟩` evaluated on the **full operator**, by a path
that never touches the Hessenberg's arithmetic — agrees with the projected eigenvalue to four
digits. The Arnoldi relation held to 1e-07 (float32 ε level) and the basis is orthonormal to
3e-04. So `H_m` genuinely **is** `VᵀBV`, and the negative direction is real in the space the
solver iterates.

The record now also names that space: `operator_coordinates=D_left_S_krylov`,
`block_scaled=1`, `right_precond=identity`. The witness holds for `D⁻¹S⁻¹AS`. It is **not**
a statement about raw physical `A`, and the record no longer lets it be read as one.

## Checklist state

All twelve review items closed. Adversarial review in progress.

## The FAILING iteration — `WRF_SDIRK3_FROZEN_MI_AB_ITER=3`

Every earlier A/B was at Newton iteration 0: the cleanest system, and not the one where GMRES
fails. The first-failure ladder put the failure at iteration 4 (0-indexed 3) for every
adequate budget. This freezes that system. `ab_valid=1`, `worst_order_delta=0`,
`operator_linear=1` (`e_hom=3.7e-07`, `e_add=2.7e-07`), `b_norm=476`, `x0_digest≠0` (a
warm start is in play here, shared by both arms).

| j | ρ_S (M) | ρ_S (I) | ρ_phys (M) | ρ_phys (I) | ρ_D (M) | ρ_D (I) |
|---|---|---|---|---|---|---|
| 4 | **1.049** | 0.9455 | 0.8725 | 0.2489 | 0.939 | 0.7223 |
| 8 | **1.029** | 0.7330 | 0.8927 | 0.2226 | 0.914 | 0.5680 |
| 16 | 0.9869 | 0.6823 | 0.9426 | 0.1882 | 0.8768 | 0.5057 |
| 32 | 0.9740 | 0.6239 | 0.9485 | 0.1822 | 0.8591 | 0.4465 |
| 48 | 0.9785 | 0.6081 | 0.8728 | 0.1842 | 0.8247 | 0.4153 |

### MEASURED — CORRECTED after adding the j=0 baseline

**RETRACTED: "anti-convergent", "the residual grows".** Those read `ρ_S > 1` against 1. But
this iteration is warm-started (`x0_digest ≠ 0`), and a minimal-residual method reduces
`‖r‖` from `‖r₀‖ = ‖b − Ax₀‖`, not from `‖b‖`. The record did not carry `‖r₀‖/‖b‖`; it does
now:

```
rho0_S=1.054   rho0_phys=0.8652
```

So M took ρ_S from **1.054 → 1.049** (j=4) **→ 0.979** (j=48). The residual did **not** grow.
M reduced it by 7% over 48 Arnoldi steps; I reduced it by 42% (1.054 → 0.608).

1. **With the production preconditioner the failing system is NEAR-STALLED** — a 7% reduction
   in ρ_S over 48 steps, and in the physical norm slightly **worse than the warm start**
   (ρ_phys 0.865 → 0.873). That last is real but must be read carefully: ρ_phys is not the norm
   FGMRES minimises, so the D-optimal iterate is free to move the physical residual the wrong
   way. It is a statement about the mismatch between D and the physical weighting, not about
   FGMRES misbehaving.

2. **Without it, the same system makes progress in every norm.** ρ_S 0.61, ρ_phys 0.18,
   ρ_D 0.42 at j=48. Not converged — but 1.6× / 4.7× / 2.0× better than M in the three norms.

3. **The effect is larger on the failing system than on the clean one.** At iteration 0 the
   M/I ratio in ρ_phys was 5–6.5×; here it is 3.5–5.2× in ρ_phys but the *absolute* picture is
   what changed: M is flat at ~0.9 while I descends. On iteration 0 both arms descended and M
   was merely slower.

### What this settles

The earlier finding that "removing M does not complete the step" stands — I does not converge
either. But the shape of M's failure on the system that actually fails is now specific: **it
is slow in the solver's own objective (ρ_D 26% in 48 steps vs I's 63%), and a net LOSS in
the physical norm at every j (ρ_phys 0.865 → 0.873–0.949)** — the per-block result says why:
its ρ_D progress is bought entirely in ru/rv/t. "Near-stalled" was the ρ_S number (7%), which is
neither the solver's objective nor the physical one. (Referee C3.)

**Method note, recorded because it is the third time this shape has appeared in this round:**
a residual ratio without its j=0 baseline is not a convergence statement. `ρ(j) > 1` was read
as growth; it was a warm start that began above 1. The fix was one more field on the record,
and the conclusion changed from "anti-convergent" to "near-stalled" — a different finding
pointing at different work.

### Classifier correction forced by this run

Production classified this iteration as **`krylov_diverged`** (`krylov_diverged=1`) — which is
correct by the rule (`raw_rel_error > 1` was observed in production's own FGMRES call) and
**refines** the earlier `krylov_stagnated`. The layer is `operator_or_timestep_or_jvp`. The
A/B then narrows it further: with `M = I` the same operator, timestep and JVP make steady
progress from the same r₀ while `M` barely moves, so the stall is attributable to `M` on this
system, and the honest layer for *this* record is the preconditioner. (Production's
`krylov_diverged` reflects `raw_rel_error > 1` in its own call — which, now that the baseline
is known, is `‖r₀‖/‖b‖ = 1.054` carried through a solve that reduced it only slightly. The
classifier's divergence test should compare against `r₀`, not `b`; tracked below.)

### The solver's own objective, with its baseline

```
rho0_D = 1.108
```

| j | ρ_D (M) | ρ_D (I) |
|---|---|---|
| 0 | 1.108 | 1.108 |
| 4 | 0.939 | 0.722 |
| 8 | 0.914 | 0.568 |
| 16 | 0.877 | 0.506 |
| 32 | 0.859 | 0.447 |
| 48 | 0.825 | 0.415 |

Both arms are **monotone non-increasing in ρ_D** — the minimal-residual guarantee, which was
an assumption about the implementation until this row. In its own objective M reduces by 26%
over 48 steps, I by 63%: M is 2.4× slower, not stalled.

The three norms together say where M's effort goes — and the inference I drew from them
first was **backwards**. It is recorded here, crossed out, because the per-block measurement
that replaced it is the point:

> ~~`D⁻¹` up-weights the small-residual blocks, so M's reduction must be concentrated there.~~

### Per-block residual of the A/B iterates, j=48 (`SDIRK3_FROZEN_AB_BLOCKS`)

`‖r_block‖/‖b_block‖` in the S coordinates, beside the same ratio at j=0. Order-invariant
(both passes identical).

| block | ρ₀ | ρ (M) | ρ (I) | M | I |
|---|---|---|---|---|---|
| ru | 1.667 | 0.428 | 0.174 | −74% | −90% |
| rv | 1.687 | 0.854 | 0.994 | **−49%** | −41% |
| **rw** | 0.877 | 0.849 | 0.239 | **−3%** | −73% |
| **ph** | 1.012 | **1.020** | 0.625 | **+1%** | −38% |
| t | 1.363 | 0.229 | 0.099 | −83% | −93% |
| **mu** | 0.858 | **0.866** | 0.135 | **+1%** | −84% |

### MEASURED

1. **M reduces the momentum and temperature residuals (ru −74%, rv −49%, t −83%) and does
   nothing — or slightly worsens — the acoustic/mass blocks (rw −3%, ph +1%, mu +1%).** It
   works on the *large*-ρ₀ blocks and leaves the small ones. The opposite of the inference.

2. **Those three blocks are not intrinsically hard.** With `M = I` the same operator takes
   rw down 73%, ph 38%, mu 84%. GMRES alone handles them; M prevents it.

3. **M beats I on exactly one block — rv (−49% vs −41%).** Its horizontal-momentum treatment
   is fine. Its vertical/acoustic treatment is the problem.

4. The aggregate norms are now explained, not inferred. ρ_D weights each block equally at j=0
   (`D⁻¹ = 1/‖r₀,block‖`), so it reports M's *average* progress across six blocks — 26%, three
   good and three nil. ρ_S is dominated by whichever blocks carry the most `‖b‖` in S
   coordinates, and M's 7% there says those are rw/ph/mu: the untouched ones. A check:
   `mean((ρ/ρ₀)²)` over the six M rows is 0.553 → 0.744, and the measured `ρ_D(M)/ρ₀_D` is
   0.825/1.108 = 0.745. The D-norm is the equal-block average to three digits.

### What this corroborates

`precond_type=2` is "UnifiedPreconditioner with W-θ coupling" — a vertical acoustic
preconditioner. **The blocks it exists to handle are the three it does not help.** This is an
independent-channel confirmation of the earlier finding that M's ph/mu rows are wrong
(ph 408× off, D_mu with the wrong sign, in the memory record `sdirk3-precond-annihilates-ph-mu`),
reached here through a controlled A/B rather than by inspecting M's coefficients.

Net-harmful at the aggregate level, then, is not "uniformly bad". It is **correct on three
blocks and broken on the three it was built for** — and the broken three are the ones that
dominate the physical residual.

### Follow-up this correction opens

- ~~The classifier's `krylov_diverged` compares against `‖b‖`.~~ **Closed.** `GMRESResult`
  now carries `initial_rel_error = ‖r₀‖/‖b‖` (set at all three FGMRES return sites, in the same
  halo-zeroed norm every later `rel_error` uses), and `krylov_diverged` means
  `rel_error > initial_rel_error·(1+1e-4)`, falling back to `> 1` only when the initial ratio
  was not measured. Contract G in `test_fgmres_contract.cpp` pins the case: a warm start that
  begins above 1 and is reduced is **not** divergence.
- ~~Per-block residual of the A/B iterates.~~ **Done** — and it reversed the inference.

---

# The third arm — Msel: M on ru/rv/t, identity on rw/ph/mu

The per-block table said M helps three blocks and fails three. Msel tests whether that picture
**composes**: keep M where it helped, identity where it did not. Diagnostic arm in the opt-in
probe; order-invariant (`worst_order_delta=0`); `ab_valid=1` for all three arms.

## Failing iteration (Newton 3), aggregate

| j | ρ_S M / I / **Msel** | ρ_phys M / I / **Msel** | ρ_D M / I / **Msel** |
|---|---|---|---|
| 4 | 1.049 / 0.946 / **0.999** | 0.873 / 0.249 / **0.231** | 0.939 / 0.722 / **0.774** |
| 8 | 1.029 / 0.733 / **0.781** | 0.893 / 0.223 / **0.237** | 0.914 / 0.568 / **0.625** |
| 16 | 0.987 / 0.682 / **0.696** | 0.943 / 0.188 / **0.168** | 0.877 / 0.506 / **0.552** |
| 32 | 0.974 / 0.624 / **0.628** | 0.949 / 0.182 / **0.192** | 0.859 / 0.447 / **0.487** |
| 48 | 0.979 / 0.608 / **0.610** | 0.873 / 0.184 / **0.203** | 0.825 / 0.415 / **0.441** |

## Failing iteration, per block, j=48

| block | ρ₀ | M | I | **Msel** |
|---|---|---|---|---|
| ru | 1.667 | 0.428 | 0.174 | 0.240 |
| rv | 1.687 | 0.854 | 0.994 | **1.127** ← worst of three |
| rw | 0.877 | 0.849 | 0.239 | 0.245 |
| ph | 1.012 | 1.020 | 0.625 | 0.606 |
| t | 1.363 | 0.229 | 0.099 | 0.128 |
| mu | 0.858 | 0.866 | 0.135 | 0.164 |

## Iteration 0, per block, j=48

| block | M | I | **Msel** |
|---|---|---|---|
| ru | 0.071 | 0.091 | 0.064 |
| rv | 0.208 | 0.334 | **0.488** ← worst of three |
| rw | 0.353 | 0.084 | 0.061 |
| ph | 0.681 | 0.513 | 0.588 |
| t | 0.104 | 0.133 | 0.120 |
| mu | 0.489 | 0.070 | 0.067 |

## MEASURED

1. **Msel ≈ I.** At the failing iteration, within 3% of the identity in every aggregate norm at
   every j. Replacing M with identity on rw/ph/mu alone **recovers the entire M-vs-I gap**.

2. **Therefore M's ru/rv/t rows contribute nothing net.** If they helped, Msel would beat I.
   It does not. The per-block "M helps ru/rv/t" was relative to M's own global iterate, not an
   independent benefit that survives composition.

3. **rv degrades when M leaves the acoustic blocks.** Msel's rv is the worst of the three arms
   at both iterations (1.127 vs 0.854/0.994; 0.488 vs 0.208/0.334). The mass tendency carries
   the horizontal divergence of (u, v), so M's mu row is coupled to rv; block-selective
   identity breaks that coupling in a way that costs rv. **The blocks are not separable**,
   and "fix the three bad rows" is not the same as "drop them".

4. rw and mu under Msel match I (0.245/0.164 vs 0.239/0.135); ph under Msel is marginally
   *better* than I (0.606 vs 0.625). Identity on the acoustic blocks is as good as it gets
   among these three arms — and still leaves rw/ph at 0.25/0.6, so **none of the arms solves
   the failing system.**

## What this says about the next move

Not "drop M on rw/ph/mu" — that is Msel, and Msel is just I. Not "tune M's ru/rv/t" — they
contribute nothing net. The measurement points at **rebuilding M's acoustic block** (rw–ph–mu
and its coupling into rv through mu) so that it does better than identity there, which is
where the earlier coefficient-level finding (ph 408× off, D_mu wrong sign) already pointed.
What is new is that the case for it is now a controlled, block-level, order-invariant
measurement rather than a coefficient inspection — and that the coupling into rv is a
constraint any replacement has to respect.


---

# Adversarial review, round 2 (red team + numerical-analysis referee) — what changed

Two independent reviewers, no shared context with the work. Both reports are in the session
scratchpad; every finding below was confirmed in code before it was acted on.

## P0 — confirmed, fixed

- **The first-failure signals accumulated for the life of the run** (red team P0-1, **the
  seventh** "rule computed, consumer reads something else"). `reset_stats()` cleared the nine
  fields it had in 2025 and none of the ten added since. `best_krylov_rel_error` was the
  process-wide minimum, `krylov_diverged` a lifetime latch, `all_steps_rejected` unreachable
  after the first accepted step of the process, and `initial_residual_measured` stayed true —
  so R13.5's fix held for the first solve only. The reported classifications were at the first
  failing stage of each run and were right by ordering, not by mechanism.
  **Fix:** `reset_per_solve()` value-initialises the struct; `Stats_Reset_Contract` pins the
  fields known today. An explicit ARK stage now resets the signals too (P1-6), and every record
  carries `signals_from_stage`.
- **"Fresh M per row" was a reference** (P0-2). The scaled-path wrapper is `[&]`, so every copy
  shared one fallback latch; the numbers survived only because the latch never fired.
  **Fix:** each row builds its own wrapper around its own by-value copy of `M_inv`; production's
  wrapper is never handed to an arm. Verified: rows identical, `worst_order_delta=0`.

## P1 — confirmed, fixed

`order_invariant` is a verdict clause (was measured and ignored); `precond_linear` is a
verdict clause (N1 — same shape, same commit); the FD-fallback counter is read before *and*
after the arms; `refinement_passes` is on the record and gates `same_solver_path` (N2); the
shared `variable_pc_event` is snapshotted around the linearity probe, which now uses
structured vectors from a probe-local generator (N3, RNG); `d_weighted` and `msel_engaged`
receipts per row (N4, P1-5i); all three norms in `all_finite`, residuals halo-zeroed,
`halo_floor_delta` on the record (P1-4); `solve_gmres` now sets `initial_rel_error` too
(P1-7 — the control arm had kept the `> 1` rule); `AB_ITER` parsed strictly with
`target_iteration_not_reached` emitted at loop exit (P1-5/N6); the FGMRES-side
`SDIRK3_NUMERICAL_RANGE` carries coordinates.

## Wording the referee rejected — corrected

- **"near-stalled"** (C3iii): INVALID. Per-step ρ_D slopes on the failing system at j∈(32,48]
  are **equal** — M 0.0021, I 0.0020. I's entire advantage (0.54 of ρ_D) is accrued in
  **j ≤ 8**, which is the production budget. Correct sentence: *"the identity's first eight
  Arnoldi directions reduce ρ_D by 0.54 where M's reduce it by 0.19; beyond j≈16 both arms
  progress at ~0.002/step."* That points at what is in I's first eight directions, not at a
  stall.
- **"those blocks are not intrinsically hard"** (C4b): INVALID as stated. "I handles them"
  means the 48-dim Krylov space of the *full coupled* operator reduces the rw/ph/mu residual
  *rows* by 73/38/84% — through A's off-diagonal coupling — and says nothing about the
  acoustic sub-operator restricted to those rows and columns. Valid version: *"the rw/ph/mu
  residual rows are reducible by 73/38/84% in 48 unpreconditioned steps from this r₀ under
  the D objective."*
- **The headline "correct on three blocks, broken on the three it was built for"** (C4e):
  INVALID twice. "Correct on three" was relative to r₀; relative to the identity, **M is worse
  on five blocks and better on one (rv)**. And `precond_type=2` is "W–θ coupling": θ is `t`,
  the block M reduces *most* (−83%). Correct: *M handles θ; it fails the vertical-acoustic
  triple w/φ/μ; against the identity it loses on ru, rw, ph, t, mu and wins on rv.*
- **"M is near-stalled / net-harmful"**: "net-harmful" had been retracted in R13.6 and was
  re-asserted here. Withdrawn again. The supported statement is *worse than the identity at
  j ≤ 48 on these two systems, in all three norms.*
- **Msel** (red team P1-5ii, referee C5): it is an **output-row projection** `P₁M⁻¹ + P₂`, not
  a block-diagonal preconditioner — M⁻¹'s ru/rv/t outputs still read v's rw/ph/mu inputs. And
  at the failing iteration Msel is *worse than I on ru, rv and t* (0.240/1.127/0.128 vs
  0.174/0.994/0.099) — the rows M was said to be "good" at — while at iteration 0 it beats I
  on four blocks. "M's good rows add nothing net" is an aggregate statement at one Newton
  iteration; it does not survive the per-block table as a blanket claim. What survives:
  Msel ≈ I in every aggregate norm at the failing iteration; the decomposition does not compose.
- **"0 ∈ W(B) explains the floor"** (C4b, C6e): withdrawn for *consistent with* (see above).

## New measurements

- **D does not create the indefiniteness.** With block scaling off (`D = I`, coordinates
  `S_krylov`): `min_eig_sym = −8.3e4`, `max = +1.1e5`, 11/20 negative, `witness_confirmed=1`.
  The referee's prime suspect is cleared; the negative numerical range is in `S⁻¹AS`. Whether
  `S` creates it (vs an energy-consistent inner product, where the acoustic part is
  skew-adjoint) is the remaining half of C4(d).
- **M⁻¹ is linear**: `eM_homogeneity = 1.3e-07`, `eM_additivity = 9.5e-08` (C5 gap closed).
- **ρ_S is ≈ ¾ ph** at the failing iteration (referee C4f, from the four rows as an LP): the
  norm production's forcing test and total-failure predicate consume is dominated by one
  block, and even the identity leaves ph at 0.625 after 48 steps.

## Ranked open measurements (referee)

1. ~~D = I rerun~~ — done; D cleared.
2. **Warm start off at the failing iteration** (C3iv): ρ₀ > 1 in four of six blocks — the warm
   start is worse than zero, shared by both arms. If I from zero beats I from the warm start
   at j=7, the warm-start policy contributes to the first failure independently of M.
3. **Extended ladder j = 96, 192** (C1): the ρ_D gap closes 0.235 → 0.190 → 0.122 over
   j = 16 → 32 → 48; a late-starting preconditioner looks like this from below.
4. Block-restricted least squares over the M arm's own Z₄₈ (C4a): separates "M's rows are
   broken" from "the D-optimum spends its subspace on the cheap blocks". Zero new matvecs.
5. Per-iteration `newton_residuals` + ared/pred (C6/C7): temporal order and the Newton rate.

## Round-2 measurements, run

### Extended ladder (referee C1) — the crossing signature is real

Failing iteration, `WRF_SDIRK3_FROZEN_MI_AB_EXTEND=1`, `ab_valid=1`, `worst_order_delta=0`,
`halo_floor_delta=9.7e-05`.

| j | ρ_D (M) | ρ_D (I) | M/I | ρ_phys (M) | ρ_phys (I) |
|---|---|---|---|---|---|
| 48 | 0.825 | 0.415 | 1.99 | 0.873 | 0.184 |
| 96 | 0.698 | 0.347 | 2.01 | 0.726 | 0.135 |
| 192 | 0.487 | **0.318** | **1.53** | 0.477 | 0.124 |

From j=96 to 192 M drops ρ_D by 0.211 and I by 0.029. **The identity plateaus at ρ_D ≈ 0.32 —
it never meets the forcing term η=0.3, even at 192 — while M is still descending.** The
M/I ratio peaks at j≈96 and falls. This is exactly the cluster-plus-outliers shape the referee
said a "right on some blocks, wrong on others" preconditioner would produce: M spends its
first ~100–200 directions on outliers and only then catches up.

So the scoping is now measured, not cautionary: **"identity beats M" holds at the production
budget (j=7) by a wide margin and through j≈192; it is not a statement about M's
asymptotics, and I has a floor of its own.** Neither arm completes the solve.

### Warm start off (referee X5) — REFUTED as a contributor, in the useful direction

Same frozen (A, b) at iteration 3, `x₀ = 0` (`x0_digest=0`, `rho0_* = 1`):

| arm | j=8 ρ_D | j=48 ρ_D | j=48 ρ_phys |
|---|---|---|---|
| I from x₀=0 | 0.885 | 0.617 | 0.233 |
| I from warm start | 0.568 | 0.415 | 0.184 |
| M from x₀=0 | 0.897 | 0.814 | **1.000** |

**The warm start makes the solve easier**, despite beginning above ‖b‖ in ρ_S and ρ_D: it
is a good Krylov seed. "The failing system was made harder by the warm start" is refuted.
And from zero, M makes **no progress at all in the physical norm in 48 steps** (ρ_phys
1.000). Production classified this run `krylov_diverged` because ρ_S at j=8 was 1.001 > 1
while ρ_D had fallen to 0.897 — the referee's X1 in a single line: production judges in a
norm the loop does not minimise.

### What this round adds to the picture

- I's advantage is early (j ≤ 8) and finite (floor ≈ 0.32 in ρ_D).
- M's failure is late-starting, not permanent — but at the budget production can afford it is
  total (ρ_phys 0.89–1.00 at j=8 on the failing system).
- The warm start is not the problem.
- The production total-failure predicate compares ρ_S against ‖b‖; the r₀ rule is now
  available **opt-in** (`WRF_SDIRK3_KRYLOV_FAILURE_VS_R0`), both readings on the record.

## Round-2 measurements, second batch

### The float32 hypothesis passes its first test (referee X3, test 1)

Numerical-range record, iteration 0, preconditioner off, block scaling on:

```
m=20  hcol_norm_min=345.2  hcol_norm_max=844    (restart 1)
m=20  hcol_norm_min=753.6  hcol_norm_max=1511   (restart 2)
```

`‖H̄eᵢ‖ = ‖Bvᵢ‖` with `‖vᵢ‖ = 1` is the operator's magnitude on the directions GMRES built.
In B-coordinates `D⁻¹ ≈ 2.1e-3` on five of six blocks, so on those directions
`‖A_S v‖/‖v‖ ≈ 1.6e5 – 7e5`, where the identity term of `A_S = I − hγ S⁻¹JS` contributes
exactly 1. The float32 matvec's measured relative error is 3e-7. **Absolute error 0.05–0.2
against an identity term of 1: the part of A that makes an implicit stage different from a
singular Jacobian solve is resolved to 5–20% on exactly the directions GMRES uses.** The earlier
"not precision" verdict compared 1e-7 to a 0.37 residual. The decisive test (a float64 replay
of the frozen identity arm) has not been run; until it has, float32 is an *open* mechanism for
the floor, and a serious one.

### The negative curvature is in the diagonal blocks (referee C6d)

```
q_min_direct=-976.8   q_min_blockdiag=-977.9
```

`⟨v_min, Σ_q P_q B P_q v_min⟩` reproduces the full quadratic form to 0.1%. The indefiniteness
lives **inside the variable blocks**, not in the coupling between them — so a by-variable
block-diagonal preconditioner is the right *class* for it (the production M's structure).
Whether M's blocks are the right *values* is a separate question the per-block A/B already
answered in the negative for w/φ/μ.

### At the production budget, M does not move ph (referee X4)

Failing iteration, m=8 (production: restart 7, one cycle), `‖r_block‖/‖b_block‖`:

| block | ρ₀ | M | I | Msel |
|---|---|---|---|---|
| ru | 1.667 | 0.583 | 0.473 | 0.533 |
| rv | 1.687 | 1.536 | 1.455 | 1.727 |
| rw | 0.877 | 0.896 | **0.292** | 0.287 |
| **ph** | 1.012 | **1.003** | **0.709** | 0.721 |
| t | 1.363 | **0.249** | 0.454 | 0.422 |
| mu | 0.858 | 0.888 | **0.172** | 0.189 |

ph is ≈¾ of ρ_S. **Under M it moves 1% in the eight steps production can afford; under I, 30%.**
M beats I on one block at this budget (t). This is the block-level form of "neither arm reaches
the forcing term, and the identity fails by less".

### Production under the r₀ rule (opt-in), with time order on the record

```
category=krylov_stagnated  gmres_total_failures=1
first_krylov_failure_iter=3  first_rejection_iter=3  argmin_residual_iter=2
```

The Krylov failure and the rejection are in the **same** iteration (3) — data-flow order, the
failed solve produced the rejected step — and the residual minimum was at iteration 2. The
classifier's new time-order clause has the information it needs. Open: why this solve is still
a total failure under the r₀ rule when the probe's M arm at j=8 sits below r₀ — production's
exit ρ and reason beside the probe rows (referee cross-cutting #2) would say.
