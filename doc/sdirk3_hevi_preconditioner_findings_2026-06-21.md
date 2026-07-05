# SDIRK3 HEVI + Preconditioner Investigation — Findings (2026-06-21)

**Goal:** make the WRF-SDIRK3 differentiable implicit IMEX solve (mode 3 / ARK324) converge at the
operational timestep **dt=600** on em_b_wave.

**Bottom line:** it does not, and this is now an evidence-backed *intrinsic conditioning limit*. The
solve converges only at **dt ≤ ~70 s** (~1/8.5 of target). HEVI removed the indefinite saddle and a
preconditioner-consistency fix (precond-inc1) was banked, but neither indefiniteness, budget, nor a
vertical-block preconditioner refinement breaks the dt=600 wall.

---

## What was committed (opt-in `sdirk3_hevi_split`, default off → baseline byte-identical)

| commit | change | effect |
|---|---|---|
| `91add52` | HEVI inc1/2: U/V-PGF + horizontal mass-div + φ horizontal-advection → explicit k_slow | GMRES 0.99→0.55 |
| `712e79d` | HEVI full-halo fix (gate horizontal PH/MU boundary fluxes; multi-tile fail-closed guard) | correctness |
| `abbb38e` | HEVI inc3: θ-compressibility horizontal → explicit (machine-exact split 4.58e-16) | convergence-neutral |
| `ac826e0` | precond-inc1: gate off M's phantom horizontal-acoustic content (HEVI-aware M) | convergence-neutral |

## What was ruled out by measurement (not assumption)

1. **Indefiniteness is not the GMRES blocker.** Divergence damping causes the operator indefiniteness
   (kdamp=0 flips all random Rayleigh negative→positive), but GMRES is byte-identical with/without it.
2. **Budget is not the blocker.** ~1771 Krylov vectors (250× default) only reach rel_error ~0.38; the
   true stage-3 residual is unmoved.
3. **M's phantom horizontal content is not the blocker.** precond-inc1 (making M horizontally
   HEVI-consistent) was convergence-neutral — the 0.378 floor is entirely the vertical W↔φ block.
4. **The vertical W↔φ block fix (precond-inc2) FAILED — made GMRES worse** (floor 0.378→0.54). The
   mathematically-correct single-count of the acoustic Schur removed an over-damped W diagonal that was
   *load-bearing* (it stabilized GMRES by suppressing the stiff W mode). The W↔φ 2×2/tridiagonal
   algebraic refinement is not the lever. Reverted (not committed).

## Convergent-dt envelope (HEVI on, mode 3, default budget, honest stagnation gate)

| dt (s) | step-1 outcome | min GMRES rel |
|---|---|---|
| 120 / 100 / 90 / 80 | fail-close (stagnation) | 0.999 / 0.895 / 0.996 / 0.587 |
| **70** | **converged** | 0.389 |
| **60** | **converged** | 0.187 |

**CORRECTION (the "converged" rows above are WRONG — see below).** The dt≤70 rows reflect *linear-solve*
descent only; the step completed but the *nonlinear update blew up*. There is **no** convergent-dt envelope.

## ⚖️ GREEN/RED TEAM VERDICT (2026-06-21, FINAL — supersedes the framings below)

A broad green-team/red-team review (4 dimensions × defend+attack + adjudication) downgraded the headline claims.
**Read this first; the sections below contain superseded over-claims kept for the audit trail.**

**MEASURED — survives the red team:**
- A finite ~1.3e12 explicit/slow-channel update blow-up at dt=60 HEVI-on (committed
  `test/em_b_wave/dt60_isolate_20260621/`), one step before the SIGABRT (`checkTensorHealth` throws only on
  NaN/Inf, so a finite 1e12 passes — code-verified at tile :22883-22909).
- dt=600 is unreached by every committed lever (dt-ladder fail-close 600→100).
- The two earlier fixes (acoustic sub-cycle; u↔φ re-split) stay unsupported; HEVI is opt-in/default-off.
- Stages 1–2 of k_slow are identical hevi on/off (Δ‖k_slow[0]‖≈0.02 at the bit-identical U_n; stage-2
  69293.6 both) → HEVI's rerouted horizontal-acoustic content is negligible *near the balanced IC*.

**NOT SUPPORTED — inference the committed data contradicts (downgraded):**
- **"HEVI merely UNMASKS a baseline cascade (byte-identical through stage 4)."** FALSE as stated: HEVI *does*
  alter k_slow (the ExplicitOnly PGF/mass-div is gated `if(do_implicit||(do_explicit&&hevi))` at tile :11637)
  and alters U_conv = U_stage + dt·a_ii·k_fast via the HEVI-modified k_fast (:6678). And **baseline aborts at
  stage 3** (severe backstop, even with the stagnation gate off) — so there is *no* baseline stage-3/4 k_slow
  to compare; the "through stage 4" identity has no operand. **Honest C1: a measured finite explicit-channel
  blow-up of UNISOLATED origin; HEVI demonstrably alters k_slow, so HEVI-influence on the blow-up is NOT
  excluded.** The cascade norms (1010→5e10) came from an uncommitted, since-reverted probe.
- **"two INDEPENDENT walls."** Over-stated. The baseline implicit GMRES residual is ru-dominated (share 0.957)
  and HEVI relocates exactly ru/rv — so "one coupled horizontal-acoustic root, two symptoms" is at least as
  well-supported.
- **"Wall-1 is a fundamental spectral limit (~0.39)."** The exact 0.39 number was a stale comment, BUT a
  follow-up MEASUREMENT (2026-06-21) settles the green-team confounds: Wall-1 is **NOT budget-limited** (floor
  ~0.37 at both 1771 and 1600 Krylov vectors — plateaued) and **NOT precision-limited** — the `RHS_DETERMINISM_CHECK`
  (after fixing its dead gating: it was gated on stage 1, which is ESDIRK-explicit with no Newton solve) reports
  `‖F1−F2‖/‖F1‖ = 0` at dt=600 (RHS bit-reproducible; float32 absolute precision ~1e-7 ≪ the 0.37 floor).
  So Wall-1 is a **genuine operator/preconditioner-conditioning limit**, not budget or precision. Smoking gun:
  `‖M⁻¹r‖/‖r‖ = 4.0` — the preconditioner *amplifies* the residual 4× (M is counterproductive on M⁻¹A),
  consistent with precond-inc2 failing. Double-precision / more budget would not help; a better preconditioner would.
- **"nonlinear cascade."** Not established — a linear non-normal operator through per-stage coefficients also
  gives non-constant ratios; the g(ε) amplitude-scaling test was never run.

**Code issues the red team surfaced (separate from the science):**
- `checkTensorHealth` tile :22886-22902 uses `.item<>()` on a detached CPU tensor *without* an explicit
  `NoGradGuard` (safe via `.detach()`, but violates the project's repeated `.item ⇒ NoGradGuard` rule).
- newton_solver.cpp:3170-3181 — stale "0.39 spectral limit" comment cited as if measured.
- newton_solver.cpp:1444/2666 — float32 solve + `RHS_DETERMINISM_CHECK` gated-off-by-default = an untested
  noise-floor confound on the GMRES-floor interpretation at cond~1e9.

**Decisive experiments (ranked, mostly un-run; require a small instrumentation + rebuild):**
1. Stage-1 k_slow discriminator — DONE (Δ≈0.02, negligible at U_n).
2. Force baseline to complete all 4 stages (disable the severe abort) → the first well-posed hevi=0/1 stage-3/4
   comparison.
3. Per-component k_slow (u/v/w/ph/t/mu) + g(ε) slope → isolate the term + linear-vs-nonlinear.
4. Double-precision, large-budget GMRES at dt=600 + `RHS_DETERMINISM_CHECK=1` → is Wall-1 spectral or budget/precision?

---

## CORRECTION (after two adversarial reviews + direct measurement): [SUPERSEDED by the verdict above]

The step-1 "convergence" at dt≤70 is not convergence. The step completes but the *nonlinear update blows up*
(finite ~1e12, passes the NaN/Inf health check, corrupts the next step → SIGABRT `NaN in U_stage_ark`).

| dt | step-4/step-1 ‖k_slow‖ amplification | crash |
|---|---|---|
| 1 / 2 / 4 | 1.3× / 2.7× / 9.3× | no |
| 15 / 30 | 387× / 84,000× | no |
| 60 | 4.9e7× | yes |

### What is SOLID (directly measured, per-stage KCHANNEL probe + dt-sweep + hevi on/off A/B)

- **The blow-up channel is the EXPLICIT k_slow** (‖k_slow‖ 1010 → 69k → 800k → 4.98e10 across the 4 ARK324
  stages; ‖k_fast‖ ~1e-7), propagating through the explicit tableau (seed₂ ≈ 60·a_explicit[1][0]·1010,
  a_explicit[1][0]=0.8717 verified).
- **The cascade is BYTE-IDENTICAL with hevi ON and OFF** (stages 1–2: 1010.2x → 69293.6 both). So it is **not
  HEVI-specific** — the acoustic terms HEVI moves are ≈0 at the balanced initial state (they cancel against
  the hydrostatic/geostrophic reference). **HEVI did not cause the blow-up; it UNMASKED a pre-existing
  baseline explicit instability** by fixing the implicit convergence: baseline (hevi off) fail-closes via
  implicit GMRES stagnation *before* the cascade completes, so it never crashes; HEVI lets the stages
  complete, so the explicit cascade runs to blow-up.
- **It is NONLINEAR** — the per-stage amplification is non-constant (69×/12×/62,000×) and super-exponential in
  dt; that is a bilinear/transport runaway, not a constant-factor linear eigenvalue.
- `dt·k_slow[1]` ≈ 52,840 ≈ the **state norm** at dt=60: a single explicit stage displaces the state ~100% —
  an explicit-slow CFL/magnitude problem.
- **NOT** an off-manifold-seed bug (‖k_fast−F_imp(U_conv)‖ ≪ seed) and **NOT** vertical diffusion
  (diff_opt=0 byte-identical).

### What was REFUTED (two of my own committed conclusions, each by the next review)

1. **"explicit-acoustic CFL → sub-cycle"** (commits ef35bd2/6ec8d65): ‖k_slow‖ was never measured (inferred);
   dt=60 is below the acoustic CFL; "|λ|≈c_s/dz" was coincidental number-matching.
2. **"asymmetric u↔φ' split (positive-real) → re-split"** (commit 2bde590): the PGF is *strictly linear* in
   the running code (qf1=0, dry) and cannot cause 62,000×; the cascade is baseline (identical hevi on/off),
   so the acoustic split is not the driver; the runaway is nonlinear, not a clean positive-real eigenvalue;
   "|λ|≈c_s/dz" was number-matching **again** (the same error). Both **sub-cycle and re-split are withdrawn.**

### UNRESOLVED (must not be stated as fact)

- **Which** baseline explicit term drives the cascade. The momentum k_slow is a *lumped* sum (nonlinear
  advection :11009/:11236 + linear PGF + Coriolis + curvature, ru/rv never restored). Nonlinear advection of
  the 100 km jet is the most plausible driver, but the per-component (u/v/w/ph/t/mu) k_slow has not been
  measured.

### Decisive next experiment (specified, not yet run)

Per-component k_slow norms at every stage (slice by the [u,v,w,ph,t,mu] offsets in compute_k_slow) + an
amplitude-scaling test g(ε)=‖k_slow(U_n+ε·d)−k_slow(U_n)‖ (slope ~1 ⇒ linear; >1 ⇒ nonlinear). This isolates
the driving term and the linear/nonlinear character, which together select the fix family: implicit/semi-
implicit (or sub-cycled) treatment of the *baseline bilinear slow tendency* — **not** acoustic re-split.

### Standing conclusions that DID survive

HEVI converges at NO tested dt (large dt: implicit stagnates; small dt: this explicit cascade). HEVI removed
the indefinite saddle and improved the *linear* solve (0.99→0.55), and that is exactly what exposed the
baseline explicit-slow instability. The dt=600 goal is unreachable; the *reason* is now understood to be two
independent walls (implicit conditioning AND baseline explicit-slow magnitude), neither of which the
committed HEVI/preconditioner work resolves.

## Method note

The decisive results came from *no-rebuild env probes* (`WRF_SDIRK3_KDAMP`, `WRF_SDIRK3_STAGE3_*`,
`WRF_SDIRK3_PRECOND_*`) and adversarially-verified design workflows. The preconditioner mismatch (M was
not HEVI-aware) and the W↔φ load-bearing over-damping were both predicted by adversarial verification
before the build that confirmed them — the verify step paid for itself.

