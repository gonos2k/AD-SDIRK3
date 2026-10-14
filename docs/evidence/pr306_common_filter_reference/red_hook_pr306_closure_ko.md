# PR306 종료 범위 감사

검토 시각: 2026-10-14 07:09:01 JST (+0900)
대상 증거: `/Users/yhlee/AD-SDIRK3-review-evidence/common-filter-reference-20261014`
검토 범위: 기존 증거만 읽음. 새 빌드·모델 실행·소스 수정 없음.

## 종료 경계

- 최종 same-native mode-3 ARK 기준쌍 `h=0.9375` 대 `h=0.46875`는 고정 V3 기준 24행 중 20행만 통과하고 W 네 행은 실패한다. 따라서 새 참조는 미자격이다. 이전 쌍 `7.5/3.75` 및 `3.75/1.875`도 각각 20/24로 실패 기록을 유지한다.
- 참조가 미자격이므로 h=15 후보의 비교는 수용·기각하지 않고 provisional로 둔다. 후보의 산술 비교에서 20/24가 예산 안에 든 사실은 공식 게이트 통과가 아니다. 기존 V3의 마스크·필드·출력 시각·ε/3 기준 및 실패 상태를 변경하지 않는다.
- 같은 native ARK timestep 쌍은 참해, 오차 상계, 형식 수렴 차수 또는 Richardson 추정의 증거가 아니다. 공통 ARK 연산자 편향이 남으므로 RK3 대 ARK 적분기 단독 정확도 비교도 주장하지 않는다.
- 세 개의 초기 RHS 진단 제어 실행은 t=0과 t=15 s 저장 필드가 서로 비트 일치한다. 이는 초기 짧은 구간의 출력 불변성만 보이며 240초 차이의 원인을 식별하지 않는다.
- 첫 Full RHS의 C++ 재진단 압력으로 재구성한 W 압력·부력 항은 초기 W 가속도 규모를 설명하고 입력 NetCDF 압력만 쓴 산식과 구별된다. 이 단일 t=0 상태의 잔차에는 다른 W 항이 포함되며, 240초 timestep 민감성의 인과 증거가 아니다.
- 240초 결과와 최초 상태 힘을 함께 인용하더라도 후보 수용, reference eligibility, 예측 원인, 전역 정확도 또는 비용 우위를 주장하지 않는다. 추가 timestep 사다리, 새 허용오차, optimizer/Hessian, 추가 예보나 배포를 열지 않는다.

이 종료 메모는 PR291·296·299·302·304·305의 기존 종료 결론을 다시 열지 않는다. 참고 근거는 `green_metric/native_ark_final_reference_pair_analysis.json`, `green_metric/native_ark_finalpair_runs.json`, `red_provenance/native_ark_reference_qualification_red.json`, `green_execution/initial_rhs_probe_three_run_analysis.json`, `green_pressure/initial_cpp_w_pressure_force_analysis_ko.md`이다.
