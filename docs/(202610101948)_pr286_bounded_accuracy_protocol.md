# PR #285 후속 정확도 실험 준비

작성 시각: 2026-10-10 19:49:43 JST

현재 native 생산 소스는 PR #285와 동일하다. 수정은 시험 실행파일의 --restart-smoke 분기, 두 수동 진단 helper, 기존 workflow 수동 선택지에 한정한다. 자동 CI는 5개 CTest·16스텝 재생·metadata consumer와 문법 검사만 유지한다.

재시작 실험은 실제 terminal 초기장에서 960×0.3125초를 재현한 뒤 같은 300초 FP64 checkpoint를 사용한다. 원래 solver의 Newton predictor를 유지한 경로, 기존 CarriedState를 새 solver에 복원한 경로, context 초기화 전에 저장한 cold constructor 상태를 복원한 경로를 각각 16스텝(305초까지) 진행한다. 복원 경로와 원래 solver 경로의 endpoint·W105는 bitwise 일치를 요구하고, cold 차이는 측정한다. 이 결과를 900초 전체 warm/cold 동등성이나 범용 restart 보증으로 확대하지 않는다.

미분 실험은 같은 실행파일의 짧은 gate 뒤 fresh 960스텝 center VJP 1회와 네 방향×두 폭(0.005,0.0025)×양·음의 16회 전방을 수행한다. center는 기존 checkpoint·예측·관측·보간구간과 정확히 맞아야 한다. 모든 FD 호출에서 관측·R·solver 허용오차·시간간격을 유지하고 보간구간 변경은 거부한다. 고정 center 잔차와 관측 예측 차분의 내적으로 gradient를 계산한다. Richardson 차이와 피연산자 기반 산술 추정치는 경험적 진단이며 전체 경로의 4D Eg에 대한 엄밀한 상한으로 표현하지 않는다.

실행 전 검증: 두 helper 모두 마지막 수치 분석까지 mock main flow를 통과했다. 실제 native 호출은 mock에 없었다. F821·문법·actionlint·diff 검사 통과. Native 시험 target을 AppleClang21/LibTorch2.10으로 새로 빌드했다. 초기 test CPP(172466...)의 실제 16스텝 local replay는 gradient 차이0으로 통과했고, warm-live snapshot 재적용을 제거한 최종 CPP(c184...)는 다시 빌드했다. 최신 Linux 최소 CI와 장시간 실험은 아직 수행 전이다.

기존 ZIP의 전체 상태 재분석은 완료했다. Source PH·온위·MU에는 descriptor의 정지 base를 더하며, 저장 분석장을 기본장으로 오인하지 않는다. 동일 sigma/stagger 소유 위치에서 U의 주기 중복면과 W/PH 고정 하단을 제외한 변수별 RMS를 기록한다. 고정 Eulerian 높이의 전체 상태 비교나 혼합 단위 norm을 주장하지 않는다.

Graphify 관련 21개 source hash 바인딩과 caller를 확인했다. 추출 결과의 중복·dangling edge는 navigation 한계로 기록했고, 수식·순서·상태 일치는 authoritative source와 실제 실행으로 검증한다.

새 WRF 실행 또는 같은 설정의 RK3 필드/시간 비교는 수행하지 않았다. 생산 소스가 같으므로 PR #283의 적합한 회귀 근거를 새 실행과 구분해 재사용한다.
