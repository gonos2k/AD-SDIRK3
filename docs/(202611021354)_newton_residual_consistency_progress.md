# 수렴 체크리스트 R1/R3 — 실제 방향·반환 잔차 정합성 수정

작성: 2026-11-02T13:54:03.458340+09:00.
기준 main9292c6c8. 실제 core 컴파일 소스 ef81e4f7348a170ed4c1bc41d84cb2412970225f,
시험 보완 소스 e0fed059358ecc052462938305099dc99f30414b(core CPP 동일).
Green·Red: GPT-6.1-Terra, medium.

[새 해소 체크리스트]((202611021316)_convergence_resolution_checklist.md)를 R→O→E→T→A→F 순서로 만들었다.
이번에는 R1과 R3의 좁은 코드 정합성을 해소하고 R2는 부분 해소, 나머지는 미완료로 둔다.
전체 수렴 보장·허용오차 조정·에너지 증명이나 장시간 계산을 추가한 변경이 아니다.

## 1. 실제 사용하는 방향의 지표

기존 코드는 halo 제거/Direct-U 대체로 dK가 바뀌면 implemented A로 잔차 벡터를
재계산했지만 success와 scalar relative error는 원 GMRES 값이었다.
이제 그 같은 잔차로 Krylov의 unweighted S-coordinate/FP64 norm 및 zero-RHS 규칙을
적용해 실제 방향의 품질·성공과 entry convergence 예외를 판단한다.
기존 tolerance 비교는 그대로 strict < 이며 NaN retry의 zero 안전 반환을 Direct-U로
덮어쓰지 않는다. 원래 GMRESResult·통계·warm-start의 방향/품질은 raw로 보존한다.
실제 방향의 새 debug 행은 raw/effective를 구분한다. raw 실패 진단·adaptive retune는
기존 raw Krylov 자료에 머무른다. exact trust model은 이미 잔차 벡터를 소비했으므로 재작성하지 않았다.

실제 public solver 제조시험은 다음을 확인했다.

| 경로 | 원 GMRES | 실제 방향 상대잔차 | 새 방향 판정 |
|---|---|---:|---|
| Direct-U, S=I | 요청 tolerance 충족 | 약0.9 | 실패 |
| Direct-U, S=4I | 요청 tolerance 충족 | 약0.9 | 실패 |
| multi-tile halo 계약 | 요청 tolerance 충족 | sqrt(14/20) | 실패 |
| cached warm-start entry | 0 Arnoldi 진입 수렴 | 약1.1 | 실패·잘못된 entry 면제 제거 |
| 변경 없는 Direct-U OFF | 기존 수렴 | 기존과 동일 | 보존 |

선형 제조식에 대한 trust rho≈1과 등가 단위 스케일의 기존 시험도 유지됐다.
이는 implemented matvec에 대한 일관성이다. 정확한 전체 nonlinear residual Jacobian,
전역 Newton forcing 또는 native 에너지 안정성 정리로 확대하지 않는다.
raw 통계와 실제 방향 failure counter의 분류기 명칭/해석은 R2 후속 항목이다.

## 2. 반환 상태의 실패 잔차

기존 일반 실패 출구는 마지막 update 전 잔차를 최신 K와 함께 반환할 수 있었다.
현재는 base 및 수용된 trust/recovery trial의 관측 K/R 쌍을 detached handle로 보존하고,
반환 K와 수치 일치할 때만 같은 고정 metric scale·mask RMS를 계산한다.
새 RHS 호출이 없어 mutable Tile callback을 실패 보고 때문에 다시 실행하지 않는다.
현재 검토한 K 갱신은 functional이며, 다음 loop의 K mask는 새 base 평가 전에 수행된다.
이 계약을 모든 opaque callback이나 미래 in-place 변경까지 증명한 것은 아니다.

제조 비선형 RHS F(x)=2x+.5x²에서1회 수용 갱신 후 예산 종료를 유도했다.
adaptive off/on 모두 반환 residual 약0.215316이 반환 K의 독립 해석식과 일치했고,
초기 residual 약0.507358과 구분됐다. 상태는 비수렴으로 유지한다.
일치하는 관측 쌍이 없는 zero-budget/unmatched 상태는 NaN·measured=false다.
그 값이 legacy mode0/1/2 severe gate에 주는 영향과 별도 예외 early-return의0보고는
아직 R4/R5로 열어 두며, 모든 실패 경로의 수용/계속 실행 parity를 주장하지 않는다.
수치 equality는 signed zero를 같게 처리한다.

## 3. 실제 h600 회귀와 검증 경계

보관된 A9/P0 연구 구성으로 재빌드·한 번의 h600 전방을 수행했다.
이 실행파일의 pressure/계측 compile flag를 제거한 일반 기본 native 구성 인증은 아니다.

- MPI1/실제 Torch1/1,100km·40×80×64·600초, 코드0·WRF success·대시간스텝1개.
- stage2/3/4 각각3 Newton iteration, scaled RMS6.296993e-14/6.345785e-14/6.344006e-14.
- wrapper3/0(시간스텝 수나 Newton 수로 혼동하지 않음), 저장 상태 유한·양의 기본 물리량.
- 끝174개 숫자 배열이 이전 A9/P0와 byte 일치. RK3와 U/V/W/PH/T/MU 거리는 그대로다.
  W RMS6.909943e-4m/s 등의 값은 방법 간 거리이지 정확해 오차가 아니다.
- 기존 RK3·native T0의21필드 일치, 새 RK3 적분/성능 수용 없음.

cpp101/101 + 독립 실패 잔차2사례를 실행했다. 최초 Trust 시험의100-vs-96 실패는
새4검사의 고정 inventory를 갱신하지 않은 집계 오류로 원로그를 보존했다.
100/100 이후 실제 halo 사례를 추가해101/101이며 수치 criterion은 완화하지 않았다.
Mac ar의 __.SYMDEF는31개 object inventory와 분리해 검증했다.

빌드는 matching archive의30객체를 검증 재사용하고 Newt 한 TU만 재컴파일했다.
헤더/class/Fortran ABI 변화는 없다. 컴파일 source와 현재 test-only 후속 commit을 구분했다.
Native runner 원결과는 pending_numerical_comparisons로 두고 같은 저장 출력의
[native_validation PASSED](evidence/newton_residual_consistency_20261102/native_validation.json)를 별도로 연결했다.
성능 표본은 false이며 102.13초 RSL을 속도 개선/퇴보로 판정하지 않는다.
큰 중복 dump는 이미 readonly인 canonical에 byte/hash 확인 후 hardlink했다.

## 다음 순서

다음은 R4 early-return 잔차 provenance, R2 classifier 해석, R5 stage 수용정책이다.
O/E/T/A/F는 checklist의 미완료 상태를 유지한다. 새로운 감쇠·solver·tol·작은 h 비교,
DA/HVP 확대나5일 실행을 선행조건으로 추가하지 않는다. 단위시험의 작은 h를 실용 비교에 쓰지 않는다.
