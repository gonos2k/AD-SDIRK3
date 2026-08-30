# R14 — simplification reset

**Status** R14.1 DONE (this branch); R14.2/R14.3 awaiting go · **Baseline** `main@6f4a861` + PR #189 (open) ·
**Independent review: NOT RUN.**

The user's diagnosis, accepted in full: the repository is in **assurance inversion** —
correctness machinery has outgrown the execution path it guards. Every link in

    defect → local rule → rule consumer → negative fixture → consumer lint → coverage ratchet

was individually correct, and the chain drifted from the only goal that matters:

    SDIRK3 advances one timestep at dt=600, correctly and stably.   ← still zero successes

PR #189 cut narrative and two monitors. It did not change the structure. R14 does.

## Measured scope (main + #189)

| | value |
|---|---|
| opt-in experiment flags in `newton_solver.cpp` | **17**, ~1,247 lines (8.8% of the file) |
| types re-interpreting one Krylov result in `first_failure.h` | **19** enums/structs |
| classification fixtures | 224 checks / 2,185 lines |
| `newton_solver.cpp` | 14,190 lines |
| end-to-end tests that already exist | tangent, adjoint (×3), primal-tangent, failed-step-map |
| dt=600 | `newton_budget_exhausted`, stage 2, unchanged |

One Krylov result currently passes through ten representations before it reaches a log line:
raw signal → derived veto → disposition → trial outcome → Newton termination → stage failure →
attribution → specific layer → telemetry record → offline classification. Every recent defect
was a later consumer reading an earlier representation.

## Principles (the user's, adopted verbatim)

1. **No new gates.** No new pure-rule helper, receipt field, taxonomy value, lint, ratchet or
   checklist layer. Simplify the executing code instead.
2. **One fact, one struct.** A Newton iteration produces one `NewtonIterationResult`; statistics,
   termination, logging and classification are *derived from it*, never reconstructed beside it.
3. **Two states only: production-supported or absent.** "Present, configurable, forced OFF,
   guarded by fixtures and a lint" is not a state.
4. **Transition tests over pure-helper tests.** Fake `A`, `b`, RHS → FGMRES result → candidate →
   trust reject → K unchanged → Newton exit → stage outcome.
5. **Five authorities at the top**: forward completes and publishes a finite admissible state;
   diagnostics OFF/ON give the same trajectory; tangent matches central FD on a successful map;
   adjoint satisfies ⟨Jv,w⟩=⟨v,Jᵀw⟩ on the same map; np=1/2/4 publish the same state.

**Hard rules:** adding a gate requires deleting one. A rule that does not change production
state is not a production-correctness gate.

## Phases — deletion first, consolidation second, dt=600 last

### R14.1 — delete what does not execute (pure removal, byte-identical dt=600)

- Armijo line search: code, `nk_line_search` config across Registry/Fortran/C++, its fixtures,
  `line_search_skipped`, `line_search_alpha_is_trustworthy`. Re-add as an independent
  implementation if ever needed.
- Candidate arbitration (`WRF_SDIRK3_CANDIDATE_ARBITRATION`) and the admission machinery:
  `effective_total_failure_veto`, `CandidateDisposition`, `candidate_arbitration_rescues`,
  `CandidateArbitration`. Measured behaviour-neutral on every record that motivated it.
- Diagnostic probes that are not needed to reach a first dt=600 step: `TAYLOR_DEFECT`,
  `TERMINAL_TAYLOR`, `DISCARDED_CANDIDATE`, `FROZEN_MI_AB(_EXTEND)`, `STAGE_REFERENCE`,
  `KRYLOV_TRAJECTORY`, `POLICY_MANIFEST`, ledgers. Keep `NUMRANGE`-class measurement only if it
  is the tool the dt=600 work will use next; decide per flag, listed in the PR.
- `lint_rule_consumers.py` and its ratchet; `lint_item_guard.py` stays (it guards a real AD
  constraint on the executing path).
- **Verification:** ctest, full WRF build, dt=600 telemetry byte-identical. Expected: several
  thousand lines removed.

### R14.2 — one result object (behaviour-preserving refactor)

- `NewtonIterationResult { LinearSolveResult linear; CandidateResult candidate;
  NewtonIterationOutcome outcome; }` produced once per iteration at the site where the
  outcome is known.
- `gmres_total_failure`, `linear_signal`, `trial_outcome`, `accepted_via`, the split counters,
  `gmres_total_failures`/`non_total_failures` all become reads of that object or disappear.
- `StageFailureSignals` shrinks to what the stage-level classification actually consumes; the
  offline classifier (`first_failure_of` + `stage_diagnosis_of` and the 19 types) collapses to
  one function over the result objects, or is removed if the telemetry line already says it.
- **Verification:** the existing dt=600 record classifies identically; one transition test
  (principle 4) replaces the pure-helper fixtures that pinned the deleted intermediates.

### R14.3 — five gates, one CI job

- **Correction from the R14.1 survey:** the four tests the plan called "end-to-end" are unit
  contracts — `operational_primal_tangent` exercises `compute_jvp_fwad_or_fd`, `failed_step_map_
  invalid` exercises the pure `step_map_verdict` rule, the other two include `hydrostatic_pressure.h`
  and `unified_rhs.h`. **No test in the tree drives the production stage or step.** All five gates
  are new: a harness that constructs the tile solver on a small synthetic domain and calls the
  same stage entry `module_implicit_sdirk3.F` calls. Forward-completion first (nothing asserts a
  published step, because none exists at dt=600 — the gate is written to FAIL until R14.4 makes it
  pass). MPI gate stays NO-GO until np>1 is unblocked.
- Retire ctest targets that test intermediates the result object removed. Target: 59 → ~25.

### R14.4 — dt=600, first complete step

Only after the above. The failure is stage 2 `newton_budget_exhausted` on the shipped config;
the campaign's own measurements say `stage2_gmres_restart=192` relocates it to stage 3. The
work restarts from that measurement with a solver that has one execution path.

## What this plan does not decide

Whether R14.1 deletes the HEVI-off / full-implicit paths (`imex_split_mode` 0/2) that the
operational mode-3 configuration never takes. Same principle applies; larger blast radius;
decide after R14.1 lands.

## R14.1 — executed

Four commits on `agent/r14-1-delete-unreached`, each three-reviewed (execution census on the
dt=600 record → cross-reference in CI/Registry/Fortran/tests → the compiler), **−4,046 lines**:

| | before | after |
|---|---|---|
| `read_experiment_flag` sites in the solver | 30 | **0** |
| `newton_solver.cpp` | 14,190 | 11,838 |
| `probe_validity.h` | 540 | 179 |
| ctest targets | 62 | 58 (four probe-only tests removed) |
| classification fixtures | 235 | 210 |

Removed: all 17 opt-in flags and their blocks, the Armijo path, the arbitration/admission
machinery, the probe verdict rules and types, the `nk_line_search` knob across all five layers,
and `fortran_registry_interface.f90` (an ABI with no implementation and no caller). The
rule-consumer lint found each next orphan as the previous one went — five chained removals.

Verification: `clean -a` + full rebuild 0 errors, `wrf.exe` and `ideal.exe` present, dt=600
`SDIRK3_*` telemetry byte-identical, ctest 58/58, lint 87/87.

Two environment traps met on the way, both recorded to memory: automatic block-cutting broke the
build twice (a static-lambda initialiser is not an if-statement; an upward search for `if (` can
land on the enclosing one) — fixed by walking up only inside the condition, bottom-up; and
`./clean -a` deletes six git-tracked files, so `ideal.exe` silently fails to link while the build
reports 0 errors.

**R14.2 scope, measured:** 14 iteration-local variables (92 read sites) collapse into one
`NewtonIterationResult`; `StageFailureSignals` has 76 fields of which the classifier reads 36 —
the other 40 are telemetry-only and split off, none is dead.

## R14.2 — design, measured (awaiting go)

**What one Newton iteration currently keeps as separate state** (13 variables after R14.1d):

| decides control flow | derived / recorded only |
|---|---|
| `gmres_total_failure` — trust-loop condition (L10449), statistics branch (L10971), zero-update exit (L11094), consistency check (L11002) | `linear_signal`, `trial_outcome`, `accepted_via`, `effective_veto` |
| `step_accepted` — 5 branches | `trust_attempted`, `recovery_attempted` |
| `gmres_total_failure_candidate` — 4 branches, `const` | `entry_mismatch_step_rejected`, `gmres_S_reached_on_entry`, `gmres_objective_mismatch_on_entry`, `arbitration_admitted` (now always false) |

**The object:**
```cpp
struct NewtonIterationResult {
    // what the linear solve reported -- written once, at the solve
    struct { bool total_failure_signal; bool entry_S_reached; bool entry_objective_mismatch; } linear;
    // what the globalizer decided -- written once, at the accepting/refusing site
    struct { TrialOutcome outcome; bool trust_attempted; bool recovery_attempted; } candidate;
    // derived, never stored separately
    bool step_applied()      const;   // outcome is one of the Accepted* values
    bool failure_stands()    const;   // linear.total_failure_signal && !step_applied()
};
```
`gmres_total_failure` becomes `result.failure_stands()`; `step_accepted` becomes
`result.step_applied()`; the four decision sites read the method, and the post-hoc
"reassert the flag after the trial" block (R13.25) disappears because the answer is computed
from the outcome instead of patched after it.

**Statistics and telemetry** derive from the object at one site after the trial:
`linear_total_failure_signals += linear.total_failure_signal`,
`unresolved_linear_failures += failure_stands()`, `globalization_rejections += (outcome is a
Rejected*)`. The legacy `gmres_total_failures / non_total_failures` pair keeps its meaning
(`failure_stands()`) so archived records compare.

**`StageFailureSignals`**: 76 fields, filled at 72 sites, **69 of them inside
`solveImplicitStage`** (from L13504) -- one function, so the fill collapses to one block. The 36
fields the classifier reads stay; the 40 telemetry-only fields move to a `StageTelemetry` the
emitter serialises, so the classifier's input is exactly what it consumes.

**Verification for the refactor:** the dt=600 record must classify identically and every
`SDIRK3_*` line must be byte-identical -- this is behaviour-preserving by construction, and the
A/B is the proof. One transition test (fake `A`, `b`, RHS -> solve -> refuse -> K unchanged ->
typed exit) replaces the pure-helper fixtures that pinned the removed intermediates.

**Estimated edit surface:** ~90 read sites in `newton_solver.cpp`, one fill block in
`tile_unified_impl.cpp`, the `StageFailureSignals` split in `first_failure.h`.

### R14.2a — executed (pending the dt=600 A/B)

`NewtonIterationResult` introduced; the solver's 13 iteration-local variables collapse to it.
`step_accepted` → `it.step_applied()`, `gmres_total_failure` → `it.failure_stands()`; the three
acceptance sites write `it.candidate.outcome`; the R13.25 "reassert the flag after the trial"
block and the R13.26 "clear the flag on recovery" line **disappear** because both answers are
now computed from the outcome. Three rules the object supersedes are removed with their
fixtures (`effective_total_failure_veto`, `linear_failure_stands_after_trial`,
`runtime_failure_flag_consistent` — the last became a tautology the moment both of its inputs
derived from one object). One fixture pins the derived methods against the old decision table.
Source: −74 lines net; ctest 58/58; lint 84/84.

### R14.2b — the `StageFailureSignals` split, refined

The first survey said 36 read / 40 unread. Counting **indirect** readers (`exit_receipt_view`
consumes twelve `exit_*` fields on the classifier's behalf) the real split is:

- **48 classifier-input fields** — stay in `StageFailureSignals`
- **28 telemetry-only fields** — `krylov_iterations`, `entry_metric_mismatch_events`,
  `globalization_rejections`, `gmres_non_total_failures`, `gmres_tolerance_reached`,
  `argmin_residual_iter`, `total_failure_vs_{b,r0}_count`, `krylov_failure_vs_r0`,
  `best_krylov_rel_error_vs_r0`, `worst_krylov_{iter,eta,tolerance_source,stopping_metric,rho_D,
  rho_S,restart_budget}`, `krylov_solves_measured_vs_r0`, `discarded_candidates_{seen,descent}`,
  `taylor_probe_last_iter`, `all_near_worst_met_tolerance`, `exit_{budget_exhausted,
  tolerance_source}`, `krylov_rule_{fellback_to_b,observed}`, `krylov_{restart_budget,max_restarts}`
  — move to a `StageTelemetry` the emitter serialises. Several of these describe probes R14.1
  deleted (`discarded_candidates_*`, `taylor_probe_last_iter`) and are candidates for outright
  removal once the emitter is checked.

The fill sites span L10181–L13589 of `solveImplicitStage`; the split collapses them to one
block each.
