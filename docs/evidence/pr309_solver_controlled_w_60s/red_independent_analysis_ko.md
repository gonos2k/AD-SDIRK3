# PR309 Red 독립 수치 감사

Local timestamp: 2026-10-15T23:41:37+09:00.

**범위와 출처.** 최종 고정 프로토콜 V3 SHA-256 `2497c354ebb3ff74fc78ea4376999f007c47e30a6e7ee0cf5d23c7353a2dde99`에 맞춰 완료된 두 실행의 CSV 192개를 각각 한 번 스트리밍 파싱했고, 모든 파일의 receipt SHA를 확인했다. 실행 조건은 0–60 s, h=0.9375(64단계)/0.46875(128단계), Newton tolerance 1e−9, Krylov tolerance 1e−7이다. 실행 파일, C++/Fortran 아카이브, 구현 해시, 입력 e71 해시가 두 run 및 동결 pin과 일치했다. 유효 namelist 차이는 `time_step_fract_den`뿐이며 실제 dt는 15/16과 15/32 s다. 여섯 packed 상태의 초기값은 bitwise 동일하고 단계 carry는 coarse 63/63, fine 127/127 모두 정확했다.

**내부 W 궤적.** 고정 E308 M0/e1과 물리 소유점 4096개를 사용해 65개 공통 시각의 fine−coarse W를 계산했다. t=60 s의 내부 FP64 W RMS는 `1.79844384225e−6 m/s`, M0 RMS `1.61160690566e−6`, 고정 e1 투영 `−7.36647105871e−7`, 초기 고정방향 e1에 직교하는 성분의 M0 RMS `1.43339731400e−6`이다. 65개 시각의 최대 unweighted RMS는 `1.96831049217e−6`, 동결 epsilon/3 `2.59814661893e−6` 아래라 내부 궤적에는 crossing이 없었다. 프로토콜에 따라 `[59.0625,60] s`를 상세 구간으로 기록했으며, 이것은 t=60 화면 판정과 별도인 궤적 진단이다.

**공식 t=60 V3 화면.** 현재 W309 `benchmark_wrf_accuracy_cost.py`의 소유점 crop과 변환을 사용해 실제 NetCDF FP32를 FP64로 올려 차이를 계산했다. t=0의 U,V,W,PH,PHB,T,MU,MUB 8개 필드는 bitwise 동일하고 유한했다. 소유점은 U=4096, V=4352(두 y벽 포함), W=4096, PH=4096, T=4096, MU=256이다. T는 `T+300 K`, MU는 `MU+MUB Pa`, PH는 기하퍼텐셜 섭동으로 처리했다. 모든 필드가 동결 epsilon/3 기준을 통과했다.

| 필드 | t=60 fine−coarse RMS | 동결 epsilon/3 | 비율 |
|---|---:|---:|---:|
| U | 2.33502123e−8 m/s | 2.58147235e−4 | 0.00009045 |
| V | 5.2800000e−9 m/s | 3.37400241e−5 | 0.00015649 |
| W | 1.79844365e−6 m/s | 2.59814662e−6 | 0.69220252 |
| PH | 1.02890808e−4 m²/s² | 3.41658351e−3 | 0.03011512 |
| T | 0 K | 1.20652380e−5 | 0 |
| MU | 0 Pa | 4.86363349e−3 | 0 |

내부 FP64 W를 FP32로 변환한 값은 양 run의 t=0 및 t=60 NetCDF W 소유점과 bitwise 일치했다. 화면 결과는 이 동결된 named-case resolution screen에만 해당한다. 기준의 비자격/provisional 상태와 무관하게 참오차, 차수, forecast 정확도 또는 일반 기준을 뜻하지 않는다.

**단계 잔차.** coarse 64단계/192 implicit stage와 fine 128단계/384 implicit stage 모두 캡처된 solver convergence, accepted-Y, capture identity, no-fast-guard 검사를 통과했다. 모든 576개 implicit stage에서 RSL Newton 카운터는 2였다. 이 카운터에는 초기 잔차 검사와 보정 1회가 포함되므로, 이는 Newton 보정 2회가 아니다. 0회 정지 stage는 없었다. 최대 scaled final residual은 coarse `7.81827504e−15`, fine `2.18280645e−15`이다. 첫 macrostep의 첫 implicit stage(stage 2)에서 W G RMS는 coarse `1.01997493e−14 m/s`, fine `4.87958527e−15 m/s`; 같은 stage의 Kfast−FI RMS는 `2.49611569e−14` 및 `2.38829582e−14 m/s²`다. 이 값들은 stage 방정식의 연관된 양이며 서로 독립적인 증명으로 세지 않는다. same-Y Full−FI−FE 잔차는 coarse 192/192, fine 384/384 implicit stage에서 계산 가능했다. 누적 직접 W 잔차 합 `dt Σ|bᵢ| RMS_W(Kfast−FI)`는 coarse `3.32903839e−12 m/s`, fine `2.92316735e−12 m/s`; 이는 후속 비선형 상태전이 증폭을 포함하지 않는 국소 증가량 계산이지 전역 궤적 오차 경계가 아니다.

Red JSON의 receipt에는 별도 fallback telemetry 필드가 없으므로, RSL 로그의 별도 오프라인 감사를 근거로 gate/retry/fallback/soft-continue 카운터가 64개 및 128개 step 행 모두 0이었음을 확인했다. RSL 감사 파일 `green_execution/pr309_rsl_step_telemetry_audit.json` SHA-256 `bbaf91535955fdd4d29d0d3aac5d77e19b10113f6fa4e96e017d6166fd3d1b5a`; 원본 receipt/log는 수정하지 않았다. 각 단계의 실제 accepted/gate 및 convergence 필드도 별도로 검사했다. 모든 작업은 오프라인 분석이며 Red에서 모델/빌드/프로덕션 소스 변경은 하지 않았다. 두 네이티브 실행은 사전 승인된 executor run이다.

**재현 산출물.** 독립 분석 JSON: `pr309_red_pair_analysis.json` SHA-256 `1aa1821491d18589a1bc83a3105e2bd3474b2b342f5002ed9f9412bcbe6de538`. Compact trajectories and endpoint fields NPZ: `pr309_red_pair_arrays.npz` SHA-256 `7fc736e4b10175cef8afed0fd55fbcde4d35ed2ce199636bb4ae3948ecc2f9f7`. 65-row common W CSV SHA-256 `e8acb8b474fd5f2bcd76aff79809d93b19af94defc27ba7a68f4073831268e92`; 192-row step/stage CSV SHA-256 `47ca7b0cda737728a2ab8c71638c9ce3606acf2e3a6cc7645a88adfb1d14237f`. Analyzer SHA-256 `f8168d62a8d5786dcb4adc9dd69569d6c7c182c02221915d0f84c327ab3bf248`.
