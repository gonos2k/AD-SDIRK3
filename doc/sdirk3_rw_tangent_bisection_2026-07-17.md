# rw tangent bisection: the discrepancy is an FD artifact of the W-damping kink; the production tangent is consistent (2026-07-17)

**Status: dated evidence record** (PR 9B, branch `pr9b-rw-tangent-localization`
off `main` = `8be48eb`). All measurements at the actual dt=600 `em_b_wave`
operands (stage 3 iter 0 positive control; stage 4 iters 0/1), via the
opt-in checker (`WRF_SDIRK3_STAGE4_JVP_CHECK=1`) plus the term/factor capture
added in this PR (`SDIRK3_RW_TERM_DIAG`), with the shadow-RHS purity gate
passing (`worst_rel_diff=0`, bit-identical) at every checkpoint.

## Verdict

**There is no production forward-mode tangent defect in the rw channel.**
The "strong candidate rw tangent defect" of PR 9A / PR 9B commit 1 is an
**intrinsic FD artifact of the W-damping term's kink structure** — central
finite differences of `|w|` (and of the δ-smoothed sign) at an operand whose
w-field contains a continuum of near-zero values measure a jump secant, not
the local derivative, at EVERY floating-point-feasible epsilon. The
production fwAD tangent is the correct (almost-everywhere) local derivative.
Class B of the post-FGMRES classification — a finite-but-wrong JVP — is
**excluded at the term level**.

Separately, the bisection surfaced a REAL, previously unknown **primal
parity deviation** in the same term (§5): the W-damping is applied without
WRF's `vert_cfl > threshold` gate, so a spurious "anti-damping" forcing of
magnitude `≈ α·crit·mass` acts wherever CFL < crit — at the stage-3 operand
this constitutes essentially the entire fast rw tendency. No fix is applied
in this PR (numerics changes are out of scope by directive); this is the
next review decision point.

## 1. Bisection ladder (state → term → factor → gate-arg)

1. **rw term inventory** (ImplicitOnly mode-3 closure): the only rw-writing
   terms are W-PGF+buoyancy, the top-boundary contribution, the bottom mask,
   and W-damping (advection/Coriolis/curvature/diffusion are ExplicitOnly).
   The top-ranked *a-priori* candidate (the in-place `copy_` recursion in
   `compute_pressure_hydrostatic`) was **refuted by an offline probe**: its
   fwAD tangent equals the analytic tangent of the (exactly linear) map to
   float precision (cos 1.00000).
2. **Term bisection** (`RwTermCapture`, double closure enforced): the primal
   closures hold exactly (`Σ terms == assembled rw`, rel 0 at both seams;
   sub-terms vs composed PGF ≈ 1e-4..6e-3 float re-association) and the
   tangent chain closure holds exactly. Per-term tangent-vs-FD:
   `pg` / `buoy_mu1` agree to 5e-8..5e-4 (cos 1.0); `buoy_mu2` identically
   zero; **the first and only divergent term is `w_damp_padded`**
   (tangent 3.1052e12 (s3) / 3.9901e13 (s4) vs FD 1.8965e14 — the FD value
   identical across the two operands; cos −0.843 / +0.297). The
   `rw_tend_final` rows equal the `w_damp_padded` rows — the entire rw
   discrepancy lives in this term.
3. **Factor bisection** (damp inputs and every chain factor):
   - inputs carry correct duals: `w_input` tangent-vs-FD rel 1e-8,
     `mu_input` 5e-8..5e-4;
   - `wd_mass_factor` (`c1f·mu + c2f`): rel 4.7e-5..7.9e-8 — correct;
   - `wd_w_sign` (δ-smoothed sign, δ=1e-3): FD **converges to the
     production tangent** at ε_abs=1.8e-7 (rel 1.77e-3, cos 1.000) —
     correct;
   - `wd_vert_cfl` / `wd_cfl_excess` (`|w|·dt/dz − crit`): **FD ∝ 1/ε with
     a constant jump norm J = 2·ε·‖FD‖ ≈ 57 across six decades of ε**
     (1.7e2 at ε_abs=0.18 → 1.6e8 at ε_abs=1.8e-7, never converging) — the
     signature of differencing across the `|w|` kink at points where
     `|w| ≲ ε·|ẇ|`, which exist at every feasible ε because the operand's
     w-field has near-zero values throughout. The production tangent
     (7.08e9) is within the smooth-slope bound `max(dt/dz)·‖ẇ‖ ≈ 1.2e10`.
4. **Offline mirror**: a standalone reproduction of the exact op chain
   (`abs → ·dt/dz → −crit → smooth-sign → mass product → pad`) with the
   production dual mechanics gives **fwAD = analytic tangent to 1.4e-7**,
   and FD converges to both once the per-point perturbation drops below the
   local scales — the ops propagate duals correctly; only the operand's
   kink-neighborhood population makes the in-tree FD non-local.

## 2. Why every earlier "confirmation" was consistent with the artifact

- **ε-ladder persistence (flat rel≈1 over 3+ decades)**: the composed
  `w_damp` FD is a sum of kink-jump secants whose ε-dependences compensate
  (the `|w|`-path secant ∝ ε⁰ where saturated); a flat ladder is NOT
  convergence unless the ladder's lower end reaches below the smallest
  smoothing scale — here δ=1e-3 and the near-zero |w| population put that
  region below float32's feasible window for the composed term.
- **One-sided plus≈minus**: a SYMMETRIC kink straddled symmetrically gives
  matching one-sided secants; smoothness of the underlying point is not
  implied.
- **Operand-independence of the FD response** (1.8965e14 at both stages,
  2.129e9 in the packed/scaled J-layer view): the jump structure is set by
  the direction and the constants (α, dt/dz, mass scale), not the operand.
- **In-situ = replay** (drift ≤ 7e-11): both are the same correct tangent.
- **Purity** (bit-identical): the FD inputs were exact; the artifact is
  mathematical non-locality, not noise.

The factor-level **FD ∝ 1/ε law** is the discriminator none of the four
earlier criteria contained: it is the fingerprint of a jump, impossible for
a converging derivative estimate.

## 3. What this resolves

- PR 9A's classification branch returns to **"JVP and A(v) consistent"**:
  the production linearization at the dt=600 operands is correct at the
  term level everywhere it is measurable, and the sole non-measurable
  region is explained and bounded.
- The rw rows of the PR 9A / PR 9B-commit-1 record sets should be read as
  FD-artifact rows, not defect evidence. Global-layer conclusions
  (J-layer consistent at 4.4e-6..9.0e-4; A-layer consistent on resolvable
  directions) stand unchanged.

## 4. Standing guard

Commit 3 adds a deterministic standing contract for the W-damping chain
(fwAD vs analytic tangent on a fixture; a deliberately dual-severed variant
must FAIL the same assertion), so a future in-place/detach regression in
this chain is caught by CTest rather than by a five-level bisection.

## 5. The real finding: ungated W-damping (primal parity deviation — no fix here)

WRF applies w-damping ONLY above the critical Courant number
(`dyn_em/module_big_step_utilities_em.f90`:
`IF ((vert_cfl .gt. w_damp_on) .and. (config_flags%w_damping == 1)) THEN`),
with `vert_cfl = |ww/(c1f·mut+c2f) · rdnw · dt|` (mass-decoupled ww).

The SDIRK3 implementation (`wrf_sdirk3_tile_unified_impl.cpp`, Step 6)
computes `w_damp = w_sign · α · (vert_cfl − crit) · mass` with **no gate and
no clamp**, and `vert_cfl = |w| · dt · rdnw` (no mass decoupling):

- wherever `vert_cfl < crit` — at the stage-3 operand that is essentially
  everywhere (max CFL ≈ 0.01 ≪ crit = 1) — the term contributes a spurious
  **anti-damping forcing** `≈ −α·crit·mass·sign(w)` per point;
- measured: at stage 3 the w_damp primal norm is 4.84e3 while all other rw
  terms sum to 0.21 — the fast rw tendency is ~100% this spurious term;
- the CFL formula itself also deviates from WRF (coupled vs decoupled w).

Whether this term contributes to the dt=600 stage-4 operand blowup is NOT
claimed here (the failure is ru-dominated, share 0.99999); but it is a
measured, reference-checked parity deviation in the production RHS.
Changing it is a numerics-policy change and is deliberately NOT done in
PR 9B (forbidden by directive); it is flagged for the next review decision.

## 6. Reproduction

```bash
# from a built em_b_wave tree (2 steps at dt=600, implicit mode 3)
cd test/em_b_wave
env OMP_NUM_THREADS=2 WRF_SDIRK3_STAGE_DIAG=1 WRF_SDIRK3_STAGE4_JVP_CHECK=1 ./wrf.exe
grep SDIRK3_RW_TERM_DIAG rsl.error.0000     # term/factor bisection records
grep SDIRK3_STAGE4_JVP_DIAG rsl.error.0000  # directional checker records
```
