# AD-SDIRK3 first-stage problem and completion boundary

Local timestamp: 2026-10-11 17:47:15 JST (+0900)

Last updated: 2026-10-22 05:16:23 JST (+0900)

This is the single project-level definition of the first-stage research target.
Case reports retain their own inputs, numerical budgets and completion status.
This definition does not certify the first-stage target as complete. A named dry `em_b_wave` forward instance now selects a 240 s window and fixed
field-error budgets with `h=0.9375 s`, Newton `1e-9`. This case-scoped selection
does not fix broader forecast/assimilation targets or certify their derivatives;
project-level completion remains withheld. Efficiency is an
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
| Time map | Effective mode 3, tableau, schedule, precision/handoff, nonlinear and transpose residual norms/tolerances; accepted/failure rules | Supported-path contracts exist; the selected native two-control 240 s window is completed below, while broader windows remain open |
| Numerical state | Predictor/cache policy, carry/publication and warm versus state-only cold restart | Must name the policy; state-only equality does not imply warm-trajectory equality |
| Forward accuracy | Evaluation times/window, owned/physical sampling positions, per-field units, physical weights/scales \(s_X\), and budgets \(\epsilon_X\) | Named dry 240 s forward case selected and accepted below; broader targets remain unselected |
| Derivative accuracy | Directions/cotangents and norms/scales, dot/Taylor/FD budgets, true transpose residual and replay checks for that same map/window | Existing budgets apply to their fixtures only; the selected native two-control VJP/FD window is complete, while full-map JVP and broader directions remain open |
| Observation/control problem | Observation operator, times, \(R\), control parameterization, any prior and admissible set | Completed rotating and native two-control idealized twin cases retained below. Real-observation assimilation remains separate |
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

## Named dry-case acceptance candidate (version 3)

A concrete [packed-U cause and criteria record](<(202610120157)_pr296_packed_u_diffusion_cause_and_criteria.md>) now defines a **proposed** research acceptance instance: dry, flat 16×16×16 `test/em_b_wave`, one MPI rank/thread, 240 s, at 60/120/180/240 s. This candidate does not select a universal project forecast window or revoke the completed rotating case. Its exact input is pinned in the evidence; effective ideal-case `hypsometric_opt=1` overrides nominal namelist 2.

The candidate freezes `epsilon_X=max(0.01*S_X(240),3*q_X(240))`, where `S` is the initial-subtracted archived RK3 h=1.875 response RMS and `q` is its raw FP32 perturbation-output spacing. These **fixed numbers** apply at every checkpoint. The 1% is a deliberate proposed research target, not a governing-equation consequence. Output spacing is not an internal arithmetic-error bound.

| Field | Fixed RMS budget | Unit |
|---|---:|---|
| U | 7.74441704e-4 | m/s |
| V | 1.012200724e-4 | m/s |
| W | 7.794439857e-6 | m/s |
| PH | 0.01024975054 | m²/s² |
| Theta | 3.619571392e-5 | K |
| MU | 0.01459090046 | Pa |

A reference qualifies empirically only if one h=0.9375 RK3 comparison to h=1.875 changes every field by at most `epsilon_X/3` at all checkpoints, with exact boundaries and admissibility. This observed sensitivity is not a proof of true-error bounds or third-order convergence. The one authorized qualification run failed all 24 field/time tests; no indefinite refinement ladder follows. Candidate-vs-reference differences remain provisional until reference qualification. No efficiency requirement is added.

[EMDIV 원인·수용 경계 기록](<(202610132029)_rk3_emdiv_cause_and_acceptance.md>)은 동일 실행파일의 관찰 ON/OFF 및 보존 RK3 출력 일치를 확인하고, 한 번의 필터 계수 대조로 이 사례의 U/V 시간간격 차이에 대한 주요 기여를 확인했다. 기준해를 해석할 때는 목표 반이산 방정식뿐 아니라 진단·경계·수치 필터 정책과 그 시간간격 의존성도 먼저 명시한다. 같은 `emdiv` 값은 같은 물리시간당 필터 작용을 뜻하지 않는다. 변경 계수 대조는 원인 판별 자료이며 V3의 새 기준해가 아니다. 위 예산·평가 영역·24개 실패 상태는 변경하지 않는다.

The frozen JSON `sampling.PH` label describes height in metres, whereas its `budgets.PH` and executed gate use geopotential in m²/s². The separate metadata erratum records this inconsistency without changing the frozen bytes or numeric gate; the PH epsilon in the table is authoritative.

The changed stress path separately requires independent primitive-flux oracles, all owned seam faces, periodic shift consistency, and first-order derivative checks. A `gamma_n*sum|terms|` bound only covers accumulation of already computed terms; it cannot certify all primitive arithmetic or full cross-model RHS. The corrected U diffusion gates have direct evidence, while complete W/PH/theta RHS and integrated observation-window derivatives remain open.

## PR306 same-native ARK refinement family and filter policy

This is a separate named-case evidence family for the native WRF scheme-6
mode-3 ARK324L2SA map. The six completed 240 s runs use the same current
executable (`a5b231b279309c3268771bc0d2746b7716fa9daafa106bd40249e3ca10421d49`),
C++ archive (`b04b827fc194a363fbf9cf6e618f4b3d51b1dbf3dea10b33b6bb8de99e077159`),
and original e71 input (`e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`),
with FP64 state carry enabled and timesteps of 15, 7.5, 3.75, 1.875, 0.9375,
and 0.46875 s. The current-source h=15 control matches the archived h=15
trajectory bitwise for all eight raw fields at all five history times. This
pins the series to one native operator and build; it does not make the archived
Fortran RK3 trajectory the truth for this native family.

The native filter policy is distinct from the stock RK3 acoustic-substep path.
The archived configuration has `SDIRK3_IMEX_SPLIT_MODE=3`,
`SDIRK3_SPLIT_EXPLICIT=F`, and `TIME_INTEGRATION_SCHEME=6`. Therefore the WRF
`smdiv=0.1`, `emdiv=0.01`, and `epssm=0.1` values are configured but their
split-explicit acoustic child is inactive. They must not be described as common
filters with the stock RK3 reference. Instead, this ARK family has
`SDIRK3_IMPLICIT_DIVERGENCE=T` and a stage coefficient `kdamp=0.2`; it is a
separate divergence-damping contribution. The initial `grid%kdamp=0.01` value
comes from `dampcoef`; `unifiedStep` replaces it with `SDIRK3_KDAMP=0.2` before
the ARK stage RHS. The earlier initialization log showing 0.01 is not a stage
readback. The `0.2` stage value is a source-and-configuration-call-order
finding, not a direct stage-time runtime readback; see the [effective-context
audit](evidence/pr306_common_filter_reference/ark3_mode3_effective_context_audit.json).
`DIFF_OPT=2`, `KHDIF=1000`, and `KVDIF=10` also remain active in this map.
`W_DAMPING=0` and `DAMP_OPT=0` disable W damping and Rayleigh/sponge
application; PBL is disabled. The source audit is recorded in
`green_pressure/ark3_mode3_filter_audit_ko.md` in the PR306 evidence bundle.

The frozen V3 24-row screen remains the operational comparison rule. The
native pairs h=7.5/3.75, h=3.75/1.875, and the final h=0.9375/0.46875 pair each
pass only 20 of 24 rows; the four failures are W at 60, 120, 180, and 240 s.
For the final pair the 240 s W RMS difference is `1.2126078e-5 m/s` against
the fixed `epsilon_W/3=2.5981466e-6 m/s` limit. The native fine reference is
therefore unqualified, so the h=15 candidate has no formal acceptance result.
The earlier RK3 V3 reference failure across all 24 rows remains a separate
result and is unchanged.

These same-ARK refinements are empirical time-resolution checks for one shared
ARK implementation. They do not establish an independent Fortran truth,
formal order, a true-error bound, or an integrator-only comparison against
stock RK3. The WRF acoustic filter paths differ, and common ARK operator bias
remains. No production source or ABI changed and no rebuild was needed. The six
240 s runs and three 15 s diagnostic controls were executed and archived
separately; this documentation audit launched no further run.

## PR307 accepted-stage W RHS and source-work diagnostic

PR307 adds a default-off `WRF_SDIRK3_STAGE_OPERAND_DIAG` observer and records
one accepted 15 s native mode-3 macrostep for the same e71 input. Its source is
commit `18abbe9a2cd4b32c68d1b3bbf0d64ea0040d48fc`; the observer build uses
`wrf_sdirk3_tile_unified_impl.cpp` SHA256
`a4d65886b5281c28a1b81a3b62b74ab0a763706c311cb7887d1eecaaaf88b076` and
executable SHA256
`5bf90cd95e6d5ac4a5a76f73ec407d9ee6e91c1b6919488df20c9df09a2a7769`.
No equation coefficients, initial conditions, or public C/Fortran ABI changed.
The C++ trace/class layout expanded, so matching C++ consumers were rebuilt.
Two archived
observer OFF/ON control pairs (the initial partial observer and final V2
observer) preserve all 16 output arrays bitwise and identical RHS-call counts;
the final V2 run records 62 RHS calls in both controls. These are short
diagnostic controls, not 240 s forecast comparisons.

On the 4,096 physical W owners, including the dynamic upper face and excluding
the fixed bottom, the directly observed `Full - FI - FE` residual for implicit
stages 2–4 is at roundoff (RMS about `1e-21 m/s²`). Stage 1 has no independently
observed Full evaluation. The FI/Full evaluation input is accepted \(Y_i\); their
auxiliary `U_ref_stage_` matches \(B_i\), while slow/FE uses an auxiliary reference
matching \(Y_i\). These auxiliary-reference labels are not RHS evaluation points.
The source residuals
\(G_W=Y_W-B_W-h a_{ii}F_I(Y)_W\) have RMS `8.60e-10`, `1.96e-11`, and
`8.31e-11 m/s` for stages 2–4. These are solver-stage residual diagnostics,
not true-error bounds. Stage 4 has valid checked-`Y`, `FI`, Full and pressure
captures, but returned-K equivalence is unknown and is not inferred from these
measurements.

The reference-state distinction is recorded explicitly: fast/`FI` uses the
stage base \(B_i\), while slow/`FE` uses accepted \(Y_i\). In this run the
effective mass-coordinate mode is WRFParity (`effective_wrf_omega_ww_cp=1`)
even though the deprecated raw Omega flag is off; `omega_w_blend=1`,
`buoyancy_use_current_w=1`, and W damping is off. The W advection, buoyancy and
pressure paths therefore use current-state W/Omega/PH/T/MU, so changing only
the auxiliary reference from \(B_i\) to \(Y_i\) does not change this case's W
RHS. This case-specific observation does not imply that the reference is
irrelevant in effective Omega-off or blended configurations.

The source-derived pressure-plus-buoyancy work is `56.433 MJ`, including
`7.019 MJ` from the dynamic top closure. With frozen input-state W mass, the
one-step W-only quadratic energy change is `51.495 MJ` against `55.717 MJ` of
method work, a defect of `−4.222 MJ`. This is a W-only, one-step diagnostic,
not a total atmospheric energy budget or an explanation of the 240 s W
qualification failures. The PR306 20/24 result and failed native reference
qualification remain unchanged. Detailed records are in the external
`accepted-ark-w-stage-20261014` bundle: `green_execution/pr307_v2_completion_receipt.json`,
`green_metric/actual_accepted_stage_w_analysis_v2.json`,
`green_pressure/actual_stage_pressure_work_v2.json`, and
`green_pressure/frozen_w_energy_identity_v2.json`.

## PR308 미세 시간간격 쌍의 초기 W 궤적

[0–15초 궤적·풀이 대조 기록](<(202610151003)_fine_pair_w_trajectory_and_solver_control.md>)은
PR307과 같은 실행파일·e71 입력으로 `h=0.9375/0.46875 s`의 16/32개 수용된
스텝을 기록했다. 공통 시각 17개는 실제 FP64 끝값과 유리수 시간으로 대응한다.
초기 여섯 상태와 t=0 여덟 NetCDF 필드는 일치하고, 4,096 W 소유면에는 동적
상단을 포함한다. 기존 기준해 20/24 판정은 변경하지 않는다.

기본 쌍의 최대 W RMS 차이는 15초의 `2.7051547e-7 m/s`다. 이 짧은 구간에서
`epsilon_W/3` 관찰 기준을 넘지 않았다는 사실은 60–240초 기준해 수용을 뜻하지
않는다. 첫 미세 스텝의 3·4단계는 전체 벡터의 Newton 기준 `1e-7`을 만족해
선형 보정 없이 종료됐지만, W 상태 잔차는 각각 `1.57258e-7`, `8.66167e-8 m/s`였다.
이는 설정한 수렴 계약의 결과이며 새 방정식 결함 판정이 아니다.

이 관찰을 근거로 별도 사전등록한 단일 대조는 같은 미세 간격·구간에서
Newton 기준만 `1e-9`로 바꿨다. Krylov·유효 EWT·물리식은 유지했다.
첫 0.9375초의 coarse 대비 W 차이는 `2.48872e-7`에서 `3.21383e-8 m/s`로
줄었지만 15초에는 `2.70515e-7`에서 `4.66568e-7 m/s`로 커졌다.
따라서 기본 쌍은 풀이 정확도 영향이 섞인 비교이며, 초기 잔차의 제거가
전 구간의 차이를 없앤다는 해석도 지지하지 않는다. 생산 tolerance 기본값,
초기조건·가중·예산을 바꾸지 않았고 추가 240초 실행도 하지 않았다.

## PR309 공통 풀이 정확도의 60초 검사

[60초 기록](<(202610152339)_solver_controlled_w_60s_reference_screen.md>)은 기존 두 시간간격 `h=0.9375/0.46875 s`를 유지하고
Newton 절대 기준을 두 경로 모두 `1e-9`로 맞춘 연속 64/128스텝이다.
Krylov·유효 EWT·e71 입력·필터와 물리식은 유지했고 생산 기본값은 바꾸지 않았다.
첫 15초의 coarse 16스텝과 fine 32스텝은 기존 PR308 자료와 여섯 packed 상태
전체가 비트 단위로 일치했다. 추가 15초 재실행이나 중간 cold restart는 없다.

기존 V3의 공개 FP32 NetCDF→FP64 비교·소유점·예산을 그대로 적용하면
60초의 여섯 검사가 모두 충족된다. W RMS 차이는 `1.79844365e-6 m/s`,
한도 `2.59814662e-6 m/s`의 약 `0.6922`배다. 별도의 내부 FP64 W 궤적은
65개 공통 시각에서 최대 `1.96831049e-6 m/s`였으며, 고정 M0/e1을 재조정하지 않았다.
모든 576개 암시적 단계가 초기 검사와 한 번의 Newton 보정으로 끝났고,
관측된 직접 W 잔차 기여의 합은 각 경로 약 `3e-12 m/s`다. 이 합은 전파오차 상한이 아니다.

이 결과는 **새 strict 계열의 60초 6개 검사**만을 충족한다.
기존 `1e-7` 계열의 20/24 기록을 소급 변경하지 않으며, 새 계열의
120·180·240초와 전체 기준해 적격성, 15초 후보의 정확도 수용은 아직 미확인이다.
사전 계획에 따라 결과와 무관하게 60초에서 멈췄고 추가 적분을 하지 않았다.
stock RK3의 기존 60초 프레임은 다른 필터 정책의 보조 비교로만 재사용했다.

## PR310 strict 계열의 240초 전체 검사

[240초 기록](<(202610201044)_strict_ark_240s_reference_screen.md>)은 같은
`h=0.9375/0.46875 s`, Newton `1e-9`, 실행파일·e71 입력·필터·수송 설정으로
원래 초기장에서 연속 256/512스텝을 완료했다. 생산식·기본 허용오차는 바꾸지 않았다.
기존 V3의 공개 FP32→FP64 비교·소유면·예산을 유지한 전체 결과는 **22/24**다.
60·120초는 여섯 변수 모두 충족하며, W만 180초 `3.40106638e-6 m/s`
(한도의 `1.30904`배), 240초 `3.27151665e-6 m/s` (`1.25917`배)로 미충족이다.
이는 새 strict 계열의 완결된 24행 판정이며 이전 `1e-7` 계열 20/24를 소급 변경하지 않는다.
전체 기준해 적격성과 기존 `h=15 s` 후보의 공식 정확도 수용은 계속 보류한다.
최초 미충족 공식 평가시각은 180초이며, 추가 모델 실행 없이 해당 구간의 자료를 보존한다.
이 결과만으로 남은 W 차이를 반복풀이 오차나 특정 파동의 감쇠·위상오차로 확정하지 않는다.

## 세 번째 간격의 기준 검사 종료와 h15 후보 판정

[후속 240초 기록](<(202610201802)_three_level_w_reference_and_h15_accuracy.md>)은
같은 strict 설정의 `h=0.234375 s` 한 계산만 추가하고 보존된 두 해를 재사용했다.
새 `0.46875/0.234375 s` 쌍은 고정한 네 시각·여섯 변수의 `epsilon/3` 검사
**24/24를 충족**한다. W의 최대 한도 대비 비율은 240초의 `0.211694`다.
따라서 이 건조 사례의 경험적 기준해 민감도 검사는 종료한다.
기존 PR310 쌍의 22/24 기록은 변경하지 않으며, 새 결과를 참오차 상한이나
3차 수렴·일반 관측창 수반 인증으로 확대하지 않는다.

이 조건이 충족돼 보존된 원래 초기장의 `h=15 s` post-CF 후보를 새 미세 해와
**원래 전체 epsilon**으로 평가했다. 결과는 **20/24**이며 W 네 시각이
한도의 약 `4.57–8.35`배로 미충족이다. 다른 다섯 변수는 네 시각 모두 충족한다.
후보의 실제 Newton `1e-7`을 포함한 설정의 정확도 결과이며 기본값은 바꾸지 않았다.
기준해 검사 완료와 15초 후보의 W 정확도 미충족을 구분한다. 후보는 재실행하지 않았다.

다음 결정은 후보 시간간격·설정과 목표 W 정확도의 적합성이다. 더 작은 기준 간격,
감쇠·초기조건·예산 변경 또는 추가 후보 실행은 이 결과의 자동 후속조건이 아니다.

## 선택한 건조 연구 사례의 전방 설정

[원배열 직접 후보 평가](<(202610201926)_research_forward_setting_h09375.md>)로 보존된
`h=0.9375 s`, Newton 절대 `1e-9` 계산도 새 `h=0.234375 s` 기준에 대해
**원래 전체 epsilon의 24/24 조건을 충족**함을 확인했다. 최대 직접 거리/예산은
W 180초의 `0.5025718511`이며, 별도 삼각부등식 상한은 `0.5029398054`다.
기존 `epsilon/3` 기준쌍의 22/24와 후보용 전체 예산의 판정은 서로 다르다.
기존 기록과 15초 후보의 W 네 시각 미충족은 그대로 보존한다.

이 사례의 연구용 전방 설정은
[`dry_em_b_wave_e71_240s_h15over16_strict_v1`](evidence/research_forward_setting/research_forward_map.json)로
고정한다. 원래 e71 입력, 240초 연속 계산, 같은 native 연산자/보조 상태·수송·CF·감쇠·경계,
내부 FP64/carry, `h=15/16 s`, Newton `1e-9`/상대 0, Krylov `1e-7`/유효 EWT `1e-6`이다.
이는 새로운 생산 기본값이나 다른 조건의 자동 수용이 아니다. 새 적분은 수행하지 않았다.

이제 이 고정 시간맵의 접선·수반·관측창 연결이 남는다. 기존 짧은 fixed-trajectory
계약/CI는 이 240초 native 관측창을 인증하지 않는다. 실제 관측 H·sampling·시각·R·
controls/prior·미분 예산은 별도로 고정해야 하며, 전방 평가시각이나 epsilon을 관측 설정으로
자동 전환하지 않는다. 같은 맵의 실제 호출·수치 상태·경계·옵션과 관측 cotangent의 연결을
확인한다. 더 큰 시간간격의 정확도·효율 개선은 이 연결의 선행조건이 아니다.

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

## Completed validation instance: native idealized twin (E0–E2)

The [frozen native idealized-twin plan](plans/native_idealized_twin_v1.md) and
[plan JSON](plans/native_idealized_twin_v1.json) defined the separate two-control
initial-state restoration case on the accepted dry 240 s native map. It is now
**executed and complete within its declared E0–E2 scope**; see the
[execution report](<(202610231710)_native_idealized_twin_e0_e1_e2_complete.md>) and
[portable state/cotangent evidence and recomputation](evidence/native_idealized_twin_execution/README.md).
The historical plan and its hash remain unchanged; its planning-time readiness
flag is not the current execution status.

The case uses model-generated FP64 truth and 384 noiseless native point samples
of U, V and dry total theta at accepted steps 128/256 (120/240 s), fixed research
R, a rotational wind mode and a smooth theta mode. The control starts after WRF
initialization/CQ; no background penalty or initialization-map derivative is used.
E0 cost/gradient/update are zero. Both FD widths identify the two controls
(singular-value ratio about 0.449); 12 full/midpoint/terminal gradient checks have
maximum relative difference 2.34e-8, and all ten E2 faults are detected.

One accepted update reduces the cost ratio to 2.53e-14, with normalized full
control gradient 7.82e-8 and relative control error 2.74e-7. The six-field
composite state errors at unused 60/180 s are 2.96e-7 / 3.52e-7 of the background
errors. Variable-wise physical RMS and unobserved-position scores are preserved.
Optimizer success, cost, gradient, control recovery and unused-state improvement
were evaluated independently against the original fixed thresholds.

The computation used 41 native attempts / 6,880 model steps within the original
caps. Two MPI launcher RC1 records remain failed. Their matching RC0 results
were bound without normalizing exit status; one exact 192-step checkpoint used a
post-computation MPI observer build, whose saved state and gradient payloads are
byte-identical and whose linkage differs by the observer object only. Remaining
calculations used the original executable. The historical MPI failure cause is
unknown; this case does not certify MPI deployment reliability.
The separate [bounded MPI exit audit]((202610240127)_mpi_exit_bounded_investigation.md) preserves that unresolved status
and leaves this completed numerical case unchanged.
The [exit-review checklist]((202610241022)_pr317_exit_review_checklist.md) records
optional future-run tracing preparation; the historical MPI cause remains unresolved.

PR #314's residual caller and the rotating inverse stay closed independently.
This result does not certify real-atmosphere assimilation, arbitrary initial
fields, all directions, an exact global FP64 adjoint or future forecast skill.
Optional noise/prior E3, a 15 s candidate, new timesteps or deployment work are
not prerequisites and are not added as new completion gates.

## Next action and validation of this definition

The highest open forward-validation task is now the **official-scale dry
`em_b_wave` comparison against genuine WRF split-explicit RK3**, using the
v4.7.0 spatial/physical setup, 600/300/150 s outer steps and a continuous five-day
window. Preserve the closed 240 s derivative/twin cases; their completion does
not certify practical 600 s forward performance. Full-map derivative and
assimilation generalization are subsequent scope decisions.

The [standard-case execution report](<(202610241316)_standard_forward_rk3_baseline_and_ark_pending.md>)
records three completed five-day RK3 baselines and exact post-initialization
state/base-field agreement. The subsequent
[ARK600 six-hour screen](<(202610241427)_standard_ark_six_hour_screen.md>) completed
36 outer steps and 108 implicit stages with RC0 and admissible saved states.
Its same-horizon field comparison to archived RK3 is now available; its RSL main-step
cost is about 485 times the RK3-600 prefix. This is a six-hour full-model-path difference/cost
screen, not a same-accuracy speedup or five-day stability result. The earlier
instrumentation-heavy setup stop remains separately preserved. Lean ARK five-day
runs, ARK internal timestep sensitivity and the full matrix remain open; no new
long-run authorization or physical accuracy tolerance is inferred from this screen.
MPI exit root-cause work remains a separate unresolved engineering item.

The subsequent [quiet-forward validation](<(202610241655)_quiet_forward_diagnostics_native_validation.md>)
gates optional JVP timers, norm diagnostics, and solver/Fortran debug output behind
existing controls. Explicit DA graph/trajectory requests remain separate from the
Newton JVP. Two new quiet six-hour runs completed normally; their t=0 and six-hour
NetCDF files are byte-identical to each method's previous outputs. The matched
quiet measurements are about 304.64 times RK3 in launcher elapsed time and 443.30
times RK3 in RSL main-step time for ARK600. These are single same-timestep,
six-hour cost observations, with no new accuracy acceptance or five-day claim.

The [bounded cost investigation](<(202610242213)_forward_cost_profile_and_rejected_candidate.md>) fixes two comparison-tool/test-inventory issues and samples one standard-grid ARK600 step. A CPU MGS fusion candidate passed focused solver checks but failed its predeclared exact-public-output parity gate and showed no native timing gain in a matched diagnostic one-step pair. It was not promoted; production Newton arithmetic is unchanged. The rejected patch, outputs and receipts are preserved. Actual forward cost improvement, continuous five-day ARK runs and the physical accuracy/cost acceptance remain open. This work adds no DA or MPI-reliability claim.

The subsequent [periodic-selection optimization](<(202610242336)_jvp_periodic_permutation_cost.md>) replaces only the PH periodic index gather with the same current-value/current-tangent roll permutation. Four preregistered A-B-B-A standard-grid h600 first-step runs completed with identical public outputs and reported residual/work records. In this telemetry-on block the median RSL step cost fell 17.79% and median measured JVP cost per call fell 18.81%; both mirrored pairs passed the frozen criteria. The candidate timings varied more than baseline, so this is a narrow first-step cost result, not a quiet six-hour/five-day or same-accuracy speedup. The MGS candidate remains rejected. Continuous five-day comparison, timestep sensitivity and physical accuracy/cost acceptance remain open.

The subsequent [five-day attempt](<(202610260948)_ark600_monitor_interruption_partial36h.md>) was interrupted after 251/720 steps (41h50 model time) when the controller process-tree query timed out and cleanup terminated its owned launcher. The 18-hour cap was not exhausted; no numerical divergence is established. Saved 0–36h outputs were postprocessed, with finite/positive saved states and increasing W distance from RK3. This is incomplete and cost-ineligible, not five-day acceptance. Future execution control now separates bounded transient monitoring loss, accepts valid cumulative wrapper statistics and publishes terminal progress; its fault tests start no WRF/MPI process. No retry was performed.

For the selected dry 240 s forward instance, the native terminal and
[one-midpoint linear VJP/FD probes](<(202610210936)_native_two_time_adjoint.md>)
are complete within their one-direction, FP32-chained scopes. The
[synthetic two-diagnostic squared-residual caller](<(202610211456)_native_residual_objective.md>)
also passes the declared full-cost and derived midpoint-increment checks with
fresh residual-dependent cotangents. Its fixed synthetic targets and weights
do not define a physical observation problem. The separately selected idealized twin above has now completed native control
restoration without real observations. Preserve its completed E0–E2 status; no
additional experiment is mandatory for that restricted instance.
These earlier probes do not certify a full-map JVP or all directions.
Do not reopen the completed reference screen or require
the 15 s candidate to pass first. Declare the observation/control problem and
derivative budgets before that test. For broader instances, freeze their own
window and physical error budgets. Adopt an efficiency target only through an
explicit scope decision; if adopted, fix its same-accuracy cost/timestep criterion too. Do not automatically add another wave, optimizer run, Hessian test,
moisture, terrain or MPI experiment. No project completion percentage is implied.

This documentation was checked against main
`35037bd2093e17beb23d27857a6d9c81ce08aed0` (including merged PR #292), the
production tableau, supported derivative contracts and preserved case reports.
Green and Red teams checked the definition, supported-path claims and scope
boundaries against those sources and found no blocking issue.
No production source/configuration was changed for this documentation update.
It records the four archived PR307 short native observer controls and existing
one-step stage analysis; this edit launched no model run. No 240 s qualification
run or same-setup RK3 field/runtime comparison was performed for PR307.
