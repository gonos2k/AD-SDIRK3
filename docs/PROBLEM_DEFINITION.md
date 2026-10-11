# AD-SDIRK3 first-stage problem and completion boundary

Local timestamp: 2026-10-11 17:47:15 JST (+0900)

Last updated: 2026-10-11 17:57:34 JST (+0900)

This is the single project-level definition of the first-stage research target.
Case reports retain their own inputs, numerical budgets and completion status.
This definition does not certify the first-stage target as complete. The target
forecast window and field-error budgets remain unselected; until those required
choices are specified, project-level completion is withheld. Efficiency is an
optional acceptance target, not adopted by this definition. If it is selected,
its same-accuracy cost/timestep criterion must be specified and met.

## Main problem, derivative problem and validation problem

The main problem is to compute a declared WRF dry dynamical configuration with a
consistent spatial operator and an accepted ARK324L2SA time map. The derivative
problem is to compute the first-order JVP and VJP of that same map and compose
its accepted steps and observation derivatives for the declared assimilation
window. A restricted inverse is a validation problem for this connection:

\[
F_\Delta\ \longrightarrow\ G_{\Delta,h}\ \longrightarrow\
DG_{\Delta,h},\quad DG_{\Delta,h}^{T}.
\]

The first-stage scope is dry dynamics, prescribed coefficients, fixed grid and
boundary branches, serial CPU execution on one MPI rank and one domain-covering
tile. `test/em_b_wave` is the required integrated WRF validation entry point.
Its particular archived grid, timestep and duration are evidence instances,
not universal project targets. The selected case must declare its metrics,
terrain, Coriolis fields, transport, diffusion and damping before comparison.

General moisture/physics coupling, other boundaries or terrain configurations,
multiple ranks/tiles, adaptive-branch differentiation, HVPs, real-observation
assimilation, deployment and additional platforms are separate targets. They do
not become automatic prerequisites for closing an already declared case.

## One fixed mathematical instance

For each proposed completion instance, freeze the following record before
using results for acceptance. An unspecified entry is an open definition gate,
not permission to reuse a value from a previous experiment.

| Item | Required definition | Current project-level status |
|---|---|---|
| State and equations | \(Z=(U,V,W,\Phi,\Theta',\mu')\); WRF coordinates, EOS, mass coupling and active dry terms | Source contracts exist; the exact selected case must be pinned |
| Spatial operator \(F_\Delta\) | Staggered positions, transport order, pressure gradients, interpolation, metrics and all boundary/flux rules | Partial source/native comparisons exist; complete stock-WRF/C++ equality is not established |
| Initial and auxiliary inputs | Initial/base fields, prescribed coefficients/forcing and frozen stage references | Exact case hashes required |
| Time map | Effective mode 3, tableau, schedule, precision/handoff, nonlinear and transpose residual norms/tolerances; accepted/failure rules | Supported-path contracts exist; chosen integrated window remains open |
| Numerical state | Predictor/cache policy, carry/publication and warm versus state-only cold restart | Must name the policy; state-only equality does not imply warm-trajectory equality |
| Forward accuracy | Evaluation times/window, owned/physical sampling positions, per-field units, physical weights/scales \(s_X\), and budgets \(\epsilon_X\) | **Window, scales and budgets unselected** |
| Derivative accuracy | Directions/cotangents and norms/scales, dot/Taylor/FD budgets, true transpose residual and replay checks for that same map/window | Existing budgets apply to their fixtures only; integrated-window coverage remains open |
| Observation/control problem | Observation operator, times, \(R\), control parameterization, any prior and admissible set | The completed rotating case specifies these; a broader instance is not selected |
| Efficiency | Same-accuracy comparator, timestep/stability requirement, whole-run cost definition and required improvement | **Optional target not adopted**; if selected, its criterion must be fixed. No equal-accuracy cost win is established |

The selected case record and this definition together form the completion
contract. Changing an entry creates a new version/instance; it does not silently
change the meaning of earlier evidence.

### Forward map and derivative target

Mode 3 is the four-stage, third-order IMEX ARK324L2SA pair, with an ESDIRK
implicit tableau whose first diagonal entry is zero. It is not a fully implicit
three-stage method. The authoritative coefficients are in
[`wrf_sdirk3_imex_ark324_coeffs.h`](../external/libtorch_wrf/sdirk3/wrf_sdirk3_imex_ark324_coeffs.h).
For the declared split \(F_\Delta=F_E+F_I\), stage states satisfy

\[
Y_i=Z_n+h\sum_{j<i}a^E_{ij}F_E(Y_j,t_j)
         +h\sum_{j\le i}a^I_{ij}F_I(Y_j,t_j).
\]

Which terms are active and assigned to each side is part of the case record;
optional HEVI and other modes cannot be substituted without changing it.
For an implicit root with assembled stage base \(B\),
\(K=F_I(B+\gamma hK)\), the first-order root derivative is

\[
(I-\gamma hF_I')\,\delta K=F_I'\,\delta B.
\]

The target is the derivative of accepted stages converged under the declared
residual tolerances, with the explicit-stage, boundary, handoff and observation
composition included. It is not a claim to differentiate every finite Newton
iteration, acceptance branch or warm-start decision. Stage residuals and
finite-difference uncertainty must be considered when assessing agreement.
An unsupported or failed forward branch cannot publish a valid gradient.

If numerical auxiliary state matters, write the algorithm as
\((Z_{n+1},S_{n+1})=\mathcal A_h(Z_n,S_n)\). A state-only map must specify how
\(S\) is initialized/reset. Boundary definitions include the lower and upper
W/PH rules and pressure/flux treatment, not just the X/Y boundary names.
In the completed rotating source case, the upper W/PH degrees of freedom are
dynamic; its top is not an assumed rigid lid.

### Accuracy and cost

Use separate field errors, for example
\(E_X(t)=\|X(t)-X_{\rm ref}(t)\|_{W_X}/s_X\), with units, positive scales and
sampling locations declared in advance. Report absolute errors and reference
signal amplitudes too. Do not hide perturbation error by dividing by total
300 K temperature or a large base column mass. A refined RK3 solution is a
numerical reference with its own resolution sensitivity, not an exact solution.

Same continuous equations, the same spatial \(F_\Delta\), and the same complete
map \(G_{\Delta,h}\) are different claims. Integrator-only comparisons require
matched \(F_\Delta\); unmatched paths must be reported as whole-path comparisons.
A small Newton residual alone does not establish a small forecast error.
Cost comparisons require both candidates to satisfy the selected field-error
budgets. Record whole-run wall time and solver work under a pinned compiler,
library, rank/thread and input setup; one small run does not establish a general
speed advantage.

## Completed restricted rotating inverse

The completed validation instance has a 484-component complex linear source,
an 8,480-component native stored state, and only two real controls. These are
three different dimensions. Write its basis as \(C\), distinct from a background
error covariance:

\[
Z_0(c)=Z_{\rm eq}+Cc,\qquad c\in\mathbb R^2,
\]
\[
y_k=\mathcal H(Z_{\rm eq}+\mathcal P e^{t_kL_{\rm ref}}q_*),\qquad
J_h(c)=\tfrac12\sum_k\|\mathcal H(G_{h,t_k}(Z_0(c)))-y_k\|_{R^{-1}}^2.
\]

The two columns represent the real physical fields from one complex mode's
\(q,iq\) phases. The admissible controls preserve positive total theta, dry
mass and layer depth and valid forward/observation evaluations. Source dynamics
are linear; native propagation is nonlinear, and physical-height W interpolation
depends on PH. Therefore zero cost or exact coefficient recovery is not required
by model equality. Source rank two is not a global uniqueness theorem for the
nonlinear native objective.

This instance fixes dry/flat/unit-map/constant-f conditions, zero physical
diffusion and damping coefficients, a 16×12×8 grid at 2.5 km spacing,
105 W observations at 150/300 s, \(R=(3\times10^{-4}\,\mathrm{m/s})^2I\),
\(h=0.3125\) s and 960 steps. Its truth/start controls are (0.78,-0.36) and
(0.25,0.15). It uses no prior. Its 600/900 s forecasts use state-only cold
restarts. The [closure record](<(202610111426)_pr290_narrow_rotating_forecast_closure.md>)
is authoritative for producer/input lineage, boundary details and results.

The experiment is **closed**: forward/replay, two-control optimization and
unused-time six-field/divergence/vorticity improvement were verified. Its raw
control-gradient threshold applies only to its fixed basis, units and \(R\):
\(\nabla_cJ=C^T\nabla_{Z_0}J\approx0\) is not full-state stationarity. Rescaling
\(R\) or the controls changes that norm. Neither this experiment nor its residual
model/source difference is a reason to reopen its optimizer or add new waves.

## Existing evidence versus first-stage acceptance

| Gate | Existing evidence | Project-level conclusion |
|---|---|---|
| Defined forward operator and integrated accuracy | Source/RHS contracts, limited temporal/spatial studies and archived WRF runs | Partial; selected instance and quantitative target must be fixed |
| Same-map first-order derivatives and observation placement | Converged-stage, tile-step and fixed-trajectory JVP/VJP/FD/Taylor contracts | Supported fixtures verified; full integrated Fortran observation-window adjoint remains open |
| Replay and actual state improvement | Completed two-control rotating inverse/cold forecasts | Closed within that instance; does not qualify arbitrary initial-state assimilation |
| Same-accuracy cost/stable larger steps | PR #292 real WRF comparison and earlier archived runs | Open; reference sensitivity/operator gaps remain and no cost win is established |

See the [core supported-path contracts](../external/libtorch_wrf/sdirk3/README.md)
and [PR #292 WRF record](<(202610111626)_pr292_wrf_accuracy_cost_provisional.md>).
The latter corrects option-2 theta mixing and records improved T/PH/W differences,
but retains U/V/MU discrepancies and provisional RK3 references. It does not
supply a complete integrated trajectory-adjoint certification.

The [same-state U/V/MU record](<(202610120016)_pr295_same_state_uvmu_rhs_snapshot.md>) now preserves complete component
comparisons at two common inputs. Small U/V residuals remain; it does not certify
full spatial-operator equality or select quantitative project error budgets.

First-stage completion requires the selected instance's forward, same-map
JVP/VJP, replay and observation/state-improvement gates to pass their declared
budgets. If efficiency is included in its acceptance contract, its same-accuracy
cost/timestep criterion must also pass. Scope expansion needs an explicit new
target; it cannot revoke a completed restricted validation instance.

## Next action and validation of this definition

Fix the still-unselected forecast window and physical error budgets, then map
existing evidence to that instance and address its first unmet gate. Adopt an
efficiency target only through an explicit scope decision; if adopted, fix its
same-accuracy cost/timestep criterion too. Do not automatically add another wave, optimizer run, Hessian test,
moisture, terrain or MPI experiment. No project completion percentage is implied.

This documentation was checked against main
`35037bd2093e17beb23d27857a6d9c81ce08aed0` (including merged PR #292), the
production tableau, supported derivative contracts and preserved case reports.
Green and Red teams checked the definition, supported-path claims and scope
boundaries against those sources and found no blocking issue.
No production source/configuration was changed and no model build, numerical
integration or new WRF/RK3 field/runtime comparison was performed for this update.
