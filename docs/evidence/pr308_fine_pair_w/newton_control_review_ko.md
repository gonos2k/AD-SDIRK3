# Newton 허용오차 제어 비교: 독립 Red 검토

**범위.** h=0.46875 s, 0–15 s의 기존 fine 기준 run과 Newton 허용오차만 1e−7에서 1e−9로 바꾼 제어 run을 비교했다. 같은 입력·실행 파일·Fortran/C++ 아카이브·프로덕션 구현이며, 32개 단계의 CSV 해시와 carry 상태 연속성을 검증했다. 비교는 물리 W 소유점 4096개와 고정 M0/e1 투영을 사용한다. 모델을 추가 실행하지 않았다.

**관측.** Newton 1e−9 제어에서 누적 직접 국소 잔차 합 `Σ dt |bᵢ| RMS(Kᵢ−FIᵢ)`는 `7.3089e−13 m/s`였다. 기준 fine은 `4.3723e−7 m/s`이다. 이 양은 상태전이 증폭을 포함하지 않으므로 전역 오차 경계로 해석하지 않는다. 첫 단계 implicit stage 2/3/4의 W 잔차 G RMS는 각각 `4.8796e−15`, `5.0120e−15`, `4.9181e−15 m/s`; `K−FI` RMS는 `2.3883e−14`, `2.4531e−14`, `2.4072e−14 m/s²`였다. 각 단계 Newton/Krylov 반복은 2/13회였고 scaled Newton 잔차는 `2.05e−15`, `2.13e−15`, `2.09e−15`였다. 첫 단계 G와 RK 잔차는 같은 관계식의 두 표기(`G/a ≈ K−FI`)이므로 독립 증거로 세지 않았다. 단계 4는 returned-K 일치값을 관측하지 못했으며, 불일치로 간주하지 않았다.

대조 궤적 차이 `D=W_fine−W_coarse`는 처음 0.9375 s에서 RMS가 기준 `2.4887e−7`에서 제어 `3.2138e−8 m/s`로 작아졌다(비율 0.129, 변화 벡터와 기준 D의 M0 cosine `−0.9964`). 그러나 끝 시각 15 s에는 기준 `2.7052e−7`, 제어 `4.6657e−7 m/s`로 제어 차이가 `1.7247×` 커졌다. 15 s 변화 `W_control−W_fine` RMS는 `2.6910e−7 m/s`, M0 RMS `2.4966e−7 m/s`; 기준 D와의 M0 cosine은 `+0.3457`이고, 고정 e1 투영 변화는 `+1.6126e−7 m/s`였다. 즉 초기에만 격차가 줄고 이후 다시 커지며, 끝 시각 결과는 단순한 반대방향 보정이 아니다. 시각별 원소 관계 `D_control = D_baseline + (W_control−W_fine)`은 부동소수점 오차 수준에서 성립한다(최대 잔차 `1.06e−22 m/s`).

32개 단계의 tolerance/fallback telemetry에는 gate, retry, fallback, soft-continue 카운터의 비영 값이 없었다. 수렴 잔차가 크게 줄었어도 15 s의 fine/coarse 차이는 줄지 않았다. 이 단일 제어는 Newton 허용오차 민감성을 보여주지만 전체 궤적 차이의 유일 원인이나 해법 정확도를 판정하지 않는다.

**재현 자료.** 계산 결과 JSON SHA-256 `7106d2d584531a0c9f95142213993b33c8b0665c3fcf3ff1f8b60cff579bbb6e`; 17개 공통 시각의 coarse/fine/control W, owner mask, M0/e1 배열 묶음 SHA-256 `a324e504fafe204aa76026fc48b0d7c1942c0f9b0f8cbaf35619d5b2ffeaf5e3`. JSON은 기준 fine 결과/NPZ와 제어 run receipt 및 CSV 해시, 유효 namelist 해시, 실행 파일·소스 아카이브 바인딩을 기록한다.
