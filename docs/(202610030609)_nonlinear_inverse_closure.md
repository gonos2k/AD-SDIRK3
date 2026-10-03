# Strong two-time inverse: algorithm closure checklist

Local timestamp: 2026-10-03 06:09:56 JST.
Base: PR #271 HEAD `793845bcd2c30c5076230f580caa7ff2fc0eb53f` above
main `0b3798e2696df8fe3fafc025789e086735833524`. Numerical milestone:
stronger nonlinearity in the established six-control, dry fixed-K CPU tile.

## Checklist and scope

- [x] Retain six W/PH/MU controls, four 0.25 s ARK steps, observations at accepted steps 2 and 4, and the physical per-point R from the two-time experiment.
- [x] Choose the larger truth increment from physical initial-state bounds rather than changing loss weights or forcing a backtrack.
- [x] Validate positive layer thickness, hybrid dry layer mass, theta, pressure and density at initial and accepted trajectory states.
- [x] Check a directional adjoint derivative and Taylor reduction at a strong nonlinear point, above the objective roundoff floor.
- [x] Preserve the independent background FD observation Jacobian and background linear MAP for comparison.
- [x] Record failure of the fixed-background metric within the unchanged 30-update limit; do not declare a lower gradient to be convergence.
- [x] Use inverse BFGS from actual adjoint gradient changes, positive curvature and an SPD inverse metric; retain Armijo and typed recoverable trial rejection.
- [x] Require a real Armijo rejection, accepted alpha below one, actual BFGS updates and the unchanged final gradient criterion.
- [x] Compare the converged point with a tighter Newton solve and demonstrate nonlinear-versus-background-linear separation above numerical estimates.
- [x] Check exact replay of cost, gradient and all internal FP64 checkpoints.
- [x] Final rebuilt native CTest inventory, source/executable manifest and Green/Red review.
- [x] Deliver PR #272 ready for review and verify all four CI jobs on numerical/evidence HEAD `6fa9101`.

Production spatial equations, the carried trajectory adjoint, solver options,
Fortran ABI and external observations are unchanged. This is an executable native
inverse example built on PR #271, not a new production optimization API.

## Mathematical and statistical contract

Z0(v)=Zb+Lv, with the same six boundary-projected W/PH/MU modes and unit
Gaussian prior, Jb=0.5*v^T*v. Each of the 105 independent W samples at each time
uses sigma_R=sqrt(105)*0.001 m/s, R=sigma_R^2 I. Observation costs are summed
without a new count/time mean. Thus the stronger twin changes y, not R or the
meaning of the controls. The nonlinear adjoint cost includes both output
cotangents and the prior gradient exactly as in the weak twin.

The original truth pattern is multiplied by the minimum permitted by initial
max|delta W| <= 10 m/s, max|delta MU|/M <= 0.20 and
max|delta layer PH|/layer PH <= 0.50. The resulting multiplier is
8233.990975178567: max|delta W|=10 m/s, dry mass fraction=0.1152944319,
layer-thickness fraction=0.3228935742. These bounds define only the INITIAL
increment. Paired truth/background W differences at outputs 1..4 reach
12.9389, 19.6885, 26.3058 and 32.7418 m/s. This is a large unbalanced dry twin
with gravity/pressure transients, not a balanced or representative weather state.
Physical admissibility is distinct from meteorological realism.

Start with H0=(I+A_bg^T A_bg)^(-1). For accepted control increments s and gradient
increments y, rho=1/(s^T y), update
H+=(I-rho*s*y^T) H (I-rho*y*s^T)+rho*s*s^T.
The update is used only if s^T y exceeds the FP64 curvature resolution floor;
the symmetric result must remain finite and positive definite. Search directions
must satisfy g^T p<0. The weak twin retains its old fixed metric. The 30-update
cap, Armijo c, physical covariance and final criterion
||g|| < 1e-6*max(1,||g_initial||) are unchanged.

## Measured evidence

The fixed-background-metric prototype failed after 30 accepted updates:
J=79,367,174.3451 -> 343,016.9876, ||g||=176,394.7216 -> 3.26157,
above the unchanged 0.176395 convergence criterion. Raw failed log and binary
are archived separately; no exact source commit for that uncommitted prototype
is claimed.

The inverse-BFGS run completes in 11 accepted updates with one actual Armijo
rejection, minimum accepted alpha=0.5, 11 curvature updates and no skipped
updates. No physical rejection or ordinary non-convergence is needed.

- J: 79,367,174.34511234 -> 343,016.97914849862.
- Observation cost: 79,367,174.34511234 -> 17,629.662310648473.
- Control gradient norm: 176,394.7216015151 -> 0.0031948168485312245.
- W RMSE over both times: 8.908825643434287 -> 0.1327767385901931 m/s.
- Nonlinear versus independent background-linear MAP control gap: 530.5052052551.
- Background two-width FD/roundoff control estimate: 0.063391773235;
  the required 100x separation is 6.3391773235.
- Strong point at half the truth: directional FD=-96624.7047268,
  adjoint=-96624.7362822, budget=48.3134635, Taylor residual ratio=3.84851176.
- Re-evaluating the final controls at Newton 1e-11 instead of 1e-10 gives
  exactly unchanged terminal state, cost and gradient in this execution.

The FD/roundoff comparison and local inverse-metric response to the tighter
solve are engineering estimates, NOT rigorous global uncertainty bounds. Tight
state comparisons use each physical block separately; PH cannot hide an error
in W through a single mixed-unit norm. Control coefficients and gradients are
already dimensionless. Prior shrinkage remains valid; truth recovery is not
forced and the nonlinear/linear difference is not a new source-code defect.

## Validation and provenance

The final numerical source is `2ad29c6`; the rebuilt test SHA256 is
`73dae0ebc170e24d132d4c11251e5c572d12f4e6bf2c3b26fdf4fff6fba078fa`.
All 115 registered local CTests pass (zero failures, 559.28 s), including the
new nonlinear inverse in 121.18 s. A separate final stdout capture passes with
exactly the reported results. Apple clang 21.0.0, Release -O3, arm64 and
Homebrew libtorch 2.10.0 were used. Ratchets and the exact inventory match pass.
Core registered MPI contract tests ran as part of this suite; they do not
qualify a WRF MPI inverse trajectory.

Source blobs, test settings, scalar measurements and reviews are recorded in
`docs/evidence/fp64_nonlinear_inverse/results.json`. Raw failed and final logs,
binaries, build receipt and graph byte-match manifest are archived at
`/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261003/fp64_nonlinear_inverse`.
The earlier fixed-metric prototype is preserved as a failed numerical variant;
its uncommitted source is not misidentified as the final source.

Green and Red read-only reviews found no numerical blocker. Red requested
alignment of filename/header timestamps, which is resolved; both teams
confirmed initial-only caps and non-rigorous estimate scope. Related weak terminal/two-time, carry,
ABI and core regressions use the existing CMake entry points. The registered
inventory is now 115 with matching CI name and documentation contracts.

Final remote receipt: [CI run 37066138671](https://github.com/gonos2k/AD-SDIRK3/actions/runs/37066138671)
ran on exact HEAD `6fa9101b3dcfdb7d4217e06c1c4a60fbf41bf83d`. All four jobs
(fast-contracts, core-linux, build-contract-negatives, required) succeeded.
Raw Linux CTest log confirms 115 registered: 114 passed, one MPS skip,
zero failures. The nonlinear inverse passed in 242.28 s. PR #272 is ready,
stacked on #271; the linked workflow was manually dispatched because its
feature-branch base is outside the automatic main/ad-main trigger.

This final receipt is a documentation-only amendment after that verified run.
The numerical source and tested executable remain those recorded above;
no production/test/CMake/CI source changes followed validation. Raw CI logs and
the final delivery manifest are archived with the local receipts.

Graphify was reused for navigation before editing and refreshed on the affected
four-file corpus after the source changes. Owning source copies are byte-checked;
missing member-call edges were inspected directly. Graph edges do not prove
numerical correctness.

No new WRF build, em_b_wave forecast or RK3 run is performed for this test-only
optimizer change. The same-setup archived RK3/WRF field and runtime evidence from
#269 is retained without claiming it validates this stronger native inverse.
No MPI, moist/diagnosed K, long-time memory replay or real-observation 4D-Var
qualification is claimed. These are later physical/window extensions.

## Next actions

Close this milestone after final native/CI evidence and independent team review.
Then choose a physically balanced or broader time-window inverse case according
to the desired application; preserve the established R and state/adjoint
contracts rather than expanding controls indiscriminately.
