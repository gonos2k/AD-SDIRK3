# PR308 fine-pair analyzer 종료 범위 Red 감사

검토 시각: 2026-10-15 10:09:39 JST (+0900)
대상 analyzer SHA-256: `b3dfa74b43eae9ced7d98670aa127714dcb95796b7e771e488834d3842e490f7`
protocol: `pr308_fine_pair_w_trajectory_protocol_v3`, SHA-256 `3620ba8d9846b97da435aac635fc4f846ad5342c0cbdc2b37842fb5a317845d9`
성격: 읽기 전용 소스·기존 receipt/결과 감사. 빌드·모델 실행·소스 수정 없음.

## 판정

**최종 analyzer scope PASS.** 앞서 지적한 실행 provenance 문제와 Green의 stage-1/continuity/duplicate-plan 항목이 모두 닫혔다.

- analyzer는 coarse/fine 실행 각각에 대해 receipt input SHA를 protocol의 e71 SHA와 비교하고, label별 namelist SHA 및 실제 namelist bytes를 frozen hash에 대조한다. CPP archive도 protocol pin과 일치시킨다. coarse/fine executable·implementation/input 일치, runtime 설정 일치와 receipt-bound initial-state gate도 유지한다.
- run plan은 정확히 두 항목만 허용하고 duplicate label을 거부한다. 16/32 accepted CSV 단계, 연속 `step_seq`·host token, 각 단계 dt, 이전 projected state에서 다음 input으로의 full-state continuity를 검증한다. Explicit stage 1은 Newton `K−FI`가 없으므로 implicit postprocess bound에서 0으로 처리하고, implicit residual 부재는 bound를 unavailable로 둔다.
- 수치 입력은 PR307 V2의 고정 `a0/e1/M0` NPZ hash에 묶이며 e1의 구성·M0 RMS·4096 owner shape/양수 weight를 재검사한다. W crop은 `[k,j,i]` 16×16×16, 고정 하단 제외·동적 상단 포함·packed closing row/column 제외다.
- 17개 공통 시각은 accepted CSV의 연속 sequence와 정확한 rational dt로 계산한다. WRF `Times`는 19자 초 단위라 t0/t15만 endpoint publication과 비교한다. protocol도 이 차이를 명시한다.
- 매 accepted step의 G, K−FI, pressure/buoyancy domain power 및 tableau work는 기존 PR307 accepted-step 분석기로 다시 읽는다. `power_w`와 적분 `work_j`를 별도 칼럼으로 둔다. Baseline pair 결과와 독립 Red CSV 계산은 17개 시각 모두 통과했고, 출력의 analyzer hash가 위 고정 SHA와 일치한다.
- optional pair-fit regression은 protocol에서 `enabled=false`다. 결과는 0–15초 trajectory의 기술 통계이며, 정확도·reference 통과, order/truth, oscillation/frequency, causality 또는 efficiency 주장에 쓰지 않는다. PR306의 20/24 기준 실패는 그대로 열린다.

## 분리 유지

Newton `1e−9` fine control은 기준 coarse/fine protocol과 별도 조건 대조다. baseline plan은 `.9375 s` 16-step과 `.46875 s` 32-step 두 실행만 포함하며, control을 baseline pair 결과나 고정 reference 판단에 섞지 않는다. PR307의 한 스텝 pressure/work 진단도 이 0–15초 비교의 per-frame 값이나 240초 원인으로 재해석하지 않는다.

근거: `green_metric/fine_pair_analysis_plan.json`, `green_metric/fine_pair_w_trajectory_analysis.json`, `red_provenance/pr308_independent_csv_primary_trajectory.json`, `green_execution/common_initial_state_gate.json`, `preflight/fixed_w_projection_basis.json`.
