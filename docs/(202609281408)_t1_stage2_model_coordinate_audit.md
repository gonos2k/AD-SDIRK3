# Stage-2 trial-state and trust-model coordinate audit

Local timestamp: 2026-09-28 14:08:49 JST

## Context and source scope

This report follows the same-call W ledger from PR #251. The examined feature
branch is `e9db243b4ee5443f7086f693a58ae7117b33a0a2` (Git tree
`6688b8c7d148e722d58aeda4dd2b06320d1e9bc3`), still separate from
`main`. All experiments below used isolated local source patches. **This
commit changes documentation only; none of the experimental patches is a
production fix.** The work concerns the strict one-rank, one-thread
`test/em_b_wave` case at `dt=15 s`, which aborts in Stage 2 before producing
a forecast step.

The stage residual is `R(K)=K−F(U(K))`. In the current source,
`U_eval=fl32(U_stage+hK)` and
`U_trial=fl32(U_stage+h(K+dK_trial))`, with `h=dt*gamma`. The trust model uses
the GMRES residual to predict the smooth-coordinate change
`R_linear,s=(1−alpha)R_s−alpha*r_g,s`. The actual trial instead evaluates
the separately rounded `U_trial`. Graphify on the exact feature tree and
the temporary affected-source corpora identified the solver→RHS→EOS/W PGF
path; all numerical claims here were checked against source and saved
runtime operands. Graph edges alone were not treated as proof.
The canonical trial construction is in
`external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp` near line 10608;
the scaled trust prediction and actual-merit comparison are near lines
10690–10710. Packed block offsets come from
`external/libtorch_wrf/sdirk3/wrf_sdirk3_state_layout.h`.

## What the saved Stage-2 operands prove

In PR #251's terminal FP32 archive, `U_eval` and `U_trial` each reproduce
their source construction bitwise. For PH, adding the nominal
`h*dK_trial` to saved `U_eval` changes **0/4,913** FP32 cells, whereas
recomposing from `U_stage+h*K_trial` changes **607** cells by one local ulp.
Across positive PH cells the nominal increment is at most `0.05251` of its
local ulp. U and MU also have smaller representation differences; raw L2
norms cannot be compared across state blocks with different physical units.

The W velocity block is `[9792,14705)` in the packed state. It contributes
`0.9998781685` of the terminal scaled residual squared norm. At the rejected
trial, `||F_trial,W−F_base,W||₂ = 2.41588e−4`, while
`||K_trial,W−K_W||₂ = 9.93601e−7`. These observations locate the
investigation; they do not identify a physical PH error or a global
precision floor.

## Isolated RHS counterfactuals

The block-probe executable SHA-256 is
`bbe2c24a4bfaacf80a02387b5a8a5c40719ebf27ad5b470dadd16ff41a1a8d12`.
Its production archive SHA-256 is
`d0e26d4658a06e075ac8b02713f234bbc23f751db264192db068d7cc1806f8da`:
it replaced only the Newton object in the validated PR #251 archive; all
other 23 archive members match byte-for-byte. The candidate-first WRF
relink reused unchanged clean WRF objects. Baseline, repeat, nominal, U,
PH, and MU arms share the same executable, `wrfinput_d01` SHA-256
`e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`,
snapshot-enabled namelist SHA-256
`51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`,
and diagnostics input SHA-256
`bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`.

One extra RHS was called **after** the production `F_trial` at Stage 2 iter 6,
attempt 1, in a separate WRF process for each arm. Their first 358 ordered
RHS digests and all 31 terminal snapshot tensors agree; removing each
probe's one digest recovers the baseline stream. A repeat of the exact trial
input gives `F_probe == F_trial` bitwise. The nominal input is
`U_nom=fl32(U_eval+h*dK_scaled_candidate)`; the terminal archive names that
same already-scaled candidate tensor `dK_trial`. The hybrid inputs start from `U_nom` and
replace only the named U, PH, or MU block with the stored actual-trial block.

| RHS probe input | `||F_probe,W−F_base,W||₂` | Post-hoc trust ratio |
| --- | ---: | ---: |
| Actual/repeat | `2.41588038e−4` | `−0.0497579` |
| Nominal | `1.04493e−11` | `+0.0744661` |
| Nominal + actual U | `1.04493e−11` | `+0.0744658` |
| Nominal + actual PH | `2.41588065e−4` | `−0.0497566` |
| Nominal + actual MU | `2.30812e−9` | `+0.0744631` |

The PH-only arm matches actual-trial W RHS within `2.25823e−9` L2. This
isolates **PH-slice sensitivity of W RHS in this one operator call**; the
hybrid is an artificial state, not a physical trajectory. The predicted
reduction held fixed in those ratios is `5.1799253e−9`. Even the positive
nominal ratio is below the unchanged acceptance threshold `0.25`.

## Direct one-candidate falsification

A separate source pair was compiled with identical compiler/ABI flags and
identical 23 non-Newton archive members. Only Stage 2 iter 6 attempt 1 used
`U_trial=U_eval+dt*gamma*dK_scaled_candidate`; every earlier candidate retained the original
formula. Baseline and targeted executable SHA-256 values are respectively
`29d61e43d1f6e84aebe6570a43f97a2eadd6f443d068cedff5bb33e23a957cc6`
and `2921d21da9614189a43f70afe7468965a9c38a1086e4a32cc16e1649595c93cf`.
The first 357 RHS digests agree, and only the selected trial input differs
at digest 358 (`0x94fcce77f9d62969` versus `0xb5c46d26968e7413`).
The only changed terminal tensors are `U_trial`, `F_trial`, and `R_trial`.

Actual reduction changes from `−2.57742e−10` to `+3.85729e−10`, while the
old prediction remains `+5.17993e−9`; `rho` changes from `−0.04976` to
`+0.07447`. **Both runs still reject and end in iter-6 ZeroStepStall.**
The same targeted executable with snapshot diagnostics off has the same
359-record RHS digest stream and initial `wrfout` hash as diagnostics on.
A separate *global* incremental-form experiment diverged at the first
trust trial and exhausted a 40-iteration Newton budget; raising that
experimental budget to 100 still left scaled RMS residual near
`1.9e−6` against `1e−7` tolerance. Those global runs solve a different
rounded map and do not establish a precision floor for the original model.

## Trust model along the realized state displacement

In another isolated, output-only probe, the actual representable
`delta_U=U_trial−U_eval` was passed to the production forward-mode JVP.
The executable SHA-256 is
`4617b381f3a56a06d1d3bf08f4f4f8c0b4056df45e3d44ec47a788c28c62e6aa`,
and the linked archive SHA-256 is
`97bc4b01979d0ea888dd969d012fa5c4eda32912f47dbcd3823e3d6f79ab05a8`.
Its baseline, actual-model, and nominal-model processes share the first
358 RHS digests and all 31 terminal snapshot tensors. The actual-model
repeat gives the same `F_trial` bitwise; there were zero FD fallbacks.
The output-only probe adds one `compute_rhs(U_model)` evaluation and one
forward-mode JVP evaluation, represented by two inserted RHS digest records.

The alternate first-order model is
`R_linear = R + (K_trial−K) − J_F(U_eval)*delta_U`.
For actual stored `delta_U`, its predicted reduction is
`−2.577101825e−10`, versus measured `−2.577419613e−10` (difference
`3.18e−14`). For the nominal counterfactual state, prediction is
`+3.857288660e−10`, versus measured `+3.857288669e−10` (difference
below `1e−18`). Those reductions use FP32 residual/scaling operands and
FP64 merit accumulation, as production does. The actual model correctly
calls this one trial **non-descent**, so it would still reject. The nominal
model concerns a different rounded trial-state construction, which need not
equal the canonical `U_stage+h*K_trial`; it was never used for
solver acceptance.

An opt-in existing directional checker was also run at Stage 2 iter 6 in
an isolated local patch. Its repeated/order-swapped RHS purity controls
passed bitwise and the terminal archive stayed unchanged, but it added
212 RHS evaluations. Its `eps_abs` is a multiplier on the direction,
not a physical displacement. W-block RHS-J finite differences agree at
much larger state perturbations; the residual-A finite difference becomes
ill-conditioned as its multiplier approaches the trust fraction. This does
not prove a general JVP defect or a precision floor.

## Completion boundary and next checklist

The bounded finding is a mismatch between the **smooth trust prediction
coordinate** and the **realized FP32 trial-state displacement**, with PH
rounding driving the observed W RHS jump at one failing candidate. It is
not a resolution of Stage-2 convergence. The actual-delta JVP model is
locally predictive, but its actual candidate is non-descent. No production
solver or threshold change is justified by these experiments alone.

| Item | Status / required closure |
| --- | --- |
| PR #251 same-call W ledger | Closed for its opt-in, one-rank FP32 scope. |
| PH state-map sensitivity at Stage 2 iter 6 | Identified for one candidate; repeat, block-hybrid, and targeted controls passed. |
| Canonical stage map and trust model | Open: choose one `U(K)` for base/trial RHS, JVP, and prediction; verify candidate decisions without relaxing residual acceptance. |
| PH precision path | Open: `calc_p_rho_wrf` already computes internally in FP64, but receives PH after FP32 rounding. Any compensated or FP64 state path must preserve EOS/PGF, halo, AD, and Fortran ABI contracts. |
| Stage-2/3 convergence and equal-accuracy runtime | Open: obtain an accepted strict model step, then compare fields/runtime against the archived same-setup RK3 reference. |
| L34 full operator, G1 decomposition, K1 coefficient derivatives, A1 active adjoint | Open under their previously stated scopes; this audit does not change their status. |

No same-setup RK3 forecast-field or runtime comparison was performed in this
audit because the strict PC2 case did not advance. The local source probes,
executables, inputs, logs, and tensor archives remain under
`/private/tmp/sdirk3-t1-stage2-jvp-probe-20260928/.validation/t1_trial_probe/`
and `/private/tmp/sdirk3-t1-incremental-falsification-20260928/.validation/t1_incremental_falsification/`.
Neither local experiment was pushed or proposed as production code.
The internal FP64 EOS cast and FP32 output boundary are visible in
`external/libtorch_wrf/sdirk3/wrf_hydrostatic_pressure.h` near lines 83–112.
The selected input, executable, archive, log, tensor, report, and temporary
source-patch evidence was also preserved in the local bundle
`/Users/yhlee/.local/share/sdirk3-t1-evidence/20260928/t1_stage2_local_evidence.tar.zst`
(SHA-256 `26d1810b6073bb8b03b1790b9332d0877c2d400a232661db007ebdc83fd55481`).
Its 44-file manifest was verified against an actual extraction; see the
adjacent `receipt.json`. The bundle is a local review artifact, not part of
the Git repository or CI evidence. Its copy of this report predates this
archive-reference paragraph; the tracked document is the final narrative.
