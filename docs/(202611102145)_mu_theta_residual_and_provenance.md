# μ/T 잔차 평가오차와 W 출처 교차검증

정확한 현지 시각: 2026-11-10T21:45:58.997604+09:00

기준 main: `326347dd334a321f2523edbf5af1c9b182bc1b6d` (PR348).
Native 자료의 소스는 기존 `d05130c0`이며 새 native 실행은 없다.

## 이번 체크리스트

| 항목 | 상태 | 이번 근거 또는 남은 일 |
|---|---|---|
| P3 W 감소 판정의 입력 SHA·차원·가중치·source 연결 | 완료 | 14개 모순 거부, 같은 source의 플랫폼별 binary 차이 허용 |
| 기존 W 전체 평가오차·W-only 감소 | 기존 완료 유지 | 새 W MPFR 실행 없이 기존 결과 재사용, 감소 CLI 출력 byte 일치 |
| 같은 두 상태의 μ 전체 평가오차 | 조건부 완료 | 현재 projected U/V에서 질량 연속식 재계산, 전체 3,321개씩 비교 |
| 같은 두 상태의 T 전체 평가오차 | ε family 아래 조건부 완료 | 실제 Tile helper의 두 ε를 포함한 전 구간, 212,544개씩 비교 |
| 저장 R/S의 norm 차이 ν | 기존 완료 유지 | 이번에 재실행하지 않고 기존 exact-integer 결과 재사용 |
| U/V 전체 평가오차 | 미완료 | primary·NH4·질량 변환·활성 divergence damping 포함 필요 |
| PH 전체 평가오차 | 미완료 | 수평 이류·수직 omega·g w 포함, 실제 FNM/FNP와 selector 출처 필요 |
| 전체 εG와 e0+e1 감소 예산 | 미완료 | U/V/PH를 0으로 두지 않는다 |
| 정확한 forcing·Stage3 반복·시간/예보 수렴 | 미인증 | JVP gap, 역증폭/비선형성/정의역과 시간 지도는 별도 |

## 변경과 수학적 범위

생산 수치식·solver 옵션·ABI·tolerance는 변경하지 않았다. P3 판정기에 reference의
전체 입력 SHA map과 manifest의 key/value 동일성, 고정 source SHA, packed N와 W count,
metric block/offset 및 min/max 관계를 추가했다. 플랫폼별 executable SHA 일치는 요구하지
않는다. 기존 참조 소스와 수치 결과는 그대로이고, 원래 evidence inventory도 보존했다.
이는 제공된 metadata의 정합성 검사이며 임의로 위조한 참조 결과의 인증은 아니다.

새 offline MPFR256 계산은 기존의 정확한 binary B/a/C/K를 입력으로 정의하고
`Y=Q(B+aK)`와 현재 mass flux를 실수 구간으로 다시 계산한다. 반올림된 native Y 또는
Mdot를 목표식의 고정값으로 대신하지 않는다. μ의 반환 tendency는 unscaled Mdot이고,
fast mode3의 coupled T tendency는 0이므로 최종 변환은

    F_mu = Q_mu Mdot
    F_t  = Q_t[-t_pert*c1h*Mdot/(c1h*(mu+MUB)+c2h+epsilon)]
    G_b  = K_b-F_b

이다. 이 경로에서 명시적 theta/omega 이류를 넣거나 absolute theta를 대신 쓰지 않는다.
출력 alias에는 physical core의 tendency를 적용하되 마지막 subtraction은 raw packed K를
사용한다. 주어진 B/K/C 이전의 형성 오차는 이 목표 밖이다.

실제 autocast 선택은 기존 packet에 없으므로 epsilon을 관측했다고 하지 않는다.
Tile-local helper의 exact promoted binary32 `1e-10f`와 `1e-4f` 사이 모든 고정 epsilon에
유효한 family 상계를 계산했다. 별도의 metric helper를 처음 읽은 Red audit는 correction을
함께 보존했다. 실수 target family의 모든 값을 감싸는 보수적 구간이며, runtime epsilon을
단일 값으로 확정한 결과가 아니다.

## 검증 결과

| 가중 mathematical RMS 평가오차 상계 (분모 full N=1,080,491) | Base | Accepted |
|---|---:|---:|
| μ | 3.6580367994881326e-16 | 3.61929848876647e-16 |
| T, epsilon family 전체 | 9.210741413599433e-15 | 9.656070767402656e-15 |

권위 값은 JSON의 upward binary64와 exact hex다. inverse metric은 두 블록 모두 실제
min=max=1이다. 각 블록 안의 오차를 직접 비교했으며 기존 W 오차를 다시 합산하지 않았다.
제조식의 zero flow, 비균일 질량/product rule, nonzero K/a, epsilon hull, 잘못된 residual과
정의역 거부 검사가 통과했다. 등록한 전체 격자 offline 실행 1회는 18.26초/exit0이고
24개 pin이 전후 동일했다. clang21/MPFR4.2.2, 소스·binary·입력 해시를 보존했다.
W 감소의 기존 자료 replay는 byte 단위 동일했다. Green/Red 소스·증거 검토를 분리했다.

서로 겹치지 않는 residual 블록의 상계는 나중에 동일 norm/N에서 제곱합할 수 있으나,
U/V/PH가 아직 없다. 따라서 전체 감소 예산 약 0.0019075383050416873을 통과했다고
선언하지 않는다. 같은 블록 내부의 여러 항은 직교 항으로 간주하지 않는다.

새 WRF/RK3 적분·native rebuild·예보장·runtime/비용 비교는 수행하지 않았다.
기존 같은 설정 RK3 자료와 one-step 회귀는 원래 범위에서 재사용하고, 이번 packet은
Stage2 수렴/Stage3 첫 수용 후 진단 exit86/완료 timestep0 범위를 유지한다.

## 다음 순서

1. PH의 실제 FNM/FNP와 활성 selector 및 PHB 출처를 고정하고, 수평/수직 이류 전체를 대응시킨다.
2. U/V primary·NH4·질량 product-rule·divergence damping을 같은 fixed-input 목표로 포함한다.
3. 여섯 블록 εG와 기존 ν를 각각 한 번씩 합성해 정확한 전체 merit 감소 예산을 판정한다.
4. 그 뒤 실제 수용 step의 정확한 forcing, 역/Lipschitz·영역 유지와 반복/시간 수렴으로 확장한다.

[공개 재현·입력·범위](evidence/scalar_blocks_20261110/README.md).
