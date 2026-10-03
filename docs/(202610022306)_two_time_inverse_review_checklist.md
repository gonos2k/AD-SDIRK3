# Two-time fixed-R inverse: review closure checklist

Local timestamp: 2026-10-02 23:06:12 JST.
Base: integrated main `0b3798e2` (PR #270). Scope: the established six-control,
dry fixed-K, single-CPU-tile, four-step FP64 trajectory. No new control variables,
checkpoint framework, observation I/O system, or physical option is added.

## Checklist

- [x] Fix the statistical contract: physical per-point R, unchanged between times and independent of observation count.
- [x] Preserve the old terminal result with std_R=sqrt(105)*0.001 m/s and R=std_R^2 I; do not average the two times again.
- [x] Replace catch-all line-search rejection with explicit physical/normal-nonconvergence rejection; propagate contract/adjoint/programming errors.
- [x] Inject output cotangents at Z2 and Z4 before the respective local pullbacks; keep terminal API compatibility.
- [x] Validate terminal-only, zero, combined/separate, omitted/mis-timed intermediate cotangent behavior and whole-cost FD.
- [x] Compare terminal versus stacked observation information/Hessian spectra at the same R and measure the weak MU posterior variance.
- [x] Run the actual two-time MAP optimization with Armijo, positive-state checks and final gradient convergence.
- [x] Check independent linear MAP, deterministic cost/gradient/checkpoints and preserved terminal regressions.
- [x] Tie source/executable/input evidence, Graphify refresh and Green/Red review to the exact candidate.

GitHub CI is recorded by final PR checks rather than a stale operational status
in this scientific closure list. Multiple-time 4D cost is the present milestone;
stronger nonlinearity, longer windows, moist/diagnosed-K/MPI are later work.

## Fixed statistical and mathematical contract

The old terminal mean loss had R=105*(1e-3)^2 I. Reuse that physical covariance:
std_R=sqrt(105)*1e-3 m/s. Each time uses
`ell_n=0.5*sum(((H(Zn)-yn)/std_R)^2)`, with no additional division by the number
of observations or times. Thus adding the step-2 observations adds A2^T A2 to
the terminal information at fixed precision; it does not rescale A4.

For accepted outputs Z1,...,Z4, cotangent slots are `[undefined,g2,undefined,g4]`.
The reverse initializes lambda4=0, then at descending output n adds gn and
applies the local `DG_(n-1)^T`. Initial prior gradient is added only in control
space: `v+L^T lambda0`. SVD values describe rotated combinations of controls;
posterior MU diagonal variance is a separate diagnostic.

Only explicit physical-state inadmissibility and returned SOFT_NO_PROGRESS /
HARD_STAGE_ABORT outcomes are candidates for line-search rejection. A fatal
input/internal status or a thrown contract, autograd or programming error must
escape. No exception-string matching or clipping is used to hide invalid trials.

## Evidence status

Implementation and the two-time inverse execution pass. Measured results:

- Fixed per-point std_R: 0.010246950765959599 m/s at both times.
- Two accepted alpha=1 updates; J: 0.9243310227405354 -> 0.004952585244300644.
- Data term: 0.9243310227405354 -> 0.0002979122530278054.
- Control-gradient norm: 19.648190091189843 -> 8.891090827329066e-9.
- Physical W RMSE over 210 samples: 0.000961421355463116 -> 0.00001726013479170442 m/s.
- Independent linear-MAP gap: 6.534206148650949e-6 under the predeclared absolute unit-prior budget 0.01.
- Weakest singular value: terminal 0.824762 -> two-time 0.922838; stacked condition 16.501.
- MU marginal variances: 0.5951195037 -> 0.5400327192 and 0.5944545696 -> 0.5394507003.
- Information-gain minimum eigenvalue: 0.1713984483 (positive).
- Missing intermediate cotangent error: 7.4189963; wrong-time error: 1.4239108; common FD norm budget: 0.0098241.

Costs from different observation sets are different objectives: only each
within-run reduction is compared. Posterior variances and sorted singular values
are distinct diagnostics, not assignments of singular vectors to MU modes.
The state checks, independent FD, terminal/zero/per-time equivalence, malformed
cotangent rejection, and deterministic cost/gradient/checkpoints all pass.
The terminal/adjoint/ABI related regressions pass 5/5, final FP64 family passes
3/3, and the final verbose two-time replay passes in 56.21 s.
All 114 registered local CTests pass, zero failures, in 240.99 s.
Production Make archive and Make/CMake membership parity pass; ratchets pass.
The exact CTest inventory and all current documentation claimants agree on 114.
Green and Red final source/evidence review found no blocker for the declared scope.

Before editing, the focused graph
corpus byte-matched the owning solver/header/test/fixture at this base. Its graph
is navigation, not a mathematical proof; member-call extraction gaps are checked
against source. No new WRF/RK3 model run was performed. Forward equations are unchanged;
retained #269/#270 same-setup RK3 fields/runtime and WRF/time-order evidence are
previous evidence, not a new multi-time whole-WRF assimilation result.
The focus is the native one-second dry six-control inverse. Stronger nonlinearity
and observed Armijo backtracking remain a subsequent experiment.

Implementation source: `7557c84a6c385b8813a009eb58fcf40901150c69`.
The final focused graph has 402 nodes / 23308 edges and verified owning
solver/header/test/fixture source bytes. Complete machine records are in
`docs/evidence/fp64_two_time_inverse/results.json`.
