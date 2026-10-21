# State-dependent residual weights on the registered native time map

Local report start: 2026-10-21T14:56:41.134500+09:00 (JST, UTC+09:00).

## Problem and scope

PR313 closed the two-time linear diagnostic. This study connects its two
normalized diagnostics to a squared-residual cost on the same registered dry
240 s map, `dry_em_b_wave_e71_240s_h15over16_strict_v1`:

`h_m=q_m^T Z128`, `h_N=q_N^T Z256`,
`J=.5*((h_m-y_m)/sigma_m)^2+.5*((h_N-y_N)/sigma_N)^2`.

The numerical fixture fixes `y=[0,0]`, `sigma=[1,1]` in the dimensionless
diagnostic space. They are synthetic targets/weights, not observations or
estimated observation errors. Forecast error budgets are not used as sigma.
The original q vectors, six-field physical direction, and actual FP32 widths
±.01/±.03 remain fixed. Control starts after initialization/CQ. Actual H/times/R,
prior/control selection and optimization remain separate work.

The retained case is dry em_b_wave, physical 16×16×16, horizontal spacing
100 km and configured top 16 km, one MPI rank/one OpenMP thread. The step is
0.9375 s (256 steps); Newton absolute/relative tolerances are 1e-9/0,
Krylov 1e-7 and effective EWT 1e-6. Native divergence damping uses kdamp=.2;
option-2 diffusion uses KH=1000 and KV=10. PH advection=1, vertical scalar
order=3, the actual WRF CF coefficients, boundaries and continuous internal
FP64 state handoff remain those of the registered map. Cotangent handoffs
through the existing Fortran wrapper are FP32.

The midpoint is the original raw T-perturbation projection at 120 s; the
terminal is the original W projection at 240 s. Changing to total theta would
also require shifting the target to preserve the same residual problem.
Neither diagnostic is an air-temperature observation.

## Shared cost and actual caller

One NumPy function `objective_pair` computes h, residual, cost and
`alpha=(h-y)/sigma^2`; both native seeding and FD postprocessing use it. Each
perturbed state's coefficient is recomputed. The baseline values are
`alpha_m=-420.1114023998378`, `alpha_N=-11.718318303682182`, with cost components
88246.79521317923 and 68.65949193320643. These are caller values from the cached
primal states; the native runs must match those accepted states bytewise before
pullback. The driver's `local_terminal_c_dot` remains an unscaled W linear
diagnostic and is not this squared cost.

Actual initial seed is `Q32(alpha_N*q_N)`; new windows 256/224/192/160 yield the
new lambda128. The caller then injects exactly once
`Q32(FP64(lambda128)+alpha_m*FP64(q_m))`, followed by 128/96/64/32. Every handoff
uses the actual preceding Fortran FP32 return. Old upper cotangents are not
substituted for the newly weighted chain. The six primal/FD trajectories are
reused; no new perturbed forecasts or time refinement are requested.

## Implementation and provenance

Only the native test-driver guard now accepts external-cotangent prefix mode
at N=256 as well as shorter horizons. It is a two-line test-only change;
production equations, public ABI, headers, defaults, input, h, filters and
boundaries are unchanged. Only that C++ translation unit was compiled and its
archive member replaced in a new copy. Other 25 archive members and retained
Fortran inputs match; 16 required symbols are present.

Production base is `ea5c39b8eeee2f73d6fc23bc964df22c2be34a33` (tree equal to
PR313 head `ed4bddee09211fedc4c067ee7012e368cd81f01c`). Test CPP SHA256 is
`7a2b921f44a9356fd7e73efa7ca82499b7c816ab43822fae28dd23933eb72a86`;
new executable is
`fd8ba93955a7f45fef10eef7646a5e1554cb1b0ba3c8718514f97c1ad346e744`.
Input SHA256 remains
`e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`.
The retained AppleClang21/GNU Fortran15.2, Homebrew MPI5.0.9, Torch2.10 ABI0,
Homebrew netCDF4.9.3/netCDF-Fortran4.6.2 stack is bound by build receipts.

Graphify reused the verified production graph and refreshed only affected
caller/checker/collector code. Cross-language edges are source-confirmed navigation,
not proof of numerical correctness. The original dirty user worktree was not
modified; new work is isolated on `codex/native-residual-objective-20261021`.

## Frozen validation contract

Protocol b65b2b01... was fixed before new residual FD numbers were evaluated.
Each total/midpoint/terminal-reference component separately requires ≤1%
central FD/AD discrepancy, ≤1% central-width disagreement, and a resolved
objective-scale signal. The original h diagnostics retain their finite-width
one-sided check. Squared-cost one-sided gaps are diagnostic only: even affine
h=a+b*eta gives cost slopes a*b±.5*eta*b^2 despite an exact central derivative.
This distinction was reviewed before data, not introduced to fit results.

Full dAD uses the actual new Fortran FP32 g0. The midpoint check uses
`v^T(FP64(g0_new)-alpha_N*FP64(g0_terminal_old))`; it is a derived composite
increment, not an independently rerun midpoint-only VJP. The scaled old
terminal gradient must independently satisfy the terminal-residual FD guard.
Old whole-gradient combinations are postprocessing predictions only.
Local FP64 captures audit FP32 narrowing, not an independent global FP64 chain.

## Actual results

All eight native cases completed: 1152 accepted model steps, 3456 converged
implicit-stage records, and 1160 FP64 checkpoint comparisons against the
retained baseline. Every comparison is byte-equal. These are overlapping
prefix comparisons, not 1160 unique physical times. The actual terminal seed,
one accepted128 injection, seven handoffs, eight Fortran returns and local
FP64-to-FP32 casts all match their bound values. Prefix32 starts at control0
without inventing a suffix restart file.

| Component | Actual/derived directional AD | FD/AD relative error, eta≈.01 | FD/AD relative error, eta≈.03 |
|---|---:|---:|---:|
| Full cost, new native g0 | 122639.64441142204 | 2.4026840e-4 | 2.1588192e-3 |
| Midpoint derived increment | -6878.801095437097 | 1.8822579e-7 | 1.9768339e-7 |
| Scaled retained terminal reference | 129518.44550685913 | 2.2752050e-4 | 2.0444079e-3 |

The respective central-width disagreements are 0.00191901, 9.45761e-9 and
0.00181730. All component gates, signal floors and underlying linear-h
one-sided guards pass; `overall_pass=true`. Root independently recomputed
all six FD comparisons from the packet with scalar FP64 summation.

The larger full/terminal finite-width discrepancies are consistent with the
exact scalar identity `FD(J)=((hplus+hminus-2*y)/(2*sigma^2))*FD(h)`.
For the terminal term, using the finite-width mean residual rather than the
baseline residual contributes about 29.4743 and 265.333 to FD at the two
widths. This is an interpretation of the fixed results, not a correction to
the native seed or a new acceptance rule.

The eight cases sum to 8784.60 s wall time, including repeated prefixes and
pullbacks, with peak sampled RSS 4076847104 bytes (3.80 GiB). These are not
forward-only performance measurements. Raw ledgers/logs and executable stay
in the local evidence root; the repository preserves a 4,671,113-byte NPZ,
eight raw receipts, source/build/graph bindings, scalar CSVs and a portable
checker. Packet SHA256 is
`f630fdf5b07ea358b61eb903b759d7c7ce1766cafc62612cbf3ac88911b5112d`;
manifest SHA256 is
`a54e5828b932828d71c11d9641372ba4a5a590b9323a3cdb20c2b6b1a2522304`.

See [directional checks](evidence/native_residual_objective/directional_checks.csv),
[objective values](evidence/native_residual_objective/objective_values.csv),
[run summary](evidence/native_residual_objective/run_summary.json) and the
[portable packet instructions](evidence/native_residual_objective/README.md).
A normal checker exit only means recomputation succeeded; all component flags
and `overall_pass` must be true.

## Archived RK3 context

No new RK3 run or field/runtime comparison was performed. Reuse the preserved
same-input 16³, 240 s comparison in `native_terminal_adjoint/archived_rk3_context.json`:
U .0135072 m/s, V .00357188 m/s, W .000319814 m/s, PH .257214 m²/s²,
T .000111423 K, MU .504089 Pa. Stock RK3 uses a different filter policy; these
are descriptive distances, not accuracy truth. Observer-enabled coarse wall
872.94 s and historical stock analysis time1.93578 s lack comparable receipts;
no speed ratio is claimed. This work does not reopen forward24/24,
reference24/24, or the h=15 s candidate's recorded W failure.

## Next boundary

This is a numerical residual-cost caller check, one mixed direction and two
finite widths on the declared dry one-rank case. It does not certify all
directions, full-map JVP, initialization gradients, second global gradient
replay, actual observation-cost scheduling or assimilation performance.

Native computation and scalar audit completed: 2026-10-21T16:59:19.142863+09:00 (JST).

## Independent closing checks

The [Green numerical review](evidence/native_residual_objective/green_closing_numerical_review.json)
independently recomputed the scalar objective and derivative checks without
importing the shared helper. The [Red raw review](evidence/native_residual_objective/red_closing_raw_review.json)
independently checked eight raw runs, all 1160 baseline comparisons and the
raw-to-packet bindings. Both pass with no blocking findings in their stated
scopes. The new native full dAD differs from the old postprocessed prediction
by about 0.00642223; that prediction was not substituted for the actual result.

The [fresh-folder check](evidence/native_residual_objective/fresh_folder_check.json)
passes both synthetic suites and reproduces the portable result bytewise.
These native checks are separate from the repository's selected standalone
CTest/replay CI; CI does not certify this native observation-cost window.
