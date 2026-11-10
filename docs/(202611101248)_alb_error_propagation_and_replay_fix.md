# alb 산술 오차의 EOS 전파와 재현 경로 정정

작성: 2026-11-10T12:48:44.620299+09:00.
기준 main `2c1dcce1d82560297b3259a5e7687dd52b2e38c2`(PR344).
이번 변경은 오프라인 분석·재현 스크립트·기록이다. 생산 수치식·tolerance·실행 파일을 바꾸지 않았다.
Green·Red: GPT-6.1-Terra medium.

## 해소한 범위

| 항목 | 판정 | 근거 |
|---|---|---|
| P3 descent_budget 절대경로·출력 덮어쓰기 | 해결 | 동봉 receipt의 script-relative 경로, stdout JSON, 격리된 cwd 재생·입력 교란 거부 |
| 상류 alb 실수식 입력 | 조건부 대응 | PB 직접 byte 일치, th_base는 재구성값과 당시 FNV 일치만 |
| 고정 입력에서 alb 산술 오차 | 조건부 상계 | 실제212544점 전체 MPFR256, source scalar/tensor dtype 대조 |
| alb가 바뀔 때 EOS 영향 | 성분별 상계 | alpha/al_pert/p의 induced shift, native alpha/p 대 합성 실수식 직접 비교 |
| alb 오차 포함 EOS 구간 | 조건부 양수 | 기존64층 state box를 새 alb 오차로 넓힌 exact-Fraction 구간 |
| full RHS·e0/e1·정확한 merit 감소 | **미완료** | 하류 연산·native al_pert 산술·입력 형성·norm/변환 상계가 남음 |

## 재현 경로를 먼저 수정

`descent_budget.py`는 작성자 머신의 외부 receipt 대신 자기 디렉터리의
`run/accepted_step_probe.json`을 읽는다. 출력은 JSON stdout뿐이며 보관된
`descent_budget.json`을 다시 쓰지 않는다. 문서의 재생 명령도 별도 출력 파일로 보낸다.
스크립트·receipt·기존 결과만 임시 checkout으로 옮긴 뒤 다른 cwd에서 실행해 결과 JSON
일치와 파일 불변을 확인했다. 복사본 receipt만 바꾸면 역사적 쌍 assertion이 거부하여
원래 절대경로에 남은 외부 자료를 읽지 않는 점도 확인했다. 새 시험 프레임워크나 native
재실행은 필요하지 않았다. 기존 manifest는 이 두 수정 파일의 hash/크기만 갱신했다.
원래 script/manifest는 main2c1dcce1에서 추적 가능하고 원 native evidence는 바꾸지 않았다.

## 실제 base 입력과 상수

Fortran의 grid%PB/T_INIT → Tile FP32 base field → FP32 T_INIT+300 → 현재 FP64 상태로의
정확한 승격 순서를 source에서 확인했다. 보관된 표준 em_b_wave wrfinput의 SHA256은
`c3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912`다.
배열은 [k,j,i]에서 [j,k,i]로 정렬하고 periodic-X/symmetric-Y copy로 [81,64,41]에 확장한다.

PB 재구성은 captured p_base와 byte 일치하며 data FNV는14087318007462321114다.
재구성 theta의 data FNV4185430885422016772도 Stage3 receipt와 일치한다. 그러나 **원래
th_base 배열 자체는 보존되지 않았다**. 따라서 theta는 collision-qualified fingerprint
근거이며 직접 byte 동등성이나 충돌 없는 증명이 아니다. 이하 수학적 상계는 재구성과
포착이 실제 입력에 충실하다는 조건을 둔다. shape/dtype/상수도 source/receipt와 대조했다.

활성 상수는 Rd=287, cv=717.5, cp=1004.5다. Rd=287.04를 넣으면 다른 문제를 분석한다.
상류 호출의 p1000mb=100000은 별도 EOS p0와 구분한다. 권위 함수는
`wrf_hydrostatic_pressure.cpp::compute_inverse_density`이며 실수 목표는

\[
 alb^*=\frac{R_d}{p_{1000}}\theta_b
       \left(\frac{p_b}{p_{1000}}\right)^{-c_v/c_p}.
\]

저장/재구성된 반올림 theta_b,p_b와 float 상수를 정확한 실수 입력으로 정의했다.
T_INIT/PB 생성·theta 형성 이전의 오차까지 포함하는 목표로 바꾼 것은 아니다.
소스는 scalar 비율을 double로 형성하지만 wrapped scalar가 tensor dtype을 자동 승격하지는
않는다. 여기서는 실제 base tensor가 FP64로 정렬된 경로이므로 FP64 계산을 분석한다.

## 전파식과 중복 계산 방지

다른 EOS 입력을 고정하고 D=c1(MUB+mu)+c2, beta=c1*MUB+c2라 하면

\[
 \Delta\alpha=\frac{\beta}{D}\Delta alb,\qquad
 \Delta\alpha'= -\frac{c_1\mu}{D}\Delta alb.
\]

이들은 현재 실수 EOS의 정확한 대수적 관계다. PHB hydrostatic 항등식은 필요하지 않으며
생산 실행식을 바꾸지도 않았다. 압력은 positive-domain EOS를 두 alb 입력에서 각각
방향 반올림으로 평가해 차이를 감쌌다. derivative의 추정값을 정확한 상계처럼 쓰지 않았다.

MPFR는 다음 세 양을 분리한다.

1. captured native alb와 이상 상류 alb*의 차이.
2. 실수 EOS에서 alb_hat→alb* 교체만으로 생긴 alpha/al_pert/p 변화(induced).
3. captured native alpha/p와 **alb*를 넣은 합성 실수 EOS** 사이의 직접 차이(composed).

3은 상류 alb와 native EOS 산술을 함께 포괄하는 직접 성분 상계이므로, 여기에 이전
EOS 성분이나2를 다시 더하면 같은 오차를 중복 계산한다. native **al_pert는 보존되지
않았다**. 그 열은2의 정확한 입력 민감도 성분일 뿐 전체 native al_pert 산술 오차가 아니다.

## 실행과 수치 결과

code·입력·preflight38개 파일을 사전 고정하고 오프라인 all-cell 계산1회만 수행했다.
종료0, 고정 파일 불변, MPFR256 RNDD/RNDU enclosure와 RNDU norm 축적을 확인했다.
재생되는 원자료는 native354748d6의 Stage3/Newton0 수용 쌍이고 새 native 실행은 없다.
두 끝점 각각212544개 EOS 원소를 사용했으며 소유점만이 아니라 alias 포함 전체 배열이다.

상류 alb의 Linf 상계는 **1.245771593971007e-15 m³/kg**, L2 상계는 **1.515964469670452e-13 m³/kg**다.

| 끝점 | induced alpha Linf(m³/kg) | induced al_pert Linf(m³/kg) | induced p Linf(Pa) |
|---|---:|---:|---:|
| base | 1.2459308967870233e-15 | 1.5930281601659905e-19 | 2.56112090719958e-11 |
| accepted | 1.2459283553145189e-15 | 1.5676134351207182e-19 | 2.5611177915286037e-11 |

| 끝점 | composed native alpha Linf(m³/kg) | composed native p Linf(Pa) | composed native p L2(Pa) |
|---|---:|---:|---:|
| base | 1.6996994953743442e-15 | 6.555505427197812e-11 | 4.484429798310192e-09 |
| accepted | 1.695081180724485e-15 | 6.656069455240462e-11 | 4.471842556310311e-09 |

표는 binary64 상계를 표시한다. 정확한 `*_binary64_hex`가 함께 보관되며 decimal을
exact rational upper bound로 잘못 해석하지 않는다. MPFR의 올바른 방향 반올림 및
충실한 포착/재구성 입력은 명시된 신뢰 가정이다. native libm의 uniform ULP bound를
추정하거나 실제 pow dispatch 이름으로 정확성을 인증하지 않았다.

## 이전 frozen-alb 하한을 그대로 재사용하지 않음

각 층 j의 alb 오차 상계 E_j와 기존 같은 state box에서

\[
 b_j\ge\max\left|\frac{c_1MUB+c_2}{D}\right|,\qquad D\ge D_{-,j}>0
\]

를 정확한 Fraction으로 계산했다. beta는 MUB 범위의 양 끝점에서 절댓값 최댓값을,
분모는 기존 양의 하한을 사용한다. 그러면 이전 alpha 구간[l_j,u_j]를
[l_j−b_jE_j,u_j+b_jE_j]로 넓혀야 한다. mu 범위로 al_pert의 입력 민감도 상계도 계산했다.
새 alpha 하한>0을 검사한 뒤 기존 theta 범위와 새 alpha 범위에서 exact-real power base를
다시 계산했다. 이는 가상의 binary64 재실행 오차까지 포괄하는 주장은 아니다.

64/64층이 양수다. 최대 alpha 확장은 약1.24605184706512e-15 m³/kg이며 보수적인 표시용
전체 alpha 하한은0.7352597028172149 m³/kg이다. power-base 하한은0.19870572374355447다.
정확한 하한 Fraction은 모두 바뀌었다. 출력 자릿수에서 이전과 같아 보이는 경우에도
이전 frozen 하한을 새 하한으로 대입하지 않았다. 원래 box·projection·fixed fields 및
basic-arithmetic 가정은 계속 필요하다. CLI는 역사적 receipt와 원래 domain hash까지
고정하여 다른 box를 이 분석과 섞으면 거부한다.

## 검증·보존 범위와 다음 항목

합성 검사는 정확한 식, 잘못된 alb의 영향, al_pert 계수, alpha≤0 domain 거부,
rdnw 부호 및 reconstruction metadata 오류를 겨냥했다. Fraction widener는 정확한
소형 예와 zero-crossing 거부를 확인했다. Green/Red가 source/input 연결,38개 pin,
16개 원배열 hash, 모든 aggregate/per-level hex와64층 Fraction을 교차 검토했다.
MPFR interval/NPY/norm utility는 이전 checker의 source hash를 명시해 재사용한 독립
검산 코드이며, 생산 수식의 권위 위치는 원래 hydrostatic 함수에 남아 있다.
일반 CI는 기존 기본 연구 경로의 근거이며, 새 MPFR 계산의 직접 근거는 별도 집중시험과
오프라인 실행 receipt다. 생산 ABI 변경이 없어 모델을 재빌드하지 않았다.
Graphify의43개 source hash를 새 main과 대조했고 재현 스크립트와 분석 코드의 영향을
추적했다. 그래프는 Fortran·callback·dtype·FP 의미의 수치 증명이 아니다.

기존 archive/native 결과와 P1 NO-GO는 재사용하며 새 WRF/RK3·endpoint 예보장·속도
비교를 하지 않았다. 원자료의 Stage2 수렴/Stage3 미완료/완료 timestep0 범위도 유지한다.
표준100km·40×80×64·h600의 초기21장·계수 A9/RK3 비교 근거는 이전 범위로만 재사용한다.

다음은 이 출력별 오차를 실제 PGF·부력·질량 변환 및 나머지 RHS로 전달하는 작업이다.
특히 native al_pert 산술, input formation, scaling/norm/float 변환이 남는다. 같은 성분을
중복 계산하지 않고 같은 W=diag(stored Sinv)와 N에서 full residual 평가오차와 norm 오차를
각각 제한해야 한다. 전체 e0+e1<4295394570649/2251799813685248는 여전히 미인증이다.
Pa나 m³/kg의 작은 오차를 dimensionless RMS 예산에 직접 더하지 않는다.
Stage3 반복 수렴·역증폭/비선형성·실제 시간 지도·W 잡음·실용 비용·5일은 계속 미완료다.

[재현 범위](evidence/alb_error_20261110/README.md) ·
[증거 목록](evidence/alb_error_20261110/manifest.json).
