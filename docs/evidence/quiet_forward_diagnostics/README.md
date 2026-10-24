# 일반 전방 진단 OFF — 코드와 native 회귀 근거

[실행·변경 보고서](../../(202610241655)_quiet_forward_diagnostics_native_validation.md)는 같은 600초·6시간 조건에서 진단 출력 제거와 예측장 보존을 확인한 기록이다.

- [native_validation.json](native_validation.json): 각 방법의 시작·끝값 174개 숫자 배열의 값·바이트 일치, 유한값, 비용과 진단 문자열 개수
- [costs.csv](costs.csv): 같은 6시간의 단조 경과시간·wall-clock 값·RSL 적분 시간·표본 RSS
- [field_distances.csv](field_distances.csv): 새 quiet ARK600−RK3 600의 변수별 거리. 정확해 오차가 아니다.
- [evidence.zip](evidence.zip), [archive_receipt.json](archive_receipt.json): 소스 패치·변경 파일·설정·빌드·시험·실행·종료 로그, Graphify와 독립 Green/Red 검산
- [data_reference.json](data_reference.json): 새 출력과 완전히 같은 기존 NetCDF payload의 위치·SHA-256

새 실행의 시작·끝값 파일 네 개가 기존 파일과 **파일 전체 바이트까지 동일**하다. 따라서 원배열을 중복 저장하지 않는다. 원배열은 이전 [6시간 근거 ZIP](../standard_ark_six_hour/evidence.zip)의 `data/runs/ark_h600/`와 `data/runs/rk3_h600/`에 있다. 두 묶음이 함께 있어야 원배열을 다시 읽을 수 있다. 새 ZIP의 `data_reference.json`과 이전 ZIP의 SHA-256을 먼저 확인한다.

이전 묶음에는 원배열을 직접 차감하는 `analysis/recompute_endpoint.py`와 고정 분석기·data-only 재분석 계획이 포함돼 있다. 새 파일이 그 payload와 동일하므로 같은 배열 차감이 새 quiet 결과에도 적용된다. 이 관계는 이번 실제 새 출력 파일을 Root·Green·Red가 각각 직접 읽어 확인한 것이며, 해시 선언만으로 대체한 계산이 아니다.

새 ZIP의 `MANIFEST.json`은 각 payload의 크기·SHA-256을 기록하며, archive receipt는 ZIP 자체 해시·CRC·내부 138개 payload 검증을 기록한다. 실행파일·라이브러리·아카이브 객체는 포함하지 않는다. 해당 바이너리와 전체 작업 자료는 `/Users/yhlee/AD-SDIRK3-review-evidence/standard-forward-comparison-20261024/`에 보존한다. 최종 바이너리·소스·입력의 핀은 manifest와 링크·시험 영수증에 있다.

저장소의 `native_validation.json`은 읽기 쉬운 요약이다. 변수별 전체 검사 목록과 40종 출력 개수는 ZIP의 상세 `quiet_forward_diagnostics/analysis/native_validation.json`에 보존했다.

`source/change_from_d6d304d9.patch`와 `source/`는 실제 최종 변경이다. 기존 헤더·ABI·허용오차·JVP/FD/INN 선택은 유지한다. 반환 잔차가 진단 OFF에서도 실제 측정값을 가리키도록 한 수정은 보고서에 구분했다. `debug_level=0, solver_telemetry=false`는 반복 진단을 끄며, 필수 수렴·비유한값·halo 검사/복구, 이상값·오류, WRF의 기본 상태·예측 출력·진행·종료 통계는 유지한다. 자료동화 궤적 저장은 별도의 기존 명시적 요청/종료 범위를 사용한다.

원래 20/21 검사 실패와 3스텝 중지 시도, private 빌드 설정·링크 수정 이력도 보존했다. 최종 통과는 수정된 같은-이력 대조의 24/24, 최종 아카이브의 고정궤적 수반 검사, 두 정상 종료 native 6시간 실행에 근거한다. 중지된 시도는 수치 실패나 유효한 비용 표본이 아니다.

Newt/JVP 텔레메트리를 껐으므로 native 실행의 반복수·JVP 호출수·JVP 시간은 새로 보고하지 않는다. Fortran 108/0은 wrapper 호출/실패 수다. 단조 경과시간 비 304.64와 RSL 시간 비 443.30은 같은 시간간격의 제한된 단일 실행 관측이며, 동일 정확도 속도비·5일 완주·MPI 일반 안정성 인증이 아니다.
