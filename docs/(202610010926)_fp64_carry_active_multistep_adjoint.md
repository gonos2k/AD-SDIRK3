# FP64 carry: active multistep discrete adjoint

Local timestamp: 2026-10-01 09:26:25 JST.

## Context and scope

Numerical base: PR #268 HEAD `4c16c69e0e0857348f5fd77eb8a1c33cc343e2ff`.
The task is the adjoint of the same solver-owned FP64 recurrence, rather than
repeated FP32 publication/reimport. The first supported validation scope is dry,
one CPU tile/rank, fixed default option-2 coefficients, periodic X/symmetric Y,
ARK mode 3, and two or four accepted steps. The native fixture enables
nonhydrostatic pressure/buoyancy, curvature, current-W buoyancy, Kh=1000, Kv=10,
and uses the existing 8x6x4 hydrostatic base at 5000 m spacing and dt=0.25 s.
No new configuration option is introduced.

## Closure checklist

- [x] Preserve FP64 local input leaves and accepted output graphs; keep FP32 host publication separate.
- [x] Compose existing local step pullbacks in reverse order, without a global forward graph chain.
- [x] Add explicit FP64 initial bootstrap and detached clone checkpoint access.
- [x] Compare every internal checkpoint with explicit FP64 reentry under the same solver context.
- [x] Validate W/PH/MU directions of a final internal-FP64 W least-squares objective.
- [x] Check the diffusion ON-minus-OFF objective derivative against central differences at three perturbation widths.
- [x] Reject doubled active sensitivity and last-step-only reverse mutations.
- [x] Keep deferred activation after caller-owned views are published; resolve boundaries through the existing authoritative resolver.
- [x] Clear bootstrap ownership when a pending trajectory is canceled.
- [x] Complete C/Fortran FP32-cotangent to internal-FP64 conversion and registered-handle N=1/N=2 ABI regression.
- [x] Finish malformed bootstrap and sub-FP32-ULP metric-change rejection checks.
- [x] Complete local 112-test regression, Green/Red code review, and exact-source WRF forward regression.
- [x] Directly compare observation-only production-carry FP64 dumps with the existing four-step-size time ladder.

GitHub CI is verified against the final PR head; its run identifier and current
status are recorded in the PR rather than embedding a stale operational status here.

## Mathematical contract

For fixed context, Z[n+1]=G(Z[n]) is the internal FP64 map; Y[n+1]=cast32(Z[n+1])
is only the public state used to verify that the host did not change the state.
Each retained step uses a new detached FP64 input leaf and its own output graph.
Reverse composition applies all local DG transpose factors. This is a short
retained-graph implementation, not memory-bounded checkpoint replay.

The objective is one half the mean square of W/0.1 minus a deterministic target.
Directions are block-local normalized vectors scaled by 0.1 for W and 100 for
PH/MU; the cotangent uses the exact derivative of that objective. Perturbation
widths 0.02, 0.01, 0.005 are state perturbations, not time steps. The full derivative
budget is 5e-4 relative, and the active increment budget is 2e-3 relative plus a
specified FP64 objective-roundoff floor. Every active signal must exceed 50 times
that floor. This floor is not a universal bound on Newton error.

## Validation status

Numerical source commit: `a3507049050bac7fd9ccadb60f71b28f97f8556a`.
The 18 active derivative comparisons pass. The largest increment-relative error
is 1.60004e-4, and the smallest signal/FP64-roundoff-floor ratio is 367.25.
The three perturbation widths resolve W/PH/MU for both trajectory lengths;
Taylor ratios are approximately 4. These are native-library executions.
The registered C ABI N=1/N=2 calls return the same FP32 initial gradient as
the core FP64 pullback narrowed once. `Base_State_Checked_ABI` passes 20/20.
The new carry test uses absolute scaled Newton tolerance 1e-10, relative
Newton tolerance zero, EWT rtol 1e-6, and nominal Krylov tolerance 1e-8.
The committed-source CTest run passes all 112 registered tests in 199.61 s.
Production Make/CMake archive membership parity and both ratchets pass.
Green verified the focused Graphify corpus against the committed source bytes
and refreshed it (362 nodes, 9716 edges). Relationships were navigation aids;
numerical conclusions use the actual source and native executions.
The initial complete 110-test run had one failure caused by a changed legacy
error marker. The existing marker was restored; the targeted lifecycle rerun
passed. This was a diagnostic compatibility failure, not a numerical failure.

## Boundaries and next actions

No moist/supplied-K/MPI/time-varying-boundary adjoint, long-run bounded-memory
replay, or operational forecast qualification is claimed. Existing forward time
order evidence from PR #267-268 is retained; the present derivative tests do not
re-measure time order. The production WRF internal-FP64 ladder is now directly checked by an
observation-only clone dump, described below; the tile test alone would not
substitute for that evidence.

## Same-input WRF forward regression

The final production archive was incrementally linked into the existing WRF
validation objects. This is not a new clean build. Dry `em_b_wave`, one rank and
thread, dt=15 s, T=240 s, fixed Kh=1000/Kv=10, carry enabled completed all 16 steps.
A same-executable repeat was byte-identical at all three output frames. Against
prior PR #268 output, t=0 and t=120 were byte-identical; the final W field differed
by RMS 2.4956e-12 and maximum 1.1642e-10 (one FP32 ULP), while U/V/PH/T/MU were
exactly equal. Whole-file byte identity with the earlier executable is therefore
not claimed. Compiler/optimization settings differ; attribution of that tiny
change to a particular compiler transformation was not isolated.

The archived same-input RK3 reference was reused. At 240 s the RMS differences
are U=8.4226e-3, V=9.5424e-4, W=6.2571e-4, PH=1.3330, T=4.1426e-2,
MU=4.4902e-1, in each NetCDF variable's units. Selected fields are finite. Single
run step-log totals are carry=7.168 s and archived RK3=0.2617 s; these are not wall
time or an equal-accuracy performance comparison.

## Direct production-carry FP64 time-ladder receipt

A separate observer executable adds only a raw write of a detached contiguous
FP64 carry clone. It does not change the recurrence, state, or tolerance. Its
patch, archive, and executable are hashed separately from the untouched production
executable. All four 15 s runs at h=.46875/.234375/.1171875/.05859375, Newton
criterion 1e-10, have 24531-value final FP64 dumps byte-identical to the existing
`carrytight{h}_15` raw reference. Only the production carry key is added to the
older stored namelists. NetCDF outputs at 15 s also match for each h.

The direct dump orders are U=2.931/2.965, V=2.928/2.965, W=2.969/2.992,
PH=2.985/2.998, MU=3.014/2.997. The internal theta block has orders 2.391/0.087
at a tiny signal, so it remains unresolved in this WRF case. This receipt confirms
existing time-order evidence; it is not a new whole-WRF adjoint test.

## Evidence and final reviews

Green and Red reviewed the committed source and execution evidence with no known
blocker for this supported scope. Machine results and the WRF receipt are in
`docs/evidence/fp64_carry_adjoint/{results,wrf_validation}.json`. Full local logs
and source patch are preserved at
`/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20261001/fp64_carry_adjoint/`.
The WRF receipt identifies the distinct production/observer executables, archive
hashes, input hashes, reference paths, and the observation-only source patch.
The scoped graph is under `/private/tmp/sdirk3-fp64-carry-adjoint-graph`.

Finalized: 2026-10-01 13:10 JST. This PR targets main and includes the already
validated PR #268 carry implementation, so the proposed integration source is
the same source used for the new adjoint and forward checks.
