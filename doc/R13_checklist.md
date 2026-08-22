# R13 — successful-step tangent authority and isolated stage-reference certification

Source: the external review of PR #165 (merged as `ce5b82e`). Every item below was
re-checked against the code at that merge before being written down, so each carries a
VERDICT that is independent of the review's own claim. Three of the review's items turn
out to be **already correct but unproven** — those become contract tests, not code changes;
two are **larger than the review stated**; the rest are confirmed.

Legend: `[ ]` open · `[x]` closed · `[~]` partially closed, the remainder named ·
`[B]` blocked upstream, blocker named.

---

## Standing verdict this checklist must not contradict

The forward does not complete a step on `em_b_wave` at dt=600 (nor at 300/120/60/20 —
`sdirk3-G-undefined-at-every-dt`). `Phi_h(U) = U` is therefore the **fail-closed rollback
sentinel**, not a numerical map, and WRF's driver agrees: `module_implicit_sdirk3.F:1363`
fatals on `HARD_STAGE_ABORT`, `SOFT_NO_PROGRESS` and every `FATAL_*`. The step never
reaches the outer integrator.

So R13 does **not** promise a converged forward. It promises that **no probe in this tree
can report a tangent, a spectrum, a reference or an equivalence that its own preconditions
do not support** — the honest failure that the campaign has repeatedly had to retract *after*
publishing. Items that genuinely need a converged step are marked `[B]` with the blocker,
not quietly re-scoped.

---

## Phase 0 — typed results and validity fields (no model run needed)

- [ ] **A1 — a failed step must not report a tangent.** (review P0-1)
      VERDICT: **CONFIRMED, and worse than stated.** `wrf_sdirk3_tile_unified_impl.cpp:4017`
      prints `worst_agree=6.4e-05  (the central difference converged and D(Phi_h).v IS a
      directional derivative)` without ever reading `getLastStepOutcomeCode()`. The
      *sibling* record (`SDIRK3_STEP_MAP_ADVANCE`, :4088) does say "the step is a NO-OP",
      but they are separate records and the TANGENT one is the one that carries a verdict.
      FIX: a `StepStatus`/`StepResult` return contract; the tangent branch collects the
      status of all four arms and emits `step_map_valid=0 reason=noncomplete_arm
      status_base=.. status_plus=..` instead of an agreement verdict. `identity=1` may
      still print as an observation; it may not print as a finding.
      GATE: `Failed_Step_Map_Is_Invalid_Contract`.

- [ ] **A2 — a JVP that fell back to FD is not a forward-mode tangent.** (review P0-5)
      VERDICT: **CONFIRMED.** `compute_jvp_fwad_or_fd` reports `used_fd_fallback` and a
      `fallback_reason` (R12 W1 added the reason), and a global counter exists, but nothing
      *invalidates a verdict* on it — a spectrum computed on an FD operator prints the same
      as one computed on a dual.
      FIX: a `TangentSemantics` enum (`ExactPrimal` / `OperationalDetachedSlow` /
      `DiagnosticFrozenReference`) and a `valid` flag on every emitted tangent record,
      false on FD fallback, on a non-complete arm, and on a reference-digest mismatch.
      GATE: `JVP_Fallback_Invalidates_Verdict_Contract`.

- [ ] **A3 — `.item()` outside `NoGradGuard` in the JVP helper.** (not in the review)
      VERDICT: **NEW — found during this inspection, and it is a hard-constraint violation.**
      `wrf_sdirk3_jvp_fwad_or_fd.h` computes the FD epsilon as
      `u.norm().to(torch::kCPU).item<double>()` at **three** sites: the
      `DNWP_DISABLE_FWAD` branch and both `catch` blocks. The `torch::NoGradGuard` declared
      inside `try` has already gone out of scope by the time a `catch` body runs, so the two
      fallback paths sync the graph unguarded — on the exact path that runs when forward-mode
      AD fails, i.e. when the graph is already suspect.
      FIX: one `NoGradGuard`-scoped epsilon helper, called from all three.
      GATE: covered by the existing AD contracts + a grep gate in CI.

- [ ] **A4 — the probe recursion latch is not exception-safe.** (review P0-2, first half)
      VERDICT: **CONFIRMED.** `inside_step_map_probe` (`:3909`) is a raw `thread_local bool`
      set true, cleared manually after the arms. If `unifiedStep` throws inside an arm it
      stays true for the life of the thread and every later probe silently no-ops.
      FIX: RAII latch.

- [ ] **A5 — the stage-reference probe leaks global config on throw.** (review P0-3, aside)
      VERDICT: **CONFIRMED.** `wrf_sdirk3_newton_solver.cpp:10443` mutates nine fields of
      `g_sdirk3_config` (budgets, tolerances, warm start) and restores them ~50 lines later
      by hand. Any exception from `solve_stage_impl` leaves the *production* solver running
      at budget 200×30, `newton_tol=1e-8`, warm start off — for the rest of the run.
      FIX: RAII restore.

---

## Phase 1 — probe isolation (observer, not intervention)

- [ ] **B1 — the one-step arms share the live solver.** (review P0-2)
      VERDICT: **CONFIRMED.** All four tangent arms call `unifiedStep` on `this`, so each
      arm starts from the solver state the previous arm left: warm-start cache, hopeless
      streak, trust radius, preconditioner fallback latch, `U_ref_stage_`. The record admits
      it in prose ("the production step below is perturbed") but the *arms themselves* are
      not isolated from each other, which is the part that matters for a difference quotient.
      NOTE: the review's "same input ⇒ same output proves nothing" is right for the wrong
      reason — R12's `STEP_MAP_PURITY` did measure bitwise agreement, but on a run where
      every arm returned its input, so idempotence was trivially satisfied.
      FIX (staged): (a) capture the carried solver state into a snapshot and restore it
      between arms; (b) fingerprint what the snapshot cannot restore and fail the record
      closed when the fingerprint moves. Full fresh-solver-per-arm is recorded as the
      target and its cost assessed; a separate-process harness is the fallback.

- [ ] **B2 — no stage-2 reference is certified.** (review P0-3)
      VERDICT: **CONFIRMED.** Two arms, `ref_agree=0.2842` against `rel_err=0.7371` — 2.6×
      separation, so these are two unconverged solves being differenced (R12 R4 already
      says so). Both arms run on the live `pImpl`, and neither a residual-decrease nor an
      `F_E`-convergence criterion is applied.
      FIX: a third, tighter arm; certify only on
      `‖R(Y³)‖/‖R(Y¹)‖ ≪ 1` AND `‖Y³−Y²‖/‖Y³‖ < τ_Y` AND `‖F_E(Y³)−F_E(Y²)‖/‖F_E(Y³)‖ < τ_F`
      AND all three outcomes complete; emit `reference_certified` as a real field (today it
      exists only in R12's prose, not in the code).
      GATE: `Stage_Reference_Certification_Contract`.

- [ ] **B3 — diagnostics must be provably non-interfering.** (review P1-3)
      VERDICT: **CONFIRMED, and it is the load-bearing one.** `ScopedProbeState` restores two
      members and *reports* (`SDIRK3_PROBE_MUTATED`) what it could not — which is a real
      improvement over a snapshot list, but it is a report, not a gate, and the fingerprint
      covers four quantities.
      FIX: an ON/OFF equivalence harness — RHS call count, ordered stage-state digest,
      outcome sequence, config digest, final-state bytes — with any difference demoting the
      probe from observer to intervention in the record itself.
      GATE: `Diagnostic_Observer_Noninterference_Contract`.

---

## Phase 2 — what the AD graph actually differentiates

- [ ] **C1 — primal and operational tangents are different models.** (review P0-4)
      VERDICT: **CONFIRMED.** `imex_slow_in_tangent` is 0 at runtime (R12 W1), so
      `compute_k_slow` detaches the slow channel: the forward integrates `F_I + F_E`, the
      tangent differentiates `≈ J_I`. For exact 4D-Var that is not an approximation, it is a
      different cost function's gradient.
      FIX: (a) rename the reported ratio — it is `slow_norm_over_full_norm`, not a
      "fraction", since `‖J_E v + J_I v‖ ≠ ‖J_E v‖ + ‖J_I v‖` and cancellation can push it
      above 1; (b) emit the cosine `⟨J_E v, J_I v⟩ / (‖J_E v‖‖J_I v‖)` alongside; (c) emit
      `e_drop(v)` over a named direction set (random / implicit-step / dominant-Ritz /
      block-localized ru,rw,t,mu); (d) state in the code that exact 4D-Var authority requires
      `imex_slow_in_tangent=true`, and that the detached configuration is a weak-constraint
      model only once `Q_k` is defined. **Default stays unchanged** (opt-in knob rule).
      GATE: `Operational_Primal_Tangent_Contract`.

- [~] **C2 — the observer covers advection, not the whole explicit RHS.** (review P1-4)
      VERDICT: **CONFIRMED but already scoped honestly.** R12 R3 states the observer's
      range; the review is right that "advection dominates `F_E`" is not established for
      PGF / buoyancy / Coriolis / diffusion / damping / source / boundary. This item stays
      open as measurement, and the claim in the docs is bounded to what the observer sees.
      REMAINDER: a full-term decomposition needs an explicit pass at stage ≥ 2, which
      R12 R3b measured as **unreachable** in an aborting run — see `[B]` D3/R3b.

---

## Phase 3 — decomposition and MPI

- [x] **D1 — the memory-domain vs tile-domain offset contract.** (review P0-6)
      VERDICT: **REVIEW'S CONCERN IS SOUND; THE CODE IS CORRECT.** Resolved by inspection,
      both ends: Fortran passes the *memory* base (`C_LOC(grid%u_2(ims,kms,jms))`,
      `module_implicit_sdirk3.F:1287`, commented "u(ims,...) not u(its,...)"), and the C++
      adapter then slices a *tile* view at `i_start = its - ims`, `j_start = jts - jms`
      (`wrf_sdirk3_tile_unified_impl.cpp:31095-31097,31120`) before `unifiedStep` ever sees a
      pointer. So local index 0 **is** global `its_`/`jts_`, and the global-norm helper's
      stated assumption holds. The review's option (1) is the true one.
      REMAINING WORK: it is correct and **unproven** — the comment at `:3854` calls it an
      assumption. Add the contract the review specifies (`ims != its`, `jms != jts`,
      halo > 0, all three staggers, synthetic `x = 1e6·i + 1e3·j + k`).
      GATE: `Memory_Tile_Offset_Contract`. → moved to F1.

- [ ] **D2 — the global-norm harness cannot tell two timesteps apart.** (review P1-2)
      VERDICT: **CONFIRMED, with one mitigation the review missed.** `parse()` sums *every*
      record in a log with no timestep grouping — but the domain-count check catches a
      two-timestep log, because the counts then come out at 2× the domain and it exits 2.
      So today it is fail-closed-but-unusable rather than silently wrong. The review's
      second point stands on its own: **count equality does not prove an exact partition** —
      *m* overlapped cells and *m* dropped cells sum correctly.
      FIX: emit the owned box explicitly (`i0 i1 j0 j1 k0 k1 stagger step_seq`) rather than
      letting Python re-derive the C++ ownership rule (a re-derivation hides a mismatch
      between them), group by `step_seq`, and check exact disjoint union, not the total.
      GATE: `Global_Ownership_Overlap_Gap_Contract`.

- [B] **D3 — np>1 equivalence.** (review P0-7)
      VERDICT: **CONFIRMED, and it is blocked BY DESIGN, correctly.**
      `dyn_em/module_implicit_sdirk3.F:925` refuses multi-rank before touching any
      communicator: `SDIRK3_MPI_STAGE_HALO_UNSUPPORTED`, because every internal SDIRK stage
      runs inside one tile-worker C++ call and stage-halo correctness is not established.
      BLOCKER: establishing stage-halo correctness — a change to the solve, not to the
      harness. The harness (R12 R2) is built and self-validated at np=1 and is ready for the
      run that unblocking makes possible. **No np-equivalence claim may be made until then**;
      `MPI production: NO-GO` stands.

---

## Phase 4 — the record itself

- [ ] **E1 — the manifest parser silently overwrites duplicates.** (review P1-1)
      VERDICT: **CONFIRMED.** `sdirk3_manifest_diff.py` does `stages[stage] = fields` and
      documents it as "last step wins". With retries, several Newton iterations, several
      timesteps or several tiles, two arms can be compared at *different occurrences* of the
      same stage and the diff still passes.
      FIX: the emitter gains an identity (`step_seq`, `newton_iter`, `rank`, `tile_id`) — it
      currently emits `stage=` and nothing else identifying — and the parser keys on the
      composite tuple and **raises** on a duplicate key rather than overwriting.
      GATE: `Manifest_Duplicate_Fail_Contract`.

---

## Phase 5 — CI

- [ ] **F1 — the new contracts, and the ratchets they trip.**
      Adding a ctest trips three gates at once in this repo (5 prior recurrences):
      `.github/ci/expected_ctest_names.txt` (exact set), the README `N-test CTest` claim
      derived from it, and — if a header is installed — `expected_install_manifest.txt`.
      Run the gate shell locally before pushing; do not push-and-see.
      New tests: `Failed_Step_Map_Is_Invalid_Contract`,
      `JVP_Fallback_Invalidates_Verdict_Contract`, `Stage_Reference_Certification_Contract`,
      `Diagnostic_Observer_Noninterference_Contract`,
      `Operational_Primal_Tangent_Contract`, `Memory_Tile_Offset_Contract`,
      `Global_Ownership_Overlap_Gap_Contract`, `Manifest_Duplicate_Fail_Contract`.
      Deferred to the run that unblocks them: `Successful_Step_JVP_FD_Contract`,
      `Successful_Step_VJP_Dot_Contract`, `Probe_Solver_Isolation_Contract` (full
      fresh-solver form), `MPI_Global_Norm_Equivalence_Contract`.

---

## Completion gates (the review's §16, with this tree's verdict)

| Gate | Condition | Status |
|---|---|---|
| Forward map | target case advances one step `COMPLETE` | **[B]** measured-dead at every dt tried |
| Step tangent | all arms complete, FD–JVP converges | **[B]** on the above |
| Stage reference | ≥2 independent fresh solvers converge in state, residual, `F_E` | open (B2) |
| Exact AD | primal JVP == operational JVP within tol | open (C1) — currently **not equal by construction** |
| Weak constraint | the dropped component is in an explicit `Q` and control | not started |
| Adjoint | dot-product on a successful map + obs-space FD gradient | **[B]** |
| MPI | np=1,2,4 outcome and global state agree | **[B]** by the Fortran pre-gate |
| Global norm | exact ownership union, no overlap/gap | open (D2) |
| Observer | ON/OFF ordered digest and outcome identical | open (B3) |
| Production | no sentinel, no FD fallback, no uncertified reference | **NO-GO** |
