# R12 — authoritative stage-RHS linearization and probe certification

Consolidates R12's new findings AND the items carried over from R10/R11.

## 0. CHECKLIST-ACCURACY CORRECTIONS (the review's §13)
R11 marked these `[x]` against probes narrower than the item's own wording. Revert first.

- [x] X1  S4 "active-domain projection P J_E P" -> edge-energy fraction only; NOT a projection
- [x] X2  T3 "RAII restore dt, refs, caches, counters" -> only dt_stage_ and U_ref_stage_
- [x] X3  A2 density -> inverted al_PERT, not al_full
- [x] X4  A3 hydrostatic / BC residual -> not emitted at all
- [x] X5  V2 "runtime validator" -> manifest print + MANUAL diff, not fail-closed
- [x] X6  S2 "3 deterministic seeds" -> no seed authority, no v0 digest

## P0 — the probe does not measure the production operator

- [x] H1  P0-3 hotfix: compute_stage_tendency's EMPTY else left a stale U_ref_stage_ under
          slow_in_tangent=false -- which the runtime echo shows is the ACTIVE configuration
          (imex_slow_in_tangent = 0), so the stale-reference path was LIVE, not latent.
- [x] H2  P0-2 hotfix: density used std::get<1> (al_pert); the contract is
          {p_pert, al_pert, al_full}. Also silently dropped non-positive-perturbation cells.
- [x] H3  P1-2 hotfix: torch::eye(m, kFloat64) is CPU while Vm may be CUDA/MPS
- [x] P1a P0-1: probes called computeUnifiedRHS directly, leaving U_ref_stage_ at the base
          state -> they linearized F_E(U, U_ref=U_0), not the production F_E(U, U_ref=U).
          Routed every probe through one evaluator that mirrors the production wrapper, with
          the reference carrying the TANGENT (or the JVP drops dF/dU_ref silently).
- [x] P1b RE-MEASURE every spectrum / directional result on the authoritative mapping and
          restate or retract: J_E, J_I, J_full RHP values, the 2.45x non-additivity, the
          directional remainders, the eigenvector block shares
- [~] P1c IMPLEMENTED and it REFUSES: comparable=0 on all four directions because
          compute_k_slow falls back to FD while probe_rhs does not, so the comparison is a true
          dual against an FD quotient and rel_diff is not structural disagreement. The contract
          working is not the contract passing -- P1c stays OPEN, and why the production wrapper
          is not forward-mode differentiable is a new question (slow_in_tangent is NOT it: both
          read g_sdirk3_config.imex_slow_in_tangent at :5762).

## P1 — probe certification

- [x] C1  P0-5: probe_topology_ok checks rank count only; the sibling stage-operand gate also
          requires tile_covers_patch. Extract the whole-patch authority and share it.
- [x] C2  probe_skip_note's `static bool warned` is shared across probes, so only the FIRST
          skip prints; also a non-atomic write under OpenMP
- [x] C3  P1-5: Ritz VALUES come from linalg_eigvals and Ritz VECTORS from a separate
          linalg_eig -- pairing across two calls is not guaranteed. One call, sort once.
- [x] C4  P1-4: env parsing is fail-silent again -- "Full"/"FULL"/"ful"/garbage all become J_E,
          and atoi("48junk")=48, atoi("abc")=0->default. Strict parse, abort on unrecognised.
- [ ] C5  P1-3: deterministic v0 (index-based, not global RNG), emit v0_digest / seed id, and
          do the m study as NESTED PREFIXES of ONE Arnoldi run
- [ ] C6  P1-1: ScopedProbeState restores 2 members; computeUnifiedRHS also mutates
          grid_info_->dt, mask counters, ru_adv_z_work_, t_adv_z_work_. Prefer a mutation
          FINGERPRINT that fail-closes over an ever-growing snapshot list.
- [ ] C7  P1-6: manifest -> machine-checkable. Emit at max_digits10, add the missing fields
          (true-residual period, stagnation window/ratio, no-early-stop override, precond
          fallback latch, JVP method + fd fallback, imex_slow_in_tangent, warm-start validity),
          and a comparator that exits non-zero on an unexpected diff or a missing field.

- [x] W1  WHY the production wrapper is not forward-mode differentiable: the AD helper's
          catch-all swallowed the reason. Surfaced, it is "tangent undefined -- the dual was
          severed", and compute_k_slow detaches k_slow when slow_in_tangent=false, which IS
          the runtime value. So the slow channel carries NO tangent in production: J_E's
          spectrum is a PRIMAL property and the adjoint has no slow-channel derivative.

## Carried from R11, still open

- [ ] R1  D(Phi_h): the full one-step tangent through the implicit solves. Contract:
          central difference of Phi_h, and <D v, w> == <v, D^T w>.
- [ ] R2  np equivalence: needs a globally-formulated quantity, not a rerun of tile-local probes
- [ ] R3  term observer for rv, rw, ph, t, mu (only ru has one)
- [ ] R4  A1: stage-2 accuracy vs a converged REFERENCE Y_2, not a budget sweep
- [x] R5  rho_max anomaly -- recheck once density is computed from al_full
