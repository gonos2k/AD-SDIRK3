# PR307 실제 수용 ARK W 범위 최종 Red 감사

작성: 2026-10-15 01:10:16 +0900
대상 보고서: `/private/tmp/pr307-accepted-ark-w-stage-20261014/docs/(202610150105)_accepted_ark_w_split_work_and_residual.md`
기준: W307 HEAD `18abbe9a2cd4b32c68d1b3bbf0d64ea0040d48fc`; `docs/PROBLEM_DEFINITION.md`; `docs/evidence/pr307_accepted_ark_w/analysis_manifest.json`. 읽기 전용 감사. 새 모델 실행이나 빌드는 하지 않았다.

## 판정

**PASS — 제한된 한 단계 진단 보고서의 근거·수치·범위가 일치한다.** 기준해 미자격, 240초 후보 정확도 미판정, Stage 4 Newton returned-K 동등성 미확인이 분명하다. 프로젝트 전역 정확도나 예보/속도 완료로 확대하는 주장은 없다.

보고서와 PROBLEM_DEFINITION은 직접 Full−FI−FE 수치가 암시적 stage 2–4에 한정됨을 적고, 명시적 stage 1의 독립 Full 평가는 미측정이라고 명시한다. 평가 입력을 실제 accepted (Y_i)로, `U_ref_stage_` 보조 참조를 fast/FI의 (B_i)와 slow/FE의 (Y_i)로 구분했다. Stage 4의 same-Y FI·Full·pressure와 accepted method K+FE는 유효한 반면 Newton returned-K 배열은 없으므로 반환 K와 후처리 K의 일치는 주장하지 않는다는 점도 정확하다.

주요 수치를 최신 JSON과 대조했다. 물리 W owner 4,096개에서 ΔW tableau replay는 raw endpoint와 일치하고, E/I 별도 조합 차이는 RMS `2.51196e−20 m/s`다. Stage 2–4 (G_W) RMS는 각각 `8.59879e−10`, `1.96352e−11`, `8.31395e−11 m/s`이며 true-error bound가 아님을 밝혔다. pressure/μ-buoyancy tableau work `+56.4331 MJ`와 source-derived dynamic-top `+7.0190 MJ`를 전체 에너지 예산으로 오인하지 않는다. frozen-M0 W-only ΔE `+51.494558 MJ`, method work `+55.716542 MJ`, quadratic defect `−4.221984 MJ`도 한 스텝 항등식으로만 해석하며 240초 실패의 원인으로 단정하지 않는다.

보고서는 one-step same-Y Full과 method K+FE work의 차이 `+253.461 J`를 명시적 stage 1의 FI+FE 조합 및 암시적 stage 2–4 Full 관측 범위로 한정하고 Newton returned-K 동등성을 전제하지 않는다. Observer ON/OFF 및 A5/OFF 16-array parity, 동일 RHS call counts, 한 스텝·0 solver 실패, stage 1의 Newton N/A, 원래 e71 입력과 source/executable binding도 영수증과 부합한다.

## 무결성 및 남은 범위

선별 evidence manifest의 13개 파일 hash가 모두 일치하고 `code_commit=18abbe9a…`다. 보고서는 실행 당시 base HEAD `192c9149`의 수정 source tree에서 빌드한 사실과 검증 바이트가 현재 commit과 일치함을 구분하므로 base HEAD 바이너리라고 잘못 표현하지 않는다. 현재 worktree HEAD는 `18abbe9a…`다.

Stage 4 returned-K 대조, 물리적 W 응답의 더 긴 궤적, PR306의 240초 W 시간민감도 설명, 기존 V3 참조 미자격/후보 판정 보류는 열린 범위로 남는다. V3 budget은 수정되지 않았고 240초 재실행, timestep 사다리, solver tolerance 대조, 압력/초기조건 강제, 총 에너지 보존 또는 성능 주장은 하지 않는다.
