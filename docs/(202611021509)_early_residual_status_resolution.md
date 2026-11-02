# Newton 조기 실패 잔차와 실패 원인 전달

작성 시각: 2026-11-02T15:09:02.878814+09:00.
기준 main: `696def3b4719fad24f2c2040d1fbe7ae5bcf1824` (PR338·339).
생산 소스: `c7a282e3032b6f760d9d8154ee9536ab3688a95b`.
집중시험 소스: `c7a282e3`. Green·Red: GPT-6.1-Terra, medium.

## 이번 범위와 체크리스트

| 순서 | 해소할 문제 | 근거 | 상태 |
|---|---|---|---|
| R4a | debug0/adaptive-off 예외가 미측정0을 측정값으로 기록 | 현재 K의 기존 scaled residual tensor 재사용, 양쪽 public entry 시험 | 집중시험 통과 |
| R4b | 비유한 초기 잔차의 result/stats 불일치 | 초기 unscaled 값과 최종 scaled 값을 구분, NaN/Inf 및 scalar 관측 전 예외 | 집중시험 통과 |
| R4c | 관측이 없는 반환과 실제 NaN/Inf 혼동 | zero-budget·checked-conversion·비표준 예외 API fixture, measured=false 보존 | 집중시험 통과 |
| R5 | Tile 생산 소비자·MPI 집계의 관측 상태 | shared tri-state/abort 규칙과 rank 혼합 순서·결합 시험, Tile 연결 소스 검토 | 집중시험 통과 |
| R2 | 원 Krylov와 후처리된 방향의 실패 혼동 | raw counters/summary와 effective 방향 분리, Direct-U·classifier 계약 | 집중시험 통과 |
| 회귀 | h600 정상 성공 궤적 보존 | 같은 A9/P0 연구 구성 한 스텝·보관 출력 비교 | 한 스텝 통과 |

새 허용오차·감쇠·clipping·분할·시간간격 옵션을 추가하지 않았다. 현재 후보의 A4/P1
NO-GO, R1/R3의 원래 완료 범위는 그대로 유지한다. 이 표는 전체 수렴 증명을 의미하지 않는다.

## 수학적·구현 계약

반환 K에서 이미 관측한 G_K와 같은 S·mask로 계산한 scaled RMS만 final_residual에
붙인다. GMRES에서 포착한 조기 예외 위치는 현재 iteration의 K update 전이므로 기존 tensor를 scalar로 읽으며, 실패 보고를
위한 RHS 재호출은 없다. 초기 unscaled residual의 비유한 판정과 최종 scaled RMS는
서로 다른 양이다. 실제 NaN/Inf와 scalar 관측 전 예외를 구별한다. 매우 큰 FP64 값의 float 변환은
LibTorch에서 Inf가 아니라 예외를 던졌으므로, 관측한 최종값으로 취급하지 않는다. Impl에서 밖으로 전파되는 예외는 final=NaN/measured=false/Exception으로 남긴다. status
API는 정의된 K 없이 실패를 반환하고, 생산 API는 원래대로 예외를 전파한다. solve entry에서
통계를 먼저 초기화해 이전 solve의 관측을 상속하지 않는다. 임의 callback의 K·입력 in-place
변형이나 Impl 반환 후 public wrapper 예외까지 인증한 것은 아니다.

Tile은 measured-finite / unmeasured / measured-nonfinite를 구분한다. 초기 비유한 값,
미측정 초기/최종 값, 실제 비유한 최종 값은 각각 원인을 표시하며 중단한다. 기존 scaled
RMS 100 초과 중단과 유한 비수렴에 대한 hard_abort/best-effort 정책은 유지한다.
초기 unscaled norm을 scaled 100 기준과 비교하지 않는다. MPI 집계는 관측된 비유한 값 >
미측정 > 관측된 유한 값 순서를 사용하고, 유한 값은 기존처럼 rank 최대값을 전달한다.
이는 global norm을 새로 계산하는 작업이 아니다. 실제 혼합 rank 실행, rank-local 예외의 MPI 동시 중단 및 collective 실패
복구는 이번 검증 범위가 아니며, 집계 계약은 MPI collective 성공을 전제로 한다.

Newton은 실제 사용 방향의 품질을 계속 소비한다. 원 Krylov receipt·warm start는 그대로
보존하고 raw 실패 집계와 후처리 방향의 미해결 실패 수를 구분한다. 요약에 raw 상대잔차와
변경된 direction 상대잔차를 나란히 적는다. 기존 zero-update exit event를 덮어쓰거나,
혼합된 여러 iteration의 원인을 새 단일 인과 판정으로 추측하지 않는다.

## 검증과 증거

- Trust 계약 101/101, 실패 분류 210/210, 조기 반환 public API 12/12 통과.
  debug0/adaptive-off 예외의 관측 scaled RMS는 양쪽 API 모두 약0.725857이며 0으로
  보고하지 않는다. NaN/Inf 초기값, zero-budget의 미측정, checked conversion·비표준
  예외에서의 미관측 처리도 구분했다. zero-budget은 직접 API 계약시험이며 정상 native
  구성의 도달성을 주장하지 않는다.
- 보관 A9/P0 연구 플래그 구성·MPI1·Torch1/1·100km·40×80×64·h600 한 스텝을 실행했다.
  stage2/3/4는 각 Newton3회, scaled RMS 6.296993e-14 / 6.345785e-14 / 6.344006e-14,
  wrapper3/0. 끝174숫자 배열은 A9/P0와 bitwise 일치했고, 초기21장/계수는 RK3와
  signed-zero 동등 비교를 통과했다. 여섯 비교장의 유한성과 저장된 양성 지표도 확인했다.
- 기존 같은 설정 RK3와의 RMS 거리(U,V,W,PH,T,MU)는 각각
  3.148492e-4m/s, 2.031948e-4m/s, 6.909943e-4m/s, 0.3963487m²/s²,
  2.922344e-4K, 0.3137946Pa로 기존과 같다. 정확해 오차나 잡음 개선을 뜻하지 않는다.
- 이번 RSL 한 스텝은120.51868초, 보관 RK3는0.55700초다. 서로 다른 실행 시점·관측 조건의
  기록으로만 보존하며 비용 수용 또는 속도비를 판정하지 않는다. 새 RK3 실행은 하지 않았다.
  rank RSS는2,082,652,160바이트다. 5GiB 여유공간·360/390초 시간 한도·사후4GiB RSS
  규약을 통과했다. RSS 상한은 실행 중 강제 제한이 아닌 사후 측정이다.
- raw runner의 pending_numerical_comparisons를 덮어쓰지 않고 별도 postvalidation을
  PASSED로 기록했다. CI 결과는 PR #340의 최종 커밋에 별도 확인한다.


bounded Graphify는 source/caller 39개에 대해 변경 전후 SHA를 고정했다. AST 관계는 탐색용이며
TLS·callback·Fortran의 연결 누락을 남긴다. 전체 그래프를 numerical proof로 사용하지 않았다.
변경된 Stats layout과 공통 header를 읽는 10개 translation unit을 재빌드하고, 나머지 21개
object는 source/header/기존 archive hash 대조 후 재사용했다. 전체 WRF를 clean rebuild한
것은 아니며, 기존에 고정한 Fortran·MPI·netCDF·LibTorch·SDK 스택으로 다시 링크한다.

첫 fixture의 [10] 반환 K와 [1,10] history stack 비교 오류는 실행 전 Red 검토에서 발견해
state-shaped predictor로 수정했다. 이 fixture 수정은 test-only였다. 추가한 FP64 overflow fixture는 실제 실행에서 실패했다.
실패 로그와 receipt는 attempt1에 보존했으며, 변환 전 예외 경로의 미관측 반환까지 수정하고
표준·비표준 예외 시험으로 구분했다. 변경한 Newton object만 다시 빌드하고, 나머지 9개는
같은 source/header/object SHA를 확인해 재사용했다. 상세 source/executable/input SHA와 실행 명령은
[evidence manifest](evidence/early_residual_status_20261102/manifest.json)에 연결한다.

## 다음 순서와 미완료

다음 작업은 O1의 mode3 G_K·고정 문맥·projection·S·mask를 하나의 실제 함수 정의에
연결하는 것이다. 이어 O2/O3의 활성 reference/halo/cache 의존성·JVP 미분 일치, E의 native
가중 adjoint와 역행렬 상계, T의 실제 split/reference와 결합 안정성, A의 물리 오차 예산을
닫는다. 이미 닫은 단일 방향 반복 JVP나 P0/P1 비용 비교를 다시 실행하지 않는다.

24–36시간 W 구조의 원인, 공간 연산자 동등성, 실용적인 전체 비용, 연속6–12스텝·5일
정확도는 계속 미완료다. 새 장기 실행·RK3 재실행을 시작하지 않는다. 짧은 timestep 계약시험은
h600 실용 비교를 대체하지 않는다. 원래 작업 폴더의 미커밋 변경은 수정하거나 stage하지 않았다.
