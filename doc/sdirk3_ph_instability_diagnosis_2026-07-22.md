# Split-explicit dt=600 ph-instability — measured diagnosis (2026-07-22)

Continuation of the PR #69 `advance_w` geopotential decomposition. All measurements were run on the
**live `em_b_wave` dt=600 executable** with `sdirk3_split_explicit=.true.` (the agent that built #69 could
not run the live executable). Every diagnostic here is **opt-in and default-off ⇒ the baseline numerical
path is byte-identical** (verified via `tests/numerical_fingerprint.sh` MATCH after each change).

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
