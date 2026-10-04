# Stable compressible wave, initial quadratures and forecast validation

Local timestamp: 2026-10-04T17:27:17+09:00.

Base: main `5201af93a8f41252956dda050ce43df690234e61` (accepted PR274).
Production correction: `ccee4100e34af655570ed16aabb25c8577216039`.
Branch: `agent/stable-wave-coupling-20261004`.

## Problem and smallest production correction

PR274's stable column establishes equilibrium and the initial Eulerian buoyancy
response. This work compares the coupled compressible dynamics with a separate
17-amplitude linear reference and estimates the initial temporal quadratures
using the existing continuous-FP64, multi-output discrete adjoint.

The independent reference exposed an old U pressure-gradient range defect:
for eight physical mass cells and nine U faces, the exclusive interior slice
ended at seven and left owned face seven uncomputed. A theta pressure-wave
probe returned zero there instead of approximately -0.0426634 m/s². The slice
now includes every face between physical mass cells. Packed periodic layouts
also wrap to the last unique mass cell rather than its storage alias. No
equation, Newton tolerance, global configuration or ABI was changed.

The new actual-RHS test prescribes pressure through an independently inverted
dry EOS, compares physical and packed owned faces against the Fortran HPG
formula, and requires resolved west-seam and last-owned-face signals. A
zero-face mutation exceeds its error budget. The separate Python test extracts
and compiles original Fortran routines, including physical/packed HPG rows.

## Independent wave definition

The four equal sigma layers have dry mass 84000 Pa, top pressure 20000 Pa and
theta 310/312/314/316 K. The horizontally uniform PH/EOS background is discrete
hydrostatic equilibrium. The wave has nonzero periodic-X wavenumber 1 on eight
5000-m cells, symmetric Y, fixed bottom W/PH and a dynamic pressure-free top
(`top_lid=false`). Its state consists of four U, four active W, four active PH,
four theta and one MU complex Fourier amplitudes; mass and U face phases differ.

`stable_wave_reference.h` transcribes the original EOS, horizontal pressure
gradient, `pg_buoy_w`, `calc_ww_cp`, scalar transport and `rhs_ph` equations.
It does not call the production RHS, JVP or Newton solver. The native centered
difference tangent is a separate consistency comparison, never the oracle.
Original WRF call ranges include the top W row; the bridge's mass-level count
must not be interpreted as a rigid lid. The reference transcribes Fortran's
`phi_adv_z=2` stencil, which the current ordinary native path implements, and
uses uniform-sigma bottom interpolation coefficients (2,-1.5,0.5). No new
native option selector was added. It does not qualify the separately defined
default Fortran `phi_adv_z=1` stencil. The
order-3 scalar correction vanishes on this exact linear theta/eta background.

The reference spectrum supplies the frequency without fitting production
outputs: select its largest positive oscillatory frequency below the profile's
Nmax. The selected omega is about 0.002109960322 rad/s, period 2977.868939 s;
the other two low-frequency branches and the acoustic branches are logged.
Opposite-frequency partners form two independent temporal quadratures. A
standing wave makes kinetic/available-potential exchange measurable; a single
travelling Fourier mode would keep horizontally integrated component energies
constant and obscure this check.

## Numerical and meteorological scope

The inviscid forward uses 298 ten-second steps (2980 s, at least one period),
plus an amplitude-halved control. It compares native modal phase/amplitude and
physical energy components at sampled quarter/half/full-period times against
the reference at those exact times. Eulerian pressure and theta include the
moving-eta displacement correction. Kinetic, acoustic and available-potential
energies use physical/staggered mass weights and the real-Fourier averaging
factor. The free-surface candidate is reported separately. Bulk energy is not
claimed to be an exactly conserved discrete invariant; top boundary work and
discretization contributions have not been independently closed.

The wave inverse starts from zero controls, observes W at 150/300 s with fixed
physical sigma=0.0003 m/s, and fits the two initial quadratures using the existing
multi-output adjoint, inverse BFGS and Armijo descent. Observations come from
the independent matrix, not the production forward. Directional differences
at 0.02/0.01/0.005 use the existing 2e-3 relative criterion; a 1e-11 Newton
control checks the 1e-10 baseline. The accepted analysis is propagated to an
unused later time and compared in modal phase, amplitude and W error.

All wave evaluations use the existing, validated `stage_damp_rel_threshold=1e6`
research setting, consistently in forward, FD and adjoint. The default setting
can reject the retained-adjoint combination at the zero background because a
tiny resolved post-solve damping would detach its converged-root graph. This
research setting changes no Newton convergence or stage acceptance tolerance;
it is not a new default or a claim that arbitrary post-damping is differentiable.
The default production retained-adjoint rejection remains intact.

## Fixed-data refinement decomposition

One extra fine forward at the coarse-optimal controls separates
Pcoarse(vc)-Pfine(vf) into Pcoarse(vc)-Pfine(vc) and Pfine(vc)-Pfine(vf).
Owned-cell block norms, their dot/cross term and vector/squared-norm closure
are reported. These terms can cancel. This comparison does not establish a new
time order and does not interpret reoptimized forecast differences as pure
integration error.

## Actual WRF validation

The affected ABI-1 C++ archive was rebuilt with clang++22.1.4, C++17/O2,
LibTorch2.10 and candidate-first linked to preserved GNU Fortran15.2/OpenMPI5.0.9
WRF objects. No Fortran or Registry source changed and no full WRF rebuild was
repeated. The same archived `em_b_wave` input completed dt=15 s/T=240 s with
continuous internal FP64, one rank/thread, 48/48 converged stages and maximum
scaled residual 5.152e-9. All three frames contain 174/174 finite floating
variables, with minimum dry column mass 89088.63 Pa and layer thickness 942.72 m.

Same-input, dt15 archived RK3 endpoint RMS differences: U 0.00210824 m/s,
V 0.000952211 m/s, W 0.000598811 m/s, PH 1.30420 m²/s², T 0.0414245 K,
MU 0.0995476 Pa. WRF step-log timing sums are 17.87177 s versus RK3 0.26754 s;
this concurrent-development single run is descriptive, not equal-accuracy
wall-time performance. The comparison is neither a truth-error measurement
nor operational weather or full-WRF adjoint qualification.

## Evidence and completion status

Raw logs, binaries, original sources, inputs and receipts are archived under
`/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261004/stable_wave_coupling`.
The WRF receipt includes the exact production/source, archive, executable,
Fortran objects, inputs and output hashes. The standalone build uses Apple
clang21 Release/O3 and the same LibTorch ABI; compiler distinctions are recorded.
The cached Graphify corpus was byte-matched before editing and refreshed with
the affected source, tests and reference. Its Fortran/preprocessor/template
extraction gaps are navigation limitations, not numerical evidence.

Final portable wave runs, post-correction refinement and Green/Red reviews
are complete. The exact 123-name inventory has passing coverage across the
core partition, two corrected-test reruns and the three separate long cases;
this is not a claim of one all-123 local invocation. The receipts in
`docs/evidence/stable_wave_coupling/` preserve the two initial failures and
their controlled diagnosis. GitHub CI is a separate, post-publication check.
MPI, moist physics,
diagnosed K, terrain, general time-dependent boundaries and real observations
remain later scope; no additional platform or checkpoint framework was added.

## Completed measurements and regression diagnosis

Completion snapshot: 2026-10-04T18:57:55+09:00, before PR publication.

The portable registered wave route compares all 17 source-derived matrix rows
with the production tangent (relative Frobenius discrepancy 3.94884e-10).
At 2980 s, modal phase error is -2.02214e-6 rad, amplitude 0.999976026 against
reference 1, and state discrepancy 1.23000e-4. Halving the initial amplitude
gives reference discrepancy 6.85958e-5; A versus twice A/2 differs by 5.81381e-5.
The maximum sampled native/reference energy-component discrepancy is 0.00171064.
Pressure and mass-coordinate buoyancy both contribute to the quarter-cycle
W acceleration (norms 7.24132e-5 and 9.70679e-5 m/s²). The phase and amplitude
are measured from both temporal quadratures, not the argument of a real
standing-wave projection.

The data-only, two-parameter wave least-squares inverse (not the earlier
six-dimensional MAP prior) recovers (0.780000341,-0.359991234) from truth
(0.78,-0.36). Seven accepted updates reduce cost 28881.5866 to 5.51750e-5 and
gradient norm 67296.5812 to 1.47589e-7, below the 1e-5 stop criterion.
Directional FD relative discrepancies are 5.12e-8/1.89e-8/7.23e-9. The tighter
Newton control has identical gradient. The final source/executable snapshot
was retained before its repeated inverse run (376.35 s, peak RSS 622880 KiB).
The 0.266-MiB checkpoint-state payload excludes retained graphs and is not a
claim of a memory-bounded recomputation algorithm.

The unused forecast starts from the accepted native Z300, rather than a
reference-propagated substitute. At 900 s, modal phase discrepancy is
1.09668e-5 rad, amplitude discrepancy 7.26651e-6, and full sampled physical W
residual 2.99157e-4 relative to independent truth (background relative residual
1). The physical W score includes all sampled native modes; Fourier projection
is used only for modal coordinates.

The new fixed-control decomposition closes for all six owned-cell blocks.
For W, integration and reoptimization norms are 2.03217e-7 and 1.11901e-7,
while the combined norm is only 1.56450e-7; their dot product is -1.46713e-14.
The cross term explains the partial cancellation. Two time increments remain
a sensitivity experiment, not a new order estimate.

Restoring the missing pressure-gradient face exposed two test assumptions:

- The strong inverse's elementwise 1e-12 superposition comparison failed in
  V entry 315 by 5.9%, although its vector discrepancy was 2.30728e-10 against
  summed adjoint norms 1.71765e6 (relative 1.34e-16). Both output contributions
  at that entry have the same sign. A source-isolated 1e-12 transpose tolerance
  control gave exactly the same result as 1e-8. The check now uses the same
  1e-12 relative criterion at the operand-vector scale used by the normalized
  transpose solve. No production tolerance, FD/Taylor gate, time-placement
  falsifier or independent-shorter-trajectory check changed. Rerun passed.
- The coarse N4→8 U order was 4.5145, outside the old upper bound. Rather than
  widen it, the handoff test retains that diagnostic and extends to N64.
  Both fine ratios on N8/16/32/64 pass the unchanged 2.6–3.4 bounds in all six
  blocks (range 2.9668–3.0325). Same-N32 Newton 1e-9/1e-11 endpoints and first
  states are bit-identical. Existing local U/theta fourth-order bounds also
  pass (U 4.228/4.087, theta 3.970/3.992). Rerun passed.

The paired same-executable WRF FP32 run still rejects the first timestep's
Stage 2 (terminal Newton iteration 9, scaled residual 1.2970e-6). Only the
two FP64/carry switches differ; all solver criteria are unchanged. Its seven
accepted and three rejected counts are Newton trial steps, not WRF timesteps.
