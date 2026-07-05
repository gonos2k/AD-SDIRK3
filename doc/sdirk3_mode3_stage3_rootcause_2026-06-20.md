# WRF-SDIRK3 mode-3 Stage-3 non-convergence — Root-cause investigation

**Date:** 2026-06-20
**Scope:** Operational IMEX **mode-3 (ARK324)** on `em_b_wave` fails the implicit solve at **ARK Stage 3**
(the ESDIRK 4th stage) for **dt ≥ 600** (measured at dt = 100, 150, 200, 300, 450, 600, 900, 1200).
**Status:** Root cause **DEFINITIVELY identified** by direct operator diagnostics. It is an **intrinsic
method/operator limit**, not a fixable preconditioner / JVP / budget bug. The P0/P1/stagnation work makes it
**fail-closed (HARD_STAGE_ABORT)** correctly — no silent state-freeze.

---

## 1. Conclusion (root cause)

At the Stage-3 stiff linearization point `U_stage3 = U_n + dt·Σ(a3j·kj)` (the ARK324 stiffly-accurate seed,
~129× the base-state magnitude, dominated by the **U-momentum (ru)** block), the Newton-Krylov operator

```
A = (I − dt·γ·J)        (dt·γ = 261.5 at dt=600, γ = 0.435867)
```

is **INDEFINITE and extremely ill-conditioned**:

- **Rayleigh quotient `vᵀAv/vᵀv = −766`** in the residual direction `v = R/‖R‖` → A is *indefinite*
  (`vᵀJv ≈ 2.94` is a modest positive Rayleigh for the RHS Jacobian, amplified by `dt·γ = 261.5` to `767`,
  which dominates the `I` and flips the sign).
- **Operator norm ‖A·v‖ ≈ 1e9** for random directions → condition number ~1e9.

Restarted GMRES cannot make progress on an indefinite, ~1e9-conditioned operator: it returns **zero useful
update at iteration 0** (`rel_error ≈ 1.0`, "K unchanged, breaking early") at *every* dt down to 100. No
symmetric/SPD-style preconditioner converts an indefinite operator into a definite one, which is why **every**
preconditioner variant tried left the result byte-near-identical.

**The bottleneck block is U-momentum (ru, horizontal acoustic), NOT W↔φ vertical acoustic.** The ARK324 Stage-3
seed amplifies the U-momentum block ~129×; the singularity check shows `‖A·v‖`: ru = 1011 (dominant), W = 0.006
(negligible). Much of the earlier preconditioner work targeted the W block and therefore could not help.

---

## 2. The pivotal measurement (downward/upward dt-ladder, mode-3)

| dt | Stage-3 | rel_R_full | R_full_norm | GMRES min rel_error | Newton |
|----|---------|-----------|-------------|---------------------|--------|
| 1200/900/600 | abort | 4.5e4 (600) | 2.9e5 (600) | ~1.0 | zero update |
| 450 | abort | 7995 | 5.2e4 | 1.0 | zero update |
| 300 | abort | 697 | 4524 | 0.9996 | zero update |
| 200 | abort | 61 | 396 | 0.9995 | zero update |
| 150 | abort | 11.1 | 72 | 0.9996 | zero update |
| 100 | abort | 1.29 | 8.4 | 0.9998 | zero update |

The absolute residual falls steeply as dt→0 (the seed `U_stage3 → U_n`), but GMRES `rel_error` stays pinned at
~1.0 — the linear solve produces **zero descent at every dt**. dt only rescales the *starting* residual; it does
not make the operator tractable (at dt=100, `dt·γ·vᵀJv ≈ 128` still flips the sign → Rayleigh ≈ −127, still
indefinite).

---

## 3. Falsification grid (10 levers, all ruled out by controlled experiment)

| # | Lever | How falsified |
|---|-------|---------------|
| 1 | Krylov budget | E1: 64–120 Arnoldi/4 restarts, still rel_error 0.82–0.96, one restart diverged |
| 2 | JVP operator (assumed wrong) | `[JVP_VS_FD]` cosine = **0.9999** vs component-scaled central FD → JVP is correct |
| 3 | W-block D_w diagonal / acoustic boost | E3: boost=2 (D_w 1.008→522) left Stage-3 byte-identical |
| 4 | GMRES block-scaling | Probe A: rel_error 0.996–1.004 |
| 5 | φ-W algebraic 2×2 coupling | Probe B/C: A_eff negligible or over-damped; no change |
| 6 | zero-init / base-cancellation | 3 code investigations: no zero-init defect; r0 is genuine |
| 7 | effective-dt / seed magnitude | dt-ladder to 100: zero descent at every dt |
| 8 | horizontal U/V Jacobi smoothing | pre-probe (α=0.1/0.25): `‖b‖`/rel_R_full byte-identical |
| 9 | W self-tridiagonal (B(iv) consistent φ-eliminated Helmholtz) | Tier-2: off-band changed 4× yet rel_error 0.9999→1.0 |
| 10 | tableau swap / mode-2 fallback | mode-3 seed abs-sum (~1125 at dt=600) ≥ mode-2 (~1112) → not milder |

**B(iv) note (lever 9):** the W "tridiagonal" was already a real Thomas solve with nonzero off-bands; its only
defect was an off-band linear-in-dt·γ vs a diagonal quadratic Schur (inconsistent), with the (falsified) boost as
a band-aid. Making it a fully consistent, diagonally-dominant φ-eliminated Helmholtz (mirroring dyn_em
`advance_w`/`calc_coef_w`, Tier-1 offline operator-match `M⁻¹@A_ac == I` to 4.6e-14) **did not help** — confirming
the W self-operator is not the bottleneck.

---

## 4. Decisive diagnostic (built-in, no rebuild)

The solver's own diagnostics fire automatically at the stall (`gmres_rel_error > 0.9` + `WRF_SDIRK3_DEBUG_LEVEL=2`;
not stage-gated, and Stages 1–2 converge so the first stall is Stage 3):

- `[JVP FWAD] Fallback` = **0** → fwAD dual path does not throw; the clamped-1e-2 FD-fallback cancellation hazard
  (`wrf_sdirk3_jvp_fwad_or_fd.h:82-90`) is **not** active. JVP is the true forward-mode-AD dual.
- `[JVP_VS_FD]` GLOBAL **cosine = 0.9999**, rel_err = 0.016 (‖Jv_ad‖ = 1102, ‖Jv_fd‖ = 1101) → JVP correct.
- `[SINGULAR_CHECK]` v = R/‖R‖: **Rayleigh = −766** (indefinite); random ‖A·v‖ ≈ **1e9** (cond ~1e9);
  per-block ‖A·v‖ dominated by **ru = 1011**, W = 0.006.

Reproduce: `WRF_SDIRK3_DEBUG_LEVEL=2` at dt=600 mode-3. Evidence: `test/em_b_wave/jvp_diag_20260620/`.

---

## 5. Honest engineering outcome

mode-3 Stage-3 at dt ≥ 600 is an **intrinsic solver-robustness limit** of the ARK324 stiffly-accurate
intermediate stage: its seed produces an indefinite, ~1e9-conditioned implicit operator that restarted GMRES
cannot solve. The completed and validated work makes this **fail honestly**:

- **P0** — non-convergence fail-closes (HARD_STAGE_ABORT) instead of the prior silent `U_new = U_n` state-freeze
  that faked SUCCESS.
- **P1** — WRMS-relative (scale-invariant) stage gates.
- **stagnation gate** (opt-in) — catches `growth ≈ 1` stagnation that the WRMS-growth gate is blind to, across all
  retry-less paths and the mode-3 retry path.

(Commits `d4ad9ea`, `e76bdcb`, `1a14c90` on `sdirk3-baseline-20260216`, pushed.)

---

## 6. Future directions (if pursued — uncertain payoff)

The diagnosis finally points at the correct target, but each carries real risk:

- **Indefinite-aware Krylov** for the U-momentum block: full (non-restarted) or deflated GMRES, or a
  spectrum-shifting / absolute-value preconditioner. Caveat: E1 (more Arnoldi/restarts) already failed, and the
  operator is genuinely indefinite + 1e9-conditioned.
- **U-momentum-targeted preconditioner** (horizontal u↔μ pressure-divergence acoustic coupling) — *not* the W
  block. No such preconditioner has been tried; the entire prior hunt mis-targeted W.
- **Operational dt constraint**: characterize the largest dt at which mode-3 converges and run mode-2/baseline
  above it.

Do **not** re-propose any item in the §3 falsification grid.

---

## 7. Evidence

- `test/em_b_wave/dt_ladder_measure_20260620/`, `dt_ladder_down_20260620/` — the dt-ladder.
- `test/em_b_wave/jvp_diag_20260620/rsl.error.dbg2` — JVP_VS_FD cosine + SINGULAR_CHECK Rayleigh.
- `test/em_b_wave/biv_tier2_20260620/`, `calc_coef_w_dump.txt`, `biv_operator_match.py` — B(iv) impl + Tier-1/Tier-2.
- `test/em_b_wave/hsmooth_preprobe_20260620/`, `probe_*_20260620/`, `stage3_budget_e1_20260620/`, `e3_confirm_20260620/`.
- `test/em_b_wave/operational_char_20260620/` — operational characterization (§8).

---

## 8. Operational characterization (2026-06-20) — measured, with stage-confound caveats

Adversarially verified (a 3-lens review corrected several first-pass overclaims). Read this section for what
may and may **not** be claimed.

### 8.1 Measured table (what each run actually did)

| run | split_mode | dt | outcome | fail-close **stage** (verified) | min GMRES rel_error | notes |
|-----|-----------|----|---------|---------------------------------|---------------------|-------|
| mode3 | 3 | 75 | ABORT (20) | Stage 3 | 0.9998 | zero-update stall |
| mode3 | 3 | 50 | ABORT (20) | Stage 4 | 0.999 | rel_R_full=246 |
| mode3 | 3 | 30 | ABORT (20) | Stage 4 | 1.0 **(CLAMPED)** | raw GMRES rel_error = **26917** (r_true=1.29e7) — a *divergence* clamped to 1.0, NOT a worse solve |
| mode3 | 3 | 10 | ABORT (20) | Stage 2, **timestep 2** | 0.5565 | **timestep 1 completed cleanly** (advanced to t=10s, all stages converged=1); 0.5565 is step-2 Stage-2 |
| mode2 | 2 | 600 | ABORT (20) | Stage 3 indefinite (step1) → inf/nan (step2) | 0.4174 | step 1 soft-continued (gate_continue=3, Stage-3 wrms_growth=0.9999), step 2 inf/nan catastrophe |
| full-impl | 0 | 600 | **"SUCCESS" but converged=0** | all stages (1,2,3) converged=0, soft-continued | 0.65 | **NOT a real solve** — every stage `converged=0` (residual ~0.5–0.7), advanced only because the lenient WRMS-growth gate (threshold 100, blind to stagnation) continues; with `WRF_SDIRK3_STAGNATION_GATE_ENABLE=1` it FAIL-CLOSES (outcome=20). Silent non-convergence (the pre-P0 behavior). |

Prior (CLAUDE.md, not re-run here): **stock RK3 split-explicit** at dt=600 WORKS (genuinely).

### 8.2 What we may and may NOT claim

- **MEASURED:** mode-3 fails-closed at every dt in {10, 30, 50, 75, 100…1200}; mode-2 fails-closed at dt=600; the
  dt=600 Stage-3 operator is indefinite (Rayleigh −766), cond ~1e9, JVP correct (cosine 0.9999, §4).
- **mode-2 = SAME root cause:** not three independent stage failures — one Stage-3 indefiniteness (identical 0.9999
  wrms-growth signature) that mode-2's lenient gate (stage_gate=100) **soft-continued** on step 1, corrupting the
  state (update_norm=6.2e13) → inf/nan catastrophe on step 2. (mode-2 indefiniteness is *inferred* from the matching
  signature; no SINGULAR_CHECK was run in that file. The direct −766 is a mode-3 dt=600 measurement.)
- **NOT a convergence trend:** the fail-close *stage migrates* with dt (3→4→4→2 at dt=75/50/30/10), so the per-run
  min-GMRES-rel_error compares *different operators with different seeds* — it does NOT establish monotone
  convergence. (Resolves the earlier "non-monotonic, investigate" flag: dt=30 is a clamped divergence.)
- **MODEL ONLY:** "indefinite for dt > ~0.78s" extrapolates a *single* dt=600 measurement of a *dt-dependent* seed
  (vᵀJv assumed constant; it is not). **Sub-10s dt is UNTESTED.** Make no sub-10s numeric threshold claim, and no
  "tractable at a few seconds / 600× steps" claim.
- **§2 framing nuance:** the "Stage-3 stagnation, rel_error≈1.0" signature holds for dt ≥ ~75/100; below that the
  limiting stage migrates (Stage 4, then Stage 2) and GMRES begins to *partially* descend (0.5565 at dt=10). The
  "Stage-3" label is the large-dt signature, not universal across the full ladder.

### 8.3 Operational recommendation + the 4D-Var trade-off (critical)

**"baseline" is overloaded — it is gated by `time_integration_scheme`, NOT `split_mode`:**

- **Stock RK3** (`time_integration_scheme != 6`, `solve_em.f90:970`): the only **confirmed-usable** dt=600 path, but
  **NON-differentiable** (no autograd / checkpoint / adjoint). Choosing it **ABANDONS 4D-Var** — all DA machinery
  lives only inside the `scheme==6` block (`solve_em.f90:6296-6317`). Forward-forecast fallback only, never for DA.
- **Full-implicit libtorch** (`scheme==6` + `imex_split_mode=0`, "full implicit baseline", `wrf_sdirk3_config.h:1899`):
  **differentiable** (preserves 4D-Var), and **MEASURED this session at dt=600 — it does NOT converge either.** It
  reported "SUCCESS COMPLETE WRF" but every stage was `converged=0` (residual ~0.5–0.7); it advanced only because the
  WRMS-growth gate (threshold 100) is *blind to stable non-convergence* (converged=0 with low growth) and
  soft-continued. This was the exact pre-P0 silent-non-convergence behavior, and a latent gap in the retry-less paths.
  **FIX (v20.14r68) — OPT-IN config `stage_require_convergence` (default OFF, per repo guardrail: new knobs must not
  change default behavior, same as the stagnation gate).** When enabled, a `converged=0` stage in the retry-less paths
  (split_mode=0 full-implicit + IMEX mode-2) is treated as catastrophic → fail-close, since the only reliable signal
  for stable non-convergence is the Newton flag itself (growth-based gates miss it). It never fires on `converged=1`
  stages (no false positives) and leaves mode-3 (its own retry path) untouched. Fully wired (Registry
  `sdirk3_stage_require_convergence` default `.false.` → namelist `&dynamics`; Fortran set_config; C++
  env/string-setter/set_config/[CONFIG EFFECTIVE]/print_config). Validated: with `sdirk3_stage_require_convergence=.true.`
  (or `WRF_SDIRK3_STAGE_REQUIRE_CONVERGENCE=1`), split-0 @ dt=600 **FAIL-CLOSES** (the gap closed honestly); default off
  preserves the legacy behavior. **Recommendation:** enable it for any run where silent non-convergence must be caught
  (it surfaces the full-implicit baseline's apparent success as the non-convergence it actually is).

**Therefore NO differentiable implicit path (mode-2, mode-3, OR full-implicit baseline) genuinely converges at
dt=600 on em_b_wave.** The non-convergence is *universal* across the implicit modes; only the GATE differs
(mode-3 = strict → fail-close; mode-2 / split-0 = lenient WRMS-growth → soft-continue, masking it). The
full-implicit operator is *milder* (residual ~0.5–0.7 / GMRES 0.65 vs mode-3's ~0.89 / ~1.0, because it lacks the
aggressive ARK324 129× intermediate seed) but still does not reach tolerance. A bare "use the baseline at dt=600"
is therefore a **silent own-goal** twice over: stock RK3 is non-differentiable, and the differentiable
"full implicit baseline" only *appears* to work. Options that **preserve** 4D-Var with a GENUINE solve:

- **(C) small-dt implicit** (dt ~ 1–few s): the only regime the single-point model predicts tractable; correct but
  ~hundreds× more steps → effectively unusable operationally. "Correct-but-expensive."
- **(D) hybrid windowed DA:** stock RK3 for the bulk forward forecast (differentiability not needed) + the
  differentiable implicit path only over the short assimilation window at small dt. Most defensible compromise;
  requires trajectory consistency between the two paths (not yet validated).
- **(E) fix the root cause** (structural, untried): the indefinite ill-conditioned stiff ru-acoustic block — restarted
  GMRES is the *wrong* Krylov method for an indefinite system. Cause-attacking levers not in the §3 falsification
  grid: MINRES / GMRES-on-normal-equations, an **IMEX split that keeps the horizontal-acoustic (ru) terms EXPLICIT**,
  or a genuinely indefinite-capable preconditioner targeting the **U-momentum** block (not W).
