# Split-explicit dt=600 ph-instability — measured diagnosis (2026-07-22)

Continuation of the PR #69 `advance_w` geopotential decomposition. All measurements were run on the
**live `em_b_wave` dt=600 executable** with `sdirk3_split_explicit=.true.` (the agent that built #69 could
not run the live executable). Every diagnostic here is **opt-in and default-off ⇒ the baseline numerical
path is byte-identical** (verified via `tests/numerical_fingerprint.sh` MATCH after each change).

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
