# 수렴 검토 해소 체크리스트

작성: 2026-11-02T13:16:08.084142+09:00.
개발 기준 main9292c6c896fcec0f338d0fec7d27bd803e4c620f(PR337 병합).
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
- [x] 현재 별도 PR338의 공통 prepared-A/P0-P1 비교는 NO_GO 기록으로 구분한다.
  현재 기준 main에는 아직 없으며, native-FWAD 성능·전체 수렴 보장으로 합치지 않는다.
- [x] 이전 dry240초·두 제어 E0–E2, roll·미분·관리기·출력 입력 검사의 원래 완료 범위를 보존한다.
- [x] 수치식을 바꾸지 않고 원래 h600·최종 Newton 기준·필터·감쇠를 유지한다.
  작은 h 제조검사는 코드 계약용이며 실용 RK3 비교를 대체하지 않는다.

## R — 실제 사용하는 방향과 반환 상태의 잔차(즉시 우선)

| ID | 해소할 조건 | 최소 근거 | 상태 |
|---|---|---|---|
| R1 | 후처리된 실제 dK의 implemented A 잔차와 성공/품질 판정 일치 | production Direct-U/halo 경로 반례, S=I 및4I, 변경 없는 대조 | 완료: 실제 public 경로 시험101/101 |
| R2 | raw Krylov receipt/warm-start와 실제 방향 지표 구분 | 원래 GMRESResult 보존, 실제 방향 log, entry exemption·trust/회복 소비자 대조 | 부분 해소: raw/실제 지표 분리; 분류기 용어 후속 |
| R3 | 반환 K의 final_residual은 그 K에서 관측한 같은 G_K·S·mask RMS | 한 번 갱신 후 예산 종료, adaptive/debug 별도, 독립 해석식 검산, 추가 RHS 없음 | 완료: adaptive on/off 독립 잔차 일치 |
| R4 | 예외/NaN/zero-budget 반환의 잔차·측정 여부 진실성 | early-return 분기별 재현; 측정하지 않은0을 실제 잔차로 쓰지 않음 | 미완료 |
| R5 | stage acceptance와 실패 정책을 경로별로 명시 | mode3 severe gate와 mode0/1/2 permissive 정책을 분리 | 검토 본문 근거; 연결 검증 미완료 |

R1의 비율은 Krylov의 unweighted S-coordinate norm 및 zero-RHS 규칙을 따른다.
그 값은 실제 사용한 matvec에 대한 잔차이며, JVP가 전체 residual의 정확한 미분이라는
증명이나 전역 inexact Newton 정리를 뜻하지 않는다. raw Krylov 실패의 원인을 후처리로
잘못 돌리거나, 후처리 실패를 raw Krylov 비수렴으로 진단하지 않도록 구분한다.
R3에서 일치하는 관측 상태가 없으면 측정 여부를 false로 남긴다. 추가 RHS는 mutable
Tile 문맥과 비용을 바꿀 수 있으므로 단순 실패 보고를 위해 자동 재호출하지 않는다.
그 미측정 상태가 기존 실패 정책에서 어떻게 분류되는지는 R4/R5 검증 대상이다.

## O — 같은 residual·연산자·문맥

- [ ] O1 mode3의 G_K(K)=K−F_I(Q+hγK;c), 고정 c, projection, S, mask를 하나의
  실제 함수 정의에 연결한다. state residual G_Y=hγG_K와 rate residual을 혼동하지 않는다.
- [ ] O2 활성 참조/omega/halo/cache 경로의 상태 의존성과 AD 연결을 확정한다.
  frozen reference가 수학적 독립 입력인지, update를 미분해야 하는지 경로별로 구분한다.
- [ ] O3 native JVP의 가법성/동차성·residual 미분 일치·오차 gap을 관련 방향에서 확인한다.
  A10의 한 방향 반복, prepared graph 검증은 그 범위만 재사용한다.
- [ ] O4 periodic seam/대칭벽/고정 슬롯/halo의 제약 공간과 projector 접선을 검증한다.
  에너지 내적에서 orthogonal하다고 가정하지 않는다.

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
실패 반환 시험을 통과했다. 보관 A9/P0 구성의 h600 한 스텝(현재 기본 구성 인증 아님)은
3stage 수렴·wrapper3/0·끝174숫자 배열 parity·저장 상태 유한값과 RK3 거리 유지로 확인했다.
다음 우선순위는 R4의 예외·NaN·zero-budget 반환과 R2 분류기 용어, R5 수용정책의 연결이다.
E/T/A/F의 증명·실험이 모두 완료된 것처럼 묶지 않는다.
검증 결과와 source/executable/input hash는 별도 진행 보고서에 연결해 상태를 한 항목씩 갱신한다.
