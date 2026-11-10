# 수렴 검토 해소 체크리스트

작성: 2026-11-02T13:16:08.084142+09:00.
최초 개발 기준 main9292c6c896fcec0f338d0fec7d27bd803e4c620f(PR337 병합).
최신 갱신: 2026-11-10T14:19:39.086928+09:00, 기준 main9e7d69d8(PR345 병합).
Stage3 EOS 정의역·산술 오차 성분: [진행 보고]((202611101123)_stage3_eos_domain_and_error_component.md).
링크 귀속 정정·O1: [보고서]((202611022330)_link_origin_o1_resolution.md).
이번 조기 반환·소비자 진행: [보고서]((202611021509)_early_residual_status_resolution.md).
O1 표현 정정·O2/O3 선택점: [진행 보고]((202611030842)_operator_contract_directional_gap.md).
Forcing source 조건·Stage3 수용 갱신: [진행 보고]((202611031859)_forcing_conditions_stage3_accepted_step.md) · [지원 조건](design/forcing_support_conditions.md).
Green·Red: GPT-6.1-Terra, medium.
기준 자료는 사용자가 제공한 수렴 이론 검토 본문이다. sandbox 첨부 전체 파일은 로컬에서
확보하지 못했으므로, 그 전체를 직접 읽거나 모든 독립 계산을 재실행했다고 주장하지 않는다.

## 완료 기준

표준 em_b_wave 100km·40×80×64·600초를 유지하고, 정확도·안정성·계산비용을 함께 판정한다.
전체 native 수렴 보장은 아직 없다. 아래 항목의 구현·집중검사·native 적용·이론적 증명은
각각 다른 범위다. 코드가 있거나 잔차가 작다는 이유만으로 다음 층을 체크하지 않는다.
순서는 R(현재 코드의 실제 수용/잔차) → O(고정 연산자/경계) → E(에너지/가역성) →
T(실제 시간 지도) → A(오차 예산) → F(짧은 전방·장시간 수용)다. 같은 검사를 반복하지 않는다.

## 0. 재사용하고 다시 열지 않을 근거

- [x] A10 종료형 실행: 선택점의 primal/JVP 반복 일치, 완료 스텝0이라는 제한을 보존한다.
  전체 시간 gate INCONCLUSIVE를 바꾸지 않는다. [기록](evidence/terminal_jvp_replay_20261102/manifest.json).
- [x] PR338의 공통 prepared-A/P0-P1 비교는 NO_GO로 종료됐고 현재 main에 병합됐다.
  같은 비용 비교를 재개하거나 native-FWAD 성능·전체 수렴 보장으로 합치지 않는다.
- [x] 이전 dry240초·두 제어 E0–E2, roll·미분·관리기·출력 입력 검사의 원래 완료 범위를 보존한다.
- [x] 수치식을 바꾸지 않고 원래 h600·최종 Newton 기준·필터·감쇠를 유지한다.
  작은 h 제조검사는 코드 계약용이며 실용 RK3 비교를 대체하지 않는다.

## R — 실제 사용하는 방향과 반환 상태의 잔차(즉시 우선)

| ID | 해소할 조건 | 최소 근거 | 상태 |
|---|---|---|---|
| R1 | 후처리된 실제 dK의 implemented A 잔차와 성공/품질 판정 일치 | production Direct-U/halo 경로 반례, S=I 및4I, 변경 없는 대조 | 완료: 실제 public 경로 시험101/101 |
| R2 | raw Krylov receipt/warm-start와 실제 방향 지표 구분 | 원래 GMRESResult 보존, 실제 방향 log, entry exemption·trust/회복 소비자 대조 | 완료 범위: raw/방향 지표·집계 분리 및 no accepted update 종료 문구; 같은 iteration 상태를 표시 |
| R3 | 반환 K의 final_residual은 그 K에서 관측한 같은 G_K·S·mask RMS | 한 번 갱신 후 예산 종료, adaptive/debug 별도, 독립 해석식 검산, 추가 RHS 없음 | 완료: adaptive on/off 독립 잔차 일치 |
| R4 | 예외/NaN/zero-budget 반환의 잔차·측정 여부 진실성 | early-return 분기별 재현; 측정하지 않은0을 실제 잔차로 쓰지 않음 | 이번 범위 완료: 관측 tensor의 예외·NaN/Inf와 관측 전 예외/zero-budget을 분리 |
| R5 | stage acceptance와 실패 정책을 경로별로 명시 | mode3 severe gate와 mode0/1/2 permissive 정책을 분리 | 3상태 소비·중단 규칙 연결 완료; 실제 혼합 MPI/예외 동시 중단은 미검증 |

R1의 비율은 Krylov의 unweighted S-coordinate norm 및 zero-RHS 규칙을 따른다.
그 값은 실제 사용한 matvec에 대한 잔차이며, JVP가 전체 residual의 정확한 미분이라는
증명이나 전역 inexact Newton 정리를 뜻하지 않는다. raw Krylov 실패의 원인을 후처리로
잘못 돌리거나, 후처리 실패를 raw Krylov 비수렴으로 진단하지 않도록 구분한다.
R3에서 일치하는 관측 상태가 없으면 측정 여부를 false로 남긴다. 추가 RHS는 mutable
Tile 문맥과 비용을 바꿀 수 있으므로 단순 실패 보고를 위해 자동 재호출하지 않는다.
그 미측정 상태가 기존 실패 정책에서 어떻게 분류되는지는 R4/R5 검증 대상이다.

## O — 같은 residual·연산자·문맥

- [x] O1 정의·소스 대응: 실제 stage base B, Y=B+aK, G_K=K−F_C(Y), G_Y=aG_K,
  projection·문맥·S/H/N 및 GMRES/merit norm 차이를 [계약](design/mode3_residual_operator_contract.md)에 고정했다.
  고정 C와 갱신 C(Y), exact A*와 selected Ahat을 분리하는 두 표현을 추가 정정했다.
  고정 c와 미분 동일성의 증명은 O2/O3, 허용 공간의 작용은 O4에 남긴다.
- [ ] O2 활성 참조/omega/halo/cache 경로의 상태 의존성과 AD 연결을 확정한다.
  frozen reference가 수학적 독립 입력인지, update를 미분해야 하는지 경로별로 구분한다.
  진행: mode3·single-tile·FP64·AD·ref-update/reuse off의 source 역할과 관측 입력 불변성,
  중심 RHS4회 bitwise 반복 일치 확인. 현재 source에서 omega memo의 call-local 재생성,
  현재 EOS pressure 게시 후 V-PGF 읽기, NH override→Tile snapshot 순서를 확인했다.
  저장점 mass-floor는 inactive이고, Stage3 accepted segment의 조건부 μ 하한도 양수다.
  후속: 같은 Stage3 수용 쌍의 EOS 분모·alpha·theta·pow base는64층에서 조건부 양수다.
  저장된 반올림 EOS 입력의 두 끝점 전체212544점씩 MPFR 산술 오차 상계를 확보했다.
  상류 alb/state formation·전이 cache/다른 branch·설정 및 full RHS FP 오차는 미입증이다.
- [ ] O3 native JVP의 가법성/동차성·residual 미분 일치·오차 gap을 관련 방향에서 확인한다.
  A10의 한 방향 반복, prepared graph 검증은 그 범위만 재사용한다.
  진행: stage2/Newton0의 후보p/Qseed/boundary-complement와 ε3개, 총9개 FD gap 측정.
  completed는 integrity 판정이며 FD 오차 상계·exact forcing 인증·전체방향 증명은 아니다.
- [ ] O4 periodic seam/대칭벽/고정 슬롯/halo의 제약 공간과 projector 접선을 검증한다.
  에너지 내적에서 orthogonal하다고 가정하지 않는다.
  진행: 이 표본에서 Qp=p, Q²seed=Qseed, Q(I−Q)seed=0을 관측했다. 전체 공간 증명은 아니다.

## E — stage 가역성과 전처리

- [ ] E1 건조 principal block의 실제 staggered gradient/divergence 가중 adjoint 및
  경계 flux 상쇄를 source-derived 관계로 확인한다. hybrid mass/map/동적 상단을 포함한다.
- [ ] E2 남은 배경 shear·비균질성·분할 항의 one-sided Lipschitz 또는 섭동 상계를 정한다.
  제한된 물리 영역의 존재·유일성/양성·영역 불변성은 별도 조건이다.
- [ ] E3 native A inverse amplification 또는 가능한 에너지 상계를 잔차–해오차 연결에 쓴다.
  참조 scalar pressure의 weighted SPD를 전체 native A의 SPD로 확대하지 않는다.
- [ ] E4 실제 S⁻¹AP⁻¹S의 norm/FOV 또는 충분한 실측 수렴 근거를 확보한다.
  한 방향 defect는 operator norm 상계가 아니며, 충분조건 미달은 발산의 증명이 아니다.
- [ ] E5 P1 내부1%를 전체 A/Newton forcing1%로 부르지 않는다. 복원 민감도와
  원래 b−Ax를 함께 판단하며, FGMRES는 A의 변화까지 자동 해결하지 않는다.

## T — 실제 시간 지도와 결합 안정성

- [ ] T1 mode0/1/2/3/4 및 split-explicit 경로를 구분한 실제 stage/final assembly를 확인한다.
  mode3은4-stage ARK324이며 최종값을 일반적으로 마지막 stage와 같게 두지 않는다.
- [ ] T2 계수의 고전적 차수·implicit A/L 안정성과 native IMEX 결합 안정성을 분리한다.
  검토의 R(2i,0),R(0,2i),R(2i,2i) 반례는 각각/결합 안정성의 차이를 뜻하며 WRF mode 원인 증명은 아니다.
- [ ] T3 활성 split/reference/stage time이 ARK의 고정 additive 함수 가정과 연결되는지 확정한다.
  필요한 상쇄 또는 다른 일반화된 시간 지도로 분석할 조건을 제시한다.
- [ ] T4 비정규·비가환 증폭과 물리 경압 성장률을 분리한다. 고유값만으로 norm을 제한하거나
  모든 모드를 감쇠시키는 것을 목표로 삼지 않는다.

## A — 물리 오차에서 반복 허용오차까지

- [ ] A1 물리량별 stage 해오차 예산→A inverse bound→실제 rate/state/scaled 잔차 예산을 연결한다.
  진행: Stage2 ηhat/δ 표본의 잔여 예산은 정리했으나 tau/rho/norm의 full-RHS 상계가 없어
  exact forcing1e-7 인증은 미완료다. μ floor의 조건부 1Pa branch allowance는 이 예산의 대체가 아니다.
  다음 목표는 같은 Stage3 수용 쌍의 exact merit 감소다. e0+e1<0.0019075383808737634의
  full endpoint error 상계는 아직 없다. EOS local Pa/alpha 오차를 RMS 예산에 직접 더하지 않는다.
- [ ] A2 균일한 조건 아래 rate defect O(h³), state defect O(h⁴)의 충분조건과,
  S=O(h⁻¹)/O(1) 체제의 scaled criterion을 구분한다. 고정tol이 곧3차 보장이라는 주장은 하지 않는다.
- [ ] A3 FP64 내부·FP32 interface/출력/전처리의 정밀도 바닥과 norm equivalence를 예산에 포함한다.
  결과를 본 뒤 tol·허용오차를 골라 통과시키지 않는다.

## F — 불확실성만 닫는 전방 확인

- [ ] F1 R/O 계약을 먼저 고정한 후보 하나의 h600 짧은 연속 실행에서 native/RK3 실제 장을
  같은 시각·영역으로 그린다. W/V/θ의 위치·연직층·파수·진폭/위상과 비용을 함께 확인한다.
- [ ] F2 같은 격자의 h/h2/h4 민감도는 시간해상도 진단으로 한정한다. 짧은 h 성공을
  실용h600 비용/안정성 대체 근거로 쓰지 않는다. 필요할 때에만 공간 refinement를 수행한다.
- [ ] F3 선별·예산 고정 후 같은 연속5일의 질량·열역학·물리 허용성·파동·전체 비용을 판정한다.
  restart 상태 이력 미검증의 이어붙이기나 강제 감쇠로 해결됐다고 처리하지 않는다.

R1과 R3의 이번 변경 범위는 완료다. Direct-U/S=4/실제 halo/warm-entry 및 한 갱신 뒤
실패 반환 시험을 통과했다. 이전 PR340 native 실행파일은 PR339와 byte-identical하여 c7 코드 검증 귀속을 철회했다.
단일 archive와 실제 Newton/Tile 메서드 출처를 확인한 fcd58bc9의 새 h600 한 스텝에서
3stage 수렴·wrapper3/0·끝174숫자 배열 parity·저장 비교장 유한값과 RK3 거리 유지를 확인했다.
이는 A9/P0 연구 구성의 제한 회귀이며 현재 기본 구성 전체 인증은 아니다.
이번 후속은 R4의 특정 예외·NaN/Inf·zero-budget 및 관측 전 예외, R2의 raw/방향 로그·집계,
R5의 3상태 정책 연결을 닫았다. 생산 API의 Impl 예외 전파는 유지하며, 혼합 MPI 실행과
rank-local 예외의 동시 중단, Impl 반환 후 public wrapper 예외는 이번 범위 밖이다.
O1의 두 조건/기호 정정을 완료했다. O2/O3는 실제 경로의 제한된 source/관측 및 sampled FD 근거를
추가했으며, 다음은 전이 cache·다른 상태/방향·FD 오차 통제와 O4/E/T/A/F의 연결이다.
Stage3/Newton0의 실제 trust-accepted K/R 쌍과 native RMS 감소를 추가 관측했다(추가 RHS/JVP/PC0).
Stage2 CONVERGED 로그는 있으나 Stage3·시간스텝은 완료하지 않았다. 한 computed merit 감소로
정확한 forcing·inverse/Lipschitz 상계나 전체 Newton 수렴을 추론하지 않는다.
E/T/A/F의 증명·실험이 모두 완료된 것처럼 묶지 않는다.
검증 결과와 source/executable/input hash는 별도 진행 보고서에 연결해 상태를 한 항목씩 갱신한다.

alb 산술·EOS 전파 후속: [보고서]((202611101248)_alb_error_propagation_and_replay_fix.md) · [자료](evidence/alb_error_20261110/README.md).

## PR343 후속의 좁은 해소 상태

- [x] P3: mass-floor mask detach/where의 부정확한 미분가능성 주석과 Pa 단위 설명 정리. 수치식 불변.
- [x] 같은 Stage3 K/R 쌍의 EOS 자료를 원래 trial 호출에서 확보하고 기존 hash·native RMS와 대응.
- [x] 지정 profile·fixed coefficients·FP64 basic-operation 가정 아래 EOS 정의역64층 조건부 양수.
- [x] 저장된 반올림 EOS 입력에서의 arithmetic-only 오차를 양 endpoint 전체점의 MPFR 구간으로 제한.
- [ ] 상류 inverse-density/alb 생성과 실제 RHS로의 전파를 제한.
  고정 반올림 base 입력의 alb 산술·EOS induced shift·native alpha/p 합성 성분은 조건부 상계를 확보했다.
  theta는 직접 byte 포착 대신 FNV-일치 재구성이라는 조건이 남고, 하류/다른 RHS는 미완료다.
- [ ] 전체 RHS/state/scaling/norm error e0,e1를 묶어 같은 수용 쌍의 정확한 감소 충분조건 인증.

새 실행은 Stage2만 수렴 로그가 있고 Stage3 첫 수용 갱신 뒤 종료86, 완료 timestep0이다.
초기21장·계수의 보관 RK3/A9 비교를 재사용했으며 새 RK3·endpoint·속도 비교는 없다.
기존 회귀와 P1 NO-GO를 다시 열지 않았고 E/T/A/F는 미완료다.

## PR344 후속의 좁은 해소 상태

- [x] P3 재현 경로: 동봉된 receipt를 script-relative로 읽고 stdout만 출력. 임의 cwd/복사본 교란 검사 통과.
- [x] 보존 base 입력의 source/dtype 대응. PB는 captured bytes 일치, theta는 FNV-일치 재구성으로 한정.
- [x] 그 입력을 정확한 실수로 정의한 alb 오차와 두 endpoint EOS 전파의 조건부 MPFR 상계.
- [x] alb 오차로 기존64층 alpha 구간을 실제로 넓혀 exact-real 양수 domain 재확보.
- [ ] native al_pert 산술 및 PGF·부력·질량 변환·나머지 RHS까지의 출력별 오차 전파.
- [ ] input formation과 같은 metric의 scaling/norm/float 변환을 포함한 e0,e1 및 정확한 감소 충분조건.

새 native/RK3·endpoint·비용 비교는 없다. 기존354748d6 자료를 오프라인에서만 분석했다.
induced al_pert는 실수식의 입력 민감도이고 native al_pert 오차가 아니다. 새 composed alpha/p는
alb와 EOS 산술을 직접 함께 감싼 값이므로 이전 EOS 성분을 다시 더하지 않는다.
전체 Newton/Stage3 반복·시간 적분 수렴, W 잡음·실용 비용·5일 미완료를 유지한다.


## PR345 후속: 미저장 출력과 활성 W 성분

[진행 보고]((202611101418)_native_alpert_and_w_pressure_component.md) · [재현·조건](evidence/eos_rhs_20261110/README.md).

- [x] native al_pert를 직접 포착하지 않고도 기존 FP64 덧셈 항등식과 rounded-operation tube로
  endpoint 조건부 상계를 계산했다. 3.83365e-15/3.82904e-15m³/kg 미만, induced shift를 중복 합산하지 않는다.
- [x] ImplicitOnly에서 W는 pressure 차, U/V는 primary와 NH4, inverse-alt diffusion은 explicit-only라는 source 소유를 구분했다.
- [ ] W pressure-input 성분의 실제 RHS/metric 연결을 닫는다. 후보 RDN/g와 source scale-floor 조건 아래
  실수 입력 민감도·가중 RMS 성분을 계산했으나 Stage3 RDN/g·실제 가중치 값은 미포착이다.
- [ ] W 자체 산술/계수 형성, U/V primary·NH4 및 다른 RHS의 평가오차를 같은 W,N으로 제한한다.
- [ ] epsG+nu로 full e_i를 조립하고 정확한 잔차 감소 충분조건을 인증한다.

새 native·MPFR 전수·RK3·예보장·성능 재실행은 없다. 기존 데이터와 source 가정을 사용한
조건부 성분 분석이며 실제 전체 W 오차, Stage3 반복 수렴 및 E/T/A/F는 완료가 아니다.
