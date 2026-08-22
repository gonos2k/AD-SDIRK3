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
- [x] C5  deterministic index-based v0 with v0_digest, m study as NESTED PREFIXES of one
          basis. Result: the DOMINANT pair converges (|lam0| +0.36%, Re +1.10% over
          m=24->48) but max_re does NOT (+20%, still climbing) and n_rhp as a COUNT scales
          with m. max_re and n_rhp were reported as operator properties and are not;
          the 2.47x non-additivity is downgraded to a fixed-m observation.
- [x] C6  P1-1: ScopedProbeState restores 2 members; computeUnifiedRHS also mutates
          grid_info_->dt, mask counters, ru_adv_z_work_, t_adv_z_work_. Prefer a mutation
          FINGERPRINT that fail-closes over an ever-growing snapshot list.
- [x] C7  P1-6: manifest -> machine-checkable. Emit at max_digits10, add the missing fields
          (true-residual period, stagnation window/ratio, no-early-stop override, precond
          fallback latch, JVP method + fd fallback, imex_slow_in_tangent, warm-start validity),
          and a comparator that exits non-zero on an unexpected diff or a missing field.
          DONE. Manifest now prints at max_digits10 (newton_tol reads 0.20000000298023224,
          the exact float32 0.2 -- at 6 digits a 7th-digit difference between two arms was
          invisible to any diff) and carries 13 further fields. A new fwAD->FD fallback
          COUNTER makes a degraded JVP operator visible without debug_level>=1; it measures
          0 on em_b_wave, so that run's JVP really is forward-mode.
          sdirk3_manifest_diff.py is the gate: 6 synthetic cases + a real two-arm sweep
          (STAGE2_GMRES_RESTART) pass, and it fails closed on an undeclared diff, a field
          present in only one arm, a missing stage, an absent manifest, AND on a DECLARED
          field that never moved (an arm that silently reran the baseline is equally void).
          Real sweep moved exactly one field: restart 7 -> 51.

- [x] W1  WHY the production wrapper is not forward-mode differentiable: the AD helper's
          catch-all swallowed the reason. Surfaced, it is "tangent undefined -- the dual was
          severed", and compute_k_slow detaches k_slow when slow_in_tangent=false, which IS
          the runtime value. So the slow channel carries NO tangent in production: J_E's
          spectrum is a PRIMAL property and the adjoint has no slow-channel derivative.

## Carried from R11, still open

- [x] R1  D(Phi_h): the full one-step tangent through the implicit solves. Contract:
          central difference of Phi_h, and <D v, w> == <v, D^T w>.
          ANSWERED, and the answer is that the contract is VACUOUS on this configuration.

          Three probes, each with its own refuting measurement:
          1. WRF_SDIRK3_STEP_MAP_PURITY -- is Phi_h a FUNCTION of U_n? A Jacobian presupposes
             it, and this solver carries hopeless streaks, a warm-start cache, a trust radius
             and a preconditioner latch ACROSS calls. Two calls on byte-identical input agree
             BITWISE on all six blocks: pure=1. Well-posed. (Note call B ran with a warm-start
             cache that call A populated and still matched -- the same idempotence that made
             R4's ref_agree degenerate.)
          2. WRF_SDIRK3_STEP_MAP_TANGENT -- central difference at TWO step sizes,
             eps=1e-3 and 5e-4: worst_agree=6.4e-05. The quotient has converged.
          3. But u_Dv = ph_Dv = t_Dv = mu_Dv = EXACTLY 1, which for a unit direction is what
             an IDENTITY map gives, and v_Dv = w_Dv = 0 because epsilon was scaled by a block
             norm that is zero for those blocks -- "never measured" printed as "perfect
             agreement". Both fixed: an absolute epsilon floor, the input norm on the record,
             and a direct measurement of whether the step moves anything.

          SDIRK3_STEP_MAP_ADVANCE: identity=1, tend_moved=0 -- all TWELVE arrays (six state,
          six tendency) bit-identical. The step is a complete NO-OP. It is not that the core
          writes tendencies instead of state (the WRF convention, and the obvious innocent
          explanation): the tendencies did not move either.

          MECHANISM, named by the code itself, not inferred:
            [STEP OUTCOME] fail-closed outcome=20; skipping unifiedStep state publish
            SDIRK3 FAIL-CLOSED outcome=20 final_update_aborted=1
          The fail-closed gate suppresses the state publish after ABORT_ON_NEWTON_FAIL. The
          no-op is BY DESIGN and correct; it means D(Phi_h) measures the GATE, not the dynamics.

          CONSEQUENCE FOR THE 4D-VAR GOAL: D(Phi_h) = I here, so <D v, w> == <v, D^T w> holds
          TRIVIALLY and certifies nothing about the adjoint -- the same failure mode as the
          severed VJP that passed symmetry, linearity, repeatability and additivity while being
          the identity. A step-level TLM/adjoint contract cannot become meaningful until the
          forward advances. This sharpens "completes ZERO steps": unifiedStep returns its input
          unchanged, bit for bit.
- [ ] R2  np equivalence: needs a globally-formulated quantity, not a rerun of tile-local probes
- [x] R3  term observer for rv, rw, ph, t, mu (only ru has one)
          DONE, with one correction to the premise: mu ALREADY had one
          (SDIRK3_MU_DIV_TRACE, div_x/div_y/div_z), so the gap was rv, rw, t, ph.
          All five now emit SDIRK3_ADV_SPLIT from ONE implementation
          (wrf_sdirk3_adv_split_observer.h) behind ONE gate
          (WRF_SDIRK3_ADV_SPLIT_BLOCKS), each record naming its stage and RhsMode --
          the first record is a stage-1 evaluation where w is identically zero, and an
          untagged row of zeros reads like a measurement.

          STRUCTURAL RESULT (measured, from the record counts): advection reaches the
          IMPLICIT channel only through ph. ru/rv/rw/t are evaluated on the explicit
          pass only, which in an aborting run runs once, at stage 1, where w=0.

          NUMERICAL RESULT (armed stage-2 implicit residual evaluation, single-variable
          A/B on WRF_SDIRK3_WRF_OMEGA_WW_CP):
            omega = mu*w (default):   adv_z=4.926e+12  horiz=3.892e+05  z/h=1.27e7  cos_z=1.000
            omega = calc_ww_cp:       adv_z=5.919e+05  horiz=3.900e+05  z/h=1.518   cos_z=0.837
          The vertical term falls SEVEN orders while the horizontal moves 0.2%, which is
          what makes it single-variable: only the term that multiplies omega responded.
          cos_z=1.000 in the default arm means the vertical term IS the total -- the
          horizontal contributes nothing to the sum.
          This corroborates the "Omega is NOT rom" root cause from an INDEPENDENT channel:
          that cause was found in mu; this is ph, a different equation with a different
          discretization, reproducing the same signature.
- [~] R4  A1: stage-2 accuracy vs a converged REFERENCE Y_2, not a budget sweep
          PROBE BUILT (WRF_SDIRK3_STAGE_REFERENCE=1, _STAGE=n to target one stage): after the
          production solve, re-solve the SAME system at an enlarged budget and report the
          relative difference in the stage increment K, overall and per block.

          FIRST MEASUREMENT, stage 2, dt=600:
            shipped_converged=0  shipped_final_res=0.4855
            ref_converged=1      ref_final_res=0.1073   (budget 120x20)
            rel_err=0.8173  K_shipped=3849  K_ref=5278
            per block: ru 0.054 | t 0.104 | rv 0.444 | mu 1.132 | ph 1.141 | rw 1.458

          The per-block RANKING is the durable part and it separates two things the campaign
          had conflated: ru is the block that dominates the stage-3 ENTRY NORM and it is the
          MOST ACCURATE block here (5.4%), while rw/ph/mu are the ones the shipped solve
          actually gets wrong (113-146%). Norm dominance is not inaccuracy.

          CERTIFICATION REVERSED THE FIRST READING. A second reference at a LARGER budget
          (200x30) returned ref_agree=0 (bit-identical K) together with a WORSE residual
          (0.9989) and converged=0 -- the signature of the second solve warm-starting from
          the first and returning it unchanged, not of a converged reference. The manifest had
          already measured warmstart_enabled=1. Self-certification that shares mutable state
          with the thing it certifies is not independent. The probe now disables the warm start
          across both reference solves. THAT RUN IS NOW IN:

            ref  (120x20) converged=1  final_res=0.1196
            ref2 (200x30) converged=0  final_res=0.4612
            ref_agree=0.2842   rel_err=0.7371   K 3849 vs 4683
            per block: ru 0.076 | t 0.121 | rv 0.512 | mu 1.155 | ph 1.165 | rw 1.416

          SETTLED, and NOT the way the first reading suggested. ref_agree=0.284 against
          rel_err=0.737 is 2.6x separation; a reference must be far closer to the truth than
          the quantity it measures, so at 2.6x these are two UNCONVERGED solves being
          differenced. There is NO converged reference at stage 2 at budgets up to 200x30, and
          rel_err is therefore NOT an accuracy. Note also that the LARGER budget did WORSE
          (0.4612, converged=0) than the smaller (0.1196, converged=1) -- GMRES(m) is not
          monotone in m so that alone does not prove non-convergence, but it does mean the
          120x20 answer is not certified as the solution.

          WHAT SURVIVES: the per-block RANKING, because it is invariant across two references
          that disagree by 28% --
            shared-warmstart run:  ru .054 < t .104 < rv .444 < mu 1.132 < ph 1.141 < rw 1.458
            independent run:       ru .076 < t .121 < rv .512 < mu 1.155 < ph 1.165 < rw 1.416
          Same order both times. ru and t are the accurate blocks; rw, ph and mu are the ones
          the shipped solve gets wrong. ru dominates the stage-3 ENTRY NORM and is
          simultaneously the most accurate block -- norm dominance is not inaccuracy, and the
          campaign had been reading them as the same thing.

- [x] R3b NEGATIVE REACHABILITY (measured): the corrected-Omega re-measurement of Wall-2's ru
          adv_z is NOT reachable from this observer. With BOTH parity knobs on
          (WRF_OMEGA_WW_CP + MU_HORIZONTAL_DIV_ONLY) the run still performs exactly ONE
          explicit RHS evaluation, at stage 1, where w == 0 and every vertical term with it.
          The run aborts inside stage 2's implicit solve before any later slow evaluation, so
          there is no explicit-pass state with nonzero w to measure. Re-measuring Wall-2 under
          the correct Omega needs a run that completes a step, or instrumentation of k_slow
          at stage >= 2 -- not this observer.
- [x] R5  rho_max anomaly -- recheck once density is computed from al_full
