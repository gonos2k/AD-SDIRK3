# Native idealized-twin plan registration

Local timestamp: 2026-10-22T05:20:12+09:00 (JST).

## Context and scope

The user selected a new two-control idealized twin on the accepted native dry
`em_b_wave` 240 s map. Real observations are not a prerequisite for this case.
It will test initial-control/forward/H/adjoint/minimizer/state restoration using
known model-generated truth. The PR #314 residual-cost fixture and the completed
rotating inverse remain closed. This change registers a **plan only**; E0, E1
and E2 have not been executed, and no restoration result is asserted.

The linked sandbox attachments were not available as local files. The plan was
transcribed from the complete supplied message, rather than claiming a
byte-identical ZIP application. The Korean plan is
[`plans/native_idealized_twin_v1.md`](plans/native_idealized_twin_v1.md); the
authoritative selected parameters are in its
[plan JSON](plans/native_idealized_twin_v1.json), which is not a solver input.

## Selected problem and explicit conventions

- Reuse the registered 16³, h=15/16 s, 256-step, Newton 1e-9/0, Krylov 1e-7,
  internal-FP64 map and its post-initialization/CQ control boundary.
- Generate noiseless U/V/dry-total-theta point samples at accepted128/256 from
  matched FP64 truth. One-based per-field physical-owner indices 2/6/10/14 give
  384 rows; native staggers are distinct and H-transpose scatter-adds to the
  matching slots. Sigma U/V=.05 m/s and theta=.01 K are fixed research weights.
- Controls are ordered wind/theta. A corner-streamfunction discrete curl and
  mass-point smooth theta template have common U/V max .1 m/s and theta max
  .01 K. Owner fields are extended through the authoritative boundary rules.
  These templates make no force-balance or physical mass-divergence assertion.
- The background error is +.5 wind−.5 theta, so a*=(-.5,+.5) cancels it. E0 uses
  the separate truth-centered b=0 parameterization; E1 starts a=0 and may not use
  the truth control in its updates or stopping rule. No Jb is selected.
- Before results, specify two control FD widths .01/.03, the full-gradient
  normalization `||g_final||inf/max(1,||g_initial||inf)`, and the six-field
  epsilon-scaled RMS aggregate at each withheld60/180 endpoint. Each withheld
  time must improve by at least 50%; epsilon remains a display scale, not R.

The user's E0 zero-innovation/gradient/update thresholds, rank ratio 1e-3,
1% control FD and control-recovery budgets, 1e-4 cost ratio/normalized gradient,
12 accepted iterations and 40 objective forwards are retained. Initial and
accepted/rejected line-search objective calls count against 40; preflight FD,
E0 and any one needed truth run are separately identified. Adjoint prefix
replay is separately accounted for, not hidden in the objective count.

Implementation preflight must still freeze truth/source/history bindings,
actual P/Zb/H coordinate/slot/hash artifacts, the existing minimizer/box/line
search and gradient/replay caps, and the metric/signal/negative-test checks.
Thus `ready_to_execute=false` means implementation artifacts are pending; it
does not mean the user has failed to choose an observation/control problem.
E2 mutations must actually be caught, including a small midpoint contribution;
declaring an expected failure in this plan is not evidence of an executed E2.
Noise/prior E3 remains optional and outside mandatory completion.

## Navigation and static validation

Base main is `b21620c4e529bae91ac029bcbf42a41b9977d67b`, tree
`93270cbc601a237f25397574bc01691fb3ec90e2`, equal to the validated PR #314 tree.
The dirty original checkout was not modified. Work is isolated on
`codex/idealized-twin-plan-20261022`.

Graphify reused verified production/targeted graphs and checked 12 unchanged
source pins against the new worktree, including the packed velocity/T contracts,
owned-box rules, native caller and boundary hooks. The four changed plan/docs
are bound separately. Planned H/control/minimizer relations are explicitly
marked as plans, not extracted implemented calls. No production graph rebuild
or numerical payload inspection was needed.

Static validation checks JSON status/links, exact fractional times, 384 unique
observation IDs/64 rows per variable/time, positive sigma² weights, affine truth
control cancellation, bounds, E0 origin/thresholds, two axes/two widths,
identifiability/call caps, withheld-time separation, field scales and E3's
optional status. All 27 static checks pass, including explicit SVD evaluation
at the E1 start/both widths and the E1-only objective budget. This does not validate generated
coordinates or runtime H/H-transpose, rank, gradients, minimization or E0–E2.
Review and static-check receipts are in
[`evidence/idealized_twin_plan/`](evidence/idealized_twin_plan/).

## Model and RK3 validation status

No WRF/Fortran/C++ model was run, no executable was rebuilt and no production
equation, ABI, tolerance or runtime configuration was changed. The selected
forward map and all prior numerical packets retain their hashes. The historical
forward-map `observation_problem_selected=false` record is left immutable; the
new plan separately selects this idealized observation/control instance.

No new same-setup RK3 field/runtime comparison was made. Reuse the existing
[archived comparison context](evidence/native_terminal_adjoint/archived_rk3_context.json):
the same-input 240 s native/stock distances are descriptive because their filter
policies differ, and their timings do not establish a fair speed ratio. No
accuracy, restoration or efficiency improvement is claimed by this plan.

## Next action

Implement and freeze this plan's control/observation artifacts and caller
budgets, reusing verified source/truth evidence. Then perform its bounded E0,
identifiability/gradient/E2 and E1 restoration/withheld-state checks. Close this
new limited instance only when E0–E2 meet the frozen conditions. Do not add a
new timestep ladder, physics, optimizer family, Hessian, MPI or deployment
requirement to the completed earlier studies or to this case after seeing data.
