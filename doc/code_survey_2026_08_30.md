# Code-wide survey — mathematical, engineering, numerical

**Scope** `external/libtorch_wrf/sdirk3/` at `agent/r14-2-iteration-result` (post R14.1, R14.2a) ·
**Method** each axis turned into a machine-checkable question, then every hit read in context ·
**Bar** "simple, clear, plain, intuitive" (the user's) — so each finding says *fix* or *delete* ·
**Independent review: NOT RUN.**

## Findings, by severity

### P0 — mathematical: the shipped run uses the wrong Ω

`mass_coordinate_mode = 0 (Legacy) → omega_ww_cp = off` on the dt=600 record. Legacy aliases
`Ω := rom = μ·w`. WRF's Ω is `μ·dη/dt` from `calc_ww_cp` — the vertical integral of horizontal
divergence, zero at both boundaries. The memory file `sdirk3-omega-is-not-rom` records this as the
**root cause** of the μ/φ/θ stage-2 anomalies, corroborated from an independent channel, and the
audited port `compute_wrf_ww_cp` exists with a parity contract. It is wired (`:14686`, `:21486`)
**behind mode 1**. The default is the definition the campaign already showed to be wrong.

This is the one finding that plausibly bears on why dt=600 has zero successful steps. Not
changed here — flipping the default is a numerics change to be **measured**, not audited in.
Recommendation: run dt=600 with `sdirk3_mass_coordinate_mode = 1` as the next experiment, before
any further solver work.

### P1 — mathematical: `D_mu` coefficient is a known open re-derivation

`unified_preconditioner.cpp:1502` `D_mu = 1 + dt·γ·μ₀·(Hx² + Hy²)`. The comment above it says
"left as-is pending the coefficient re-derivation" and explains why the term is the wrong
coupling (bare `H` where `|C_u_mu|` belongs; the missing factor is `h·c_s²/μ₀`). Memory
(`sdirk3-precond-annihilates-ph-mu`) records `A_μμ = 0.95 ⇒ the diagonal belongs below 1`, and the
code has `1 + 4.7e-3`. Known, documented, **unfixed**. Small at dt=600/100 km (4.7e-3 vs 1.58)
but grows as 1/dx.

### P2 — engineering: three definitions of γ, and the canonical one has no reader

`coefficients.h:29` (the documented Butcher tableau, `double`), `tile_unified.h:1992` (`gamma_`,
the one the tile actually uses, 11 sites), `tile_unified_impl.cpp:41465` (`gam = 0.4358…f`, a
`float` copy inside the adjoint probe). `SDIRK3Coefficients::gamma` has **zero** consumers. One
number, one home: the tile should read `coefficients.h`. *Fix* — not done here because touching
the 11 numeric sites deserves its own A/B.

### P2 — engineering: a runtime write to global config

`tile_unified_impl.cpp:6313` writes `g_sdirk3_config.precond_uv_vertical_fraction` from the
warm-up. The comment says "the ONLY point where config is written" and all tiles compute the
same value. Acceptable as a warm-up latch; a global that is written at runtime is still a global
that is written at runtime. *Note*, not a defect.

### Fixed in this commit (`b579d27`)

| | |
|---|---|
| probe residue | `candidate_delta` lambda + `probe_dK_from_solve` survived R14.1's deletion of the probes that used them — **deleted** (−18) |
| unguarded division | Jacobian-reuse check `‖U−U_cached‖ / ‖U‖` is 0/0 on a zero state; every comparison after a NaN reads false — **clamped** |
| hardcoded `600·γ` | a debug diagnostic printed `gamma_dt = 261.52` at any dt — **now `dt_stage_ · γ`**, at three sites |

## Checked and clean

| axis | check | result |
|---|---|---|
| numerics | `.item()` outside `NoGradGuard` | lint baseline 74 = actual 74 |
| numerics | dyadic FD/Taylor step sizes | none (α = 1/3 where used; the only 0.25 is a threshold) |
| numerics | divisions by norms | 7 of 8 guarded (`std::max`, `< 1e-20 continue`, `> 0 ?`); the 8th fixed above |
| numerics | FP64 reductions at decision sites | the `candidate_merits` S-norm reduces in native dtype as trust does (R13.26 §11); the remaining `.to(kFloat64)` sites are raw-L₂ telemetry |
| math | η orientation / `rdnw` sign | signed convention applied explicitly at the acoustic kernel (`:5032`), per the pressure-orientation finding |
| engineering | `(j,k,i)` memory layout | no `(i,k,j)`-ordered `from_blob`; one `from_blob` site total |
| engineering | `set_default_dtype` | two mentions, both in a comment explaining why it is forbidden |
| engineering | explicit-tendency `.detach()` | `k_slow.detach()` at `:6737` is the `!slow_in_tangent` branch — gated as intended; `:7809` likewise |
| engineering | opt-in probes | 0 `read_experiment_flag` sites remain |

## What this survey did not do

It did not re-derive the SDIRK3/ARK324 tableau, the preconditioner Schur blocks, or the HEVI
split against the WRF reference — those are the P0/P1 items above and each is a measurement, not
a read. It did not run dt=600 under mode 1. It did not audit `tile_unified_impl.cpp`'s 42k lines
line by line; the sweeps above are pattern searches with every hit read in context.

## P0 follow-up — measured (three verified dt=600 runs, same binary)

| run | Ω | stage-2 Krylov budget | first solve `‖b‖ → ‖r‖` (ratio) | outcome |
|---|---|---|---|---|
| shipped | mode 0, `μ·w` | 7 vectors | 1039 → 574 (**0.55**) | `newton_budget_exhausted`, 12 iters |
| P0 test | mode 1, `calc_ww_cp` | 7 vectors | 465 → 465 (**1.00**) | `zero_update_after_total_failure`, iter 1 |
| budget | mode 1 | 85 vectors | 465 → 463 (**0.9965**), then 0.989, 0.988 | `krylov_budget_exhausted` |

Every knob was read back from `[CONFIG EFFECTIVE]` before a number was read. A fourth run whose
namelist edit silently did not apply was discarded — it measured mode 0 and would have been
reported as mode 1.

**What it settles.** The correct Ω is a *different operator*, not a perturbation of the shipped
one: the stage-2 residual halves and its momentum share drops from 0.037 to **0.000** — the
residual now lives entirely in the mass/geopotential/thermo blocks the Ω term enters. On that
operator the shipped preconditioned GMRES is **not budget-limited**: twelve times the Krylov
vectors buys 0.35 % per solve, against 33 % on mode 0 at the same budget. Mode 0's apparent
progress was progress on the wrong operator.

**Consequence.** GMRES/Newton budget tuning for dt=600 is measured dead on the correct operator.
The next lever is the preconditioner's mass/geopotential coupling under mode 1 — which is the P1
above (`D_mu`, the open re-derivation), now with a measurement behind it. Every prior dt=600
preconditioner measurement in this campaign was taken on `Ω = μ·w` and carries that caveat.
