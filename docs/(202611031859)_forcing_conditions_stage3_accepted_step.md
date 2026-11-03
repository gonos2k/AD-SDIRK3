# Forcing 조건 정리와 Stage3의 실제 수용 갱신

작성: 2026-11-03T18:59:58.475776+09:00.
기준 main `3a82d60130d3c48c6492ae863a66187f26f905f3`(PR342).
진단 소스 `bc0f66461efddded7ffd181265d44f7e6caa7add`. Green·Red: GPT-6.1-Terra medium.

## 해소한 조건과 남은 것

| 항목 | 결과 | 범위 |
|---|---|---|
| 오래된 omega cache 가설 | mass-flux memo는 RHS마다 현재 projected state로 재생성 | 해당 cross-call freeze 의심 해소; 모든 cache 증명은 아님 |
| NH 활성 설정 | Fortran env 뒤 WRF override, 그 뒤 Tile constructor snapshot | 실제 NH on; 초기 off 출력은 override 이전 |
| 저장점의 mass-floor | exact mass 최솟값89073.03125Pa, nonpositive0 | 저장된 입력점·reference의 branch만 확인 |
| 실제 Stage3 갱신 | accepted K/R pair와 post-K 일치, native RMS 감소 | 한 수용 갱신; Stage3 수렴/forcing 인증 아님 |
| accepted-segment mu floor | 조건부 하한89063.88575467332Pa | 아래 IEEE/source 가정의 μ branch만 |
| exact forcing1e-7 | full RHS tau/rho/norm 오차 상계 없음 | 미완료 유지 |

## 소스·기존 데이터로 먼저 닫은 부분

[지원 조건 ledger](design/forcing_support_conditions.md)에 활성 EOS/PGF·연속방정식·omega,
reference/cache·branch·AD 전달과 증명 경계를 고정했다. 실제 WRFParity 모드는 deprecated raw
omega flag보다 우선한다. `mass_flux_memo`는 `computeUnifiedRHS`의 stack-local 객체이며
호출마다 현재 u/v/mu로 만든다. EOS의 현재 pressure를 게시한 뒤 V-PGF가 읽는 순서도 확인했다.
이는 해당 경로의 source facts이며, transitive cache·branch·전체 AD graph의 보증은 아니다.

저장된 U_eval/reference와 기존 base fields를 사용해 MUB/PHB/C1H/C2H 등의 관측 fingerprint를
대조했다. exact-rational mass 최솟값은2850337/32Pa이고, conservative theta+300 하한258.83935546874994K,
total geopotential layer 차 하한2110.515136718749m²/s², hybrid-mass 하한도 양수다.
이로써 그 저장점의 nonpositive mass-floor는 비활성이다. 주변/segment·inverse-density·다른
clamp 및 full RHS FP 오차까지 닫은 것은 아니다. 새 epsilon 또는 이전 링크/회귀/P1 실험은 없다.

## 선택한 최소 실험

미지 tau/rho를 숫자로 가정해 forcing 인증을 만들지 않았다. 다음 한 불확실성인 **실제로
수용된 Stage3 갱신과 잔차 쌍**을 production이 이미 계산한 값으로 관측했다. 원래
observed_K/R 및 accepted_residual을 사용하고, 추가 RHS/JVP/전처리는 호출하지 않는다.

새 compile-time probe는 기본 OFF다. stage3/Newton0/ordinal3에서 실제 공통 K update 뒤
수용 trial과 K/R의 유한 numeric equality, 같은 S_inv·H=I, 관측 고정 입력/ref/metadata를
검사한다. unavailable/예외/미수용이면 terminal guard가 실패 종료해 예보를 재개하지 않는다.
추가 호출로 문맥을 복원한 채 예보를 이어갔다고 하지 않는다. 수치 수용 조건은 바꾸지 않았다.
원래 A9/pressure 보고기는 이 진단에서만 비활성이고, 원래 validator를 완화하지 않았다.

## 실제 결과

표준100km·40×80×64·dt600·MPI1·Torch1/1, FP64·mode3·ref-update/reuse off·raw principal에서
한 번 실행했다. 종료86, Stage2 CONVERGED 로그는 확인했지만 Stage3는 아직 완료하지 않았고
완료 대시간스텝은0개다. Stage3로 진행했다는 사실만으로 Stage2 수렴을 추론하지 않았다.
초기21장·계수는 보관 A9/RK3와 signed-zero 동등 비교를 통과했다. 새 RK3·endpoint·실행시간
비교나 장기 실행은 하지 않았다. rank RSS1,988,575,232바이트로 등록한 사후4GiB 기준 안이다.

| 실제 관측 | 값 |
|---|---:|
| native scaled RMS before | 0.0019075394375249743 |
| native scaled RMS accepted pair | 1.056651210973314e-9 |
| after/before | 5.539341363994629e-7 |
| accepted mechanism | trust |
| outer_update_alpha | 1 (trust 내부 scale과 구분) |
| actual delta-K S-norm | 0.0681644601651611 |
| effective linear relative quality | 3.267509995907858e-8 |
| 관측용 RHS/JVP/전처리 추가 | 0 / 0 / 0 |

RMS는 production `compute_scaled_rms_norm`의 실제 계산을 사용했다. 별도 FP64 analysis
L2/sqrt(N)을 bitwise native 값이라고 부르지 않는다. post-K와 수용 trial의 observed_K,
accepted_R와 observed_R가 유한 numeric equality를 만족했다. 한 computed merit 감소가
exact mathematical residual 감소, Jacobian 역상계 M, Lipschitz L, Stage3 수렴이나 전체
Newton 수렴을 증명하지는 않는다. 한 표본으로 L/M의 상계를 역산하지 않는다.

## μ branch의 조건부 정량 하한

실제 base EOS의 projected mu/MUB extrema와 B_mu, K0_mu, K1_mu extrema를 기록했다.
Q_mu는 copy map이므로 infinity norm1이다. 저장 FP64 입력에 대한 exact fraction 계산으로
m0=min(mu)+min(MUB), a=저장된 양의 coefficient, k_i=max|K_i_mu|를 쓴다.
accepted real segment와 endpoint coordinate box에서 m0−a(k0+k1)로 변화를 제한한다.

사전에 정한 magnitude envelope는 **89100.49851781964Pa < 2^39**다. 관련 중간값이2^40 미만이고
binary64 basic multiply/add가 one-ULP 이내라는 조건 아래, base construction2회와 이후
construction/base-mass addition3회의 오차 합은5/4096Pa 이하이다. 이를 덮는 **1Pa** 여유를
데이터 전에 정의했고 solver tolerance로 사용하지 않았다.

exact-rational m0−a(k0+k1)−1Pa의 outward lower는 **89063.88575467332Pa >0**이다.
따라서 고정 B/MUB/a·source Q_mu·FP64/rounding/no-overflow 가정 아래 이 구간에서
`mu_full<=0` floor가 활성화되지 않는다. 1000은 replacement 값이며 trigger가 아니다.
이 결과는 다른 clamp·libm·norm error·full RHS 평가오차 rho·FD 오차 tau의 상계가 아니다.
`mass_tube_bound.py`의 조건과 allowance는 실행 전에 등록했으며, 실패한 조건을 사후 완화하지 않았다.

## 남은 forcing 연결

Stage2의 기존 finite-difference sampled gap을 true upper bound로 바꾸지 않았다.
tau+(1+t)rho+norm_budget <= t−etahat−delta_epsilon 같은 조건에 넣을 full-RHS 상계는
아직 없다. t=1e-7의 code quality target 인증과, 적절한 국소 조건 아래 etabar<1인 inexact
Newton 이론은 다르다. 허용오차를 바꾸거나 기준을 낮춰 통과시키지 않았다.

O2/O3는 활성 source 조건과 한 branch를 좁힌 상태다. O4의 oblique projection norm 관계,
다른 states/directions/branches, 전체 floating evaluation error, 역증폭/비선형성·시간 지도와
물리 예산·연속 전방·W 잡음·실용 비용·5일 정확도는 계속 남는다. 다음에는 남은 source
branch/산술 오차를 특정해 연결하며, 이미 닫은 테스트나 더 작은 epsilon sweep을 반복하지 않는다.

[증거 manifest](evidence/forcing_path_20261103/manifest.json). helper의 독립 analytic RMS,
잘못된 pair·비유한 input 거부 시험이 통과했다. 새 ABI 변경은 없고 Newton만 재컴파일했으며
다른30archive member는 byte 비교했다. 실제 linker/map/두 메서드 귀속을 새 binary에 고정했다.
전체 WRF clean rebuild는 하지 않았다. Graphify43파일 AST와 source audits는 탐색/조건 근거이며
수치 증명으로 오인하지 않는다. 원 바이너리·NetCDF/NPY·기존 toolchain은 외부 경로에 보존한다.
