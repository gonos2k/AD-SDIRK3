# dt=600 operand after W-damping parity: the spurious term's Jacobian was load-bearing (2026-07-17)

**Status: dated evidence record** (PR 9C commit 4, branch
`pr9c-wdamping-primal-parity` off `main` = `e86fa06f`). Same configuration as
every prior dt=600 record (em_b_wave, 2 steps, single rank, OMP=2, IMEX
mode 3, stage-2 budget 8/1, `WRF_SDIRK3_STAGE_DIAG=1
WRF_SDIRK3_STAGE4_JVP_CHECK=1`).

## 1. What changed and what the run confirms about the wiring

`[CONFIG EFFECTIVE] wdamp_parity: wrf_w_damping=0 wrf_zadvect_implicit=0
w_damp_alpha=0.3 wrf_w_crit_cfl=1 legacy_w_crit_cfl=1 implicit_wdamp=1` —
the WRF activation flag arrives from the namelist path (Registry default 0;
absent from the em_b_wave namelist), so under parity the W-damping term is
**not computed at all**, exactly WRF's behavior for this case. The
split-explicit production path is untouched (six stage norms bit-identical
to the tracked golden with both diagnostic flags ON).

## 2. Before / after at dt=600 (same format as the post-FGMRES classification)

| | pre-fix (main = e86fa06f) | post-fix (this branch) |
|---|---|---|
| stage 2 | r0 2.345264e-02, converged 1 iter | **identical to all digits** |
| stage 3 initial | r0 5.004250e-01 / L2 1.760896e+03; FGMRES `rhs_norm=5.201751e+02` | **identical to all digits** |
| stage 3 FGMRES iter 0 | rel_err 0.5302 (7/7 Arnoldi, `max_budget`) — usable inexact-Newton direction | rel_err **1.000125** (7/7 Arnoldi, `max_budget`) — **zero linear reduction** |
| stage 3 Newton | 0.50 → 0.27 → 0.19, converged (3 iters) | iter-0 direction rejected (`gmres_total_failure=1`, dk_norm=0); damped fallback 1.694 → 1.684 (damp 0.5); stall at iter 1 |
| stage outcome | stage 3 converged; **failure at stage 4** (mid_budget_hopeless / arnoldi_stagnation) | **failure at stage 3** (gate `rel_R_full=28.65`, outcome=20); stage 4 unreached |

## 3. Seam isolation (the review's first-divergence requirement)

The first divergence is **exactly at the W-damping seam, and only on the
OPERATOR side**:

- The **residual construction is measurably unchanged**: stage-2 records and
  the stage-3 initial residual (`r0`, `res_l2`, and the FGMRES `rhs_norm`)
  are identical to every printed digit. This is explained by seed
  self-consistency: the retired term entered both the stage seed K and
  F(U_stage + dt·γ·K) nearly identically at the near-zero-w operand
  (its primal value ≈ −α·crit·mass·sign(w), state-independent to leading
  order), so it canceled in R = K − F. The spurious term did **not**
  corrupt the residual; PR 9B measured exactly this (rw fast-tendency
  dominance 4.84e3) while R0 stayed the same.
- The **linear operator changed**: A = I − dt·γ·J lost the retired term's
  Jacobian — the enormous w-row contribution measured in PR 9B
  (`α·mass·(dt/dz_eta)`-scale; the 1.8965e14 operand-independent FD
  response). With the same b (identical `rhs_norm`), the same budget, and
  the same preconditioner, FGMRES goes from a usable 47% reduction to
  **none at all** (rel_err 1.000125 ≥ 1).

## 4. Interpretation (bounded; not overclaimed from one run)

- **MEASURED**: removing the spurious, WRF-inconsistent W-damping makes the
  dt=600 implicit solve fail EARLIER (stage 3, zero linear reduction) with
  an identical right-hand side. The retired term's Jacobian was therefore
  load-bearing for the Krylov solve at this operating point.
- This independently re-confirms the 2026-06 preconditioner finding
  (precond-inc2, `doc/sdirk3_hevi_preconditioner_findings_2026-06-21.md`):
  "the over-damped W diagonal was load-bearing" — that over-damping is now
  identified as (in part) the JACOBIAN of this spurious primal term.
- **Candidate mechanism, flagged not claimed**: the preconditioner's
  `eff_wdamp` mirror (keyed on `implicit_wdamp`, deliberately untouched —
  preconditioner changes are out of PR 9C scope) still MODELS a W-damping
  diagonal that the operator no longer contains — an operator/preconditioner
  inconsistency. Whether stage 3 would retain usable inexact-Newton
  directions with a consistent preconditioner is NOT determined here.
- The fix itself is not thereby wrong: it is WRF parity, confirmed by the
  independent scalar reference contract (26 cases, with the retired legacy
  formula failing it as a negative control). What this run shows is that
  the pre-fix solve's apparent stage-3 health rested on an unphysical
  operator feature.

## 5. Decisions this opens (for review; all out of PR 9C scope)

1. **Preconditioner consistency**: `eff_wdamp` should presumably follow the
   same parity gate as the RHS term (or be re-derived), but preconditioner
   changes are forbidden in this PR line and need their own directive.
2. **Legitimate stabilization**: if the implicit path at dt=600 needs the
   stabilization the spurious term accidentally provided, that is a design
   decision (e.g., a physically-motivated implicit damping with a wired,
   WRF-consistent activation), not a parity question.
3. The planned PR 9D operand decomposition should run on THIS operator
   (post-parity), per the standing roadmap.

## 6. Reproduction

```bash
cd test/em_b_wave   # built at this branch head; 2 steps at dt=600
env OMP_NUM_THREADS=2 WRF_SDIRK3_STAGE_DIAG=1 WRF_SDIRK3_STAGE4_JVP_CHECK=1 ./wrf.exe
grep -E "SDIRK3_(NEWTON|FGMRES|STAGE)_DIAG|CONFIG EFFECTIVE. wdamp" rsl.error.0000
```

The run fail-closes at stage 3 with `outcome=20` (expected — the object
under diagnosis). With `w_damping=1` in the WRF namelist the parity term
activates with WRF semantics (gate w_beta=1.0 or w_crit_cfl under ieva>0,
mass-decoupled CFL, hard oracle sign), contract-tested in
`test_wdamp_wrf_reference_contract.cpp` and `test_wdamp_tangent_contract.cpp`.
