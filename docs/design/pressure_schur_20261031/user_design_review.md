# AD-SDIRK3 돌파구 설계: 해결 가능성, 구조적 병목, 압력 기반 전방 코어

검토일: 2026-10-31. 저장소 기준: `gonos2k/AD-SDIRK3`, `bceffd2f190d5d038dcca4a7d615fe581d8a88c0` (PR #327 병합).

## 0. 판정

건조 비정역학 WRF 방정식을 600초 수준에서 암시/반암시적으로 계산한다는 목표 자체는 해결 가능한 연구·개발 문제다. 현재 사례에 대해 해가 존재하지 않는다는 근거는 없다. 다만 일반 비선형 전방문제의 전역 존재·유일성이나 임의 시간간격의 정확도를 증명한 것은 아니다.

현재의 **큰 tensor RHS 전체를 매 Krylov 방향마다 forward AD로 재평가하는 구조**, 현재의 불완전한 전방 공간 연산자 대조, 수백 배의 역사적 비용 격차를 그대로 둔 채 국소 수정만 반복하는 접근은 실용적 전방 대체를 위한 주력 경로로 권하지 않는다. ARK324 계수 자체를 버릴 이유보다, 풀어야 하는 선형계의 구조·계산 표현을 바꿀 이유가 훨씬 강하다.

권고는 기존 코어를 기준/검증 경로로 보존하면서, **압력 Schur 축약 + 연직 line solve/수평 다중격자 + 컴파일된 fast 연산자**를 개발하는 것이다. 단기에는 기존 비선형 잔차와 시간맵을 바꾸지 않고 압력 기반 전처리를 추가한다. 목표 구조는 고정된 한 대시간스텝 기준장의 affine fast split을 통해 같은 선형계를 세 암시적 단계에서 재사용하는 형태다. 두 변경은 분리 검증한다.

## 1. 이번에 실제 확인한 범위

- GitHub 연결로 최신 main을 다시 확인했다. 이후 변경이 없는 기준에서 검토했다.
- `docs/PROBLEM_DEFINITION.md`의 목표·완료 범위, 표준 RK3/ARK 실행 요약, mode 4의 조건부 참조수송 설계, 현행 Newton `apply_jacobian`, forward-AD router, raw principal 전처리 구성 코드를 읽었다.
- 40,000행 이상 monolithic tile 구현 전체를 이번에 재독해한 것은 아니다. 대형 파일의 행별 fetch가 빈 내용을 반환하여, 현재 리비전에 대응하는 앞선 패치·해당 문서·작은 모듈과 직접 반환된 함수 범위를 사용했다. 완전한 전 저장소 인증을 주장하지 않는다.
- WRF 공식 기술문서, IMEX/HEVI 성능 비교, LFRic 압력 Schur/다중격자, Rosenbrock-W 대안, CliMA 공식 implicit solver 구현 설명, PETSc/SUNDIALS 문서를 조사했다.
- `design_checks.py`로 압력 축약과 고정 기준 affine split의 독립 제조계산을 실행했다. WRF 실행·실제 5일 예측·native 속도 측정은 하지 않았다.

## 2. 왜 현재 경로가 오래 걸리는가

### 2.1 목표와 완료 기준의 불일치

프로젝트 상단 정의는 여전히 240초·h=0.9375초 제한 사례를 중심에 두고 효율을 선택사항으로 적고 있다. 사용자가 현재 선택한 목표는 표준 100 km·h=600초에서 기존 split-explicit RK3 대비 전방 정확도와 비용이다. 제한된 수반·쌍둥이 사례의 통과가 이 목표의 완료를 대신하지 않는다.

새 전방 목표는 하나로 고정한다: 표준 `em_b_wave`, 같은 초기·경계·물리 정책, h=600초 중심, 300/150초 민감도, 5일 비교. 정확도·안정성·비용을 동시에 평가한다. 과거 결과는 폐기하지 않되 별도 범위로 유지한다.

### 2.2 비용 모델

현재 한 대시간스텝은 여러 Newton 보정과 수십 회씩의 Krylov 적용을 요구한다. `apply_jacobian`은 고정된 U_eval에서도 방향마다 `compute_rhs`를 forward AD로 다시 실행한다. AD는 미분의 도구이지 구조를 활용하는 선형솔버의 대체가 아니다.

역사적 quiet 6시간 비교는 동일 h에서 ARK/RK3 적분시간 약443배였다. 이것은 최신 속도비가 아니며 동일 정확도 인증도 아니다. 그래도 단순한 시간간격 확대만으로 상쇄할 수 있는 수준이 아니다. 비용비 r, 시간간격비 k이면 동일 예측시간의 비용비는 대략 r/k다. r≈443이면 시간간격만으로 손익분기를 맞추는 h는 약2.66e5초로, 이 문제의 수송·위상 정확도와 양립한다고 볼 근거가 없다.

mode4 첫 스텝에서 JVP가240→192로 줄었지만 계측시간/호출은0.189→0.504초로 커졌다. 많은 항을 암시화하면 반복 수가 줄어도 더 느려질 수 있다. 이 문제는 단순한 옵션·감쇠 조정으로 해결되지 않는다.

### 2.3 해법을 바꿔도 같은 공간 문제인지 아직 완결되지 않음

저장소 자체가 stock WRF와 C++ Full 공간 연산자의 전체 동등성을 확정하지 않는다. 같은 상태의 순간 경향 비교와 서로 다른 시간적분 예측장 비교를 먼저 구분해야 한다. 필터가 다르면 전체 경로 비교로 보고해야 한다. RK3가 항상 정확해는 아니다.

## 3. 문헌이 주는 설계 결론

### 3.1 Gardner et al. (2018)

비정역학 대기모델에서 21개 ARK/IMEX 방법과 분할·solver를 비교한다. ARK324와 ARS343은 좋은 결과를 보였으나, 수평 성분을 더 암시화한 방법은 더 큰 시간간격을 얻어도 전역 풀이비용을 상쇄하지 못한 사례가 있었다. 따라서 현재 문제를 ARK324 계수 하나의 실패로 보기보다 partition·solver·구현의 결합으로 봐야 한다.

### 3.2 Giraldo et al. (2024)

HEVI에서 Newton-GMRES, Newton-LU, 선형화된 HEVI를 비교한다. 논문의 이상화 시험에서 각각의 비용 차이는 구조화된 직접풀이/선형화가 중요한 이득을 줄 수 있음을 보여준다. 보고된 배수는 해당 모델 결과이며 WRF에 이전되는 보장이 아니다. HEVI의 수평 음향 제한 때문에 이번 h600 목표에 그대로 적용할 수는 없다.

### 3.3 Maynard, Melvin & Müller (2020), Betteridge et al. (2023)

LFRic은 큰 혼합 시스템을 압력 Schur 문제로 줄이고, 연직 line smoother와 수평 semi-coarsening 다중격자를 사용한다. 큰 수평-연직 비와 압력 결합을 이용한다는 점이 핵심이다. 현재 raw vertical principal을 폐기하기보다 그것을 Schur 내부의 연직 처리로 재사용할 수 있다.

### 3.4 Reddy et al. (2023)

비정역학 Euler의 압력 Schur 형식을 유도해 빠른 계를 작은 elliptic 문제로 줄인다. WRF의 hybrid 질량좌표·stagger·상단은 별도 유도가 필요하지만, 보존변수와 압력 축약의 방향은 직접 참고 가능하다.

### 3.5 CliMA (2026 논문 및 현재 공식 구현 설명)

default sparse analytic Jacobian, 구조화된 직접풀이, 행렬 갱신 주기, sparse/dense AD 참조를 구분한다. AD를 버리라는 뜻이 아니라 dense/범용 AD 경로를 검증 기준으로 사용하고 생산 solve는 구조화할 수 있음을 보여준다. 수직 column HEVI이므로 그 분할의 h600 안정성은 별도 문제다.

### 3.6 Rosenbrock-W와 저마하 반암시 방법

근사 Jacobian을 활용하는 검증된 W-method는 대안이지만, 기존 ARK에서 Newton을 한 번만 하고 끝내는 것과 같은 방법은 아니다. 현재 단계에서 tableau·변수·split·solver를 한꺼번에 바꾸지 않는다. 압력 solver가 마련된 뒤 외부 time integrator만 비교할 수 있다.

### 3.7 Stock WRF RK3에 대한 주의

WRF 기술문서는 해당 RK3가 선형 문제에는 3차, 일반 비선형 문제에는 2차라고 명시한다. 별도의 음향 subcycling·필터도 결합된다. RK3/ARK 모두 무조건8배 오차 감소를 요구하는 기준은 부적절하다. 같은 공간 문제·필터 정책 아래 각 방법의 기대 차수와 실제 세분화 경향을 판단해야 한다.

## 4. 권고 아키텍처

### 4.1 보존할 것

WRF 입력/출력·Fortran bridge, 검증된 stagger와 실제 face-flux 계산, EOS, FP64 상태 전달, 물리 허용성 검사, ARK324 계수, 기존 fixed-tolerance 결과, AD JVP/VJP oracle, 제한된 완료 사례를 보존한다.

### 4.2 주 경로에서 줄일 것

Krylov마다 전체 primal+dual RHS 재평가, 크기가 작은 Hessenberg/Givens의 tensor 객체 연산, 동일한 기본장/격자계수 재구성, 모든 수송을 암시부에 넣어 비싼 JVP를 반복하는 것을 주력으로 삼지 않는다. 모든 그래프를 영구 보관하는 방식도 피한다.

### 4.3 첫 수정은 시간맵을 유지한 전처리 교체

기존 A=I-a J_I, a=h*gamma와 원래 nonlinear residual은 유지한다. 전처리의 압력-발산 수평 결합을 보강한다. 현재 raw principal은 연직 W/PH/theta 중심 근사이므로 수평 압력계 해석을 추가하는 것이 새로운 핵심이다.

Krylov 좌표가 S^{-1} A S이면 전처리 적용도 S^{-1} P^{-1} S로 맞춘다. D 가중 내부 노름과 최종 비가중 Krylov/물리 노름을 혼합하지 않는다. 원래 A로 계산한 true residual이 최종 판단이다.

### 4.4 목표 구조: macrostep-fixed affine fast split

Q'=F(Q), r_n=Q_n, L_n은 현재 기준에서 구성한 음향·중력 principal 연산자로 둔다.

    F_I(Q;r_n)=L_n(Q-r_n)
    F_E(Q;r_n)=F(Q)-F_I(Q;r_n)

r_n과 L_n은 해당 대시간스텝의 모든 단계에서 고정한다. 실제 F는 바꾸지 않는다. 강한 비선형 나머지는 F_E에 남으므로 explicit stability를 확인해야 한다. L_n의 근사가 아무리 조악해도 무조건600초가 안정해지는 것은 아니다.

ARK implicit stage의 B_i가 정해지면,

    (I-h*a_ii*L_n)(Y_i-r_n)=B_i-r_n.

ARK324의 세 implicit diagonal은 같은 gamma이므로 그 세 단계에서 같은 행렬/전처리 구성을 재사용할 수 있다. 이는 단순히 기존 nonlinear Newton을 중간에 중단하는 것이 아니라, 정확히 정의한 다른 additive split이다.

왜 차수를 기대할 수 있는가: 한 스텝마다 파라미터 r_n,L_n을 고정하면, 그 스텝은 두 고정된 additive 함수에 기존 ARK를 적용하는 문제다. F_I+F_E=F이고 L_n과 필요한 미분이 유계이며 smooth한 조건에서는 고전적 국소 차수 논리를 적용할 수 있다. 강직 극한의 uniform accuracy, 경계/limiter/비미분 분기, 실제 WRF 차수는 별도 검증이다. 현재 U_ref_stage_처럼 단계 또는 Fast/Slow 호출 사이에 다른 값으로 바뀌는 참조를 그대로 사용하면 이 논리가 성립하지 않는다.

### 4.5 보존변수 좌표를 검토할 이유

물리 q와 질량 L을 따로 적분할 때 L*qdot=C-q*Ldot의 취소가 분할 사이에서 일어난다. 가능하면 실제 WRF의 질량·지도계수·stagger와 같은 결합변수 Q_q=L_q*q를 단계 미지수로 사용하여, 보존 flux와 질량 변화를 같은 stage에서 처리한다.

단순히 모든 곳에 mu*q를 쓰면 안 된다. U/V/W마다 다른 위치의 질량·map factor와 동적 상단 규약을 사용한다. Q_t=L_t*t에서 총온위로 바꾸면300*L_t의 기여도 일관되게 변환해야 한다. nonlinear 좌표변환은 finite-h time map을 바꾸므로 기존 출력의 bit equality를 새 방법의 유일한 수용조건으로 쓰지 않는다.

이 좌표 전환은 압력 전처리 개선과 동시에 하지 않는다. 먼저 현 좌표의 선형계/비용을 개선하고, 별도 통제된 실험으로 보존좌표를 비교한다.

## 5. 압력 Schur 설계

균일계수 예시에서 c로 정규화한 압력과 속도에 대해

    [ I    a*c*G ] [u] = [b_u]
    [a*c*D   I   ] [p]   [b_p]

이고 D는 divergence, G=-D^T라면

    S_p = I-a^2*c^2*D*G = I+a^2*c^2*D*D^T
    S_p*p = b_p-a*c*D*b_u
    u=b_u-a*c*G*p.

따라서 z^T*S_p*z=||z||^2+a^2*c^2*||D^T*z||^2>0. 이 단순 문제는 큰 음향 CFL에서도 선형계가 가역이다. 이것은 모든 nonlinear WRF stage의 존재/유일성 정리가 아니다.

실제 격자에서는 weighted adjoint G=-M_u^{-1}D^T M_p와 경계 항을 사용한다. 지형·Coriolis·수송·부력·EOS·hybrid mass·현재 Omega에 의한 결합 때문에 full Schur가 단순 SPD가 아닐 수 있다. SPD가 확인되지 않으면 PCG를 강제하지 않고 기존 FGMRES/GCR 외부 solve를 유지한다. 압력은 현재 진단변수이므로 algebraic EOS를 포함해 추가한 압력보정 블록의 정확한 소거를 유도해야 한다.

### 실제 구현 순서

1. 상수/수직계수 기준계: periodic X에 FFT, symmetric Y에 적합한 cosine transform, 각 수평모드의 연직 tridiagonal solve. 원래 방정식의 모든 변동계수가 separable이라고 가정하지 않는다.
2. 실제 계수: 기존 연직 raw principal/column solve를 smoother로 사용하고 X/Y만 coarsening한다. 동일 timestep의 coefficient cache를 재사용한다.
3. 일반 A: 위 풀이를 P^{-1}로 사용하고 original A true residual로 검증한다.
4. affine fast 경로가 완결되면 해당 fast principal의 pressure solve를 직접 stage solve로 사용한다.

현재 위상 잡음에 대해 C-grid를 단순 collocated central difference로 대체하면 안 된다. C-grid의 적절한 G,D는 Nyquist에서 gradient 크기2/dx이며 collocated centered gradient처럼0이 되지 않는다. 따라서 줄무늬가 있다고 해서 C-grid 자체의 checkerboard null을 원인으로 단정하지 않는다.

## 6. HEVI·다른 대안의 우선순위

- **압력 Schur + native fast operator:** h600을 유지하는 주력 권고.
- **현재 Newton + Schur 전처리:** 물리/시간맵을 그대로 두는 저위험 첫 단계.
- **macrostep-fixed linear fast ARK:** Newton의 반복 비용을 제거할 목표 경로. fixed-reference/order/잔여 stiffness 증명이 필요하다.
- **HEVI column-LU:** 유력 대안이지만 h600에서 수평 음향 CFL을 통과한다고 가정하지 않는다. 더 작은 h로도 총 time-to-solution이 이길 수 있으며, h600 필수 목표와는 구분한다.
- **Rosenbrock-W:** 검증된 coefficient와 order 조건을 갖춘 별도 time integrator 후보. 현 ARK를 임의로 한 Newton 반복으로 자르는 것과 다르다.
- **기존 RK3 + 정확한 derivative:** 암시적 방법이 연구 효율 문턱을 통과하지 못할 때의 전방 안전경로. 현재 사용자가 자료동화보다 전방을 우선하므로 gradient 확장을 먼저 하지는 않는다.
- **GPU·다중 MPI·LLM 자동튜닝:** 올바른 구조가 결정된 뒤의 구현 도구이며, 현재 큰 work-count와 미확정 noise 원인의 대체 해결책이 아니다.

### HEVI의 구체적 제한 예

상수 c=340m/s, dx=100km, h600, 필터 없는 1차원 C-grid 음향계에서 최대 h*omega는4.08이다. 현 ARK explicit tableau의 imaginary-axis 안정경계는 약2.48418이며, 그 최대모드의 증폭률은약7.26388이다. 단순 HEVI 전환으로는 이 예의600초를 유지할 수 없다. 1차원 예의 한계 h는약365.32초다. 이 수치는 실제 WRF 지형·2D 모드·필터의 안정한계가 아니라, 수평 음향 제약을 무시할 수 없다는 반례다.

## 7. 잡음 원인을 짧게 판별하는 절차

고파수 W 증가가 존재하지만, 현재 원인은 확정되지 않았다. 세 가설을 분리한다.

A. 동일 상태의 Full spatial operator 자체가 다른가? 동일한 FP64 상태에서 압력/질량/부력/수송/경계 경향을 비교한다. RK3 예측장과 다른 ARK 예측장의 경향을 비교하면 상태 차이와 구현 차이가 섞인다.

B. Full은 같지만 splitting/time map이 불안정한가? 실제 동일 상태의 J_full,J_fast,J_slow와 one-step linear response를 비교한다. 상수계수 frozen-symbol 분석은 가설 선별이며 nonuniform 전체모델 안정성 증명이 아니다.

C. 반복풀이의 근사 오차가 문제인가? 같은 time map에서 tight vs candidate solve를 비교한다. h를 바꾸거나 damping을 함께 바꾸지 않는다.

먼저 보존된24/30/36h 상태와 이미 있는 grid-band 진단을 사용한다. local same-state RHS 평가는 저장된 상태를 사용할 수 있지만, 그 평가를 연속 WRF restart로 주장하지 않는다. native accepted-state snapshot과 계수·경계 문맥이 필요하다. V/W seed가 초기화에서 사라진 과거 문제를 반복하지 않도록 초기화 이후 실제 perturbation을 확인한다.

줄무늬를 지우기 위해 damping을 먼저 키우지 않는다. 실제 pressure-wave damping symbol을 RK3 필터와 비교하고, 같은 이름의 coefficient라도 차원·적용 변수·시간의존성·위치가 다르면 다른 연산자로 기록한다.

## 8. 완료 문턱과 중단 규칙

과거 제한 사례는 완료 상태로 보존한다. 이번 구조 선택에는 세 개의 산출물만 요구한다.

### Gate A: 같은 native 단계의 linear problem

대표 stage의 상태/방향/스케일/경계가 고정된 비교. 새 Jv의 AD 대조, Schur 적용, original A true residual과 전체 stage 비용을 함께 측정한다. 쉬운0.1초 fixture 통과를600초 성능으로 대신하지 않는다.

### Gate B: 초기 6–12개 연속600초 스텝

초기화 이후 동일 상태, source/executable 고정, 1/2/4 threads 중 선택한 조건 고정. wall·monotonic·적분구간, JVP/linear solve수, setup/apply/RHS 비용, 실제 field 차이·고파수 변화 기록. 모든 새 scalar time series를 생산로그에 추가할 필요는 없다.

가속 목표는 제안값이다: 작은 개선 후보를 무한 반복하지 말고, 구조 교체 후보는 먼저 기존 ARK 대비 큰 폭(예: 총stage비용10배 감소 가능성이 있는지)을 판단한다. 이것은 달성 예측이나 과거 허용조건 변경이 아니다. 하한 비용으로도 목표를 못 맞추면 그 후보 확장을 중단한다.

### Gate C: 한 후보의 표준5일 비교

600초 중심300/150초 민감도, 기상학적 진폭·위상·성장·질량/열역학·grid-band 진단. 정확도 합격과 정상 종료를 분리한다. 비용은 같은 장비·출력·정밀도에서 비교하며, 후보별 정확도 문턱을 만족하는 설정만 효율비에 사용한다.

새 물리 오차예산은 perturbation 원점에 의존하는8*FP32eps*max(|x|,1)만으로 정하지 않는다. 반복풀이에 의한 상태오차를 time-discretization 오차보다 작게 배분하고, 참조 불확실성과 기상적 요구를 함께 기록한다. 허용오차를 결과에 맞춰 사후 조정하지 않는다.

## 9. 독립 계산 결과

`calculations.json`을 기준으로 한다.

- 40x80x64 scalar pressure example: true relative residual4.1787e-16, solution error4.4734e-12.
- 독립 full coupled block와 Schur소거 비교: relative solution difference1.3647e-12, full true residual6.6934e-12.
- 제조 nonlinear ODE의 macrostep-fixed affine split: 마지막 두 observed orders2.8931,2.9451, 3차로 접근.
- 순수 수평 음향 HEVI 반례: 현 explicit ARK at h*omega4.08 amplification7.26388.

**이 계산들은 실제 WRF 예측/속도 증명이 아니다.** 압력 예제의 상단은 rigid boundary다. 기존 WRF의 동적 W/PH 상단을 대체하면 안 된다. 실제 variable-coefficient operator에서는 이 pressure solver를 우선 preconditioner/oracle로 취급한다.

## 10. 실행 코드와 저장소 적용

- `design_checks.py`: standalone numpy/scipy 수학 검산 및 3D pressure reference solver.
- `calculations.json`: 실행된 결과.
- `sources.json`: 원자료와 읽은 범위.
- 기존 저장소/branch에는 변경하지 않았다. 생산용 mode·ABI·solver는 추가하지 않았다.

첫 개발 묶음은 현재 branch와 격리한 research worktree에서, 기존 raw principal에 수평 pressure Schur를 연결하는 작은 구성요소와 native stage driver 하나로 제한한다. monolithic 전체 파일 정리, 새 저장소 강제 이전, 모든 CI/수반/Hessian 확대는 선행조건이 아니다.

**최종 결론:** 문제를 풀 수 있는 방법 계열은 이미 존재한다. 현재 코드의 검증 성과도 재사용할 수 있다. 그러나 지금과 같은 범용 전체상태 Newton–AD-JVP 비용구조를 고집한 채 미세 패치만 이어가는 것은 실용적 전방 완성을 위한 좋은 전략이 아니다. ARK324를 유지하더라도 핵심 solve를 pressure-based structured method로 바꾸는 설계 결정을 내려야 한다.

## 참고문헌 및 코드 원자료

**[repo-main] AD-SDIRK3 pinned main**

https://github.com/gonos2k/AD-SDIRK3/tree/bceffd2f190d5d038dcca4a7d615fe581d8a88c0

검토 범위: GitHub connector; current ref verified

**[problem] Project definition and historic standard cost results**

https://github.com/gonos2k/AD-SDIRK3/blob/bceffd2f190d5d038dcca4a7d615fe581d8a88c0/docs/PROBLEM_DEFINITION.md

검토 범위: Lines1–115,450–510 read

**[newton] Actual apply_jacobian and repeated compute_rhs**

https://github.com/gonos2k/AD-SDIRK3/blob/bceffd2f190d5d038dcca4a7d615fe581d8a88c0/external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp

검토 범위: Lines6860–7040 read

**[precond] Actual vertical raw principal Schur builder**

https://github.com/gonos2k/AD-SDIRK3/blob/bceffd2f190d5d038dcca4a7d615fe581d8a88c0/external/libtorch_wrf/sdirk3/wrf_sdirk3_unified_preconditioner.cpp

검토 범위: Lines5535–5660 read

**[fwad] Forward AD/FD router**

https://github.com/gonos2k/AD-SDIRK3/blob/bceffd2f190d5d038dcca4a7d615fe581d8a88c0/external/libtorch_wrf/sdirk3/wrf_sdirk3_jvp_fwad_or_fd.h

검토 범위: Full small header read

**[reference] Future reference split audit**

https://github.com/gonos2k/AD-SDIRK3/blob/bceffd2f190d5d038dcca4a7d615fe581d8a88c0/docs/evidence/implicit_transport_short_20261031/future_reference_split_audit.md

검토 범위: Full design audit read; not an implemented feature

**[mode4] PR327 matched native comparison**

https://github.com/gonos2k/AD-SDIRK3/pull/327

검토 범위: Current-context source/patches/metrics; one-step scope only

**[WRF] Skamarock et al., A Description of ARW Version4, NCAR/TN-556+STR (2019)**

https://www2.mmm.ucar.edu/wrf/users/docs/technote/v4_technote.pdf

검토 범위: Parsed text and screenshot of printedp16; RK3 order, acoustic linearization

**[Gardner2018] Gardner et al., GMD11 1497–1515 (2018), IMEX RK for nonhydrostatic models**

https://doi.org/10.5194/gmd-11-1497-2018

검토 범위: Publisher HTML and PDF text; PDF screenshots failed; no extracted chart claims

**[Giraldo2024] Giraldo et al., JCP,113275 (2024), HEVI performance study**

https://doi.org/10.1016/j.jcp.2024.113275

검토 범위: Publisher abstract and text excerpts; reported speedups are paper-specific

**[Maynard2020] Maynard, Melvin, Müller, QJRMS146,3917–3936 (2020)**

https://doi.org/10.1002/qj.3880

검토 범위: Publisher text and author preprint text; pressure Schur and vertical-line MG

**[Betteridge2023] Betteridge et al., Hybridised multigrid preconditioners (2023)**

https://doi.org/10.1002/qj.4515

검토 범위: Publisher abstract and author archive

**[Reddy2023] Reddy et al., Schur complement IMEX DG nonhydrostatic models (2023)**

https://www.sciencedirect.com/science/article/pii/S0021999123004564

검토 범위: Publisher abstract and 2022 author preprint https://arxiv.org/pdf/2212.06247 ; final/preprint distinguished

**[CliMA2026] Yatunin et al., CliMA Atmosphere Dynamical Core (2026)**

https://doi.org/10.1029/2025MS005014

검토 범위: Publisher abstract and appendix excerpts

**[CliMA-code] CliMA official implicit-solver documentation**

https://github.com/CliMA/ClimaAtmos.jl/blob/main/docs/src/implicit_solver.md

검토 범위: GitHub lines1–190; blob b7f0ddb264b46758bcc278d0c0b6fd3ee6cb62f2

**[Lee2024] Lee, comparison of Rosenbrock–Wanner and Crank–Nicolson (2024)**

https://doi.org/10.1002/qj.4608

검토 범위: Publisher abstract; mixed efficiency outcomes, not universal superiority

**[Lee2025] Helmholtz preconditioning with Lorenz staggering (2025)**

https://doi.org/10.1002/qj.4887

검토 범위: ANU author abstract; not direct WRF verification

**[SUNDIALS] ARKODE mathematical considerations**

https://sundials.readthedocs.io/en/v7.7.0/arkode/Mathematics_link.html

검토 범위: Official documentation; linear/nonlinear solve and reuse

**[PETSc] PCFIELDSPLIT / Schur preconditioning**

https://petsc.org/main/manualpages/PC/PCFIELDSPLIT/

검토 범위: Official documentation; not a claim that a library switch alone improves performance
