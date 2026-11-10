# 선택한 W 호출의 계수·가중치 관측과 실제 입력 민감도 연결

작성: 2026-11-10T16:01:36.719398+09:00.
기준 main `e556771080495bedaa3a2fe7bbffa7cbdb7872c9`(PR346).
실제 native 소스 `ddc9475fdb0e5cf6ecdb8d32dee6524c9ff5602b`.
Green·Red: GPT-6.1-Terra medium. 이전 보관 자료의 조건부 계산은 그대로 보존한다.

## 이번에 해소한 것

| 항목 | 이번 결과 | 인정 범위 |
|---|---|---|
| W에서 실제 선택한 RDN | CPU FP32 64개를 consumer에서 포착 | 쓰인 인덱스1..63, base/trial 값 동일, 이전 후보와 일치 |
| 실제 중력·top RDNW | FP32 피연산자 값을 consumer에서 포착 | 두 호출 모두 이전 후보와 일치 |
| W 가중치 상한 | 실제 cloned Sinv의 rw block min=max=1 | floor 설정 추정·전체 배열 저장 없이 실제 상한 |
| canonical/dry·map·분모 경로 | 실제 branch와 양의 min/max 포착 | 해당 source/capture 신뢰 및 지정 topology 조건 |
| 같은 수용 쌍·EOS 입력 | 기존 K/R/S/ref/context 및16개 NPY SHA 일치 | 기존 MPFR 입력/목표를 그대로 재사용 |
| 전체 W native 산술·full RHS·norm 오차 | 미완료 | 새 관측이나 작은 성분 값으로 완료 처리하지 않음 |
| 정확한 merit 감소·Stage3 반복/전방 | 미인증 유지 | Stage3/시간스텝 완주 자료가 아님 |

## 1. 최소 관측 위치와 원래 계산의 보존

기존 기본 OFF `WRF_SDIRK3_ACCEPTED_STEP_PROBE`와 `ScopedEosCapture`를 재사용했다.
새 runtime 옵션·전역 설정 변경·별도 평가기를 만들지 않았다. Tile의 실제 W 경로에서
다음 피연산자를 읽는다.

- `getRdnTensor(CPU,Float32)` 뒤 실제 사용 vector와 사용범위, 실제 g_val_wpgf.
- 실제 top `rdnw_km1_val`의 Float32 값. EOS의 Float64 RDNW로 대신하지 않는다.
- 실제 canonical branch, cq1 전체가1인지, msfty·level_mass_w·velocity_mass_w·alpha_w의 finite positive 범위.

기존 EOS capture TLS 안에서 관측하며 detached copy/NoGrad read만 사용한다. 원래
base와 각 trial의 EOS context에 연결하고 최종 출력에는 수용된 실제 trial과 base를 쓴다.
기존 post-K/observed-K/R 일치 및 trial input=B+aKpost 검사를 유지했다. 같은 고정 RDN/g/top
및 기록된 map extrema를 대조하지만 이를 모든 private Tile 상태의 동일성으로 확대하지 않는다.

Newton은 기존 cloned receipt_sinv에서 실제 cached-layout의 rw 범위를 찾아 min/max와
count/dtype/full hash를 기록한다. 가중치 전체를 새로 저장하지 않는다. max/min은 저장
FP64 값의 선택이고, sum/norm의 오차를 인증하는 계산이 아니다. 포착 실패·중복·비유한·누락은
terminal guard의 실패로 이어져 예보를 재개하지 않는다.

Collector source에 추가 RHS/JVP/전처리 호출은 없다. 기존 postaccept receipt 구간의
카운터 delta도0/0/0이다. 이 delta만으로 RHS 내부의 모든 비용을 증명한 것은 아니다.
복제·검사·스캔·hash·출력은 실제 비용이 있고, 이 실행은 성능 비교에 사용하지 않는다.

## 2. 빌드·링크와 최초 실패 기록

PressureEosContext의 내부 C++ layout이 바뀌므로 dependency를 확인한 다섯 consumer를
matching header/flags로 clean rebuild했다: Tile, Newton, pressure_stage_probe,
native_pressure_reference, v_primary_probe. 나머지26개 archive member는 원본과 byte 동일하다.
Fortran/C ABI는 바꾸지 않았고 기존1270개 Fortran artifact와18개 dylib entry 및 SDK27.2,
compiler/OS/linker의 일치를 확인했다. 전체 WRF clean rebuild를 했다고 하지 않는다.

최초8706 소스의 build는 generic lambda의 dependent `.item<T>()` 문법 오류로 Newton
compile에서 중단됐다. helper/model은 실행하지 않았다. 로그와 partial build를 보존했고,
ddc9475에서 lambda 입력을 명시적 WConsumer 타입으로 고친 뒤 다섯 consumer를 다시 빌드했다.
수치식·object layout·수용 조건을 바꿔 통과시킨 수정이 아니다.

집중 helper의 compile/run 종료0과 assertions 활성 상태를 확인했다. 별도 case 수를
출력하지 않으므로 개수로 부풀리지 않는다. Tile compile의 기존 경고4개도 로그에 남겼다.
단일 archive 링크와 전체 map에서 직접 Tile/Newton 메서드 본체 ID465/466 출처를 확인했다.

- archive SHA256: `e4f005c556d62480867ca7d138cb6fc7972f491d8167375f712393a9b8dc558d`
- executable SHA256: `97be491ae6ed7907447d33e68d391a4a0058d5e56f31fdb2f0425f35e1b1e5c3`
- link map SHA256: `0f3c28f09fa43e3ab97cc54a0686ff245fcbcfcb82ddb97860ca252d18058f81`

## 3. 한 번의 native 실행과 동일성

표준 em_b_wave100km·40×80×64·h600, MPI1·Torch1/1, FP64 mode3 raw-principal,
reference-update/prepared-JVP reuse OFF를 유지했다. source/build/analysis/review382개 pin,
단일 시도, timeout/자원 및 기존 수용 쌍 일치 gate를 실행 전에 등록했다. 재시도나 gate 완화는 없다.

결과는 diagnostic_completed, errors=[], 종료86이다. K0/R0/K1/R1/Sinv/reference와 base
context가 이전 쌍과 일치했다. 새로운16개 EOS NPY도 기존 ALB 분석의 SHA와 모두 같아
MPFR 전수 계산을 반복하지 않았다. 그 계산의 theta fingerprint·source arithmetic 가정은 유지한다.

| 실제 관측 | 값 |
|---|---:|
| native RMS before | 0.0019075394375249743 |
| native RMS accepted | 1.0566512109733139e-9 |
| W RDN dtype/count/used | Float32 /64 /1..63 |
| actual gravity Float32→double | 9.8100004196167 |
| actual top RDNW Float32→double | 201.27182006835938 |
| actual rw inverse metric start/count/dtype | 432896 /215865 /Float64 |
| actual rw inverse metric min/max | 1 /1 |
| full cloned Sinv FNV | 14474524215718693222 |
| rank peak RSS | 2117730304 bytes, 등록4GiB 이하 |

초기21장·계수는 보관 A9/RK3 비교를 통과했다. Stage2 CONVERGED 로그가 있고 Stage3 첫
수용 갱신 뒤 종료했으며 완료 timestep은0이다. wrapper3/0이나 끝174장 비교 같은 one-step
완주 결과를 이번 실행으로 주장하지 않는다. 새 RK3·endpoint 예보장·공정한 속도비는 없다.

## 4. 새 계수 참조는 alias 동일성을 가정하지 않음

소스의 `alpha_w`는 EOS의 inverse-density `alt`가 아니다. 여기서는 native의 canonical
mass/map 피연산자를 A_w라고 쓴다(단위Pa). `level_mass_w`는 core 계산 뒤 alias 확장을,
A_w는 packed 전체의 별도 계산을 사용하므로 두 값의 점별 동일성을 extrema만으로 인증하지 않는다.
실제 velocity_mass_w min도 level_mass_w min과 FP64 마지막 자리에서 다르다.

새 분석은 실제 포착된 A_w, msfty, velocity_mass 및 RDN/g/top/cq1을 고정하는 실수
중간 모델 F_int를 정의한다. 그 모델 안에서만 양의 velocity_mass가 약분된다. 계수 상계는

\[
 C_k\le\frac{|g\,rdn_k|}{msfty_{min}A_{w,min}},\qquad
 C_{top}\le\frac{2|g\,rdnw_{top}|}{msfty_{min}A_{w,min}}.
\]

Native ratio 형성이나 곱/나눗셈의 roundoff를 소거한 것이 아니다. 전체 목표 F*와의 차이는
예를 들어 다음처럼 분리해야 한다.

\[
 \widehat F-F^*=[\widehat F-F_{int}(\widehat p;\widehat d)]
 +[F_{int}(\widehat p;\widehat d)-F_{int}(p^*;\widehat d)]
 +[F_{int}(p^*;\widehat d)-F^*].
\]

이번에 제한한 것은 가운데 pressure-input 항뿐이다. 첫 항의 native 연산과 마지막 항의
coefficient/state formation 및 나머지 RHS는 E_rest에 남는다. 이전 조건부 mass 식의
전체 native 정확성이 소급 통과한 것으로 처리하지 않는다.

## 5. 실제 관측값으로 계산한 성분

각 층에서 pressure 오차는 기존 composed MPFR 결과를 쓴다. 내부층의 이웃 두 오차 합과
동적 상단의2배 one-sided 계수를 적용했다. 실제 inverse weight 최대값1을 그대로 사용해
이전 floor/reciprocal proxy1+2^-23의 가정이 이 두 호출에서는 필요하지 않게 됐다.
H=I와 지정 Qw의 copy/reflection/zero 작용 및 Nw215865/N1080491에서4Nw<N이므로
수학적 가중 성분은 weight_max*maxB/2 이하이다. backend norm/float conversion 인증은 아니다.

| 끝점 | actual A_w 최소(Pa) | pressure-input Linf 근삿값(m/s²) | 가중 수학적 성분 근삿값 |
|---|---:|---:|---:|
| base | 89072.53174120278 | 4.802262241646415e-13 | 2.4011311208232075e-13 |
| accepted | 89072.71343334371 | 4.729164593016812e-13 | 2.364582296508406e-13 |

표는 표시용 근삿값이며 엄격한 상계는 JSON의 Fraction이다. 이전 후보 RDN/g/top와
이번 관측값은 모두 일치했다. 이전 artifact의 verified=false는 과거 기록으로 보존하고,
새 관측/비교 artifact에서 선택한 두 호출에 대한 확인만 기록한다. 이후 Newton·stage·전방에
같은 계수/가중치를 사용한다고 추론하지 않는다.

## 검증과 다음 작업

소형 Python 검사는 actual A_w/map 사용(level_mass를 일부러 다르게 둔 예), observed weight,
interior/top 및 malformed artifact-set/endpoint 거부를 겨냥했다. 16파일과2개 endpoint를
고정해 누락을 조용히 건너뛰지 않도록 한다. Green/Red는 source, 현재382개 pin, 16개 raw
identity,128개 RDN 값, metric block과65층 Fraction을 독립 대조했다. 같은 MPI/WRF나
MPFR을 중복 실행한 검토가 아니다. Graphify의 source/dependency pin으로 헤더 consumer와
호출 영향을 확인했으며, 그래프 관계 자체를 수치 증명으로 사용하지 않았다.

일반 CI는 default-OFF 경로의 근거이고, 활성 capture는 새 집중시험과 이번 native 진단의
별도 근거다. 전체 W native 산술, U/V primary·NH4, 다른 RHS, formation 및 norm/변환오차를
제한해야 전체 e_i와 정확한 merit 감소 예산을 판정할 수 있다. E/T/A/F와 W 잡음·실용 비용·
5일 정확도는 미완료다. 실제 coefficient/weight의 이번 확인을 그 완료로 바꾸지 않는다.

[재현·검증 범위](evidence/w_runtime_20261110/README.md) ·
[증거 manifest](evidence/w_runtime_20261110/manifest.json).
