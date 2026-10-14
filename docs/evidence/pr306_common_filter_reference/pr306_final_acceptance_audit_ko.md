# PR306 최종 수용 문서 Red 감사

작성: 2026-10-14 07:12:47 +0900
대상: W306 HEAD `5dc69a35823a5af8f6d5e24978933a25ee2b7081`, `docs/(202610140710)_common_filter_reference_and_initial_w.md`, `docs/PROBLEM_DEFINITION.md`, `docs/evidence/pr306_common_filter_reference/`
범위: 최종 문서·고정 증거 읽기 전용 감사. 모델·빌드·테스트를 실행하지 않았다.

## 판정

**수치 판정과 프로젝트 범위는 정확하다. 문서 공개 전 아래 세 문구/경로를 수정해야 한다.** 이들은 수치 결과를 바꾸지 않지만 구현 경로와 검증 범위를 오해하게 할 수 있다.

1. `kdamp=0.2`가 stage RHS 전에 전달된다는 문장에서 “Fortran bridge의 solver-local setter가 전달”이라고 쓰지 않는다. 실제 근거는 초기 grid 설정의 `0.01` 다음에 `unifiedStep`이 `cfg.kdamp=0.2`를 `grid_info_`에 복사해 stage RHS 전에 덮어쓰는 소스 호출 순서다. 직접 stage-time readback이 없다는 현재 단서도 유지한다.
2. 문서가 지정한 `docs/evidence/pr306_common_filter_reference/AD-SDIRK3_PR306_common_filter_reference_evidence.zip`은 현재 해당 경로에 없다. 실제 생성된 archive의 host 경로와 최종 SHA/manifest를 확인한 뒤 링크를 고치고, 휴대용 path map/검증 기록과 일치시킨다. 패키징 전 완료된 것처럼 쓰지 않는다.
3. “후속 PR의 CI가 필요하다”는 문장은 이번 결과를 확장할 새 WRF Fortran integration CI 실행이 선행조건인 것처럼 읽힐 수 있다. 그 테스트를 실행하지 않았다는 사실만 기록하고, 이번 narrow evidence 보고서에 새 CI 범위 확대를 요구하지 않는다.

## 수치·범위 감사

- `native_ark_final_reference_pair_analysis.json`과 독립 Red gate는 h=0.9375/0.46875초가 고정 V3 24행 중 20행 통과, W 4개 시각 실패임을 일치하게 기록한다. 240초 W RMS는 `1.2126077728351374e-5 m/s`; 고정 `ε_W/3=2.5981466189324436e-6 m/s`의 4.6672배다. h=7.5/3.75 및 3.75/1.875 실패도 각각 20/24로 별도 보존한다.
- 현재 a5 실행파일/C++ archive/e71 입력으로 h=15초 제어가 보관 PR304 h=15 결과와 8개 raw field × 5개 시각 40/40 배열 bitwise 일치했다. 다른 다섯 timestep도 같은 실행파일·입력·연산자 계열 및 carry를 쓴다는 receipt/consistency 자료가 있다. h15/3.75 receipt의 return code 부재는 문서가 성공 marker와 출력 기반 상태로 정확히 구분한다.
- 모든 pair comparison에서 ε, owner mask, 시각 또는 필드 정의를 바꾸지 않았다. PH를 geopotential `m²/s²`로 평가하고 metadata erratum을 적용했다. 비교를 named-case empirical resolution screen으로 제한하고 Richardson/형식차수/참오차 상계/독립 Fortran truth/stock RK3 accuracy를 주장하지 않는다.
- 후보 간 epsilon 산술값을 제시하되 reference가 미자격이라 h=15 후보의 formal acceptance를 보류한다. 이전 Fortran RK3의 V3 24행 실패를 별개로 남기며 원인·결과를 섞지 않는다.
- W 원인 진단은 초기 W=0 관측, 시간간격 차이 shape와 한 번의 t=0 Full RHS 분석으로 제한한다. same-call 재진단 pressure/buoyancy 항과 W tendency의 cosine `0.9980567`, remainder RMS `6.84301e-7 m/s²`(약 6.23%)를 기록하면서 remainder의 물리 항을 미배정으로 둔다. 이를 240초 전체 W 차이의 원인 증명으로 확대하지 않는다. raw NetCDF P 계산을 실제 C++ RHS로 오인하지 않는다.
- 기록한 wall time을 단일 실행의 관찰값으로 제한하고 비용·속도 게이트나 우위 주장을 하지 않는다. 사용자 선택 또는 프로젝트 전역 정확도 기준 채택, 연구/예측 완료, production/ABI 변경을 주장하지 않는 점도 적절하다.

## 최종 상태

필수 세 문구/경로가 정정되면 **이번 narrow named-case reference audit은 PASS**다. 고정 예산으로 적격 native-ARK 참조를 얻지 못한 사실과 이에 따른 미결 상태는 명확히 남아야 한다. 문서에 명시된 초기 W RHS 조사는 다음 진단 후보일 뿐이며, 이 감사는 새 실행을 요구하지 않는다.

## 2026-10-14 07:13:11 +0900 업데이트

최종 Green 수정본을 다시 읽었다. (1) `bridge`의 초기 `kdamp=0.01`과 stage 전 `unifiedStep`의 config `kdamp=0.2` 동기화를 정확히 구분했고 직접 readback 부재도 남겼다. (3) 테스트 문장은 로컬 C++ core CTest와 아직 실행하지 않은 WRF integration CI를 구분해 새 CI 범위 확장을 이 narrow 보고서의 선행조건으로 요구하지 않는다. 두 항목은 닫혔다.

(2) 남은 자료 링크는 미해결이다. 보고서가 여전히 존재하지 않는 repo 내 ZIP 경로를 가리킨다. 현재 archive 파일은 `/Users/yhlee/AD-SDIRK3-review-evidence/common-filter-reference-20261014/AD-SDIRK3_PR306_common_filter_reference_evidence.zip`에 있지만, 아직 최종 hash/manifest 검증이 끝났다는 근거는 확인하지 않았다. 최종 archive 검증 뒤 실제 배포 위치를 링크하고 SHA/manifest를 기록해야 한다. 그때까지 결과는 수치 범위 PASS, 패키지 링크 조건부다.

## 2026-10-14 07:13:27 +0900 최종 재확인

최신 경로 `/private/tmp/pr306-common-filter-reference-20261014/docs/(202610140710)_common_filter_reference_and_initial_w.md`의 SHA-256 `ce7e22678e6ed7684456caffefe2ea42cb5e0c6cea511521408d9d0c4c4c5989`를 직접 확인했다. 이전 업데이트를 쓰는 동안 Green 수정이 반영되어 있었고, 그때의 ZIP 경로 지적은 오래된 문서 본문을 읽은 데서 생긴 오탐이다. 최신 본문은 E306 로컬 archive의 실제 절대경로를 정확히 가리키며 최종 archive hash/CRC receipt는 검증 뒤 추가한다고 정직하게 표시한다. 이를 blocker로 유지하지 않는다.

현재 문서 본문과 `PROBLEM_DEFINITION.md`, 기준·실패 수치 JSON을 대조한 결과 수용범위 관련 blocker는 없다. **내용 감사 PASS; archive CRC/SHA 검증 receipt만 패키징 절차상 아직 대기 중**이다. 고정 V3 예산 아래 eligible reference를 얻지 못했고 후보의 공식 수용 결과도 없으며, 이를 전체 연구 완료·프로젝트 기준 채택·정확도 또는 속도 향상으로 확대하지 않는 경계가 유지된다.
