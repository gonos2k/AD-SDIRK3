# PR308 W 궤적·단일 tolerance 대조 최종 Red 감사

작성: 2026-10-15 10:15:45 +0900  
대상 보고서: `docs/(202610151003)_fine_pair_w_trajectory_and_solver_control.md`  
PROBLEM_DEFINITION SHA-256: `ba46c71c4def022ad8907ab1d88e2daa73cd7c29e314763109e36aa520144b5d`. 읽기 전용 마감 검토이며 새 모델·빌드·CI 실행은 하지 않았다.

## 판정

**PASS — 보고서와 프로젝트 정의의 주장 범위 및 수치는 고정 지표와 맞는다.** coarse/fine pair와 단 한 번의 동일-h Newton tolerance 대조를 기술하고, 이를 0–15초의 제한된 sensitivity 진단으로 한정한다. V3의 20/24 실패와 미자격 reference, 240초 W 원인 미해결은 유지한다. 참해, 시간 차수, accuracy qualification, global tolerance 채택, 240초 인과 결론, 속도 우위를 주장하지 않는다. 기존 U/V adapter, THM Taylor, 일반 관측창/수반 및 프로젝트 전역 window 선택도 닫지 않는다. 이 작업은 세 짧은 native 실행(pair 2개와 단일 tolerance 대조 1개)이며 새 native CI 통과로 표현하지 않는다.

보고서가 구분한 17개 공통시각의 fine−coarse W RMS 최대 `2.70515469e−7 m/s`, fixed `epsilon_W/3` 미초과는 정확도 합격으로 해석되지 않는다. 한 tolerance contrast에서 0.9375초는 `2.4887185e−7→3.2138305e−8 m/s`(87.09% 감소), 15초는 `2.7051547e−7→4.6656761e−7 m/s`(72.47% 증가)이며 두 시각의 cosine도 각각 `−0.996375`, `+0.345660`이다. 따라서 결과는 solver 설정 민감도가 초기 차이에 관여할 수 있음을 보이지만 전체 15초/240초 원인이나 참해 방향을 정하지 않는다. 직접 잔차 increment 합은 local tableau accounting으로만 제한돼 전파오차 상계가 아니다.

Newton setting은 namelist에서 `1e−7→1e−9` 한 항목만 바뀌고 Krylov `1e−7`, effective EWT `1e−6`, input/executable/물리 연산자가 고정됐다. RSL의 일부 `NEWTON SOLVER ENTRY`에서 `dt=0.5`, `newton_tol=0.0`이 보이는 출력은 stream format state로 설명하는 별도 감사 JSON과 연결돼 있다. 실제 step CSV의 `newton_tol_config≈1e−9`, 96개 수렴 헤더의 `atol/effective_tol=1e−9`, effective EWT/Krylov metadata가 이를 뒷받침한다. 재실행 없이 이 로그 표시를 실제 설정 변경으로 해석하지 않는다.

## 증거 및 문서 상태

보고서와 `PROBLEM_DEFINITION.md`는 coarse/fine 수치, Newton 대조, source-work, 미결 정확도 경계를 일관되게 기록한다. 작은 저장소 재계산 예제와 NPZ hash를 제공하며, 17 common-time Green/Red 분석은 별도 JSON에서 대조된다. 일부 RMS 재집계는 부동소수점 순서상 bitwise 같지 않지만 차이는 약 `1e−16` 상대 수준이며 보고서는 이를 exact equality라고 부르지 않는다. Pressure-work source contribution은 별도의 단위로 구분되고 240초 원인/전체 에너지 수지로 승격되지 않는다.

- 최종 보고서 SHA-256: `732d9f816b60cd77b0888eb5aa3df5bb815659c67271456e85a2971abb639d55`.
- Green/Red actual metric crosscheck SHA-256: `5dd8d063f00a0d1be33642092f73cf6e1b21b1c5eaaf52751f87fea34dc3a187`.
- Newton tolerance output-format audit SHA-256: `b3481309b3ecd888b92de3f69a868eda61fe6ac142af65bef90820ef1bc02a22`.
- Compact NumPy recomputation receipt SHA-256: `c975b8da9466c1970d1f25a6436854f4f9f0c9e531b02e6ad2ab511baaea32c9`.

PR306 reference failure, PR307 one-step work diagnostics, and closed restricted inverse remain separate. No production source, solver equation, physics coefficient, initial condition or production tolerance changed in this result; the new analysis/reproduction tools are diagnostic only. The Newton counter is not a production tolerance recommendation.
