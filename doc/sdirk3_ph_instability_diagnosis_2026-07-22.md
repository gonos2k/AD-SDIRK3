# Split-explicit dt=600 ph-instability — measured diagnosis (2026-07-22)

> ## ⏱ AUTHORITATIVE STATUS (2026-07-24, review 9F.D3) — READ THIS FIRST; supersedes all below
>
> **1. Current verified conclusion.** With the matched-input dyn_em parity harness now made
> AUTHORITATIVE (pointwise, per-`tests/rk_tendency_parity_diff.py --selftest`): at step-1/rk_step-1 on
> the identical em_b_wave IC, **5 of 6 slow-tendency channels match WRF POINTWISE** — ru/rv/ph/t/mu have
> `e2 ≤ 2.7e-3` and `corr = 1.00000` (genuine field equality, not a max-ratio; the tool's sign-flip /
> k-permute / one-cell-shift negative controls are all DETECTED). The **`rw` channel FAILS**: `e2 = 0.35`,
> `corr = 0.92`, worst at levels **k=1–6 (near-surface) and k=64 (lid)** — a vertical-PGF band deficit.
>
> **2. Current PRODUCTION status (honest).** The split-on default currently **OMITS** WRF's legitimate
> vertical-PGF/gravity term (`pg_buoy_w`, :2494) because it was fed a broken pressure (`p_pgf`, 6–16×
> too large at mid/upper levels). Omitting it removed the dominant early rw distortion (per-step w-ratio
> went from climbing-1.4-from-step-15 to a flat ~1.000 for 24 steps) — **but end-to-end dt=600 stability
> is NOT resolved** (blowup still ~step 38), and the omission is an interim workaround toward an
> INCOMPLETE equation, not a physically complete production fix. "primary solved" (below) overstates it.
>
> **3. Measured facts (survive).** epssm reaches calc_coef_w (cof ×2.98); dnw<0/rdnw>0/dnw·rdnw=−1
> (now validated UNCONDITIONALLY, not behind debug_level); φ-denominator never collapses (nonpos=0);
> `w_new` numerator explodes (~4.9e6) while denominator holds ~89000; net Σ DMDT ≈ roundoff (now
> reduced in FP64 and separated from mu_tend); the acoustic operators + prep/finish + composition are
> pointwise-faithful; `fzm≡fnm, fzp≡fnp` by WRF definition (module_advect_em.F:10688-9), so the helpers'
> use of fnm/fnp is faithful (reviewer's fzm/fzp hypothesis REFUTED).
>
> **4. Still UNRESOLVED.** (a) correct pressure for :2494 so `rw` reaches pointwise parity (the "p-ladder":
> diag_p_al vs diag_p_hypso2 vs make_thermo — compare vertical GRADIENT ∂_η p, not just p); (b) restore
> the legitimate :2494 term to the production default once its pressure is correct (or fail-close split-on
> until then); (c) a rigorous spectral verdict via a full one-step JVP/Arnoldi at a fixed checkpoint — the
> `w_rms` ratio is strong geometric-growth evidence but NOT a ρ(J) proof.
>
> **5. Next acceptance experiment.** Per-term rw dump (advection / curvature / coriolis / vertical-PGF /
> total) on BOTH WRF and port + pointwise parity per term; then the pressure-gradient parity `e_{∂p}(k)`.
>
> **5b. PRESSURE-LADDER MEASURED (2026-07-24).** Dumped WRF `grid%p` and port `p_pgf` at step-1 and
> compared VALUE vs vertical GRADIENT (what :2494 uses): **p value matches — corr 1.0000, e2 0.0098
> (~1%)** — but its **vertical gradient `∂_η p`=p(k)−p(k-1) is off — corr 0.94, e2 0.36, up to 1.008 at
> k=31**. Root: CANCELLATION. `p_pgf ≈ p` to ~1 Pa, but `dp ~ 0.3` is a small difference of large
> near-hydrostatic values, so the 1% p error becomes 30–100% in `dp` at mid/upper levels (where `dp` is
> smallest). pg_buoy_w = `g·rdn·dp` divides exactly those differences ⇒ the 6–16× mid/upper explosion.
> **⇒ the complete fix is NOT a better pressure VALUE (already 1%) but a pressure whose vertical
> DIFFERENCES match WRF** — e.g. reconstruct `dp` directly from the hydrostatic/EOS relation instead of
> differencing a reconstructed p. Acceptance = `e_{∂p}(k) < tol` at ALL levels, then restore :2494 and
> re-measure rw parity + |λ|.
>
> **5c. Carried-pressure shortcut TESTED → DEAD END (2026-07-24).** The port carries `p_pert_` ("WRF
> perturbation pressure"), which matches WRF's `p` HORIZONTALLY (corr −1.0000, i.e. p_pert_≈−grid%p) —
> so it looked like a free difference-consistent source. But feeding `−p_pert_` to pg_buoy_w gives
> `max|pg_buoy_w| ≈ 2e-4` (≈0): `p_pert_` has a **~zero VERTICAL gradient** (its −1.0 correlation is a
> horizontal-pattern match; the vertical structure is absent). ⇒ no carried field shortcuts the
> reconstruction; `dp` must be built from the hydrostatic/EOS relation. The complete fix is genuinely
> the deep p-ladder reformulation.
>
> **6. Superseded hypotheses:** "pg_buoy_w is a double-count of advance_w thermal buoyancy" (WRONG — it is
> WRF's legitimate :2494 vertical-PGF, just fed a broken pressure; corrected 2026-07-23) and "ρ(G)≈1.4
> proven" (downgraded to geometric-growth evidence pending JVP/Arnoldi). Any "double-count / Mechanism
> confirmed / primary SOLVED" wording in the older sections below is SUPERSEDED by this header.

Continuation of the PR #69 `advance_w` geopotential decomposition. All measurements were run on the
**live `em_b_wave` dt=600 executable** with `sdirk3_split_explicit=.true.` (the agent that built #69 could
not run the live executable). Every diagnostic here is **opt-in and default-off ⇒ the baseline numerical
path is byte-identical** (verified via `tests/numerical_fingerprint.sh` MATCH after each change).

## COMPLETE DIAGNOSIS (2026-07-23, supersedes "double-count" framing)
Deeper parity (per-level, pg_buoy_w isolated) corrects the mechanism: `pg_buoy_w_stage` is NOT a
double-count of advance_w's *thermal* buoyancy — it is the port's implementation of WRF's **:2494
vertical-PGF/gravity term** (`(1/msfty)·g·(rdn(k)·(p(k)−p(k-1)) − c1f·mu)`, module_big_step_utilities
_em.F:2484/2494), which IS part of WRF's `rw_tend`. But it is fed a broken pressure reconstruction
(`p_pgf` from diag_p_al). Per-level vs WRF `rw_tend`:
- **low levels k=1–5: corr 0.61–0.75, magnitude ~right** — it correctly captures the vertical-PGF that
  dominates WRF's low-level rw (where curvature∝u²≈0 since the jet's surface wind is weak).
- **mid/top k=20–64: 6–16× TOO LARGE, corr 0.29–0.4** — spurious huge vertical p_pgf gradients explode
  the term where WRF's rw is curvature-dominated (~1007 at k=40 vs port 6027).

So WRF `rw_tend` = curvature(:4463, mid-level, ~u²) + vertical-PGF(:2494, low-level+top, ~dp/dz). The
port has curvature_w (corr 0.92 with the mid part) but its :2494 (pg_buoy_w) is right at low levels and
6–16× too big at mid/upper. That mid/upper explosion, regenerated each RK step, drove the primary
`|λ|≈1.4` w↔φ eigen-instability.

**FIX SHIPPED (interim, correct given the broken p):** drop pg_buoy_w (rw = rw_slow + curvature_w).
This removes the mid/upper 6–16× explosion → |λ| neutral 1.000 for ~24 steps (primary SOLVED), at the
cost of the low-level vertical-PGF → a weaker SECONDARY (rw 0.90×, low-level/top band missing, blowup
step 38). **COMPLETE FIX (next):** supply :2494 a CORRECT pressure so pg_buoy_w matches WRF at ALL
levels — i.e. fix the diag_p_al / p_pgf reconstruction's mid/upper vertical gradient (the known "p-
ladder" problem: memory records hypso-1/hypso-2/make_thermo attempts, hypso-1 was "best"). The parity
harness (`WRF_PARITY_RK_TEND_DUMP`) is the exact fix-verify oracle: iterate the p reconstruction until
rw ratio→1.000 and corr→1.0, then re-measure |λ|.

## FIX LANDED (2026-07-23) — pg_buoy_w (broken :2494 vertical-PGF) removed; primary instability resolved
The matched-input parity harness found the root cause (below) and the fix is committed:
`rw_coupled = rw_slow + curvature_w` (dropped `pg_buoy_w_stage`; A/B restore `WRF_SDIRK3_KEEP_PG_BUOY_W`).
MEASURED impact:
- **Parity restored:** rw ratio 2.63→0.896, corr 0.21→0.92; ru/rv/ph/t/mu all exact 1.000.
- **|λ| primary instability GONE:** per-step ratio is now **1.000 (neutral) for ~24 steps** (was
  climbing 1.0→1.4 from step 15). Baseline byte-identical (split path opt-in).
- **Mechanism confirmed:** `pg_buoy_w_stage` re-applied the buoyancy that `advance_w` already handles
  (acoustic buoy term :706) — a spurious slow-forcing DOUBLE-COUNT (corr 0.014 with WRF `rw_tend`),
  inflating the frozen w-forcing 2.6× and driving the per-step w↔φ eigen-instability.

**SECONDARY residual (remaining, characterized):** with pg_buoy_w gone, rw is still ~10% low
(0.896, corr 0.92) — a `curvature_w_stage` accuracy gap, NOT a missing w-advection (at step 1 the
balanced IC has w≈0 so w-advection≈0; WRF's rw_tend=70.75 is the CURVATURE term). WRF's w-curvature
is `(u²+v²)/a + 2·u·Ω·cos(lat)` (ADT eqn, advance_w comment :1300); `curvature_w_stage` (1/a only)
likely omits the `2·u·Ω·cos` Coriolis-curvature component → the 0.08 decorrelation + 10% deficit.
This residual drives a SLOWER secondary instability (|λ| neutral to step ~24, then climbs to 1.38 by
step 37, blowup 38) — an order of magnitude weaker onset than the primary.

**Secondary investigation (measured):** the Coriolis `e`-term (`coriolis_w_stage`, added) is a faithful
WRF port but INACTIVE for em_b_wave — it's an f-plane with `e=2Ω·cos(lat)=0` (only `f=2Ω·sin(lat)`),
so rw is bit-identical with/without it (it will matter for real-earth 4D-Var). Per-LEVEL parity then
localizes the true residual: port rw matches WRF at mid/upper levels (k=20–63, ratio 0.87–0.96, corr
~0.9) but is **≈0 at the low levels (k=1–10, where WRF L2~200) and at the top (k=64, WRF=248)** — a
NEAR-SURFACE + TOP-BOUNDARY rw deficit that the curvature reconstruction misses (WRF's curvature loop
is `k=MAX(2,kts),ktf`; the near-surface/lid rw_tend has a boundary contribution not reproduced). This
boundary-band deficit is the remaining secondary source; closing it (reproduce WRF's low-level + lid
rw_tend) is the next step to drive rw parity → 1.000 and re-measure |λ|.

## ROOT CAUSE FOUND (2026-07-23) — matched-input dyn_em parity: rw slow forcing 2.6× too large
Built the matched-input parity harness (env `WRF_PARITY_RK_TEND_DUMP`): a stock RK3 run
(time_integration_scheme=3) dumps WRF's `rk_tendency` at step-1/rk_step-1
(`solve_em.F` after :1164, big-endian stream), the split run (scheme=6+split) dumps the port's
assembled frozen forcing (`ru_coupled..ph_coupled/t_coupled/mu_slow`, tile_unified :7220) at the
IDENTICAL step-1 IC, and `tests/rk_tendency_parity_diff.py` diffs per channel. Result:

| channel | WRF max\|·\| | port max\|·\| | ratio | verdict |
|---|---|---|---|---|
| ru | 33.37 | 33.38 | 1.000 | ✅ |
| rv | 82.09 | 82.09 | 1.000 | ✅ |
| **rw** | **70.75** | **186.2** | **2.63** | ❌ 2.6× (L2 4×) |
| ph | 1732 | 1732 | 1.000 | ✅ |
| t  | 9.36 | 9.36 | 1.000 | ✅ |
| mu | 0 | 0 | — | ✅ |

**5 of 6 channels match WRF exactly; the `rw` (slow w-momentum) forcing is 2.6–4× too large** — and
the measured growing eigenmode is exactly w↔φ. Component decomposition (re-dump under ablation):
zeroing `pg_buoy_w_stage` brings rw 186→63 (≈ WRF 70.75); `curvature_w` is negligible (186→170).
⇒ **`pg_buoy_w_stage` is a spurious/double-counted w-buoyancy forcing**: `rw_slow` (from
computeUnifiedRHS) ALREADY contains the w pressure-gradient/buoyancy (giving ~63 ≈ WRF's 70.75), and
the port ADDS `pg_buoy_w_stage` on top (+123), inflating the frozen slow w-forcing 2.6×. This excess
w-forcing, regenerated each RK step, is the resolved-scale positive feedback that produces the
per-step `|λ|≈1.4` w↔φ eigen-instability.

**CAVEAT (measured):** the earlier step-count ablation of `pg_buoy_w` gave 38 steps (~baseline 39) —
zeroing it entirely OVER-corrects (rw 63 < WRF 71, −10%) rather than matching WRF, and the blowup
persisted; so the FIX is to make `rw` MATCH WRF (remove the double-count so rw_slow stands, or
reconcile the ~10% residual), then RE-MEASURE `|λ|`. This is the first defect the whole campaign has
localized to a specific term with a quantified error against the dyn_em reference — the harness did
exactly its job.

## SYNTHESIS (2026-07-23): the |λ|>1 IS "Wall-2" (slow-RHS advection), acoustic machinery exonerated
The custom slow tendency is `compute_k_slow` → **`computeUnifiedRHS(RhsMode::ExplicitOnly)`** — the
SHARED slow/advective RHS used by the whole SDIRK3/ARK machinery (not a small split-only builder). So
the split-explicit `|λ|≈1.4` is a property of the **resolved-scale explicit advection RHS**, which is
exactly the previously-documented **Wall-2**: "the explicit u-momentum slow tendency (`ru` block) blows
up as a bilinear/quadratic stage cascade; the term is horizontal advection; the tableau over-
extrapolates the sheared-jet advective tendency at large dt." This session's contribution is to
**definitively exonerate the acoustic machinery** (every operator + prep/finish + composition faithful;
per-substep operator STABLE) — so Wall-2 is localized to the slow RHS, OUTSIDE the acoustic solve, and
the two "walls" are now understood: Wall-1 = implicit indefiniteness (ARK path, precond-dead); Wall-2 =
slow-advection over-extrapolation (split path, this result). The fix target is `computeUnifiedRHS
(ExplicitOnly)` — either a defect vs WRF's `rk_tendency` (find via matched-input parity) or a genuine
RK-stability limit of the explicit advection at dt=600 that WRF avoids via its specific advection
order/limiter (compare the advection scheme + order). Definitive next unit unchanged: matched-input
dyn_em parity of `R_port(U)=computeUnifiedRHS(ExplicitOnly)` vs WRF `rk_tendency` at one state.

## CONSOLIDATED CONCLUSION (2026-07-23): faithful acoustic machinery + custom slow tendency is the locus
After an exhaustive term-by-term parity pass, EVERY piece of the split-explicit acoustic machinery is
a verified-faithful WRF port:
- Operators: `advance_uv` (3-term PGF + non-hydro 4th + damping), `advance_mu_t` (DMDT + muave + muts +
  mudf + ww recurrence + theta), `advance_w` (+ rhs/ww/t_2ave build), `calc_p_rho` (al/p/smdiv,
  muts-arg confirmed) — all match `module_small_step_em.F` verbatim (modulo em_b_wave's unit factors
  cqw=msft=1, damp_opt=0 inert).
- Structure: `small_step_prep` coupled-perturbation setup (u"/v"/t"/w"/ph"/mu" :866-878 ↔ :243-276),
  the u_1/u_2 time-level split, and the SSP-RK3 `dt/3,dt/2,dt` composition — all faithful.
- The per-substep acoustic operator is STABLE (bounded oscillation, measured), and the isolated w-φ
  Thomas is `|λ|=0.998`.

Yet the assembled per-STEP map is `|λ|≈1.4`. Since the sub-step machinery is faithful+stable and the
composition is faithful, the defect is confined to the ONE thing that is NOT a direct WRF port: the
**custom stage slow-tendency builders** (`rhs_ph_stage`, `pg_buoy_w_stage`, `curvature_w_stage`,
`compute_stage_tendency`, the slow-PGF `dpx_st`/`dpy_st`) — the resolved-scale `R(U)` regenerated each
RK step. The memory records these had MEASURED discrepancies vs dyn_em (`ph_tend` 26× low; rv
geostrophic residual 6× large). Ablating the individual slow forcings (pg_buoy_w, curvature_w, rhs_ph)
is NULL because the error is in the tendency VALUE (a wrong-but-nonzero forcing), not its presence —
only **matched-input dyn_em parity** of `R_port(U)` vs `R_wrf(U)` at one state can pin it. The
`solve_em.F` `[PARITY sub]` dump hooks (:1697/:1819/:1939) are the substrate. This is the definitive
P0-5 unit and the correct next build; the FD amplification matrix (perturb U_n, one full RK step,
read `|λ|`+eigenvector) is the cross-check.

## LOCUS SETTLED (2026-07-23): the |λ|>1 is per-RK-STEP (slow tendency), NOT per-substep (acoustic)
Decisive within-loop measurement: at `num_sound_steps=32`, one stage-3 acoustic loop's 32 substeps
(FIXED forcing + FIXED operator) give `w_rms` = 12.7→47→7.8→40→0.5→35→7→31→13 — a **bounded,
slightly-DECAYING oscillation**, NOT growth. ⇒ the **per-substep acoustic operator is STABLE**
(`|λ|≤1`; the acoustic wave bounces vertically with bounded amplitude). The earlier "within-loop
growth" (n_sub=4: 20→73) was just the first quarter-cycle of this oscillation.

Therefore the measured per-STEP `|λ|≈1.4` is introduced **once per RK step**, by the **slow-tendency
regeneration + RK3 composition** — NOT the acoustic sub-stepping. This is fully consistent with every
acoustic operator being faithful (they are) and the isolated w-φ Thomas being stable (0.998): the
sub-step machinery is correct; the defect is in the resolved-scale (slow) forcing `R(U)` or the stage
composition, applied once per step. The forced-steady-state reconciliation holds: within a stage,
`x_{k+1}=G·x_k+b` with STABLE G and a large fixed `b` (the slow forcing) — the oscillation is bounded.

Ablations so far (env-gated, byte-identical): slow-w components `pg_buoy_w`/`curvature_w` (incl. the
spherical-curvature 1/a term, a spurious-forcing candidate for the Cartesian channel) have **no
effect** on the step-39 failure ⇒ NOT the driver. Remaining slow-forcing suspects for the w↔φ
eigenmode: `rhs_ph_stage` (the slow φ tendency) and the horizontal-PGF/advection tendencies feeding
`ru_slow`/divergence. Next: ablate/parity-check `rhs_ph_stage` and the slow advection, then the
per-STEP FD amplification matrix (perturb U_n, run one full RK step, read `|λ|`+eigenvector) — now
correctly scoped to the STEP map, not the substep.

## DEFINITIVE MECHANISM (measured 2026-07-23): fixed |λ|≈1.4 w↔φ eigen-instability
`[SPLIT-EXPLICIT AMP]` logs `w_rms`/`ph_rms` every substep. Because one stage's acoustic loop uses a
FIXED operator/`dts`, the model itself performs **power iteration** on its dominant mode — so the
end-of-stage-3 (`sub=4`) `w_rms` ratio across consecutive physical steps IS the per-step amplification
`|λ_step|` of the exact LIVE operator (no offline reconstruction / toy). Full 39-step trajectory:

- steps 1–14: initial transient **decays** (w_rms 69→10, ratios <1) — the mode is not yet aligned.
- steps 15–30: dominant unstable mode emerges, step-ratio **climbs** 1.01→1.06→1.17→1.27→1.36.
- steps 30–38: step-ratio **plateaus at ≈1.36–1.44** (w_rms 73→100→136→186→257→360→513→741) — the
  classic power-iteration convergence to `|λ_max|`. `ph_rms` co-amplifies in phase.
- step 39: nonlinear runaway (ratio 1739, w_rms→2e6).

**MEASURED:** the dominant coupled w↔φ acoustic mode grows geometrically with a per-step ratio that
**converges to ≈1.4 > 1** ⇒ `ρ(G_step) ≈ 1.4`, a **fixed |λ|>1 local eigen-instability**. The
power-iteration *convergence* signature discriminates the three review candidates decisively:
**affine drift** (linear growth, constant additive) — REFUTED (growth is geometric, ratio→plateau not
constant slope); **non-normal transient** (`ρ≤1, ‖Jᵏ‖>1`, peaks then decays) — REFUTED (growth is
sustained and the ratio asymptotes to a stable >1 over ~10 steps); **fixed |λ|>1** — CONFIRMED. This
supersedes the P0-6-retracted "|λ|>1" guess — it is now measured, with the discriminating signature.

**INFERRED (well-founded, not directly measured):** WRF's split-explicit acoustic scheme is
*stable* at this dt by construction — that is why WRF uses acoustic sub-stepping. A ported acoustic
scheme showing `|λ|>1` therefore points to a **defect in the ported w↔φ coupling** (a dropped/
mis-signed/mis-weighted term in `advance_w`'s w-RHS or the φ-update off-centering), not to intrinsic
stiffness. ⇒ **`advance_w` is the priority target for dyn_em matched-input parity (P0-5)** — term by
term on the w-solve and the w↔φ off-centering, at a matched mid-run substep. The mass-continuity
channel (`advance_mu_t`, P0-4/§7 decomposition) is de-prioritized: mass + φ-denominator are healthy;
`w` is the exploding variable.

## P0-5 (advance_w term-by-term parity) — advance_w is FAITHFUL; the defect is in the COUPLING
Compared the ported `advance_w` (`wrf_sdirk3_acoustic_substep.cpp:687-742`) against WRF
`dyn_em/module_small_step_em.F:1366-1465` term by term:

- **w-RHS interior (:1405-1417)** and **top (:1421-1429)**, and the **φ-update (:1462)** match verbatim.
  The only reference factors absent from the port are `msft_inv(i)` (both PGF+buoyancy terms) and
  `cqw(i,k,j)` (PGF term). For em_b_wave both are **exactly 1.0**: `mp_physics=0` ⇒ dry ⇒ `cqw≡1`
  (cqw is the moist coefficient; it also multiplies the `calc_coef_w` a/b/c :632-639, all cqw=1 here),
  and idealized ⇒ unit map ⇒ `msft_inv=1`.
- **`damp_opt==3` top w-Rayleigh damping (:1445-1458)** — em_b_wave has `damp_opt=0`, so WRF applies
  none; the port correctly treats the branch as inert. **Not the defect.**

⇒ `advance_w`'s arithmetic is a faithful WRF port for this case, so the measured `|λ|≈1.4` is **NOT an
`advance_w` formula defect.** This reconciles with the prior offline verification that the **isolated
w-φ Thomas solve is `|λ|=0.998` (stable)**: the instability is not in any single operator. Per the
differentiable-core discipline — *isolated primitives can each be faithful while the coupled
semi-implicit step is `|λ|>1` from one subtly dropped/mis-coupled term* — the defect lives in the
**coupling of the acoustic operators** (`advance_uv→advance_mu_t→advance_w→calc_p_rho` composition, the
`t_2ave`/`muave` off-centered averages that thread thermodynamics into w, the `rhs`/`ww` build) and/or
the **RK3 stage composition** (per-stage frozen-tendency regeneration + stage-state handoff). The
definitive P0-5 unit is therefore **matched-input dyn_em parity of the FULL acoustic substep** (replay
a dumped WRF intermediate state through the coupled operators, compare term by term), not a
single-operator check — which are all already verified.

## P0-5 continued — advance_w's FULL INPUT BUILD is also faithful (t_2ave lifecycle resolved)
Extended the parity inward to everything that feeds the (already-faithful) w-solve, vs WRF
`module_small_step_em.F:1305-1368`:

- **rhs pass-1 (:1318)** `rhs=dts·(ph_tend + 0.5·g·(1-eps)·w)` — matches port :656.
- **`ww·d(φ)/dη` advection (:1343-1355, ELSE branch; em_b_wave `phi_adv_z` unset ⇒ default)** — matches
  port :662-675. Verified the 1-based↔0-based `fnm/fnp/wdwn` index map term by term:
  `rhs₀[r] -= dts·(fnm₀[r]·wdwn_body[r] + fnp₀[r]·wdwn_body[r-1])` on both sides (the apparent
  off-by-one dissolves once WRF's `wdwn(k+1)` storage index is tracked correctly).
- **rhs pass-2 (:1368)** `rhs = ph + rhs/(c1f·mut+c2f)` — matches port :678 (msfty=1).
- **`t_2ave` off-centering (:1314-1317)** — matches port :684-685, INCLUDING the subtle old-time arm:
  WRF passes `t_2save` as advance_w's `t_2ave`; `advance_mu_t` (:969, arg `t_ave`↔`t_2save`)
  OVERWRITES `t_2save = t_old` at :1141 every substep BEFORE advance_w reads it ⇒ the `(1-eps)` arm is
  the pre-update theta this substep, NOT a recursively-accumulated value. The port's `o.t_ave = s.t`
  (:589) → `s.t_ave` reproduces exactly this. **No accumulation-semantics defect.**

⇒ `advance_w` and its **entire input build** are faithful. The coupling defect (or the genuine
scheme property) that yields `|λ|≈1.4` is therefore confined to the **remaining coupled operators**
(`advance_uv` PGF/damping, `calc_p_rho`, the `muave` (1±eps) mass average) and/or the **RK3 stage
composition** (per-stage slow-tendency regeneration `ru_tend/rw_tend/ph_tend/t_tend` + stage-state
handoff + the `dt/3, dt/2, dt` acoustic schedule). Those are the next matched-input-parity targets.

## P0-5 continued — advance_uv is ALSO faithful
Compared `advance_uv` (`acoustic_substep.cpp:446-544`) vs WRF `module_small_step_em.F:805-868`:
- `u += dts·ru_tend` (:805) ✓
- 3-term horizontal PGF `dpxy` (:828-831 — ph-gradient at k+1&k, `(alt+alt₋)·dp`, `(al+al₋)·dpb`)
  ✓ (`msfux/msfuy=1`); the wrap_d/wrap_s periodic differences map verbatim.
- **non-hydrostatic 4th term** (:848-862; em_b_wave IS non-hydrostatic) ✓ — `dpn` surface
  (`cf1/cf2/cf3`), interior (`fnm/fnp`), top-lid-off=0, `php` gradient, and
  `rdnw·(dpn(k+1)-dpn(k)) − 0.5·c1h·(mu+mu₋)` all match, incl. the `rdnw` WRF-sign and the dpn
  level indexing.
- `u −= dts·cqu·dpxy + c1h·mudf_xy` (:868, cqu=1) and the `−emdiv·dx·(mudf−mudf₋)` divergence damping
  (:809) ✓. v is the y-symmetric analog (wall rows frozen, :790-797).

⇒ **`advance_uv` is faithful.** Cumulative verified-faithful set: isolated w-φ Thomas (|λ|=0.998),
`advance_w` arithmetic, the rhs/ww/t_2ave build, `advance_uv` (PGF + non-hydro + damping), mass
conservation, φ-denominator. The `|λ|≈1.4` coupling defect (or genuine scheme property) is now
confined to: `advance_mu_t`'s `muave` (1±eps) average + the `ww` vertical recurrence (feeds
advance_w's rhs), `calc_p_rho` (the EOS pressure/density update that CLOSES the acoustic feedback
loop p→PGF→u→div→μ,θ→p — a prime amplification suspect), and/or the RK3 stage composition. Next:
`calc_p_rho` term-by-term, then the RK3 slow-tendency regeneration.

## P0-5 continued — calc_p_rho is ALSO faithful (muts-arg confirmed)
Compared `calc_p_rho` (`acoustic_substep.cpp:751-770`) vs WRF `module_small_step_em.F:515-567`:
- `al = -1/(c1h·mut+c2h)·(alt·c1h·mu + rdnw·(ph(k+1)-ph(k)))` (:522-523) ✓ (rdnw WRF-sign).
- `p = c2a·(alt·(t_2 - c1h·mu·t_1)/((c1h·mut+c2h)·(t0+t_1)) − al)` (:527-528) ✓.
- divergence damping `p += smdiv·(p − pm1)` (:562, step≠0) ✓.
- **The `mut`-arg subtlety (candidate for the feedback loop) is REFUTED:** the port uses `s.muts`
  (per-substep updated mass) in the density denominator, and this is FAITHFUL — WRF's `calc_p_rho`
  call (`solve_em.F:1382`) passes `grid%mu_2, grid%muts` into the `(mu, mut)` dummies (:440), so the
  reference `Mut` here IS `grid%muts` (updated), exactly as the port uses. Not a defect.

⇒ **all four acoustic operators are now verified faithful**: `advance_uv`, `advance_mu_t` (DMDT
conservation), `advance_w` + its input build, `calc_p_rho`. The remaining unverified pieces are
`advance_mu_t`'s `muave` (1±eps) average + `ww` vertical recurrence (term-by-term), and — the prime
remaining suspect — the **RK3 stage composition** (the `dt/3, dt/2, dt` acoustic schedule + per-stage
slow-tendency regeneration + stage-state handoff), which is the SDIRK3-specific outer structure that
diverges most from stock WRF and is where a `|λ|>1` composition defect would live.

## Composition probes — uniform per-stage growth + divergence damping is the WRONG stabilizer
- **Per-stage amplification is UNIFORM.** `[SPLIT-EXPLICIT COMPONENTS]` w-increment `‖dx‖` per stage
  across steps: stage-1 ≈1.4-1.5, stage-2 ≈1.4-1.5, stage-3 ≈1.4-1.5 — all three RK stages amplify at
  the SAME ~1.4/step. ⇒ NOT a single-stage composition bug (no one stage carries the mode); the shared
  acoustic-coupling eigenmode is reflected uniformly in every stage.
- **Divergence damping does NOT stabilize the mode.** `smdiv=emdiv ∈ {0, 0.05, 0.1}` all reach the
  same step-39 failure ⇒ the `|λ|≈1.4` mode is NOT an acoustic-divergence mode (smdiv is designed for
  that; it has no effect here). At `smdiv=emdiv=0.5` the run instead CRASHES at step 6 (ratios
  1.7→4.2→11.5→32) — the forward-pressure-weighting term itself becomes destabilizing at large
  coefficient. Both the formula (`p += smdiv·(p−pm1)`, `mudf_xy = −emdiv·dx·Δmudf`) and this behavior
  are consistent with WRF; the point is the growing mode lives OUTSIDE the divergence-damping subspace.
- ⇒ the mode is the **w↔φ vertical acoustic/gravity mode**, whose WRF stabilizer is the implicit
  `epssm` off-centering. Yet the isolated w-φ Thomas (with that off-centering) is `|λ|=0.998` and the
  `epssm` sweep doesn't fix the coupled step — so the defect is in the coupling the isolated solve
  omits (it holds u/μ/θ/p fixed). LAST un-verified coupling outputs: `advance_mu_t`'s **`muave`**
  ((1±eps) mass average → advance_w buoyancy + t_2ave normalization) and the **`ww` recurrence** (→
  advance_w rhs). Those are the next term-by-term targets; if both are faithful, the coupled scheme's
  `|λ|>1` at dt=600 must be validated against a stock-RK3 dt=600 control (memory: baseline done ⇒ stock
  is stable ⇒ port defect) and then via the full-step FD amplification matrix.

## Result summary
The split-explicit geopotential (`ph`) blowup is **parameter-insensitive** (buoyancy/epssm/damping sweeps
do not stop it) and localized by per-operator logging to the **horizontal / mass-continuity channel** — the
μ growth in `advance_mu_t` (or upstream `advance_uv` feeding a divergent u that μ accumulates). **The exact
mechanism is NOT yet established (review P0-6):** "fixed |λ|>1 eigen-instability" is only one candidate;
non-normal transient growth (ρ(J)≤1, ‖Jᵏ‖>1) and affine mass-drift (ρ(J)≈1 + steady forcing) are equally
consistent with the current evidence and are arguably MORE likely given the higher-priority open items
below (effective-config not fully audited, `dnw·rdnw=-1` sign contract un-asserted on production operands,
seam/wall/global-mass conservation un-contracted). The next step is dyn_em `[PARITY substep]` matched-input
arm-parity + the provenance/sign/mass-domain contracts, NOT an `advance_mu_t` arithmetic change.

## The measurement chain
1. **Closure is roundoff-exact.** The #69 decomposition `Δφ = frozen + old_w + vertical_adv + new_w` closes
   to `|C_φ|_rms/|Δφ|_rms ~ 1e-8` across all 274 records ⇒ the diagnostic decomposition faithfully
   reconstructs the CURRENT C++ `advance_w` arithmetic. **CAVEAT (review P0-4/6): this is C++ SELF-CONSISTENCY,
   not an independent WRF-correctness proof** — a shared sign/indexing error in both production and diagnostic
   (e.g. the same `rdnw` sign) would still close to ~0. Independent correctness needs an FP64 reference
   decomposition and/or dyn_em matched-input parity.
2. **Physical-unit reveal.** The coupled `ph_tend`=1732 is physical `ph_tend`≈0.019 once divided by the
   ph-denominator `mf=c1f·muts+c2f` (~89000). **CAVEAT (review P0-3/6): `mf` is the ph DENOMINATOR, not the
   column mass μ** (equal only for the c1f=1,c2f=0 idealization). The mass-coupled interpretation + `advance_w`
   DECOUPLING are contract-pinned by #67, but production `rhs_ph_stage` GENERATION has NOT yet passed
   matched-input dyn_em parity — "ph_tend correct" is over-stated until it does.
3. **Trajectory: neutral → slow growth → collapse.** stage-1/sub-1 `actual_delta` oscillates ~0.5–1.77
   (|λ|~1.0) for ~85 substeps, THEN the `new_w` arm grows monotonically (0.48→…→7.8e5) while `actual`
   stays ~1 (**cancelled** by frozen/vertical), until the cancellation breaks and `mass_new` goes NEGATIVE
   → catastrophic blowup. "big arm + small actual = cancellation" — never clip on individual-arm max.
4. **Parameter sweeps eliminate the tunable hypotheses (env, no rebuild):**
   - buoyancy ablation (`WRF_SDIRK3_ABLATE_BUOY_W=1`): trajectory UNCHANGED ⇒ buoyancy REFUTED.
   - `epssm` 0.1/0.9: the effective value + Thomas stiffness `cof` PROVEN to change ×2.98 at the point of
     use (`[SPLIT-EXPLICIT COEF]`, epssm_effective 0.1→0.9, cof 1.16e6→3.47e6, max|coef.a| 30500→90995 —
     the env override genuinely reaches calc_coef_w, past the Fortran set_config pass-through). Despite that
     3× change in the implicit vertical stiffness, the blowup is UNCHANGED (ph 982→952 at step 1, a ~3%
     early difference swamped by step 2) ⇒ the instability does NOT live in the vertical w–φ off-centering
     operator (a 3× stiffness change doesn't stop it). [NB: an earlier note said "IDENTICAL" — corrected;
     there is a small early epssm effect, just swamped by the dominant instability.]
   - `smdiv/emdiv` 0.1/0.5/0.9: ~1.4% effect ⇒ divergence damping REFUTED.
   ⇒ parameter-INSENSITIVE ⇒ a **STRUCTURAL** coupling error (fixed |λ|>1), not marginal stability.
5. **Per-operator localization** (`[SPLIT-EXPLICIT SUBSTEP]`, log window widened to catch the slow onset):
   μ from `advance_mu_t` slowly grows (1.41→…→11.7); `advance_w`'s w/ph merely respond. ⇒ the
   mass-continuity channel.
6. **Code read.** `advance_mu_t` (`wrf_sdirk3_acoustic_substep.cpp`) is structurally faithful to WRF
   `module_small_step_em.F:1094-1119` (and the divergence discretization was validated offline at 9.78e-4)
   ⇒ the bug is subtle here OR upstream in `advance_uv` (μ integrates any persistent u-divergence bias).
   Coupled operators mean "which output grows" cannot separate cause from accumulator.

## P0 verifications (review 2026-07-22)
- **P0-1 (epssm effective):** VERIFIED — env `WRF_SDIRK3_SPLIT_EXPLICIT_EPSSM` reaches `calc_coef_w`
  (cof ×2.98 at 0.1→0.9); the blowup is epssm-insensitive despite the 3× stiffness change (small ~3% early
  effect swamped). `[SPLIT-EXPLICIT COEF]` logs `epssm_effective`/`cof`/`max|coef.a|` at the point of use.
- **P0-2 (dnw/rdnw signed-metric):** VERIFIED on production operands — `dnw ∈ [-0.0356, -0.00497]` (NEGATIVE,
  WRF-signed ✓), `rdnw ∈ [28, 201]` (POSITIVE ✓), `max|dnw·rdnw+1| = 5.96e-08` (reciprocity exact ✓). The
  **dnw-sign-flip hypothesis is REFUTED** — `DMDT = Σ dnw·dvdxi` is not sign-flipped. A `TORCH_CHECK` now
  locks the contract (dnw<0, rdnw>0, dnw·rdnw==-1) on the live operands so a future regression fails closed.
  (The file-level comment block at :28-53 describes the STANDARD-path positive convention; the split path
  uses WRF-signed negative dnw via `dnw_d=-1/rdnw_d`, documented at :7015-7017.)

## P0-4 (global mass conservation) — mechanism reframed, MEASURED
`[SPLIT-EXPLICIT MASS]` logs the column-integrated mass tendency after `advance_mu_t`
(`S.mudf = DMDT + mu_tend`, `acoustic_substep.cpp:572`) on the live dt=600 split run, 35 records
stage-1 → the crash:

- **NET mass is conserved to roundoff at EVERY substep, including the catastrophic one.**
  `max |Σ_ij DMDT| = 1.77e-08`, `max rel_imbalance = 4.67e-09` across all 35 records. ⇒ the
  divergence operator is exactly conservative; there is **no net mass source**. **REFUTED: the
  affine-mass-drift hypothesis** (steady-forcing / non-conserving seam-wall flux / reference-flux
  divergence — the review's `ρ(J)≈1 + steady forcing` alternative). μ does not globally grow.
- **The blowup is a CONSERVATIVE gross-amplitude explosion.** `sum|DMDT|` (gross divergence
  activity) sits ~14–37 for ~30 substeps, then in the FINAL substep jumps `37.3 → 222.9` with
  `max|DMDT| 0.9 → 42.1` — a single-substep catastrophic amplification **while the net stays
  exactly 0**. Equal-and-opposite mass shuffling whose amplitude explodes = a growing
  **mass-conserving** mode, not a leak.

⇒ The μ drift localized earlier (`advance_mu_t` μ 1.41→11.7) is **not** a rising mean (mass is
conserved) but a growing **local** perturbation amplitude. Mechanism class is now **conservative
amplification of a divergence/acoustic eigenmode**, consistent with a structural `|λ|>1` coupling.
**Still not over-claimed (P0-6):** MEASURED = conservation + gross growth; the strict
`|λ|>1` vs non-normal-transient (`ρ≤1, ‖Jᵏ‖>1`) distinction remains open and needs the assembled
amplification-matrix analysis on a matched substep. But the entire **non-conservation / mass-source
class is measured-dead.**

## Acoustic-substep CFL sweep — the last tunable, EXCLUDED (structural confirmed)
`num_sound_steps` (`split_explicit_time_step_sound`, via `WRF_SDIRK3_SPLIT_EXPLICIT_TIME_STEP_SOUND`)
sets the acoustic substep `dts = dt/N` (stage 3) — the split-explicit **CFL knob**, and the one
parameter the earlier epssm/buoyancy/smdiv sweeps did NOT cover. Production-path (debug=0) step-reach:

| N | dts (stage-3) | steps completed | outcome |
|---|---|---|---|
| 4  | 150.0  | 39 | stage-3 catastrophic-growth gate (ph-dominated) |
| 8  | 75.0   | 39 | same |
| 16 | 37.5   | 39 | same |
| 32 | 18.75  | 38 | same |

**`num_sound_steps`-INDEPENDENT** (38↔39 is FP/threading run-variation, not a systematic trend): an
8× refinement of the acoustic CFL does not move the failure. ⇒ the blowup is **structural, not an
acoustic-substep CFL violation** — the skill's dts-sweep discriminator returns "structural". This
excludes the last tunable and corroborates the parameter-insensitive/structural thesis.

**Instrumentation caveat (measured, important):** at debug≥2 the `[SPLIT-EXPLICIT MASS]` gross
`Σ|DMDT|` looked N-dependent (223 at N=4 vs ~30 at N≥8). That is a **logging-window artifact**, NOT
a CFL effect: the probe's global substep counter caps at 400, so N=4 (7 substeps/step) logs all the
way to the step-39 failure and catches the 223 spike, while N=32 (69 substeps/step) stops logging at
step ~6 — its "~30" is only the neutral early phase. The 35-vs-47 MASS-line counts are exactly
`8 + min(#substeps,400)/10`. `advance_substep` == `advance_uv→advance_mu_t→advance_w→calc_p_rho`
(the debug≥2 per-operator path is byte-identical to production; probes are read-only detached), so
both paths fail at the same step. The **P0-4 conservation result is window-independent and stands**
(net Σ DMDT ≈ 1e-8 at every logged substep, all N).

## P0-3/§5/§6 — ph-arm numerator/denominator separation: DENOMINATOR-COLLAPSE REFUTED
The `advance_w` ph update is `ph = rhs + (0.5·dts·g·(1+eps))·w_new / mf_ts`, `mf_ts = c1f·muts+c2f`
(the ph DENOMINATOR — NOT column mass μ). `[SPLIT-EXPLICIT PHDENOM]` separates the growing new-w
arm into numerator (`w_new`) vs denominator (`mf_ts`) on the live N=4 run to the step-39 failure:

| substep (last 6) | mf_ts_min | nonpos | nearz | w_new_max | neww_arm_max |
|---|---|---|---|---|---|
| … | 89032 | 0 | 0 | 1096 | 10.0 |
| … | 89036 | 0 | 0 | 7426 | 67.5 |
| … | 89038 | 0 | 0 | 3044 | 27.7 |
| … | 88979 | 0 | 0 | 16777 | 152.6 |
| … | 88797 | 0 | 0 | 14106 | 128.3 |
| **final** | **82089** | **0** | **0** | **4,897,398** | **48284** |

- **Denominator collapse REFUTED.** `mf_ts_min` holds ~89000, only dips ~8% (→82089) at the very
  end, with `nonpos=0` and `nearz=0` at **every** substep incl. the blowup. ⇒ the review's leading
  ph-mechanism (mf_new → 0/negative) is measured-DEAD, and **a production mass-domain fail-close
  (P0-3) would NOT catch this failure** — the denominator never violates. (A near-zero/non-positive
  guard is still defensible defense-in-depth, but it is not the fix here.)
- **The blowup is a `w_new` NUMERATOR explosion**: `w_new_max` 138 → 235 → 390 → 2430 → 7426 →
  16777 → **4.9e6**; `neww_arm` tracks it (→48284) with the denominator flat. ⇒ the ph growth is
  DRIVEN by the vertical velocity `w` exploding — a coupled **w↔ph acoustic mode amplifying** (`w`
  drives `ph` via `(1+eps)·w/mf`; `ph` drives `w` via the pressure-gradient/buoyancy w-RHS),
  consistent with the P0-4 conservative growing mode. This refines the earlier `advance_mu_t`
  μ-channel localization: μ grows (~11) but `w` is the EXPLODING variable (4.9e6), and mass +
  denominator are healthy ⇒ the amplifying mode lives in the **w-solve / w↔ph coupling**, not
  mass-continuity.

## Diagnostics added here (opt-in, default-off byte-identical)
- `[SPLIT-EXPLICIT MASS]` (debug_level≥2) — `Σ DMDT` (net, conservation), `Σ|DMDT|` (gross),
  `rel_imbalance`, `max|DMDT|` after each `advance_mu_t`. Proved conservation exact + gross blowup.
- `WRF_SDIRK3_ABLATE_BUOY_W` — zero the `advance_w` buoyancy + mass-feedback term (used to refute buoyancy).
- `[SPLIT-EXPLICIT SUBSTEP]` per-operator log window widened (first 8 + every 10th to 400) so the slow
  onset (~substep 85+) is captured, not just the neutral early phase.
- (from earlier: `[SPLIT-EXPLICIT RK]` per-variable breakdown, `[SPLIT-EXPLICIT TEND]` frozen-tendency
  magnitudes, `WRF_SDIRK3_ADVANCE_W_PH_DIAG` #69 decomposition.)

## Next unit
dyn_em `[PARITY substep]` with MATCHED INPUTS on `advance_uv`/`advance_mu_t` at a mid-run substep (seq ~90,
where the slow growth is developing — substep-1 is uninformative, both ~0): feed dyn_em's dumped inputs into
the libtorch operators offline and compare outputs term-by-term to pin the diverging arm.
