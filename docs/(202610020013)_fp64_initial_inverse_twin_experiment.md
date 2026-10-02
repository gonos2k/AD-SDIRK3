# FP64 initial-state inverse twin experiment

Local timestamp: 2026-10-02 00:13:31 JST.
Numerical base: main `9d32c60d73b812da6cd05705b539916b12217d40` (PR #269).

## Objective and closure checklist

The next milestone is a real small optimization using the established continuous
FP64 forward and its matching discrete adjoint. Production equations, precision,
solver criteria, boundary policy, and public API remain the established reference.
The example reuses the existing native dry, fixed-K, single-CPU-tile trajectory.

- [x] Define six dimensionless smooth W/PH/MU control coefficients on boundary-compatible physical directions.
- [x] Observe independent terminal W locations, excluding periodic/symmetric aliases and fixed levels.
- [x] Check observational rank and singular values independently of the prior.
- [x] Generate twin observations with the same four-step FP64 forward.
- [x] Form background plus observation cost and `v + L^T lambda_0` with the existing trajectory pullback.
- [x] Compare the control gradient with independent differences and verify deterministic evaluations.
- [x] Complete Armijo iterations with actual cost reduction, valid accepted states, and reduced gradient.
- [x] Compare the nonlinear result to an independently computed linear MAP reference; do not require exact truth recovery.
- [x] Validate the affected example and preserved adjoint regression; verify Graphify inputs and obtain Green/Red review.

Final-head GitHub CI status is recorded by the PR checks, separately from the
completed local algorithm checklist.

## Declared mathematical contract

`Z0(v)=Zb+L v`, with two modes each of W/PH/MU, dimensionless `v`, and
physical direction scales 0.1/100/100. Mode construction uses the same packed
boundary projector as the solver. Observations use independent physical W core
locations. A synthetic truth produces observations; this is an algorithm twin
experiment, not a real-atmosphere forecast or assimilation qualification.

The terminal cost is

`J(v)=0.5*v.dot(v)+0.5*mean(((H(ZN)-y)/sigma)^2)`.

The stated observation scale `sigma` is a loss scale. With `Nobs` observations,
the equivalent covariance is `R=Nobs*sigma^2 I`; the independently whitened
Jacobian and innovation use `1/(sigma*sqrt(Nobs))`. The terminal cotangent is
scattered only to observed W entries, divided by `Nobs*sigma^2`. Thus the control
gradient is `v+L^T lambda_0`. The fixed SPD search metric is `I+A^T A`, which
changes the search direction, not the objective or forward equations.

The independently constructed linear MAP reference is
`v_lin=(I+A^T A)^-1 A^T d`. The prior makes this system invertible even if
observations do not resolve every mode; observational rank must be tested
separately. Neither the regularized optimum nor `v_lin` must equal the synthetic
truth exactly. Physical rejection reduces the line-search step; no clipping is
used to fabricate an admissible state.

## Validation status and next actions

The new inverse and existing carry-adjoint tests pass (2/2, 51.26 s).
All affected test targets build; exact inventory and all three documentation
claimants agree on 113 registered cases. Ratchets pass. This is a test/example
change, so unaffected model/MPI contracts were not redundantly rerun locally.
Final-head GitHub CI is recorded in the PR.

Measured terminal inverse: 105 observations, six resolved control directions,
condition number 16.4981, smallest singular value 0.824762. The Frobenius norm
of the two-width Jacobian difference bounds operator uncertainty at 1.6332e-8;
the predeclared 50x uncertainty/rank bound is 8.1660e-7. Two Armijo steps were
accepted at alpha=1, with no clipping or rejected trials. Total cost decreases
0.5901735309307103 -> 0.004875652707041067; observation term decreases
0.5901735309307103 -> 0.00030505207319913446. Physical W RMSE is
0.001086437785545689 -> 0.000024700286362677434 m/s. Control-gradient norm
is 13.174716481793219 -> 5.94769974658751e-9.

The nonlinear-vs-independent-linear MAP coefficient gap is 5.642177114559395e-6.
The predeclared small-neighborhood budget is 0.01 in unit-prior coefficient
norm when the reference norm is below one; it is an absolute budget, not a
1% relative-error assertion. Actual truth recovery is not required by this
regularized cost. Every accepted checkpoint passes finite, positive pressure,
density, theta, total column/hybrid-layer mass, and geometric/eta-thickness
checks; raw EOS stays above the production clamp floor. Same final controls
reproduce the objective, gradient and every checkpoint exactly.

Green and Red reviewed the implementation and evidence with no blocker for
this scope. Sorted singular values are reported separately from mode coefficients
because SVD vectors are rotated combinations of controls.

The scoped Graphify corpus was verified against the base before editing and
refreshed against the final changed test/fixture bytes after implementation.
The extracted graph links the pullback to fixed-context checks, but does not
reliably extract the anonymous test `run` to autograd path; authoritative source
inspection establishes that call instead.

No new whole-WRF, MPI, or RK3 model run/comparison has been performed in this
change. The production source is unchanged; the same-setup archived comparison
and direct FP64 time-ladder evidence from PR #269 are retained rather than rerun.
Multi-observation-time costs and long-window checkpoint replay are later steps.

## Reproduction and evidence

Build the existing `test_fp64_carry_adjoint` target and run it with `--inverse`,
or run CTest `FP64_Initial_Inverse`. Default invocation retains the original
carry-adjoint regression. The final inverse-only replay passes in 38.72 s.
Machine results, all six control estimates, the separately sorted singular
spectrum and six derivative records are in
`docs/evidence/fp64_initial_inverse/results.json`. Raw logs are preserved at
`/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261002/fp64_initial_inverse/`.

The next algorithm step is a two-observation-time cost after review of this
terminal inverse result. It is not included in the present closure scope.

Implementation source: `6f79692f55b181360792a35fb602f66bc4e9f88d`.
The final focused graph contains 384 nodes and 17744 edges; it extracts
`evaluate_map -> run` but misses the member-call edge to the trajectory pullback.
The authoritative source confirms that call.
