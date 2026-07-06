# SDIRK3 Split-Explicit Rebuild — Validation Findings (2026-07-06)

Audit trail for the differentiable WRF-style split-explicit core rebuild. Companion to
`doc/sdirk3_split_explicit_rebuild_plan.md`. **MEASURED** = observed from a run/probe;
**INFERRED** = reasoned but not directly measured. All commits on `AD-SDIRK3:main`.

## Increment validations (committed)

| Inc | commit | what | result |
|-----|--------|------|--------|
| 0 | `f0ce105` | opt-in `sdirk3_split_explicit` knob + scaffold | **MEASURED** byte-identical baseline when off (fully wired: Registry/Fortran/C++) |
| 1 | `be7927a` | libtorch `small_step_prep` `c2a` = (cp/cv)·(pb+p)/alt | **MEASURED** c2a mean 44424 vs dyn_em 44073 = **0.8%**; p_full bit-perfect |
| 1 | `be7927a` | **cvpm inverse-density BUG FIX** | **MEASURED** WRF cvpm = −cv/cp (share/module_model_constants.F:26); helper used +cv/cp → alt ~5× too small (0.4976 vs dyn_em 2.4525). Shared with computeUnifiedRHS (pre-existing RHS bug). After fix: c2a matches. Wall-2 `k_slow(u)` byte-identical → bug was in buoyancy/density terms, independent of the u-advection cascade |
| 2 | `42786ef` | `calc_coef_w` vertical acoustic tridiagonal a/α/γ | **MEASURED** cof EXACT (1.16446e6); γ 1.5% vs dyn_em (γ = c·α is **dts-independent** → indexing proven). a/α residual 5–24% = **INFERRED** c2a/MUT input parity (~8% at level-max, amplified by squared band terms), not a formula bug |
| 2 | `42786ef` | **dts_rk is PER RK STAGE** diagnosis | **MEASURED** stage 1 = dt/3 (not dt/num_sound_steps=dt/4); the (200/150)²=1.78× a/α error + dyn_em cof overflow confirmed dts=200 |
| 2b | `eb24b11`→`07df325` | differentiable Thomas solve (advance_w :1433-1443) | **MEASURED** M⁻¹·A·v == v rel_err **6.32e-6** (float32 floor; offline double 4.6e-14). Backward proven: grad_L2=34465 finite/nonzero |

### Structural confirmation (MEASURED, read of reference)
`advance_w`'s tridiagonal solve (module_small_step_em.F:1433-1443) is **identical** to the Inc 2b
Thomas solve using `calc_coef_w`'s a/α/γ → the implicit machinery is the *operational* advance_w
solve, not an abstraction.

## Graph-integrity refinements (Inc 2b, Codex-caught, all MEASURED)

| commit | defect | fix |
|--------|--------|-----|
| `65d1c67` | compute path under a blanket `NoGradGuard` | scoped guard to `.item()` dumps only; compute path grad-enabled |
| `fad2098` | in-place `index_put_` recurrence breaks backward | rebuilt out-of-place (per-level slabs + `torch::stack`); proven via `.backward()` |
| `07df325` | backward smoke test not isolated from live graph | run on **detached** bands → backward flows only through a local leaf (no `U_n.grad()` pollution / shared-buffer free) |

Lesson (folded into `differentiable-dynamical-core` skill): "grad-enabled ≠ differentiable"; prove
with a real `.backward()`; isolate embedded backward probes with detached copies of shared tensors.

## Offline discretization validation set (COMPLETE — all types)

All references analytic, non-zero-exit on FAIL, under `test/em_b_wave/`.

| type | operator | ref | rel-err | floor |
|------|----------|-----|---------|-------|
| 1st-diff **onto** grid | u-PGF (advance_uv :828-831) | `upgf_operator_match.py` (`e71cd21`,`b407cb8`) | **9.78e-4** | O(dx²) 3.9e-3 |
| 1st-diff **off** grid | mass-flux divergence (advance_mu_t :1094-1098) | `divergence_operator_match.py` (`99871aa`) | **9.78e-4** | O(dx²) 3.9e-3 |
| 2nd-diff (Laplacian) | vertical PGF (advance_w RHS :1408-1413) | `vpgf_operator_match.py` (`e593d87`) | **5.14e-4** | O(dη²) 5.1e-4 |
| tridiagonal solve | calc_coef_w + Thomas | `biv_operator_match.py`, Inc 2/2b | 3.7e-6 / 6.3e-6 | — |

## Key architectural findings (MEASURED)

1. **Two field families.** `calc_coef_w`'s `c2a` uses the **full stage state** (computed once/stage);
   `advance_uv`/`advance_w`/`advance_mu_t`/`calc_p_rho` operate on **accumulating small-step
   perturbations** (`p'`,`ph'`,`u'`,…) reset to ~0 each RK stage. Measured via the Inc 3a u-PGF
   attempt: feeding the hydrostatic perturbation (large) gave dpxy_L2=2778 vs dyn_em 0.13 (~0 at
   substep 1, perturbations undeveloped). Conflating them would produce a plausible-but-wrong loop.
2. **`calc_p_rho` ≠ Inc 1 EOS.** Acoustic `al'` is diagnosed from the **geopotential gradient**
   (:522-523), `p'` from a **temporally-linearized EOS** (:527-528) — using `alt`/`c2a` (Inc 1) as
   inputs but distinct formulas. Reusing Inc 1's full-EOS would be an Inc 4–5 bug.

## Assembly-stability proof (2026-07-07, von-Neumann; MEASURED)

The discretization set validates the *primitives*; the **coupling** — the assembled semi-implicit
w–φ substep — is a risk isolation cannot cover. `acoustic_amplification_match.py` (`85e60dc`)
builds the exact amplification matrix of one `advance_w` substep using the **exact** inverse of the
`calc_coef_w` tridiagonal (`np.linalg.inv`, not a hand-rolled Thomas):

| assembly | \|λ\|_max | meaning |
|---|---|---|
| explicit, no implicit A | 68.0 | A is **load-bearing** (removes a wild instability) |
| **semi-implicit, epssm=0.1** | **0.9983** | STABLE, correctly damped |
| eps=0 + implicit A | 1.0000 | neutral / energy-conserving |

**SCOPE (do not overclaim).** This is a **simplified model**: 1-D vertical column, **constant**
metrics/`c2a`, pure w–φ mode with `t'=0` (buoyancy term dropped), no horizontal coupling
(`advance_uv`/`advance_mu_t` horizontal divergence), no `calc_p_rho` feedback. What it **proves**:
the core vertical w–φ coupling *structure* is stable and the implicit A is load-bearing. What it does
**not** prove: stability of the **full** scheme with variable stratification (`c2a(z)`), the buoyancy
term, and the 3-D horizontal coupling. ⇒ the vertical-coupling *structure* is de-risked; **full-scheme
stability still requires the Inc 5 assembled-substep-vs-dyn_em check.** A byte-faithful port inherits
this stable *structure*, not a guaranteed `|λ|=0.998` for the real variable-metric case.

**Correction:** three earlier hand-rolled 1-D column probes blew up and *appeared* to prove the
coupling structurally unstable. A dts sweep (dts-independent growth) and sign sweep (no combination
stable) ruled out CFL and sign errors; the von-Neumann matrix (exact `A⁻¹`) then showed the scheme is
in fact stable — the toy had mis-implemented its own Thomas. Lesson (folded into the
`differentiable-dynamical-core` skill): validate a coupled semi-implicit scheme with the exact-inverse
amplification matrix, never a hand-rolled time loop.

## Status

Operator map 100% complete and precise; every discretization *primitive* validated; the implicit
solve proven and confirmed operational; the **core vertical w–φ coupling structure** proven stable in
a simplified constant-metric model (von-Neumann `|λ|=0.998`, implicit A load-bearing). **Remaining
Inc 5 risk (not yet retired):** full-scheme stability with variable stratification, the buoyancy
term, and 3-D horizontal coupling — to be confirmed by validating the *assembled substep* against a
dyn_em `[PARITY substep]` dump. Inc 5 work: small-step state infrastructure + the substep loop
(reusing the validated `calc_coef_w`+Thomas), then that assembled-vs-dyn_em check.
