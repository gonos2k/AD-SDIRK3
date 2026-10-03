# Remaining algorithm checklist after the two-time inverse stack

Local timestamp: 2026-10-03 10:32:10 JST.
Candidate base: PR #272 `5963589`; main remains PR #270 `0b3798e2`.
PR #271/#272 are review-ready candidates, not merged main features.

## Reconciled prior findings

| Previous item | Current disposition |
| --- | --- |
| Strict Stage-2 failure | Closed for supported internal FP64 carry; default FP32 strict path is not claimed fixed. #266-269 contain actual strict forward completion and time-order evidence. |
| V final tendency / W RHS and PH/MU derivative | Closed for the dry fixed-K supported contracts; broader operator parity remains separate. |
| FP64 handoff / short active multistep adjoint | Closed within 2/4-step native profile and published WRF forward receipts. |
| Physical R / intermediate observation cotangent | Candidate #271 closes the native two-time objective, with fixed per-point covariance and actual optimization. |
| Stronger nonlinear optimizer | Candidate #272 closes the large unbalanced dry twin, with BFGS, genuine Armijo backtracking and convergence. |
| Old PR #265 snapshot | Historical source-state record; do not treat its then-open PH/MU and strict forward findings as newly open at this source. |

## Active bounded milestone: nonzero hydrostatic inverse background

- [x] Build a horizontally uniform nonzero theta/MU/PH background using the native EOS and signed-eta pressure balance.
- [x] Verify intended positive layer thickness, dry mass, pressure and density without clipping inadmissible states.
- [x] Gate live Full/ExplicitOnly/ImplicitOnly RHS in the same declared context; report each physical block separately.
- [x] Require a complete FP64 four-step window to preserve the equilibrium within an independently defined equilibrium precision budget at unchanged Newton tolerance; do not reuse a K-form tolerance as a state-error bound.
- [x] Retain six W/PH/MU controls, fixed physical R and observation times 2/4; truth perturbations are not equilibrium states.
- [x] Execute actual two-time inverse optimization, FD/Taylor, replay and independent linear-MAP checks using the existing implementation.
- [x] Correct the root README's overly broad claim that no native observation-window adjoint is supplied.
- [x] Validate affected regressions, refresh the source-matched graph and obtain independent Green/Red review.
PR delivery and final exact-source remote CI are recorded below after completion;
the local scientific checklist is closed without asserting an unfinished CI result.

## Subsequent checkpoints, not silently completed

A modestly broader native window can first use existing vector cotangents.
Measure retained-graph scaling before introducing checkpoint/recompute. If needed,
segment replay must preserve exact FP64 boundaries, host step tokens, dt and
fixed live RHS context, then verify exact primal and adjoint equivalence.
The full reset already clears last-step graph references; do not open a reset
bug merely from an incomplete read of `invalidateMapFactorCaches`.

Full WRF observation-window/MPI adjoints, moist or diagnosed K, spatial parity
for broader maps/boundaries and whole-model energy/flux budgets remain distinct
physical/algorithm extensions. This report does not relabel them as completed
by a short native inverse or by general core MPI contract tests.

No new WRF/RK3 run or forecast comparison has yet been performed in this milestone.
Existing same-setup #269 RK3 comparison is preserved: at 240 s RMS differences
U=8.4226e-3, V=9.5424e-4, W=6.2571e-4, PH=1.3330, T=4.1426e-2,
MU=4.4902e-1 in output variable units; step-log totals carry=7.168 s and
archived RK3=0.2617 s (not equal-accuracy cost nor wall time). These do not
validate the new native equilibrium until its own gates pass.

## Candidate implementation and pilot record

Background is a resting uniform sigma column with theta=310 K, total dry mass
84000 Pa and ptop=20000 Pa. Prescribed p perturbations are [3500,2500,1500,500]
Pa. Native alpha is obtained from compute_inverse_density in FP64, then the
native calc_p_rho perturbation relation is inverted:

    delta(PH)_k = (alpha_target*M_full - alpha_base*M_base)/abs(rdnw_k).

The native EOS uses analytic base alpha, while imported PH base is rounded
FP32. These must not silently be interchanged. Native p/alpha are checked with
FP64 roundoff budgets; geometric full-PH alpha is separately checked against
that FP32 base precision. The geometric positivity diagnostic is not used as
proof of native EOS balance. Exact theta-base/sigma/metric assumptions are
checked rather than promoting the formula to general hybrid coordinates.

The actual four-step window prepares native ARK face/context fields. The
setup-only call does not do so; no extra pre-integration is performed. Accepted
checkpoints are retained for stationarity, then Full/E/I are evaluated at the
original background with the same reference and prepared fixed context.

RHS checks convert acceleration to per-step velocity before comparison. The W
budget is dt*g*2*abs(rdnw)*pressure_floor/M_full divided by a 1 m/s reference
unit. Other exact-zero blocks and split closure use FP64 epsilon and explicit
physical unit floors. Four-step drift is gated by four times the per-step
precision allowance plus state-roundoff allowance per block. This is a scoped
engineering equilibrium precision gate, not a rigorous general nonlinear
state-error bound or an unscaled Newton-residual conversion.

Pilot 3 passes: native pressure error 1.4552e-11 Pa and relative alpha error
1.0687e-16; Full W RHS max 6.7978e-15 m/s^2; Full-E-I is exactly zero.
After four steps W drift is 6.7943e-15 m/s and U/V/PH/theta/MU drift is zero.
The perturbed two-time inverse accepts two updates, J .8712274 -> .004924472,
control gradient 18.43734 -> 6.6215e-9, W RMSE .000933396 -> .0000172106 m/s.
Final numerical source is `7c954ae`; rebuilt executable SHA256 is
`530d4d77c35201084faa6498f94d54fd14ab7957ec5c12c27feea83810a7eff7`.
The affected family passes 4/4 (277.09 s). After aligning the W budget with the
actual grid gravity and adding an actual-evaluation flag to the summary, the
final balanced target passes again (55.23 s). No production operator changed.
Exact registered CTest inventory is 116 and ratchets pass. The previous parent
115-test suite is retained; the full final Linux inventory is checked in CI.

The final PH-layer falsifier has scaled W response 0.00128407637 against an
approximately 1.91e-11 equilibrium budget. Geometric alpha relative error is
1.6249e-7 under the imported-FP32 allowance 1.9073e-6. The balanced experiment
DOES NOT perform a tighter-Newton reevaluation; placeholder terminal-summary
fields are not evidence for one, and `tighter_newton_evaluated=0` is explicit.
The old strong nonlinear experiment retains its actual tighter-Newton check.

Green and Red reviews found no numerical blocker; their requested assumptions,
unit conversions, and distinction between native EOS/geometric pressure and
K-residual/state error are reflected in the final source. Graphify source
copies are checked against the owning worktree; inline/template extraction gaps
are navigational gaps verified against source, not correctness proof.

Machine results are in `docs/evidence/fp64_balanced_inverse/results.json`.
Raw pilot failures, final binary, build and CTest receipts are archived at
`/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261003/fp64_balanced_inverse`.

Two early pilot failures are preserved: an incorrect test-generator axis in a
full-PH integration attempt, and a setup-only RHS call before native context
preparation. Neither is presented as a discovered production numerical defect.
No acceptance threshold was expanded to pass either failure. The native delta
formula and an actual measured window resolved them.
