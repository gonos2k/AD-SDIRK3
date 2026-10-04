# Observation-window and withheld-forecast review checklist

Local timestamp: 2026-10-03 15:58:12 JST.
Source base: merged feature stack `dd5932d` (tree equals reviewed PR #273
`0ca9986`). Main is `4285bbd` (#271); the accumulated feature source is explicit.

## Findings already closed

The review accepts native multiple-output cotangent injection, fixed physical R,
recoverable-only line-search rejection, BFGS convergence in the strong twin,
and native nonzero neutral hydrostatic background. Do not reopen them or infer
full WRF/MPI qualification from them. Information gain is not condition-number
improvement; the background linear covariance is not an exact nonlinear posterior.
A strong MAP residual may be consistent with the prior and is not forced to zero.

## Active ordered checklist

- [x] Reuse the six physical W/PH/MU controls, fixed per-point R, and the same accepted FP64 forward/adjoint; parameterize only window length and time increment.
- [x] Run eight- and sixteen-step windows at h=.25 s with two observation times at half/end of each window; require cost/gradient convergence, physical states and repeatability.
- [x] Report prior and observation gradient components separately, including their cancellation and independent total directional derivative.
- [x] Predict beyond the assimilation window at physical withheld times (3 s after a 2 s window; 6 s after a 4 s window) and compare analysis/background to the same truth in W, PH, MU and theta.
- [x] Use physical cell-area/layer-mass dry-mass and mass-weighted-theta budgets; distinguish a vanishing signal from predictive improvement.
- [x] Generate one fixed observation dataset and compare h=.25 and h/2=.125 at identical physical times (2/4 s map to indices 8/16 versus 16/32), preserving the exact R and observations.
- [x] Separate numerical differences in cost, gradient, controls and forecasts from solver error with an appropriate tighter-solve comparison; do not require complete truth recovery in the unit-prior MAP.
- [x] Add one monotone dry stable theta profile, using the same native EOS/PH equilibrium construction; require positive discrete N², physical states and rest-state persistence.
- [x] Verify coordinate-correct initial restoring buoyancy response with paired vertical pulses and amplitude scaling, then run the same inverse/forecast on the stable case.
- [x] Measure retained-graph peak memory at these window lengths; only introduce recomputation if the measured size becomes limiting.
- [x] Refresh source-matched Graphify, review with Green/Red, archive exact source/binary/inputs and run affected local tests and exact-source remote CI before PR delivery.

## Mathematical and statistical definitions

For Z0=Zb+Lv and accepted outputs at fixed physical times, use
J=.5*v^T*v + sum_t .5*(H(Zt)-yt)^T R^(-1)*(H(Zt)-yt).
Per-point std_R=sqrt(105)*.001 m/s; adding times does not add mean normalization.
Reverse adds each output's cotangent BEFORE its local DG^T, and returns
v+L^T lambda0. Record gb=v and go=L^T lambda0 separately. The same data/model
at one h is an algorithm twin; the fixed-data h/h2 comparison checks how much
integration accuracy changes the solved inverse.

Cost reductions are assessed within each experiment; different observation
windows have different data/costs. Forecast diagnostics compare states at the
same physical withheld times and keep each variable's units. A constant-theta
neutral background can have no distinguishable theta forecast signal; do not
force a relative improvement over roundoff or silently pass an unresolved signal.
Budgets use the fixture's physical area and dry pressure-layer weights, not a
packed unweighted sum or a quantity called heat that is actually integral theta.

## Meteorological stable-column contract

Choose a horizontally uniform monotone mass-level theta profile and prescribe
ptop/total dry mass/sigma pressures. Invert the current native alpha relation
for PH layers; use imported-FP32 geometric allowance separately. Diagnose
N²=g/theta_bar*(theta[k+1]-theta[k])/(z_m[k+1]-z_m[k]), with mass-center
heights from full w-level PH. Kv=0 is needed if otherwise scalar vertical
mixing changes the stratified profile under the supported zero-source contract;
Kh can remain nonzero. This is a distinct declared fixed-coefficient case.

Theta is stored on moving eta surfaces. A vertical pulse can have theta_dot_eta
near zero even when its Eulerian tendency is nonzero. Use
  theta_dot_z = theta_dot_eta - (PHdot/g)_mass * theta_z,
then bdot=(g/theta)*theta_dot_z ≈ -N²*W_mass. Verify that PHdot/g matches the
physical W interpolation in the stated pulse setup. Paired amplitudes/signs
must be resolvable and scale consistently. Do not mistake a stable-minus-neutral
W difference for pure buoyancy: the theta change also changes the acoustic
operator. Two-to-four-second windows cannot establish a full Brunt period or
actual weather forecast skill.

## Validation reuse and boundaries

The six-file Graphify corpus byte-matches this feature tree before editing.
Template/inline extraction gaps were checked in authoritative source.
No new WRF/RK3 forecast is yet performed. Existing same-setup #269 evidence is
preserved: 240 s RMS differences U=.0084226, V=.00095424, W=.00062571,
PH=1.3330, T=.041426, MU=.44902 in output units; step-log carry7.168 s versus
archived RK3 .2617 s is not equal-accuracy cost nor wall time.
MPI, moist/diagnosed K, terrain/geostrophic and real-observation windows remain
separate physical/algorithm expansions. No new deployment/API/large replay
framework is a prerequisite for the active bounded checklist.

## 2026-10-03 18:42 JST — numerical candidate and independent reviews

Numeric source is `a3f0ee3` on `agent/fp64-window-forecast-20261003`.
The bounded 8/16-window and stable pilots pass. The fixed-data refinement,
119-test final rebuild/run and same-setup WRF checks are still in progress;
unchecked items above are not completion claims.

### Two exposed stage-quality/AD defects

The first eight-step stationary background stopped at step 5/stage 2 despite
scaled Newton residual 5.78e-19: initial WRMS residual was at construction
roundoff, giving relative growth 158.2 against the unchanged 100 cap.
The equivalent stage equation G=Y-B-aF=a(K-F), a=h*gamma>0, gives a
componentwise FP64 construction estimate gamma5*(|B|+|Y|+|aK|+|aF|)/a.
It uses the same WRMS weights as both residuals, only for internal FP64;
it does not bound nonlinear RHS/Jacobian evaluation error or loosen Newton.
Resolved residual growth still fails the 100 cap (unit falsifier: 200).

A floor-only candidate then exposed a 0.64% W0 FD/adjoint plateau at two
FD widths. Legacy post-root damping scaled tiny converged K under NoGradGuard,
erasing the custom implicit-root gradient at the baseline while perturbed
states kept it. The final candidate retains converged roots below construction
precision. Resolved post-root damping with retained adjoints fails explicitly;
the non-retained legacy path and FP32 calculation remain unchanged.
The original .02 FD width was restored; 8/16 windows now pass without
relaxing the existing derivative budget. Pre-floor failure, floor-only AD
failure and final pilot binaries/patches are archived separately.

### Ownership, physical diagnostics and scope

Fixture ids=1, ide=nx+1=9 and nx=8 do not satisfy the production packed-periodic
predicate. All 8x6 mass cells are owned; the 7x5 W observation pattern is a
subset, not a budget mask. An exploratory subset mass drift was not a
production conservation failure: the full-owned RHS mass sum is zero.
Field RMSEs now cover actual owned cells and all physical vertical levels;
the fixed-R 105-site observation operator is unchanged.

Dry mass/weighted theta use A*|deta|*(c1*(MUB+MU)+c2)/g, with factored
endpoint product changes. The all-theta threshold is a deliberately weak
engineering drift gate, not an exact or arbitrary-profile RK conservation
proof. Stable Kv=0 excludes vertical scalar mixing and the flat fixture
excludes terrain. Initial Eulerian buoyancy tendency is verified, not a full
gravity-wave period. Withheld same-model truth measures synthetic forecast
improvement, not independent operational weather skill.

Green budget/meteorological review and Green fixed-data review found no
blocker. Red stage/AD review found no blocker; it notes no focused runtime
falsifier yet forces resolved retained-adjoint damping rejection, although the
production check is explicit and floor math/resolved growth are unit-tested.
The gamma5 estimate retains its construction-only wording.

### Completed fixed-data pilot (same observation times/data/R)

The 16×.25 and 32×.125 inverses both use the exact fine-generated W pair at
2/4 s and sigma_R=.010246950765959599 m/s. Control difference is 1.52003e-7,
objective difference 1.19184e-8 and background-gradient difference 3.95149e-4.
Both baseline and final tighter 1e-11 solves produce exactly identical states,
costs and gradients to their 1e-10 counterparts. The temporal gradient signal
is resolved; solve-induced change is zero. The 6 s coarse/fine analysis
forecast differences are W1.00731e-8 m/s, PH5.51048e-7 m²/s²,
MU3.34533e-7 Pa and theta4.99766e-14 K (theta is unresolved).
Per-window field RMSEs compare each window's regenerated model truth, not a
single shared withheld truth. The fixed-data forecast result is the direct
coarse/fine endpoint difference. Two h values do not establish a new order.
Pilot: 1748.51 s wall, RSS1,222,918,144 bytes, peak footprint1,171,048,080
bytes, no swap. Retained graphs remain O(N); no limiting memory shortage was
measured, so no recomputation engine was added.

### Actual affected-component WRF regression

Matching clang22/O2/ABI1 C++ clean rebuild was incrementally relinked against
preserved WRF/Fortran objects. Original objects/executables/inputs were not
modified. The new em_b_wave strict h15 s/T240 s internal-FP64 carry run
completes all 48 implicit stages, max scaled residual5.1523e-9; three frames
are finite with positive dry column/layer geometry. FP32 OFF preserves the
archived first-failure marker exactly at Stage2 residual1.274e-6 (expected
failure, not successful FP32 qualification). New step-log sum6.49975 s vs
archived RK3 .26174 s is not equal-accuracy or wall-time performance.
Final RMS vs same-input RK3: U.0084225882, V.00095424146, W.00062570814,
PH1.3329873, T.041425747, MU.44902415 in output units. Prior U/V/PH/T/MU
are array-exact; W RMS difference is2.49563e-12 m/s. Exact source/library/
executable/input hashes and all-frame checks are in
`docs/evidence/fp64_window_forecast/wrf_validation.json`.

Final local validation (2026-10-03 19:14 JST): numeric source a3f0ee3,
119 registered/119 passed/0 skipped/0 failed on macOS, including actual MPS.
New window/refinement/stable cases take405.49/1752.63/109.58s. Suite wall2083.30s.
Exact per-test results and stdout hash: `docs/evidence/fp64_window_forecast/local_suite.json`.
Earlier pending snapshots above are superseded. Remote Linux final gate remains pending.

## Final completion — 2026-10-03T20:13:18+09:00

The ordered bounded checklist is closed. Final local source a3f0ee3 passes all
119/119 tests (MPS executed on macOS). [Remote CI37114137980](https://github.com/gonos2k/AD-SDIRK3/actions/runs/37114137980)
on ddc59e1 succeeds in core-linux, fast-contracts, build-contract-negatives
and required, including CTest/Fortran ABI/install. Its source tree d713d007
is EXACTLY the tree of main-integration commit ea7f56d; that merge changes
ancestry only, with no file changes. The final evidence commit adds only docs.
Raw CI counts/measurements are retained in the CI receipt. Local source,
binary, input, WRF comparison and team receipts are under
`docs/evidence/fp64_window_forecast/`; raw archives remain outside the
worktree in Documents/AD-SDIRK3-worktree-archive/20261003/fp64_window_forecast.

No whole-WRF optimization, real observations, long gravity-wave period,
exact arbitrary-profile theta conservation, moist/diagnosed-K/MPI or general
boundary qualification is inferred. Those are future expansions, rather
than failures of this explicitly completed short dry single-tile checklist.

Raw Linux CI summary:119 registered,118 passed,1 MPS skip,0 failed;
new window/refinement/stable tests596.12/2807.43/164.28s; total4345.07s.
All seven Fortran/C++ parity steps, ABI and install/header/C-ABI smoke pass.
See `docs/evidence/fp64_window_forecast/ci_numeric.json` for exact job/step,
log/artifact and measurement provenance. This supersedes prior remote-pending
snapshots; macOS MPS ran, while the Linux MPS skip is explicit.
