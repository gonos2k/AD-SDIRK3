# Native al_pert의 조건부 상계와 W 압력 입력오차 성분

작성: 2026-11-10T14:18:33.018072+09:00.
기준 main `9e7d69d8d0cd1c335420a79f62c25c5b99ce83e6`(PR345).
기존 native source354748d6의 Stage3 수용 쌍을 재사용한다. Green·Red: GPT-6.1-Terra medium.
이번 변경은 오프라인 분석·문서이며 생산 수치식·ABI·tolerance·실행파일을 바꾸지 않았다.

## 해소 범위

| 항목 | 상태 | 범위 |
|---|---|---|
| 미저장 native al_pert의 오차 | 조건부 상계 확보 | source FP64 덧셈·기존 rounded-operation tube·기존 MPFR 상계 |
| 활성 EOS 소비 경로 | source에서 분리 | W는 pressure만, U/V primary와 NH4 별도, diffusion은 explicit-only |
| W 압력 입력오차→수학적 가중 RMS | 조건부 계산 | 실제 rdn/g·scale-floor 경로 동일성을 추가로 가정한 성분 |
| 실제 W 전체 native 오차·full RHS·e0/e1 | 미완료 | W 산술/계수 형성, U/V·NH4·다른 RHS와 norm 오차 |
| 전체 잔차 감소·Stage3 반복 수렴·E/T/A/F | 미인증 유지 | 새 native/RK3/예보장/성능 비교 없음 |

## 1. 미저장 출력은 덧셈 항등식으로 제한

소스 `calc_p_rho_wrf`는 먼저 al_pert를 계산하고, 별도 FP64 tensor 덧셈
`alt=al_pert+alb64`를 수행한다. 이번 profile의 ph와 출력 dtype은 FP64이므로 마지막
`.to(output_type)`도 정밀도를 바꾸지 않는다. 실제 미저장 값을 u_hat, 포착 alb를 b_hat,
포착 alt를 alpha_hat이라 하고 그 덧셈 오차를 eta_add라 하면

\[
 \widehat\alpha=\widehat u+\widehat b+\eta_{add},\quad
 \alpha^*=u^*+b^*,\quad
 \widehat u-u^*=(\widehat\alpha-\alpha^*)-(\widehat b-b^*)-\eta_{add}.
\]

따라서 E_u <= E_alpha_composed + E_alb_upstream + E_add다. 이전 induced al_pert를
다시 더하지 않는다. 같은 합성 실수 EOS 및 반올림 base 입력 목표를 유지한다.

마지막 덧셈의 피연산자는 **반올림된 native u_hat**여야 한다. PR345의 넓힌 exact-real
alpha 구간으로 대신하지 않았다. PR344의 고정 native-alb basic-operation I 구현을
읽고 source의 곱·합·부호·나눗셈 직후 u_hat 구간을 재구성했다. D와 마지막 rounded-alt
구간이 원래64층 결과의 Fraction과 정확히 같아야 한다. 그 후 덧셈 직전 u_hat+b_hat은
추가 FP 여유 없이 정확한 Fraction 합으로 계산한다.

각 층에서 pre-add 합과 rounded alpha의 절댓값이8 미만임을 확인했다. 기존 one-ULP
finite FP64 basic-operation 가정 아래 E_add=2^-50을 쓸 수 있다. Source 순서, 분리된
연산, 중간 정밀도와 no reassociation을 조건으로 두며 실제 backend를 새로 인증하지 않았다.

| 끝점 | E_u의 위쪽 binary64 상계(m³/kg) |
|---|---:|
| base | 3.833649509045476e-15 |
| accepted | 3.829031194395618e-15 |

두 값은 정확한 4/10^15보다 작다. 정확한 Fraction 합과 binary64 hex를 제공한다.
이는 **직접 al_pert를 포착하거나 새 MPFR을 실행한 결과가 아니다**. 입력 대응과 theta
재구성의 fingerprint 조건, 이전 MPFR 반올림 신뢰 및 source box 조건이 계속 필요하다.

## 2. 실제 implicit 소비 경로를 먼저 구분

mode3의 Newton callback은 ImplicitOnly다. 활성 source Step3.5/4/5/6과 explicit slow
Step7–10을 구분했다. inverse-alt를 쓰는 diffusion은 이 do_explicit 아래 있으므로
현재 callback에 들어오지 않는다. `diff_opt=1`이라는 이유만으로 implicit 경로에 더하지
않았고, diffusion이 전체 적분에서 꺼졌다고도 하지 않는다.

- U/V primary PGF는 p', alt, al_pert를 사용한다. Native NH4 보정은 별도 pressure 경로다.
- W의 결합 PGF/buoyancy는 pressure 차와 질량항을 쓴다. 예전 alt_w 경로는 dead source이며,
  현재 W에서 alt/al_pert를 직접 사용하는 것으로 세지 않는다.
- 같은 state에서 EOS pressure만 바꾸면 dry WPGF의 mass/buoyancy 부분은 바뀌지 않는다.
  source의 coupled→physical 변환과 map/mass 소거는 실수 산술 항등식으로만 사용한다.

## 3. W의 한 성분을 동일 metric의 상계로 옮기기

조건부 입력 계수와 state를 고정한 실수 RHS를 생각한다. m_k=c1f_k(MUB+mu)+c2f_k>0,
cq1=1인 dry profile에서 pressure 입력 변화에 대해

\[
 |\delta F_{W,k}|\le\frac{|g\,rdn_k|}{m_{-,k}}(E_{p,k}+E_{p,k-1}),\quad 1\le k<n_z,
\]
\[
 |\delta F_{W,top}|\le\frac{2|g\,rdnw_{n_z-1}|}{m_{-,top}}E_{p,n_z-1}.
\]

m_-는 실제 저장된 mu/MUB의 전체 extrema와 부호 있는 c1f를 이용해 Fraction으로
보수적으로 계산한다. 바닥은0, 이후 Qw의 copy/reflection/zero 작용은 infinity norm을
늘리지 않는다는 지정 topology 조건을 쓴다. 실수식 소거는 native FP 나눗셈/곱 오차의
상계가 아니며 그 오차는 E_rest에 남긴다.

자료에서 C1F/C2F·unit map·dry cqw 후보는 Stage3 fingerprint와 일치했다. Captured
FP64 rdnw는 top source의 FP32 요청으로 정확히 변환한다는 getter 의미를 조건으로 둔다.
RDN은 입력과 **같은 실행의 초기 wrfout**에서 FP32 byte 일치하고 두 원파일 hash도 확인했다.
그러나 Stage3에서 선택된 RDN을 직접 포착하지는 않았다. g 역시 source 초기값9.81f를
사용했지만 실제 runtime 값은 별도 관측하지 않았다. 결과의 두 verified flag는 false다.

실제 Sinv 배열도 저장되지 않았다. Source/profile에 scale_u=1, physics scaling 대체 없음,
일반 stage scale 경로라는 조건을 두면 momentum S>=1이다. FP32/64 reciprocal의 one-ULP
여유까지 포괄해 저장 가중치 상한 w_max=1+2^-23을 사용한다. 실제 가중치를1로 측정했다고
하지 않는다. 이 source-profile 조건은 arbitrary API/다른 설정으로 일반화하지 않는다.

Nw=81*65*41=215865, N=1080491이고4*Nw<N이다. H=I, 지정 Qw 및 실제 W의 상한 조건에서

\[
 \frac{\|W\,\delta G_W\|_2}{\sqrt N}
 \le w_{max}\sqrt{Nw/N}\,B_W < \frac{w_{max}}2 B_W.
\]

K를 고정했으므로 deltaG_W=-deltaF_W다. 최종 부등식은 Fraction 계산으로 평가하며
backend norm, sqrt(float(N)), float 변환의 오차 nu를 인증한 것은 아니다.

**다음 값은 RDN/g 후보 동일성과 scale-floor 경로를 가정한 exact-real 입력 민감도 성분이다.
실제 native W 전체 오차의 인증값은 아니다.**

| 끝점 | W 압력 성분 physical Linf(m/s²) | 가중 수학적 RMS 성분 |
|---|---:|---:|
| base | 4.802262241646415e-13 | 2.4011314070603423e-13 |
| accepted | 4.729164593016812e-13 | 2.3645825783885814e-13 |

W 표는 표시용 근삿값이며 엄격한 상계의 권위는 JSON의 정확한 Fraction이다.
이 값에는 U/V primary·NH4, W 자체 산술, coefficient/state formation, 다른 RHS와
scaling/norm/float conversion 오차가 들어 있지 않다. 한 성분이 작다고 full e0+e1 예산을
통과했다고 하지 않는다. Runtime RDN/g와 scale 조건의 누락을 수치가 작다는 이유로 닫지 않는다.

## 4. U/V에서 다음에 사용할 source 관계

Native face의 pressure 차 P, alt 합 A, base pressure 차 B에 대해 primary bracket의
EOS 입력오차는 다음처럼 cross product를 포함해 제한할 수 있다.

\[
 |\delta(AP+LB)|\le2|A|E_p+2E_\alpha(|P|+2E_p)+2|B|E_u.
\]

L은 al_pert의 face 합이다. 여기에 face geometry·moisture·spacing과 coupled→physical
계수를 적용해야 한다. 여기서 momentum mass/map 계수와 EOS inverse density alpha를
같은 기호로 혼동하지 않는다. NH4는 별도 항이며 이 primary 관계로 대체하지 않는다.
현재 보고서는 U/V의 수치 RMS 상계를 완료한 것으로 표시하지 않는다.

## 검증과 다음 미완료 조건

새 code/입력/review를 고정한 뒤 Fraction 평가를 각각1회 수행했고 등록한 pin 불변과
종료0을 확인했다. al_pert는 합성 예의 정확한 합·위쪽 변환·binade/hex 거부를,
W는 interior/top·RMS size/H·부호/분모 오류를 겨냥한 소형 검사를 통과했다.
Green/Red는 source, datum 연결, 모든64/65층 Fraction 및 출력 합계를 교차 확인했다.
Graphify로43개 기존 생산 소스와 관련 분석 의존성을 source hash에 맞춰 확인했다.
Graph 관계는 Fortran/stagger/branch/FP 의미를 증명하지 않는다.

새 WRF/RK3/native build/endpoint 예보장/성능 비교는 없다. 기존 100km·40×80×64·h600
입력의 A9/RK3 초기21장 비교와 one-step 회귀는 원래 범위로만 재사용한다. 이번 source
자료의 Stage2 수렴·Stage3 미완료·완료 timestep0이라는 범위는 바꾸지 않는다.
원 native archive/출력·P1 NO-GO·E/T/A/F를 다시 열거나 새 성공으로 대체하지 않는다.

다음은 실제 W 계수/가중치 계약의 관측 공백, U/V primary·NH4 및 나머지 RHS 산술이다.
압력/alt/al_pert 성분을 중복 계산하지 않고 같은 W,N에서 평가오차 epsG와 norm/변환오차
nu를 분리해야 한다. 전체 e_i와 정확한 잔차 감소·Stage3 반복 수렴은 여전히 미인증이다.

[공개 재현과 조건](evidence/eos_rhs_20261110/README.md) ·
[증거 manifest](evidence/eos_rhs_20261110/manifest.json).
