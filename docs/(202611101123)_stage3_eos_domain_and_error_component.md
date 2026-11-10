# Stage3 수용 쌍의 EOS 정의역과 산술 오차 성분

작성: 2026-11-10T11:23:11.600048+09:00.
기준 main `4907880c00e105f61e1b50296cc0f1d146479f0b`(PR343).
새 native 진단 소스 `354748d60248d2b407ef18b646b618e28cbf445a`.
그 이후 `05aac9ac`는 mass-floor 주석만 수정했다. Green·Red: GPT-6.1-Terra medium.

## 완료한 것과 아직 인증하지 않은 것

| 항목 | 판정 | 범위 |
|---|---|---|
| P3 mass-floor 주석 | 완료 | mask detach는 0에서의 불연속을 없애지 않는다. 수치식·tolerance 불변 |
| 다음 목표 선택 | 고정 | 같은 Stage3 수용 쌍의 정확한 residual 감소. exact forcing1e-7과 별도 |
| 같은 수용 쌍 재확보 | 완료 | 기존 K0/R0/K1/R1/S_inv/reference hash 및 native RMS 일치 |
| EOS 정의역 | 조건부 양수 | 64층의 hybrid denominator·inverse density·theta·pow base 구간 |
| EOS 끝점 산술 오차 | 독립 상계 확보 | 반올림된 EOS 입력을 고정한 calc_p_rho_wrf만, 두 끝점 전체212544점씩 |
| 정확한 residual 감소 | **미완료** | 전체 RHS·상류 계수·state formation·scaling/norm 오차 e0,e1 미확보 |
| Stage3 수렴·연속 전방·W 잡음·5일·전체 가속 | 미완료 유지 | 이번 실행은 Stage3 첫 수용 갱신 뒤 종료, 완료 timestep0 |

## 목표와 충분조건

고정 B·문맥·S·N에서 정확한 residual RMS m_i*와 native 기록 mtilde_i의 차이를
독립 상계 e_i로 제한해야 한다. e0<mtilde0이고

\[
 |m_i^*-\widetilde m_i|\le e_i,\qquad
 e_0+e_1<\widetilde m_0-\widetilde m_1
\]

이면 정확한 residual이 엄격히 감소한다. 기존 쌍에서 오른쪽은 정확히
`4295394570649/2251799813685248`, 약 `0.0019075383808737634`다.
이 예산을 목표로 선택했지만, EOS 압력 오차(Pa)를 dimensionless scaled RMS 예산에
직접 더하지 않는다. 전체 RHS로 전파하는 bound와 상류/state/norm 오차가 먼저 필요하다.
`descent_budget.json`은 `exact_decrease_certified=false`를 유지한다.

production RMS는 `R_scaled.norm().to(CPU).item<float>() / sqrt_N`이다.
FP64 norm 뒤 float 변환·float sqrt_N/division이 있으며, 이전 fwd_ad_safe_norm의
계산을 이 경로의 norm contract로 대신하지 않는다. 검증되지 않은 backend 연산 수와
pow ULP 가정을 넣어 full e_i를 만들어 내지 않았다.

## 소스에서 먼저 구분한 EOS

권위 소스는 `wrf_hydrostatic_pressure.h::calc_p_rho_wrf`다. 그 ph는 **섭동 PH**이며
PHB 자체를 받지 않는다. r=양의 captured rdnw, c1/c2=hybrid 계수라 하면 실수식은

\[
 D=c_1(MUB+\mu)+c_2,\quad
 \alpha=alb-\frac{alb\,c_1\mu-r\Delta\phi'}{D},\quad
 \theta=t+t_0,\quad
 p'=p_0\left(\frac{R_d\theta}{p_0\alpha}\right)^{c_p/c_v}-p_b.
\]

실제 코드는 perturbation inverse density를 먼저 계산해 alb를 더한다. 구간 계산은
그 연산 순서를 따른다. total PH 차이 양수만으로 alpha의 양성을 대체하지 않았다.
`alb*(c1*MUB+c2)=r*Delta(PHB)`라는 hydrostatic identity를 별도 확인하지 않고
그렇게 단순화하면 안 된다. 합성 반례가 이를 직접 검사한다.

alb는 원래 Fortran ALB라고 가정할 수 없다. 실제 RHS에서 base theta/pressure로
계산된 inverse-density 계수다. 이번에는 두 같은 호출의 alb·MUB·p_base·c1/c2/rdnw와
열역학 상수의 값/metadata/hash가 같은지 확인하고 **그 저장값을 고정 입력**으로 쓴다.
alb를 만든 상류 pow/반올림 오차는 이 EOS 성분 상계에서 제외한다.

## 같은 호출의 자료와 최소 변경

기존 기본 OFF `WRF_SDIRK3_ACCEPTED_STEP_PROBE` 안에서만 확장했다. 기존 trial RHS를
같은 wrapper로 한 번 호출하는 동안 EOS context를 포착한다. 추가 RHS/JVP/전처리는 없다.
수용된 실제 post-K와 trial input=B+aKpost, 기존 accepted K/R 쌍의 일치를 검사한다.
스냅샷은 다음 호출이 scratch를 덮어쓰지 않도록 보관하고, 실패하면 진단을 중단한다.
기존 수용/회복/trust 분기와 원래 Newton 방정식·허용오차·감쇠는 바꾸지 않는다.

두 끝점의 projected ph/t/mu, native alt/p_pert 및 공통 MUB/alb/p_base/c1/c2/rdnw를
16개 flat FP64 NPY로 보관했다. 원래 dtype과 shape, data FNV 및 별도 SHA256을 연결한다.
원래 FP32 계수는 FP64로 정확히 승격됐는지 signed zero까지 검사한다.
복제·hash·파일 출력은 실제 시간/메모리 비용을 가진다. solver 호출0은 무비용·이어가기
비간섭성을 뜻하지 않는다. 진단 후 예보를 재개하지 않는다. 새 runtime 옵션이나 ABI 변경은 없다.

Graphify는 먼저 base와43개 소스 hash를 맞춘 뒤 dependency/caller를 확인했다.
3개 변경 소스를 재추출하고 마지막 Tile 주석 변경도 갱신했다(885 nodes/2819 edges).
간접 callback·전처리·Fortran 및 mutable cache/AD 의미는 graph만으로 증명하지 않았고
권위 소스와 별도 review에서 확인했다. graph pins와 extraction gaps를 보관한다.

## 한 번의 native 실행

표준 em_b_wave 100km·40×80×64·dt600·MPI1·Torch1/1, FP64·mode3·reference-update/reuse OFF,
raw-principal profile이다. 실행 전에 단일 시도, 입력·source/checker hash, 자원 한도,
기존 쌍 exact-match gate, 정의역 조건 및 분석기를 등록했다. threshold를 사후 조정하거나
조건을 완화해 재시도하지 않았다. 1회 결과는 `diagnostic_completed`, errors=[], exit86이다.

| 관측 | 결과 |
|---|---:|
| Stage3 native RMS before | 0.0019075394375249743 |
| accepted native RMS | 1.0566512109733139e-9 |
| after/before | 5.539341363994629e-7 |
| 관측 추가 RHS/JVP/전처리 | 0/0/0 |
| 원래 accepted pair와 hash·RMS 대응 | 일치 |
| 수렴 로그 | Stage2만 명시적 확인 |
| Stage3 완료 / 대시간스텝 완료 | 아니오 / 0 |
| rank peak RSS | 2012364800 bytes, 등록한4GiB 이하 |

초기21장·계수는 보관 A9/RK3와 signed-zero 동등 비교를 통과했다. 새 RK3나 endpoint
예보장 비교는 없다. 이번 실행은 forecast regression 완주·cost comparison이 아니며,
진단 elapsed/CPU를 RK3 속도비로 만들지 않는다. 원래 완료된 h600 회귀·P1 NO-GO는
원래 범위에서 재사용하며 다시 열지 않는다.

실행 archive SHA256은 `2959e850b5a421e986cd4a8af6e4ef10d039127ee203c84c8ceb13364a3bbb34`,
executable은 `8c90b83f7f08647ab92e72c8ac86b85e3cb7d1c19c86b204769bd9332e195c04`다.
기존 object30개는 보존하고 영향 있는 Newton을 matching headers로 재컴파일했다.
단일 SDIRK archive를 링크했으며 실제 ld와 전체 map의 직접 Tile/Newton 메서드 본체
ID465/466 출처를 확인했다. 보존한 Fortran artifact1270개·dylib18개와 SDK/driver/OS를
대조했다. 이후 Tile diff는 주석6행 교체뿐이며 이 native에 포함됐다고 하지 않는다.

## EOS 정의역의 조건부 구간

기존 mu tube와 같은 방식으로 고정 B,a,K0,K1 및 source copy boundary를 쓴다.
μ,t의 base 극값은 a(k0+k1)+1만큼, perturbation PH 층 차이는 2a(k0+k1)+1만큼 확장한다.
각 1은 각각 Pa,K,m²/s²이며 solver tolerance가 아니다. 입력 전에 정한 보수적 allowance이고,
state magnitude envelope<2^39 및 각 basic operation의 one-ULP/no-overflow 가정 아래
state construction과 layer subtraction 오차를 덮는다. 실제 EOS basic operation마다
exact Fraction 끝점을 binary64 바깥쪽 두 nextafter로 감싸 계산했다.

| 64층 전체 구간 | 하한(약) | 상한(약) |
|---|---:|---:|
| theta(K) | 257.8270518284963 | 449.0073385326346 |
| hybrid D(Pa) | 89063.88575467323 | 89103.95799532677 |
| alpha(m³/kg) | 0.735259702817215 | 6.305903798710927 |
| EOS pow base(무차원) | 0.19870572374355433 | 1.1803601974868345 |

64/64층 조건을 만족했고 실제 두 endpoint alt/theta가 구간 안에 포함됐다.
이는 해당 frozen coefficient·state enclosure·basic arithmetic 가정 아래 수용 선분과
그 endpoint coordinate box에서 이 EOS의 분모/양의 실수 pow 정의역이 유지된다는 결과다.
libm pow의 정확도, 상류 alb/domain, 다른 clamp, 전체 RHS smoothness/오차를 증명하지 않는다.
정확한 Fraction 끝점과 assumptions는 `eos_domain_analysis.json`이 권위다.

## 전체 endpoint의 독립 MPFR 오차 성분

256-bit RNDD/RNDU interval로 같은 **저장된 반올림 입력**의 실수 EOS를 평가했다.
모든 basic operation, cp/cv와 positive-base power를 구간화한 뒤 native 출력과의 차이를
위로 감싸고, Linf와 L2 합/제곱근도 RNDU로 제한했다. 두 끝점 각각81×64×41=212544점을
모두 사용했다(소유점만의 metric이 아니라 EOS 배열 전체다). native pow의 특정 ULP 계약을
추정하지 않았다. [MPFR rounding contract](https://www.mpfr.org/mpfr-current/mpfr.html#Rounding)를
기반으로 하며, MPFR와 자료 포착의 정확성은 명시된 신뢰 가정이다.

| 끝점 | alpha Linf 상계(m³/kg) | p' Linf 상계(Pa) | p' L2 상계(Pa) |
|---|---:|---:|---:|
| base | 4.768452044508382e-16 | 5.627079411241238e-11 | 3.801457115247935e-9 |
| accepted | 4.742962635239994e-16 | 4.990436858974591e-11 | 3.790213640174664e-9 |

상계는 MPFR_RNDU로 변환한 binary64다. JSON decimal을 exact rational로 해석하면
아주 작게 방향이 바뀔 수 있으므로 정확한 `*_binary64_hex`도 함께 제공한다.
합성 검사는 정확한2층식, 잘못된 native 출력, 근접0분모·음의rdnw·PH부호 반례,
잘못된 dtype/promotion/signed-zero/NPY order를 구분했다. 집중 C++ helper와 enabled Newton
빌드가 통과했다. Green/Red가 캡처 경로·구간 수식·MPFR·hash/units/범위를 교차 검토했다.

## 남긴 오차 항과 다음 하나

이번 EOS 오차는 **e_i 자체가 아니다**. 다음 항은 아직 독립 상계가 없다.

- B+aK·projection 입력 형성 및 upstream alb/base coefficient 산술 오차.
- EOS 오차가 PGF·부력·다른 활성 항과 최종 K−F에 전파되는 민감도 및 그 나머지 RHS 산술.
- 실제 Sinv/mask/scaling·backend norm·float 변환을 포함한 전체 RMS 평가오차.

다음은 이미 보존한 base fields와 `compute_inverse_density`의 입력·pow 정의역/오차를
source와 연결해 상류 alb 항을 제한하고, 필요한 원래 RHS 전파식에 넣는 작업이다.
단일 EOS local 성분의 작은 값이나 반복 일치를 full-RHS e_i의 상계로 승격하지 않는다.
새 Stage2 FD gap을 Stage3에 옮기거나 작은 epsilon sweep, 같은 P1 비용 비교, 닫힌 native
회귀를 반복하지 않는다. 상위 derivative/역증폭/비선형성·시간 지도·물리 예산과 E/T/A/F는 열린다.

[공개 재현 범위](evidence/eos_descent_20261110/README.md) ·
[증거 manifest](evidence/eos_descent_20261110/manifest.json).
