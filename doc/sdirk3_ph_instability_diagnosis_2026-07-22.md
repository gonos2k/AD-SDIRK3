# Split-explicit dt=600 ph-instability — measured diagnosis (2026-07-22)

Continuation of the PR #69 `advance_w` geopotential decomposition. All measurements were run on the
**live `em_b_wave` dt=600 executable** with `sdirk3_split_explicit=.true.` (the agent that built #69 could
not run the live executable). Every diagnostic here is **opt-in and default-off ⇒ the baseline numerical
path is byte-identical** (verified via `tests/numerical_fingerprint.sh` MATCH after each change).

## Result summary
The split-explicit geopotential (`ph`) blowup is a **genuine, structural semi-implicit-scheme instability**
in the **horizontal / mass-continuity channel** — not the tendency value, not instrumentation, not any
tunable parameter. It is localized to `advance_mu_t`'s column-mass (μ) growth (or upstream `advance_uv`
feeding a divergent u that μ accumulates), pending dyn_em `[PARITY substep]` matched-input arm-parity to
pin the exact term.

## The measurement chain
1. **Closure is roundoff-exact.** The #69 decomposition `Δφ = frozen + old_w + vertical_adv + new_w` closes
   to `|C_φ|_rms/|Δφ|_rms ~ 1e-8` across all 274 records ⇒ the decomposition/instrumentation is correct.
2. **Physical-unit reveal.** The coupled `ph_tend`=1732 is physical `ph_tend`≈0.019 once divided by the
   column mass (~89000); the large coupled value is purely the mass-coupling (matches dyn_em per #67).
3. **Trajectory: neutral → slow growth → collapse.** stage-1/sub-1 `actual_delta` oscillates ~0.5–1.77
   (|λ|~1.0) for ~85 substeps, THEN the `new_w` arm grows monotonically (0.48→…→7.8e5) while `actual`
   stays ~1 (**cancelled** by frozen/vertical), until the cancellation breaks and `mass_new` goes NEGATIVE
   → catastrophic blowup. "big arm + small actual = cancellation" — never clip on individual-arm max.
4. **Parameter sweeps eliminate the tunable hypotheses (env, no rebuild):**
   - buoyancy ablation (`WRF_SDIRK3_ABLATE_BUOY_W=1`): trajectory UNCHANGED ⇒ buoyancy REFUTED.
   - `epssm` 0.1/0.5/0.9: IDENTICAL ⇒ the vertical w–φ off-centering (the offline-proven |λ|=0.998 part)
     REFUTED.
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
