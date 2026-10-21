# Native two-time adjoint on the registered dry 240 s map

Local report start: 2026-10-21T09:36:27.736327+09:00 (JST, UTC+09:00).

## Context and change

PR312 closed the one-terminal-objective check. This work adds exactly one fixed
linear T-perturbation objective at accepted step 128 (120 s), keeping its W
terminal objective and mixed control direction. The control is the solver-entry
state after initialization/CQ. Production equations, ABI, tolerances, timestep,
inputs, boundaries and filters are unchanged; no rebuild was needed.

The registered map is `dry_em_b_wave_e71_240s_h15over16_strict_v1`, dry
`test/em_b_wave`, physical 16×16×16, dx/dy 100 km, 240 s, h=0.9375 s,
Newton abs=1e-9/rel=0, Krylov=1e-7, effective EWT=1e-6, MPI=1/OMP=1.
PH1, vertical scalar order3, actual CF, kdamp0.2 and diff_opt2 are retained.

`Jtwo = q128^T Z128 + qN^T Z256`. The midpoint vector is
`cast32(c/||c||2/1 K)`, with `c=cos(.23(i+1)+.19(j+1)+.07(k+1))`
at the 4,096 physical T owners and zero in all other packed slots. The old qN,
physical W mask, v and actual FP32 eta values are unchanged. Values are promoted
to FP64 for objective/FD arithmetic. The raw T perturbation is used without a
+300 constant; this is a signed numerical diagnostic, not an observation cost.

## Reuse and new calculation

The six preserved full forward ledgers supply exact accepted Z128/Z256 states;
there is no interpolation or new perturbed forward calculation. Baseline/replay
match all 257 packed states. Independent raw checks bind these rows to receipts.
The upper four terminal VJP windows are unchanged. The old lambda128 is the
output of **prefix160**, not prefix128. At step128 only, the actual new input is
`Q32(FP64(old_lambda128) + FP64(q128))`.

Four new native processes reexecute original prefixes ending at 128,96,64,32,
record only the last 32 steps, and use the existing checked Fortran pullback.
Each subsequent input is the previous actual FP32 return. This adds 320 model
steps/960 implicit stages; no state-only restart, new full256 run or FD run.
Every new packed prefix checkpoint must match the old baseline bytewise, and
every FP32 return must equal the cast of the local pre-cast FP64 capture.

## Validation status

All four cases completed without failure: 320 accepted model steps and 960
implicit stages. All 324 checkpoint comparisons across the four overlapping
prefixes match the baseline bytewise. The four checked Fortran pullbacks and
actual FP32 chain/cast checks pass. Maximum sampled native RSS was 3.773 GiB.
New model wall times were 1754.30, 1435.18, 1207.53 and 838.57 s; these include
prefix reexecution and VJP work and are not forward-performance measurements.

| Contribution | Adjoint directional value | FD at eta≈.01 | FD at eta≈.03 | Maximum relative discrepancy |
|---|---:|---:|---:|---:|
| Full two-time objective | -11036.2740715452 | -11036.2741291686 | -11036.2739242272 | 1.335e-8 |
| Added midpoint term | 16.3737700962 | 16.3737578909 | 16.3737580441 | 7.455e-7 |

Both contributions pass all frozen FD, signal, width and one-sided checks.
The independent root `math.fsum` products agree to floating-point summation
precision. The local midpoint addition-rounding relative L2 value is 2.915e-5;
this is not a propagated-gradient error bound. No conditional extra diagnostic
model runs were needed.

The full two-time Taylor diagnostics have observed orders 1.99986 and 2.00059.
The added midpoint term alone has orders about 1.06 and 0.90 at these two widths;
its residual scale is consistent with eta times the small FD/adjoint discrepancy.
A resolved quadratic Taylor remainder for that separate term is not claimed,
nor is rounding proven to be its sole cause. This does not change the frozen
FD acceptance criteria or close the previously separate THM Taylor question.

The objective, widths, settings and checks were frozen before viewing midpoint
FD numbers. Full and midpoint increment separately require ≤1% FD/AD error at
both widths, resolved FD signals, ≤1% width difference, and no >1% one-sided
gap at both widths. The midpoint AD value is `v^T(FP64(g0new32)-FP64(g0old32))`.
It is never normalized by the much larger terminal derivative. The omission
negative test shows why: a missing midpoint gives only 0.148% full relative
error but 100% midpoint error. Taylor residuals remain descriptive diagnostics.

## Revisions and provenance

Production base: `a8c470f6f194104bd7090dd990682c19f1ca8e34`, same tree as PR312
head `63a9197f3468290ed779178b332262f9f5a1f1cf`. Ten authoritative source hashes
match the retained P6 build. Reused executable SHA256:
`71927be0736ecd7c8e51b4a22177122af1a38b924e345836d1d6590470514249`.
Original input SHA256:
`e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`.
The frozen protocol, launch binding, source/executable/build receipts and graph
navigation audit are retained in the compact evidence packet. Verified cached
C++/Fortran graphs were reused and authoritative callers checked directly;
new Python dependencies are refreshed separately. Graph edges are navigation,
not proof of equations or numerical correctness.

Compiler stack is the retained AppleClang 21/GNU Fortran 15.2, Homebrew MPI 5.0.9,
Torch 2.10 (C++17 ABI0), Homebrew netCDF 4.9.3/netCDF-Fortran 4.6.2 build.
Original working directory changes were left untouched in the user's checkout;
all new work is isolated on `codex/native-two-time-adjoint-20261021`.

## Archived same-setup RK3 context

No new RK3 run or field/runtime comparison was performed. The prior same-input
16³, 240 s comparison against preserved stock RK3 h=1.875 s has RMS distances
U=.0135072 m/s, V=.00357188 m/s, W=.000319814 m/s, PH=.257214 m²/s²,
T=.000111423 K, MU=.504089 Pa. Stock RK3 has a different filter policy, so these
are descriptive cross-method distances, not a truth-based accuracy test.
The observer-enabled prior coarse wall time was 872.94 s; the archived stock
analysis timing 1.93578 s lacks a matching wall receipt. No speed ratio is claimed.

## Scope and next action

This tests one midpoint T term plus the preserved terminal W term, one mixed
six-field direction and two finite widths on the registered dry map. Segment
FP64 captures are local pre-cast values for FP32-connected inputs, not an
independent global FP64 adjoint. No second whole-gradient replay, all-direction
Jacobian, initialization-map gradient or general observation-window/assimilation
performance is certified. Actual observation H/times/R/prior remain separate.
Forward 24/24 and reference sensitivity 24/24 stay closed; the h=15 s candidate's W
failure remains recorded. Do not repeat closed physics/refinement checks.

Completion update: 2026-10-21T10:46:16.114168+09:00.

The two-time linear connection check is complete in the declared scope. Next
work is to define actual observation operators, times and weights separately;
this test does not add a production observation scheduler or R/prior model.

Evidence: [portable packet](evidence/native_two_time_adjoint/README.md).
