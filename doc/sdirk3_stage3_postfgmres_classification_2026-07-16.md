# Post-FGMRES classification of the implicit-stage convergence failure (2026-07-16)

**Status: dated evidence record.** Produced with the PR 8 opt-in diagnostics
(`WRF_SDIRK3_STAGE_DIAG=1`) on branch `pr8-science-doc-stage3` at commit
`73a6da8` (production numerics identical to `main` = `f432cb24`; the branch
adds only documentation and the read-only diagnostics).

## 1. Configuration fingerprint

Everything held at its current production value — nothing was tuned for this
run (per the PR 8 diagnosis rules):

- Case: `test/em_b_wave`, single MPI rank, single tile, `OMP_NUM_THREADS=2`,
  2 timesteps (`run_seconds=1200`), **dt = 600 s** (the operational target).
- Path: implicit IMEX (`sdirk3_imex_split_mode = 3`, ARK324L2SA);
  `WRF_SDIRK3_SPLIT_EXPLICIT` unset.
- Solver: current FGMRES (flexible, right-preconditioned) inside the
  Newton–Krylov loop; Eisenstat–Walker forcing; current vertical
  preconditioner; current line search / trust region / damping.
- Krylov budget (namelist, unchanged): `sdirk3_stage2_gmres_restart = 8`
  (effective Arnoldi budget 7 per solve), `sdirk3_stage2_max_krylov_restarts
  = 1`, `sdirk3_stage2_krylov_tol = 0.0` (Eisenstat–Walker active, observed
  forcing tol 0.3).
- Newton: observed effective tolerance 0.2 (scaled-RMS), `max_newton_iter`
  small (stall exit observed at iter 2).

**Stage-index mapping.** In mode 3 the ARK324 internal stage 1 is explicit
(no Newton solve); the three implicit solves are internal stages **2, 3, 4**.
The failure historically called the "Stage-3 failure" (e.g.
`doc/sdirk3_mode3_stage3_rootcause_2026-06-20.md`, which counted implicit
solves 1–3) is the **third implicit solve = `stage=4`** in these records.

## 2. Same-format evidence, all three implicit stages (timestep 0)

Full record set (verbatim from `rsl.error.0000`):

```text
SDIRK3_NEWTON_DIAG ts=0 stage=2 iter=0 event=residual res_scaled_rms=2.345264e-02 res_l2=2.437823e+01 tol=2.000000e-01 r0=2.345264e-02 state_finite=1 rhs_finite=1
SDIRK3_STAGE_DIAG stage=2 converged=1 newton_iters=1 final_res=2.345264e-02 msg="Newton solver converged successfully"
SDIRK3_NEWTON_DIAG ts=0 stage=3 iter=0 event=residual res_scaled_rms=5.004250e-01 res_l2=1.760896e+03 tol=2.000000e-01 r0=5.004250e-01 state_finite=1 rhs_finite=1
SDIRK3_FGMRES_DIAG ts=0 stage=3 iter=0 path=fgmres rhs_norm=5.2018e+02 x0_norm=0.0000e+00 tol=3.0000e-01 restart=7 max_restarts=1 iters=7 restarts=1 final_res=2.7577e+02 rel_err=5.3016e-01 converged=0 breakdown=0 stagnation=0 dx_finite=1 variable_pc=0 msg="FGMRES max iterations reached (7 Arnoldi)"
SDIRK3_NEWTON_DIAG ts=0 stage=3 iter=0 event=update accepted=1 alpha=1.0000e+00 dk_norm=6.6951e-04 res_after=7.4664e+02 gmres_total_failure=0 state_finite=1
SDIRK3_NEWTON_DIAG ts=0 stage=3 iter=1 event=residual res_scaled_rms=2.6530e-01 res_l2=7.4664e+02 tol=2.0000e-01 r0=5.0042e-01 state_finite=1 rhs_finite=1
SDIRK3_FGMRES_DIAG ts=0 stage=3 iter=1 path=fgmres rhs_norm=2.7578e+02 x0_norm=3.0820e-04 tol=3.0000e-01 restart=7 max_restarts=1 iters=7 restarts=1 final_res=1.9482e+02 rel_err=7.0643e-01 converged=0 breakdown=0 stagnation=0 dx_finite=1 variable_pc=0 msg="FGMRES max iterations reached (7 Arnoldi)"
SDIRK3_NEWTON_DIAG ts=0 stage=3 iter=1 event=update accepted=1 alpha=1.0000e+00 dk_norm=6.2629e-04 res_after=3.0286e+02 gmres_total_failure=0 state_finite=1
SDIRK3_NEWTON_DIAG ts=0 stage=3 iter=2 event=residual res_scaled_rms=1.8742e-01 res_l2=3.0286e+02 tol=2.0000e-01 r0=5.0042e-01 state_finite=1 rhs_finite=1
SDIRK3_STAGE_DIAG stage=3 converged=1 newton_iters=3 final_res=1.8742e-01 msg="Newton solver converged successfully"
SDIRK3_NEWTON_DIAG ts=0 stage=4 iter=0 event=residual res_scaled_rms=9.9999e-01 res_l2=2.7115e+08 tol=2.0000e-01 r0=9.9999e-01 state_finite=1 rhs_finite=1
SDIRK3_FGMRES_DIAG ts=0 stage=4 iter=0 path=fgmres rhs_norm=1.0395e+03 x0_norm=0.0000e+00 tol=3.0000e-01 restart=7 max_restarts=1 iters=4 restarts=1 final_res=1.0294e+03 rel_err=9.9034e-01 converged=0 breakdown=0 stagnation=1 dx_finite=1 variable_pc=0 msg="FGMRES Arnoldi stagnation early exit (restart 1/1)"
SDIRK3_NEWTON_DIAG ts=0 stage=4 iter=0 event=update accepted=1 alpha=1.0000e+00 dk_norm=2.4107e+00 res_after=2.6414e+08 gmres_total_failure=0 state_finite=1
SDIRK3_NEWTON_DIAG ts=0 stage=4 iter=1 event=residual res_scaled_rms=9.8038e-01 res_l2=2.6414e+08 tol=2.0000e-01 r0=9.9999e-01 state_finite=1 rhs_finite=1
SDIRK3_FGMRES_DIAG ts=0 stage=4 iter=1 path=fgmres rhs_norm=1.0191e+03 x0_norm=1.0455e-03 tol=3.0000e-01 restart=7 max_restarts=1 iters=4 restarts=1 final_res=1.0222e+03 rel_err=1.0031e+00 converged=0 breakdown=0 stagnation=1 dx_finite=1 variable_pc=0 msg="FGMRES Arnoldi stagnation early exit (restart 1/1)"
SDIRK3_NEWTON_DIAG ts=0 stage=4 iter=1 event=update accepted=0 alpha=1.0000e+00 dk_norm=0.0000e+00 res_after=2.6414e+08 gmres_total_failure=1 state_finite=1
SDIRK3_STAGE_DIAG stage=4 converged=0 newton_iters=2 final_res=9.8038e-01 msg="Newton solver exited early (stall/stagnation) at iter 2"
```

Post-failure context (existing telemetry, same run):

```text
[NEWTON DAMP] Stage 4: K damped by 0.1226 (step=630.6 > 5*ref=15.46)
[RESIDUAL_REEVAL] Stage 4 (IMEX mode>=2): ||R_fast||=2.594e+05, ||R_newton||=0.9804, ||K_fast||=0.2957, rel_R_fast=8.775e+05, wrms=5005, wrms_growth=0.9997
[NEWTON DAMP] Stage 4: reverted (||R|| 2.594e+05 -> 2.598e+05, no absolute improvement)
[STAGE GATE] IMEX ARK Stage 4 failed ... ABORTING (severe non-convergence in mode3 safety gate)
[STEP OUTCOME] fail-closed outcome=20; skipping unifiedStep state publish before Fortran fatal gate.
```

### Comparison table (identical format, one row per implicit stage)

| | initial R (scaled-RMS) | initial R (L2) | Newton contraction | FGMRES per Newton iter | outcome |
|---|---|---|---|---|---|
| stage 2 | 2.35e-2 | 2.44e+1 | already < tol | not needed | **converged** (1 iter) |
| stage 3 | 5.00e-1 | 1.76e+3 | 0.50 → 0.27 → 0.19 (< tol 0.2) | 7/7 Arnoldi, rel_err 0.53 / 0.71, no stagnation | **converged** (3 iters) |
| stage 4 | 1.00e+0 | **2.71e+8** | 1.00 → 0.98 → stall | **4 Arnoldi, stagnation=1, rel_err 0.990 / 1.003** | **FAILED** → gate outcome=20 |

Two stage-4-only anomalies, each absent at stages 2/3:

1. **The linear solve stagnates, and the available updates give negligible
   nonlinear progress.** Precisely (`rel_err` is the LINEAR quantity
   `‖b−Ax‖/‖b‖`; it says nothing directly about nonlinear descent):
   stage 3's FGMRES also misses its tolerance (rel_err 0.53–0.71 on the
   same 7-vector budget), yet its directions contract the nonlinear
   residual 0.50→0.19. Stage 4's FGMRES *stagnates* (early exit after 4 of
   7 vectors): at iter 0 it reduces the linear residual by only ~1.0%
   (rel_err 0.990), and at iter 1 it ends ABOVE ‖b‖ (rel_err 1.003, with
   x0 ≈ 0 so the start is ≈ ‖b‖ — no net reduction). The nonlinear
   consequences, as measured: the iter-0
   step IS accepted and reduces ‖R‖ by only 2.6% (2.71e8 → 2.64e8 — weak
   but real descent, so "non-descent direction" would overstate); the
   iter-1 direction is rejected **by the total-failure policy**
   (`gmres_total_failure=1`, predicate rel_err ≥ 0.999) *without a
   nonlinear trial evaluation* — no descent measurement exists for that
   direction; the externally damped fallback (a different direction: the
   damped K, not the Krylov correction) measurably increases
   ‖R_fast‖ 2.594e5 → 2.598e5 and is reverted. Net: two Newton iterations
   move the scaled residual 1.000 → 0.980 and the stage stalls.
2. **The stage-4 operand is ~5 orders of magnitude larger.** Initial
   nonlinear residual L2: 1.76e+3 (stage 3) vs 2.71e+8 (stage 4). The
   scaled-RMS hides this by construction (S is built from R0, so
   `r0_scaled ≈ 1`); the unscaled magnitude is the anomaly.

## 3. Hypothesis classification (A–H), evidence and refutation

- **A. Linear solve failure — CONFIRMED as the proximate mechanism, not the
  root.** Evidence: stagnation=1, rel_err 0.990/1.003 at stage 4 only —
  the LINEAR solve reduces its residual by only ~1.0% (iter 0) and then
  not at all (iter 1, rel_err 1.003 > 1). Refutation as root cause:
  stage 3 fails the same linear tolerance on the same budget (0.53/0.71)
  and still converges — an unconverged linear solve is not sufficient for
  nonlinear failure. What distinguishes stage 4, as measured, is that its
  one evaluated direction yields only 2.6% nonlinear reduction and its
  second direction is rejected untested by the rel_err ≥ 0.999 policy.
  Whether the stagnated directions are strictly non-descent for the
  nonlinear merit was NOT directly measured (no directional-derivative or
  trial evaluation exists for the rejected direction) — that is
  discriminating measurement (4) in §3a.
- **B. JVP inconsistency / non-finite direction — the NON-FINITE half is
  EXCLUDED; the CONSISTENCY half is NOT INDICATED but NOT excluded by this
  run.** Measured: every record shows `state_finite=1 rhs_finite=1
  dx_finite=1`, zero NaN-retry events, no `breakdown` — a non-finite or
  NaN-poisoned direction did not occur. NOT measured here: a JVP-vs-FD
  directional consistency check **at the stage-4 state** (a finite but wrong
  J·v is one way to produce exactly the observed linear stagnation and
  near-zero nonlinear progress). The
  standing JVP/FGMRES contract tests are green at this commit, but they
  exercise test operators, not this operand. This is discriminating
  measurement (1) in §3a.
- **C. Nonlinear globalization failure — EXCLUDED as root (observed
  following its policies).** At stage 4: iter-0 accepts a step that
  reduces R by 2.6% (2.71e8→2.64e8); iter-1 rejects by the
  `gmres_total_failure` policy (rel_err ≥ 0.999) without a trial
  evaluation; external damping (0.1226) is tried and honestly reverted
  when ‖R_fast‖ increases. Every observed decision matches its documented
  policy. Caveat within this run's scope: the untested iter-1 rejection is
  a POLICY choice — this run cannot say whether a trial evaluation of that
  direction would have progressed (see §3a item 4); it can only say the
  globalization did not misbehave relative to its rules.
- **D. Residual scaling / WRMS gate artifact — EXCLUDED.** The gate fired on
  a genuine non-contraction (`wrms_growth=0.9997` — no contraction), the
  scaled and unscaled residuals tell the same stall story, and the fail-close
  abort (`outcome=20`) matches the recorded stall rather than creating it.
- **E. Preconditioner / Krylov-subspace quality — MEASURED symptom;
  CONSISTENT WITH the prior indefiniteness measurement, but not
  independently re-established by this run.** Measured here: stagnation
  within 4 Arnoldi vectors with rel_err ≈ 1 under the vertical
  preconditioner, at stage 4 only. NOT measured here: the operator spectrum
  (no Ritz/numerical-range probe ran in this configuration). The prior
  Wall-1 measurement (`doc/sdirk3_walls_measurement_2026-07-05.md`:
  intrinsic indefiniteness of `A = I − dt·γ·J_fast` at dt=600) is the
  standing explanation this data is consistent with — but per §4 those
  records are pre-FGMRES and configuration equivalence is unproven, so this
  run treats indefiniteness as the leading HYPOTHESIS for the stagnation,
  not as re-confirmed fact. Alternatives not excluded by this run: a
  preconditioner defect specific to the stage-4 operand scale, or a wrong-
  but-finite JVP (see B).
- **F. Stage RHS/state construction — MEASURED: the stage-4 operand is ~5
  orders larger; NOT DETERMINED by this run whether that is physical
  over-extrapolation or a construction defect.** What this run measured is
  only the magnitude jump (initial nonlinear residual L2 1.76e+3 → 2.71e+8
  between the second and third implicit solves) — a large operand is what
  BOTH explanations would produce. The physical-over-extrapolation reading
  (Wall-2: the ARK324 explicit accumulation over-extrapolates the sheared
  jet's u-momentum tendency at large dt) and the small-dt convergence of the
  same construction come from the pre-FGMRES records, which per §4 are
  reference-only here. No per-term/per-component slice of the stage-4
  operand was run in this configuration. This is discriminating
  measurement (2) in §3a.
- **G. Geometry/metric input — EXCLUDED.** Single-rank supported path; the
  geometry contract matrix and per-call validation are standing (15-test
  suite green at this commit); no geometry markers in the run.
- **H. Timestep outside the model's convergence region — SUPPORTED, not
  confirmed by this run.** This is a single 2-step run at one dt; a
  dt-dependence claim needs the post-FGMRES dt-ladder, which was not run
  here. What this run shows is consistent with H: at dt=600 the failure
  concentrates in the stage that carries the largest accumulated operand,
  while stages 2/3 converge. The historic dt-ladder (dt ≤ ~70 converging)
  is pre-FGMRES and reference-only per §4. This is discriminating
  measurement (3) in §3a.

**Classification, scoped to what this run measured:**

- **MEASURED and confirmed: class A at stage 4** — the linear solves
  stagnate (stagnation=1 within 4 of 7 Arnoldi vectors; rel_err 0.990 /
  1.003 — a ~1.0% linear-residual reduction at iter 0 and none at
  iter 1), and the nonlinear iteration
  makes negligible progress on what they return: the one evaluated step
  (iter 0) reduces ‖R‖ by only 2.6%, the iter-1 direction is rejected
  UNTESTED by the rel_err ≥ 0.999 total-failure policy (dk_norm=0), and
  the externally damped fallback — a different direction, measured in
  ‖R_fast‖ — increases 2.594e5 → 2.598e5 and is reverted. Simultaneously
  the stage-4 operand is ~5 orders larger than stage 3's. ("Non-descent
  direction" is deliberately NOT claimed: rel_err is a linear quantity,
  the only evaluated Krylov step did descend weakly, and no
  directional-derivative/trial measurement exists for the rejected one —
  §3a item 4.) Non-finite directions, gate artifacts, and geometry inputs
  are excluded for THIS run (B's finiteness half, D, G).
- **RANKED HYPOTHESES for the root, not re-established here: E + F under
  H** — the prior Wall-1/Wall-2 measurements provide the standing
  explanation and nothing in the new records contradicts it, but this run
  did not re-measure the spectrum, the operand composition, or the
  dt-dependence, and the pre-FGMRES records are reference-only (§4).
  A finite-but-wrong stage-4 JVP (B's consistency half) also remains
  unexcluded.

**Linear vs nonlinear failure, distinguished (measured):** stage 3 shows the
nonlinear iteration tolerates failed linear tolerances (linear failure ≠
nonlinear failure); stage 4's nonlinear stall coincides with the linear
model stagnating (a ~1.0% linear-residual reduction, then none) — and the
globalization machinery is observed following its policies on those
directions. Whether those directions were strictly non-descent for the
nonlinear merit was not directly measured; the one that was evaluated
descended weakly (2.6%).

## 3a. Discriminating next measurements (diagnosis-only, no numerics change)

1. **JVP-vs-FD directional check at the stage-4 state** — separates a
   finite-but-wrong J·v (B) from an operator/preconditioner property (E).
2. **Per-term/per-component slice of the stage-4 initial operand** —
   separates physical over-extrapolation from a construction defect (F).
3. **Post-FGMRES dt-ladder** at the current head — establishes the
   dt-dependence (H) in a configuration-equivalent setting.
4. **Directional-derivative / nonlinear trial evaluation of the rejected
   stage-4 Krylov direction** — the iter-1 rejection is a policy decision
   taken without a trial; measuring the direction's actual merit-function
   slope (or one trial residual) separates "the subspace contains no
   useful direction" from "the policy discards a usable one".

## 4. Relation to pre-FGMRES records

`doc/sdirk3_mode3_stage3_rootcause_2026-06-20.md` and
`doc/sdirk3_walls_measurement_2026-07-05.md` reached the same wall structure
with the pre-FGMRES solver. Those runs used evolving diagnostics, different
budgets, and partially different code; configuration equivalence with this
run is **not proven**, so they are cited as **reference material only**, not
as a quantitative before/after comparison. Qualitatively, nothing in the
post-FGMRES records contradicts them; the FGMRES conversion changed the
linear algebra bookkeeping (P1-1), not the operator's indefiniteness or the
explicit-cascade operand growth.

## 5. What this does NOT license (per the PR 8 rules)

No numerics change is proposed or made here: no tolerance relaxation, no
restart/budget change, no preconditioner swap, no line-search change, no dt
reduction as a "fix", no stage-equation modification. If a code change is
later justified by this classification, it goes in its own commit/PR with
its own evidence.

## 6. Reproduction

```bash
# from a built em_b_wave tree (single rank, supported path)
cd test/em_b_wave
# 2 steps at dt=600, implicit mode 3 (namelist unchanged apart from run length)
env OMP_NUM_THREADS=2 WRF_SDIRK3_STAGE_DIAG=1 ./wrf.exe
grep -E "SDIRK3_(NEWTON|FGMRES|STAGE)_DIAG" rsl.error.0000
```

The run fail-closes at stage 4 with `outcome=20` (expected — that failure is
the object under diagnosis). With `WRF_SDIRK3_STAGE_DIAG` unset, the run
emits zero diagnostic markers and the split-explicit production path
reproduces the six golden stage norms bit-identically (verified both ways at
this commit).
