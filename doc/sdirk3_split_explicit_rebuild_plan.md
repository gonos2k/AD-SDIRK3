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

## Incremental roadmap — STATUS as of 2026-07-07 (see sdirk3_split_explicit_validation_2026-07-06.md)

- **Inc 0 ✅ DONE (`f0ce105`)** — opt-in knob `sdirk3_split_explicit`, byte-identical baseline verified.
- **Inc 1 ✅ DONE (`be7927a`)** — libtorch `c2a` = (cp/cv)(pb+p)/alt, 0.8% vs dyn_em. **Surfaced + fixed a real
  shared-RHS bug:** `compute_inverse_density_hydrostatic` used `cvpm=+cv/cp`; WRF is `−cv/cp` (alt was ~5× wrong).
- **Inc 2 ✅ DONE (`42786ef`)** — `calc_coef_w` a/α/γ; cof exact, γ 1.5% (indexing proven; a/α residual = c2a input
  parity). **dts_rk is PER RK STAGE** (stage 1 = dt/3, not dt/N).
- **Inc 2b ✅ DONE (`eb24b11`→`07df325`)** — differentiable Thomas solve; M⁻¹@A=I rel_err 6.3e-6 AND backward proven
  (out-of-place slabs+stack, detached-band isolation). This solve **IS** `advance_w`'s operational solve (:1433-1443).
- **Discretizations ✅ validated offline** (`upgf`/`divergence`/`vpgf_operator_match.py`, all O(Δ²) floor).
- **Assembly stability ⚠️ partial** (`acoustic_amplification_match.py`): vertical w–φ coupling STRUCTURE stable
  (von-Neumann |λ|=0.998) for constant AND variable c2a(z); implicit A load-bearing. NOT yet: buoyancy + horizontal.
- **Inc 3/4 — mapped precisely, NOT yet ported.** `advance_uv` u-PGF discretization validated offline. `advance_w`
  RHS (vert-PGF + buoyancy + frozen rw_tend + ph update) + `advance_mu_t` (mass/θ, emits muave/ww/muts/t_ave) +
  `calc_p_rho` mapped. **CORRECTION: `calc_p_rho` ≠ Inc 1 EOS** — acoustic `al'` from the geopotential gradient
  (:522-523), `p'` from the temporally-linearized EOS (:527-528). Two field families: full-state c2a vs accumulating
  small-step perturbations.
- **Inc 5 — full acoustic loop (THE remaining implementation). PRECISE SPEC:**
  1. Port `small_step_prep` (module_small_step_em.F:16-290): couple RK-stage state → perturbations
     (`u_2←((c1h·muus+c2h)u_1−(c1h·muu+c2h)u_2)/msfuy`, etc.; `ph_2←ph_1−ph_2`), compute `c2a`[Inc 1]/muts/muus/muvs,
     and SAVE slow refs `*_save`. At rk_step 1, `u_1=u_2` ⇒ perturbations start ~0.
  2. Substep loop ×N (solve_em.F:1525-1869): `advance_uv` → `advance_mu_t` → `advance_w` (RHS off-centered
     (1+εsm)·rhs+(1−εsm)·ph, rhs built with old-w half :1318/:1368, then Thomas[Inc 2b], then ph new-w half :1462)
     → `sumflux` → `calc_p_rho`. **Port LINE-BY-LINE (semi-implicit off-centering is unforgiving; a hand-rebuild
     blew up ×1e5).** Reuse the validated `calc_coef_w`+Thomas.
  3. `small_step_finish` (:295-434): perturbations → full (uncouple + add `*_save` back).
  4. **VALIDATE:** dump dyn_em's w/ph/u/v/θ/mu after ONE substep from a matched acoustic state (`[PARITY substep]`),
     compare the libtorch assembled substep. THIS is what retires the buoyancy + horizontal (full-scheme) risk.
- **Inc 6 — RK3 outer + frozen forcing:** full split-explicit step; validate a full timestep ≈ stock RK3.
- **Inc 7 — AD:** JVP-vs-FD (cos≈1) through the forward; reverse-mode adjoint + Taylor test (checkpoint if needed).
  Note the Inc-2b lesson: in-place recurrences break backward — keep the loop out-of-place (slabs+stack).

## Home & reuse

Lives in the existing libtorch tile solver (`external/libtorch_wrf/sdirk3/`), reusing the zero-copy state,
`StateLayout`, and the RHS infrastructure for the slow tendency. The acoustic kernels are new libtorch tensor
ops mirroring `module_small_step_em.F`. Validation harness = the parity-dump pattern
(`doc/sdirk3_walls_measurement_2026-07-05.md`).

## Inc 5 STATUS (2026-07-07): substep machinery COMPLETE + validated

`small_step_prep` + `advance_substep` (uv→mu_t→w→calc_p_rho) + `small_step_finish` all implemented in
`wrf_sdirk3_acoustic_substep.{h,cpp}` and validated by `test_acoustic_substep.cpp` at SIX levels:
assembly-iterates-x3 (all fields finite), **differentiable** (VJP==FD rel 3e-7), prep→substep consumable,
**prep↔finish invertible** (u,v,w,ph,mu rel=0 — true inverses, mu coupled like ph = mu_1−mu_2), coeffs
M⁻¹A 7.7e-7, advance_w |λ|=0.998315. Operators feature-complete except two `php`-blocked terms
(non-hydro 4th, ww advection) that wire at integration. **Only the WIRING remains.**

## Inc 5→6 WIRING field map (U_n + base state → PrepInput / Const)

The acoustic loop is called ONCE PER RK STAGE inside the split-explicit RK3 (Inc 6). The coupling needs
TWO DISTINCT states — do NOT read both from one tensor (that is the classic miswire):
- **`u_1/v_1/w_1/t_1/ph_1/mu_1`** ← the **time-n** step-start state `U_time_n`, held FIXED by the RK3
  driver across all 3 stages (a SEPARATE tensor, NOT the current stage state).
- **`u_2/v_2/w_2/t_2/ph_2/mu_2`** ← the **current RK-stage predictor** `U_stage` (time-n + RK increment so far).
  At rk_step 1, `U_stage==U_time_n` ⇒ `u_1==u_2` ⇒ perturbations start ~0 (validated property).

**Saves threading (the miswire to avoid):** `small_step_prep` snapshots the predictor into `Saves`
(`u_save=u_2`, …, `mu_save=mu_2`) and `small_step_finish` adds them back. These `Saves` MUST be the
PREDICTOR values captured at prep time and threaded UNCHANGED into finish — the wiring passes the `Saves`
object through, and must NOT re-extract `u_2/…` from the loop-mutated `U_stage` (which by then holds the
perturbation, then the reconstructed field). prep already returns `Saves`; the wiring just carries it.

Sources in the God file (`wrf_sdirk3_tile_unified_impl.cpp`, split_explicit branch ~:6142):
- `u_2/…` prognostics ← `extractStateVariables(U_stage)` (u,v,w, get<4>=θ', get<5>=μ'); full = pert+base.
- `u_1/…` prognostics ← `extractStateVariables(U_time_n)` (the driver's held time-n copy) — the SECOND state.
- `c2a/alt/pb` ← the Inc-1 hydrostatic helpers already in-branch (:6166-6174); `a/alpha/gamma` ← Inc-2 calc_coef_w (:6188+).
- masses: `muts=mub+μ'`; `muus/muvs/muu/muv` = 0.5 mass-averages (module_small_step_em.F:198-207); `mut=mub`.
- metrics `c1h_/c2h_/c1f_/c2f_`, `rdn/rdnw` via existing getters; `fnm/fnp` from the vertical grid; `msf*=1` (em_b_wave idealized).
- **frozen slow tendencies** `ru_tend/rv_tend/rw_tend/ft/mu_tend/ph_tend` ← the RK slow-forcing (the existing RHS
  infra computes these; they are HELD CONSTANT across the N substeps — solve_em freezes them per stage).
- `php` (base geopotential) — wires HERE from the base state, unblocking non-hydro 4th + ww advection.

Loop per stage: `prep → calc_p_rho(step=0) [pressure init] → for n=1..N advance_substep(·,n) → finish`.
Then combine stages (RK3). **VALIDATE via dyn_em `[PARITY substep]`** (matched inputs, one-substep compare).

## Inc 6 driver-structure finding (2026-07-07 reconnaissance)

The existing solver is SDIRK3 = matrix-free Newton–Krylov PER STAGE; its `U_ref_stage_` member is the
per-stage **linearization point** (mutated each stage, sometimes `= U_n`), NOT a fixed time-n state.
The split-explicit RK3 (Wicker–Skamarock) needs a DIFFERENT control structure: a time-n state `U_time_n`
held CONSTANT across all 3 stages + a working stage state advanced by the acoustic loop. So Inc 6 is a
**new driver path GATED on `split_explicit`** (not a tweak to the Newton loop):

```
U_time_n = U_n (clone, held fixed)            // u_1 source for every stage
U_stage  = U_n
for s in 1..3 (RK3 fractional steps dt/3, dt/2, dt):
    R_slow = slow_rhs(U_stage)                // frozen forcing for this stage (existing RHS infra)
    prep   = small_step_prep( u_1<-U_time_n, u_2<-U_stage, masses, metrics )   // TWO states (Codex fix)
    S,Saves= prep;  S = calc_p_rho(S, C, 0)   // pressure init
    N = num_sound_steps for this stage        // stage 1: 1 big step (dt/3); 2/3: dt/num_sound
    for n in 1..N: S = advance_substep(S, C_with_R_slow, n)
    U_stage = pack( small_step_finish(S, Saves, prep_in, s, 3, N, dts, h_diabatic) )
U_out = U_stage
```

This path REPLACES the ARK324 fall-through only when `split_explicit` is on (baseline byte-identical when
off — the hard-constraint). Sub-tasks: (a) slow_rhs → the six frozen tendencies (reuse computeUnifiedRHS
slow terms); (b) mass averages muus/muvs/muu/muv from mub+μ'; (c) php from base state; (d) pack/unpack
State↔U_n via StateLayout; (e) per-stage dts/N schedule. VALIDATE incrementally: first ONE stage vs dyn_em
`[PARITY substep]` (matched inputs), then a full step ≈ stock RK3 (Inc 6 exit criterion).

## Inc 6 sub-task status (2026-07-08)

- (b) mass averages — DONE+tested (`mass_to_u/vpoint`, 66bff9b; y-symmetric BC fix 20d292e).
- (e) schedule — DONE+tested (`acoustic_schedule`, e490046).
- (c) php-wire — INFRA READY: `ph_base` exists (wrf_sdirk3_types.h:211, {ny,nz+1,nx} w-staggered).
- (d) State↔U_n adapter — INFRA READY: `packState`(:7921)/`unpackState`(:7913) already invert the state
  layout; the adapter is a thin wrapper (unpack→perturbations+base; pack FinishOutput u/v/w/ph/t/mu back).
- (a) slow_rhs → 6 frozen tendencies — NOT done. THE remaining delicate piece: separate the SLOW forcing
  (advection/Coriolis/physics) from the FAST acoustic terms (PGF/divergence/buoyancy, which the substep owns).
  The God file already has the IMEX slow/fast split (RhsMode / effective_imex_split_mode) to reuse.
- Driver assembly — the gated RK3 loop tying (a)-(e) together, validated by dyn_em [PARITY substep].

The standalone components are all built+validated; the remainder is God-file-coupled integration (slow_rhs
extraction + adapter + gated driver), which is the culminating unit and needs the dyn_em parity gate.
