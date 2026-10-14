# PR307 종료 범위 Red 감사

검토 시각: 2026-10-15 01:12:45 JST (+0900)
대상: W307 HEAD `18abbe9a2cd4b32c68d1b3bbf0d64ea0040d48fc`, 최종 PR307 문서 및 E307 V2 증거
성격: 읽기 전용 감사. 빌드·테스트·모델 실행·저장소 소스 수정 없음.

## 판정

**내용 범위 PASS.** 앞서 제기한 문서 정정이 반영됐다.

- 같은 accepted 15초 ARK mode-3 macrostep의 W `Full−FI−FE` roundoff를 암시 단계 2–4로 한정했고, 명시 단계 1의 independent Full 미측정을 표와 요약에 밝혀 두었다. Stage 4 Newton returned-K 미관측도 parity claim으로 채우지 않는다.
- 일률(W, stage별 `0 / 8.20137 / 6.14721 / 6.12725 MW`)과 tableau 시간가중 일(J, pressure+buoyancy `56.433 MJ`)을 구별했다. 이 수치는 전체 대기 에너지 예산이 아니며, 고정 M0 `−4.222 MJ`는 한 macrostep의 W-only quadratic defect로 제한한다. 240초 W 기준 실패의 원인이라고 주장하지 않는다.
- `PROBLEM_DEFINITION.md`에서 public C/Fortran ABI 불변과 확장된 C++ trace/class layout을 구별하고 matching header 소비자를 재빌드했다고 기록했다.
- V3/native reference의 20/24와 W 네 시각 실패, 후보 정확도 판정 보류를 유지한다. 새 240초 run, timestep 축소, 새 CF 설정, optimizer/Hessian 또는 추가 CI 범위를 열지 않는다. PR291·296·299·302·304·305·306 종료 정책도 다시 열지 않는다.

## 패키징 주의

보고서 `docs/(202610150105)_accepted_ark_w_split_work_and_residual.md`는 현재 `.gitignore`의 `docs/` 규칙에 걸려 ignored 상태다. PR에 싣는 단계에서 강제 추가해야 한다. 이는 문서 내용 PASS를 바꾸지 않지만 공개 diff에 포함됐는지는 별도 확인 사항이다. 현재 `docs/PROBLEM_DEFINITION.md`만 tracked modification으로 보인다.

검토 근거: `green_execution/pr307_v2_completion_receipt.json`, `green_metric/actual_accepted_stage_w_analysis_v2.json`, `green_pressure/actual_stage_pressure_work_v2.json`, `green_pressure/frozen_w_energy_identity_v2.json`, `red_precision_actual_stage_w.json`, `green_build/v2_direct_link_receipt.json`.
