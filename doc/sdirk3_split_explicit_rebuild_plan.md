# Differentiable split-explicit core — rebuild plan (2026-07-05)

## Why (settled by measurement)

The SDIRK3-**implicit** approach hits, at dt=600, the two walls WRF's split-explicit design exists to avoid:
- **Wall-1:** `A = I − dt·γ·J_fast` is intrinsically indefinite (Ritz spectrum straddles origin, ~linear) →
  preconditioner-dead → Route B dead.
- **Wall-2:** the ARK324 explicit tableau over-extrapolates the (physical) u-advection at large dt → cascade.
  dyn_em parity confirmed the RHS is *not* inflated (SDIRK3 ExplicitOnly ~1010 vs dyn_em complete slow tendency
  ~2842, and stock RK3 integrates the larger one stably) → Wall-2 is the *scheme*, not the RHS.

Both point to the same fix: **a differentiable reimplementation of WRF's RK3 + acoustic-substep core** — slow
tendency evaluated at physical RK3 stage states, frozen as a constant source through explicit acoustic sub-steps,
with the single vertically-implicit w–φ Thomas solve. This is stable at dt=600 by construction (it's why stock
RK3 works) and its adjoint comes *free* from autodiff (fixed sub-step count → fixed-length reverse graph),
converging with the deferred WRFPLUS/4D-Var goal.

## Reference (mirror one-to-one; reuse coefficients — do NOT invent surrogates)

Driver: `dyn_em/solve_em.F` `Runge_Kutta_loop` (:813). Kernels: `dyn_em/module_small_step_em.F`. Slow forcing:
`dyn_em/module_em.F` `rk_tendency` (:190) + `rk_addtend_dry` (:959). Coupling:
`dyn_em/module_big_step_utilities_em.F` `couple_momentum` (:329, `ru = μu/msf`).

Per-big-step: `num_sound_steps` (:467-484, even, ≥4), `dts = dt/num_sound_steps` (:486). Per RK3 stage
(:839-853): stage1 dt/3 ×1 substep; stage2 dt/2 ×(N/2); stage3 dt ×N. Acoustic loop (:1511-1965):
`advance_uv` → `advance_mu_t` → `advance_w` → `sumflux` → `calc_p_rho`. Off-centering `epssm`, divergence damping
`smdiv`/`emdiv`. Frozen forcing = `ru_tend…mu_tend` (INTENT(IN) inside kernels → constant across the sweep = the
balance-preserving property).

The ONE implicit op: `calc_coef_w` (:570-652) tridiagonal `a/alpha/gamma` + `advance_w` Thomas (:1433-1443).
**Already verified** against dyn_em (Tier-1 operator-match, worst relerr 3.7e-6; `test/em_b_wave/biv_operator_match.py`).

## Discipline (per differentiable-dynamical-core skill)

- Simple/clear/1-to-1 with the reference — a convergence failure must be unambiguously numerics, not plumbing.
- Every increment **opt-in default-off, byte-identical baseline**, fully wired (Registry + Fortran + C++).
- **Validate each increment against dyn_em** using the parity-dump harness (temp `WRITE`s in the dyn_em kernel +
  the C++ kernel, compare same quantity/norm) before moving on.
- AD: fixed sub-step count = fixed-length graph; differentiable Thomas solve; forward-mode JVP for any matvec,
  reverse-mode VJP for the adjoint (checkpoint the sub-step loop if the tape is too deep).
- `.item()` only inside `NoGradGuard`; `index_put_` for assignment; keep `(j,k,i)` layout + zero-copy contract.

## Incremental roadmap (each: build → validate vs dyn_em → byte-identical when off)

- **Inc 0 — scaffolding:** opt-in knob `sdirk3_split_explicit` (default off, fully wired). RK3 stage + acoustic
  substep-count/`dts` machinery (mirror solve_em.F:465-853). Gate: off ⇒ byte-identical baseline.
- **Inc 1 — coupling + prep:** `couple_momentum` + `small_step_prep` (perturbation split, `c2a`, `muts`) in
  libtorch. Validate coupled state + `c2a` vs dyn_em.
- **Inc 2 — vertical implicit w–φ:** reuse verified `calc_coef_w` coeffs; differentiable Thomas (`advance_w`
  solve). Validate w,φ update vs dyn_em (offline M⁻¹@A=I already PASS).
- **Inc 3 — advance_uv:** frozen `ru_tend` + acoustic PGF perturbation `dpxy` + `emdiv`. Validate u,v vs dyn_em.
- **Inc 4 — advance_mu_t + calc_p_rho:** mu′, θ″, ω (`ww`), EOS `al/p` + `smdiv`. Validate vs dyn_em.
- **Inc 5 — full acoustic loop:** assemble +`sumflux`+`small_step_finish`; validate one RK stage vs dyn_em.
- **Inc 6 — RK3 outer + frozen forcing:** full split-explicit step; validate a full timestep ≈ stock RK3.
- **Inc 7 — AD:** JVP-vs-FD (cos≈1) through the forward; reverse-mode adjoint + Taylor test (checkpoint if needed).

## Home & reuse

Lives in the existing libtorch tile solver (`external/libtorch_wrf/sdirk3/`), reusing the zero-copy state,
`StateLayout`, and the RHS infrastructure for the slow tendency. The acoustic kernels are new libtorch tensor
ops mirroring `module_small_step_em.F`. Validation harness = the parity-dump pattern
(`doc/sdirk3_walls_measurement_2026-07-05.md`).
