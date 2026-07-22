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

## Diagnostics added here (opt-in, default-off byte-identical)
- `WRF_SDIRK3_ABLATE_BUOY_W` — zero the `advance_w` buoyancy + mass-feedback term (used to refute buoyancy).
- `[SPLIT-EXPLICIT SUBSTEP]` per-operator log window widened (first 8 + every 10th to 400) so the slow
  onset (~substep 85+) is captured, not just the neutral early phase.
- (from earlier: `[SPLIT-EXPLICIT RK]` per-variable breakdown, `[SPLIT-EXPLICIT TEND]` frozen-tendency
  magnitudes, `WRF_SDIRK3_ADVANCE_W_PH_DIAG` #69 decomposition.)

## Next unit
dyn_em `[PARITY substep]` with MATCHED INPUTS on `advance_uv`/`advance_mu_t` at a mid-run substep (seq ~90,
where the slow growth is developing — substep-1 is uninformative, both ~0): feed dyn_em's dumped inputs into
the libtorch operators offline and compare outputs term-by-term to pin the diverging arm.
