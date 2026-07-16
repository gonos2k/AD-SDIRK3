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

1. **The linear solve produces no descent direction.** Stage 3's FGMRES also
   misses its tolerance (rel_err 0.53–0.71 on the same 7-vector budget), yet
   Newton contracts 0.50→0.19 — inexact Newton works when the returned
   direction is a descent direction. Stage 4's FGMRES *stagnates* (early
   exit after 4 of 7 vectors) at rel_err ≈ 0.99, and at Newton iter 1 at
   rel_err = 1.003 — the best Krylov correction makes the linear residual
   *larger*. The update is rejected (`accepted=0, dk_norm=0`), the external
   damping is reverted for "no absolute improvement", and the stage stalls.
2. **The stage-4 operand is ~5 orders of magnitude larger.** Initial
   nonlinear residual L2: 1.76e+3 (stage 3) vs 2.71e+8 (stage 4). The
   scaled-RMS hides this by construction (S is built from R0, so
   `r0_scaled ≈ 1`); the unscaled magnitude is the anomaly.

## 3. Hypothesis classification (A–H), evidence and refutation

- **A. Linear solve failure — CONFIRMED as the proximate mechanism, not the
  root.** Evidence: stagnation=1, rel_err 0.990/1.003 at stage 4 only.
  Refutation as root cause: stage 3 fails the same linear tolerance on the
  same budget (0.53/0.71) and still converges — an unconverged linear solve
  is not sufficient for nonlinear failure; a *non-descent* direction is.
- **B. JVP inconsistency / non-finite direction — EXCLUDED.** Every record
  shows `state_finite=1 rhs_finite=1 dx_finite=1`; zero NaN-retry events;
  no `breakdown`; the standing FGMRES/JVP contract tests are green at this
  commit (15/15). Nothing distinguishes stage 4's JVP from stage 3's.
- **C. Nonlinear globalization failure — EXCLUDED as root (behaves
  correctly).** At stage 4 the machinery does what it should given a
  non-descent direction: iter-0 accepts a step that reduces R by only 2.6%
  (2.71e8→2.64e8), iter-1 rejects (`gmres_total_failure=1`), external
  damping (0.1226) is tried and honestly reverted. Globalization cannot
  manufacture descent that the linear model does not provide.
- **D. Residual scaling / WRMS gate artifact — EXCLUDED.** The gate fired on
  a genuine non-contraction (`wrms_growth=0.9997` — no contraction), the
  scaled and unscaled residuals tell the same stall story, and the fail-close
  abort (`outcome=20`) matches the recorded stall rather than creating it.
- **E. Preconditioner / Krylov-subspace quality — CONFIRMED component.**
  Stagnation within 4 Arnoldi vectors with rel_err ≈ 1 under the vertical
  preconditioner is the behavior previously *measured* for this operator at
  dt=600: the fast acoustic operator `A = I − dt·γ·J_fast` is intrinsically
  indefinite (Ritz/numerical-range spectrum straddles the origin;
  `doc/sdirk3_walls_measurement_2026-07-05.md`, Wall-1). The new data adds:
  the FGMRES conversion (flexible preconditioning) does not change this —
  expected, since the indefiniteness is operator-level, not an artifact of
  preconditioner switching.
- **F. Stage RHS/state construction — PARTIALLY CONFIRMED, as physical
  over-extrapolation, not a construction bug.** The 5-orders operand blowup
  entering the last implicit stage matches the measured Wall-2 signature
  (the ARK324 explicit accumulation over-extrapolates the sheared jet's
  u-momentum advective tendency at large dt;
  `doc/sdirk3_walls_measurement_2026-07-05.md`). The same construction
  converges at small dt (historic dt-ladder), so the construction itself is
  not indicated as defective. INFERRED (not re-measured here): the per-term
  attribution to `ru` horizontal advection is carried over from the Wall-2
  measurements; this run adds the stage-resolved magnitude (1.76e3 → 2.71e8)
  but did not re-slice by component.
- **G. Geometry/metric input — EXCLUDED.** Single-rank supported path; the
  geometry contract matrix and per-call validation are standing (15-test
  suite green at this commit); no geometry markers in the run.
- **H. Timestep outside the model's convergence region — CONFIRMED as the
  umbrella conclusion.** At dt=600 the two measured walls meet in the last
  implicit stage: the explicit accumulation hands stage 4 an operand ~1e5×
  larger than stage 3's, and the implicit operator that must absorb it is
  intrinsically indefinite, so the preconditioned Krylov space contains no
  descent direction. Stages 2/3 (smaller operands, same operator class)
  still converge; dt≤~70 historically converges. This is consistent with —
  and now stage-resolved by — the prior measurements that motivated the
  split-explicit rebuild.

**Classification: E + F under H, with A as the proximate symptom; B, D, G
excluded by direct evidence; C behaves correctly.**

**Linear vs nonlinear failure, distinguished:** stage 3 *proves* the
nonlinear iteration tolerates failed linear tolerances (linear failure ≠
nonlinear failure); stage 4's nonlinear failure is *caused by* the linear
model producing non-descent directions (rel_err ≥ 1) on an operand the
operator cannot treat — not by the nonlinear globalization.

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
