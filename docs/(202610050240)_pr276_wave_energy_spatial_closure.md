# PR276 wave energy and spatial closure

Local timestamp: 2026-10-05T02:40:17+09:00.

## Scope and result

This follow-up closes the dry-wave energy diagnostic, fixed-geometry source
spatial convergence, and four-control two-mode inverse checks. No production
solver source was changed. The work adds standalone source-reference checks
and extends the native carry-adjoint test. It does not claim a production
conservation repair, a native fine-grid qualification, or exact recovery of
truth coefficients beyond the precision printed by the current native log.

## Energy budget

The independent Python reference maps the packed source state to Eulerian
`(u,w,p_E,b,zeta_top)` and derives kinetic, acoustic, available-potential, and
free-surface weights from the dry equations. For the moving constant-pressure
top it uses `p_top = rho_top g zeta_top`, `zeta_top,t = w_top`, and EOS surface energy
whose rate cancels the physical top pressure-work flux. The EOS
boundary density is used; the last mass-cell density remains a separate
candidate. Analytic theta gradient, imported FP32 PHB geometry, and the legacy
finite-difference theta gradient are kept distinct.

At the exact quarter period, t=744.467 s, the source mode reproduces the
following integrated diagnostic budget (real-Fourier physical weights):

| Quantity | Change / integral |
|---|---:|
| Bulk diagnostic | +16.684 MJ (+2.9162%) |
| EOS top-surface energy | +1.321 MJ |
| Physical outward top pressure-work flux | -1.321 MJ |
| Bulk change minus boundary contribution | +18.005 MJ |

The remaining term partitions into U work (~0), interior W (+2.953 MJ),
special top W (-0.030 MJ), pressure conversion (+12.007 MJ), buoyancy conversion
(+3.075 MJ), and the small mass/geometry metric term (~52.6 J). Independently
assembling the Eulerian pressure and buoyancy tendencies checks the conversion
commutators; the partition is not only a renamed endpoint residual. With the
same source trajectory and only analytic theta_z diagnostic weights substituted,
the bulk percentage becomes 0.4401%. Legacy FP32 PHB versus analytic geometry
changes that sensitivity by about 7.6e-6 percentage points. These controlled
comparisons separate diagnostic-gradient and geometry effects. Independently derived top work and EOS surface storage close their
boundary identity, but top flux alone does not explain the measured bulk
change. The source-discrete matrix is compared with a separately derived
Eulerian conservative weighted-adjoint matrix. Its weighted skew-adjoint
identity supplies the conservative metric reference; the source-minus-reference
row powers expose interior stencil, metric, and coordinate-conversion
contributions. The row partition and integrated matrix budget close in the
standalone contract. These are differences between two explicit
discretizations, not a fitted invariant or a claim that the production RHS has
an energy bug. Algebraically, with x=Tq and physical/source weights H,
`C_source=A_source* H + H A_source` partitions into the conservative comparator
metric term and `DeltaA* H + H DeltaA`; `DeltaA` is checked by explicit
Eulerian pressure/buoyancy tendency rows. The energy-compatible comparator's
natural and blockwise weighted-adjoint cancellation is below 1e-12, and removing
the physical pressure-stratification exchange fails that contract. A source-wide
physical total-energy conservation theorem remains outside this result.

## Spatial mode check

The source-transcribed operator holds `Lx=40 km`, `Ly=30 km`, `ptop=20 kPa`,
dry mass `84 kPa`, and the continuous `theta(eta)=317-8 eta K` profile fixed.
The same one-node-W mode is followed by phase-independent physical-profile
overlap. The independent Eulerian BVP uses the same compressibility and free
surface. Horizontal and vertical refinement are reported separately in the
contract artifact; the coupled sequence is:

| Coupled `(nx,nz)` | Source frequency (rad/s) | Relative error to continuum |
|---|---:|---:|
| `(8,4)` | 0.00210996035 | 8.442% |
| `(16,8)` | 0.00225250780 | 2.256% |
| `(32,16)` | 0.00229120965 | 0.5767% |
| `(64,32)` | 0.00230115676 | 0.1450% |
| `(128,64)` | 0.00230366282 | 0.0363% |

The continuum frequency is `0.00230449918 rad/s`; the observed coupled orders
approach two (1.904, 1.968, 1.991, 1.999). This qualifies the source-discrete
modal contract and its continuum limit only. It does not qualify a native WRF
fine grid or a forecast.

## Native two-mode inverse

The archived native run independently observes two branches at
`0.002109960322` and `0.000987212995 rad/s`. The 210 physical-W observations at
150 and 300 s identify all four quadratures (rank 4, condition number 17.97).
The 30-step FP64 carry-adjoint inverse passed its directional finite-difference
check (relative error `1.43e-8` at epsilon 0.01; `3.57e-8` at 0.02), converged
in 11 updates, and had one update requiring backtracking to alpha=0.25. Cost fell from `34815.8972` to
`0.000861601`; final gradient norm was `3.05e-7`. Forecasting the accepted
native Z300 analysis to Z900 gave 0.6375% relative physical-W residual against
the independent observation state. The log prints recovered controls to four
decimal places, so exact truth-coefficient recovery is not asserted. The run
took 349.99 s. Its pre-run receipt archives the test/reference sources,
executable and library hashes under
`/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261005/wave_energy_spatial/two_mode_native/`.

## WRF context and remaining checks

No new WRF `em_b_wave` run or same-setup RK3 comparison was made for this
follow-up. The existing PR275 receipt is reused: production producer
`ccee4100e34af655570ed16aabb25c8577216039` (source SHA256
`28588df9ea75...`), executable SHA256 beginning `529c1709a297`, one rank,
`dt=15 s`, `T=240 s`, and 48/48 converged stages. Against the archived RK3
reference, endpoint RMS differences were U `0.00210824`, V `0.000952211`, W
`0.000598811 m/s`, PH `1.3042 m²/s²`, T `0.0414245 K`, and MU `0.0995476 Pa`.
The recorded timing sums were SDIRK3 `17.87177 s` and RK3 `0.26754 s`; this is
one concurrent-development run and is not an equal-accuracy performance claim.

The source-spatial contract is
[`docs/evidence/wave_energy_spatial/spatial_convergence.json`](evidence/wave_energy_spatial/spatial_convergence.json); the native two-mode log
is [`docs/evidence/wave_energy_spatial/two_mode_metrics.log`](evidence/wave_energy_spatial/two_mode_metrics.log) (raw log SHA and archive in the execution receipt). Local affected checks passed: new energy/spatial contracts (0.50/1.31 s),
unchanged-budget single-mode inverse (272.39 s), actual-RHS horizontal PGF and
compiled Fortran source-row checks (0.31/1.45 s). The registered/expected CTest
sets both contain exactly 126 unique names. The mixed-mode executed source is
archived; its only subsequent numerical-source difference is console formatting
of recovered controls. Full exact-head CI will rerun the mixed mode. Green packaging, Red scientific and Red mixed-mode reviews passed. Remote
CI has not yet been dispatched; submission/CI status will be maintained on the
new PR so the evidence snapshot remains tied to these measured files. MPI, moist physics, terrain, and general-weather
qualification remain outside this result.

Final local closure timestamp: 2026-10-05T02:50:57.852064+09:00. Affected checks and independent
Green/Red reviews pass. See [validation receipt](evidence/wave_energy_spatial/validation_receipt.json)
and [energy results](evidence/wave_energy_spatial/energy_budget.json).
