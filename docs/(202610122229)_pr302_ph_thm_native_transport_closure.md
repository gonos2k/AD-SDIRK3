# PR #302: PH·THM 연직 수송 원인과 국소 수용

기록 시각: 2026-10-12 22:29:35 JST (+0900)

## 범위

PR #301에서 남긴 PH·THM 경향 차이를 같은 실제 상태와 실제 `wwE`에서
분해하고, 확인된 두 연직 수송식을 수정했다. 건조·평탄 16³
`test/em_b_wave`, 단일 MPI rank·thread·전영역 타일의 상태 전용 cold 호출이다.
`scalar_adv_opt=1`, `zadvect_implicit=0`, `phi_adv_z=1`,
`v_sca_adv_order=3`, `use_input_w=true`를 유지했다. 각 호출은 h=15 s,
run=15 s이며 비교 대상은 첫 Full RHS이다. 새 240초 예측은 수행하지 않았다.

원래 60초 frame에서 THM과 내부 W를 포함해 보존한 입력을 재사용했다.
입력 SHA-256은 `fc37c8c0c8be8053a16e0d3424609d496f4a023c6c17ee3a06371970639d14df`이다.
관찰 전용 A 두 호출과 수정 후 B 두 호출, 총 네 native 실행을 모두 완료했다.
원래 사용자 worktree는 수정하지 않고 독립 worktree에서 작업했다.

## 확인한 원인과 변경

PH의 기존 C++ Full 경로는 WRF의 `phi_adv_z=1` 설정을 전달받지 않고 옵션 2를
사용했다. 옵션 1은 Ω를 먼저 질량면으로 보간하고 PH 기울기와 곱한 뒤 다시
W면으로 보간한다. 옵션 2는 PH 기울기를 보간한 뒤 W면 Ω와 곱한다.
보간과 곱셈은 일반적으로 교환되지 않는다.

THM의 기존 Full 경로는 가중 평균 온위의 연직 플럭스를 사용했지만, 실제
Fortran 호출은 `v_sca_adv_order=3`의 hard upwind 플럭스를 사용했다.
두 실제 피연산자와 분리된 수평·연직 이류, 혼합, 추가 경향을 포착해 확인했다.
계수 fitting 없이 A의 연직항 차이는 전체 PH·THM 차이를 거의 재현했다.

기존 WRF 옵션 두 개를 checked C ABI로 각 solver에 전달하고 fingerprint에
포함했다. 고정 궤적 요청·기록 중에는 변경을 거부한다. PH 식은 기존 acoustic
구성요소의 공통 함수 한 곳에서 선택한다. native THM 차수 3은 기존
`wrf_vert_adv3`를 hard sign으로 호출하며 같은 호출이 계산한 플럭스를 기록한다.
전역 smoothing과 운동량 수송은 바꾸지 않았다.

현재 native 연결은 PH 옵션 1/2와 scalar 연직 차수 3, 질량층 4개 이상만
지원한다. 다른 차수는 잘못된 식으로 대체하지 않고 초기화 오류로 거부한다.
미설정 standalone 기본값은 기존대로 Full PH=2, acoustic PH=1,
Full THM=가중 보간이다. 새 namelist·환경 옵션은 추가하지 않았다.

## 실제 같은 상태의 결과

Fortran·C++의 여섯 실제 입력 상태는 source-60과 정확히 같다. A의 Full
스냅샷 두 개는 각각 PR #301 자료의 파일 해시와도 같다. B에서도 입력은
그대로이고 Fortran 전체 경향 파일은 A와 같다. 소유 영역과 동적 상단을 유지했다.

| 전체 C++−Fortran 경향 | A RMS | B RMS | 감소 배율 |
|---|---:|---:|---:|
| PH (m²/s³) | 3.80669119e-5 | 1.68022416e-8 | 2,265.6 |
| THM (K/s) | 2.50972082e-7 | 1.85868795e-10 | 1,350.3 |

위 표는 FP32 Full 스냅샷을 FP64로 승격한 뒤 소유 영역에서 재계산한 값이다.
내부 FP64 성분 패킷을 직접 비교한 B RMS는 PH 1.68014607e-8, THM
1.85867937e-10이며 출력 반올림 때문에 끝자리가 다르다. 두 자료를 혼용하지 않는다.

C++의 U·V·W·MU 경향은 A→B에서 소유 영역 전체가 정확히 같다.
Fortran의 여섯 경향도 같다. 이는 첫 호출의 국소 수정을 확인하는 대조이며,
예측 개선율이나 전체 반이산 연산자 동일성을 뜻하지 않는다.

실제로 읽은 Ω=wwE의 단위는 Pa/s이다. 활성 16×17×16 인터페이스에서
C++ FP64−Fortran FP32 RMS는 7.0913e-9 Pa/s, 최대 4.2058e-8 Pa/s이며
상·하단은 0이다. B의 PH 잔차에는 이 피연산자 평가 차이가 지배적으로 남는다.
B의 작은 THM 잔차는 수평 이류와 다른 기여의 결합·상쇄이며, 더 이상 연직
3차식 누락으로 귀속하지 않는다. 두 경로의 완전한 비트 일치를 주장하지 않는다.

Fortran의 혼합 `t_tendf`는 조립 때 지도계수로 나누어 들어가므로 물리 변환은
각 경향의 실제 결합 규약을 따른다. 추가 경향에는 혼합이 이미 포함돼 있어
중복 합산하지 않았다. THM add 산식의 잔차는 raw basis에서 3.53e-8,
물리 변환 후 3.96e-13 K/s RMS이다. C++의 `other_raw`는 차감으로 정의한
나머지이므로 독립적인 인과 증거로 취급하지 않았다.

## 빌드·미분·회귀 검증

A producer는 `2627dd4e4d958613d1bb1f7385cebd2a5c88836f`,
B 생산 소스는 `cd6a47e208d5ef97ff675691d97ff65a69b129ae`이다.
뒤의 테스트·문서·주석 커밋은 생산 식과 ABI를 바꾸지 않는다.

| 기록 | A | B |
|---|---|---|
| WRF executable SHA-256 | d7c08bd48f31cf434d0034ae4d1b91130a024750af5091a057d1d6f91b277f1d | 8c3f4cb140a03e7184548b57e7bcaccd38d3195f74123b7bd73955b70ce20a5c |
| C++ archive SHA-256 | 4d65965dec37560cb0c86940fc5756f24add5ddec4664ea24ac29c1cb7860c6b | 20142c079bcb883433a73637a8d1d63da008ed91e1f5292290caefcd825c6912 |

A는 바뀐 객체와 Fortran 모듈 소비자를 재빌드했다. B는 class header와 acoustic
signature 변경에 맞춰 C++ archive의 24개 객체를 모두 clean rebuild하고
Fortran bridge·`solve_em`을 다시 빌드했다. 정확한 compiler/MPI/netCDF/Torch
명령과 archive 멤버·실행파일 freshness는 영수증에 남겼다. 빌드 준비 실패와
중단된 broad build 기록도 보존했으며, 이 시도들은 native 실행이 아니다.

새 라이브러리를 직접 연결해 기존 네 테스트만 새 헤더로 컴파일했다.
`FNM_FNP_Geometry_Contract`, `Potential_Temperature_Contract`,
`Fixed_Trajectory_Lifecycle`, `Base_State_Checked_ABI`는 모두 통과했다.
PH 1/2 독립 산식, 비균일 격자, THM의 legacy−hard3 전체 RHS 차이,
물리 경계 tangent·owner cotangent, field별 JVP/VJP·FD, ABI 거부와
고정 문맥 변경 거부를 확인했다. FD fallback은 사용하지 않았다. 미분 검사는
부호 margin이 큰 합성 활성 타일에서 수행했다. 실제 포착 상태는 내부 Ω의
최솟값이 약 1.89e-7 Pa/s이고 12개 활성 면이 1e-6 Pa/s 미만이므로,
이 검사가 실제 상태의 작은 부호 margin 방향까지 인증한 것은 아니다.

THM의 약한 랜덤 cotangent pairing은 FD 상쇄로 안정 구간을 확보하지 못했다.
전체 물리 THM 벡터를 비교하고 해당 field의 정규화한 JVP를 cotangent으로
고정해 보완했다. Ω 부호 margin으로 허용 ε를 산정한 후 반감 scan했다.
ε=3.125e-4와 1.5625e-4에서 상대차 1.20e-7, 6.03e-8로 기존 2e-7
기준을 만족했다. PH의 field별 FD 차이는 1.6e-11 이하이고 THM 독립
delta 산식 차이는 1.94e-14였다. 영 Ω의 일반 미분이나 통합 관측창을 보증하지 않는다.

Graphify는 영향을 받는 C++ 함수·호출자를 갱신하고 Fortran BIND(C) 경로는
권위 소스로 확인했다. 캐시의 80행은 실제 74개 고유 경로였으며 중복을 정정했다.
기존 aggregate 교체가 거부된 기록과 추출 공백을 보존하고, 7개 변경 파일의
현재 소스 subgraph를 별도로 확인했다. 그래프를 수치 정확성의 증명으로 쓰지 않았다.

상세 원자료 통계와 빌드·실행·테스트 영수증은
[`docs/evidence/ph_thm_native_transport`](evidence/ph_thm_native_transport/)에 보존했다.

## 국소 수용과 남은 목표

이번 PH·THM 연직 수송 수정은 같은 상태·실제 피연산자·단위·영역·분기,
독립 산식, 회귀 및 field별 미분 검사 범위에서 수용한다. 남은 순간 RHS
차이를 모두 제거했다거나 전체 프로젝트가 완료됐다고 판정하지 않는다.
실제 배열의 전체 교차 경향과 성분 닫힘에는 새 경험적 허용치를 적용하지
않았다. 합산용 `gamma_m` 예산의 전체 통과나 참오차 상한도 주장하지 않는다.

기존 회전파 역문제와 미사용 예측은 종료 상태를 유지한다. V3의 240초
예측 예산은 완화하지 않았고 RK3 기준해 24개 검사 미충족도 그대로다.
새 240초 필드 비교·일반 관측창 수반·동일 정확도 비용 우위는 검증하지 않았다.
네 짧은 실행의 wall time은 기록만 하며 속도 우위를 주장하지 않는다.

Green·Red 팀은 생산 소스, 실제 배열, 파서 단위·소유 영역과 테스트를
서로 확인했다. 관찰 파서의 필드 순서·소유 영역·receipt 형식 오류는 수정 후
원자료로 재분석했고, PH 상단 문제라는 한 Red 의견은 실제 slice 재현에서
잘못된 판정으로 확인돼 철회했다. 실패 기록을 수치 결함이나 성공으로 바꾸지 않았다.
