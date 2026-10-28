# ARK600 표준 전방 연속 실행 시작

공개 실행 근거: [ARK600 디스크 중단 자료](evidence/ark600_disk_stop_20261028/README.md). 아래 호스트 절대경로는 당시 로컬 원자료의 위치다.
작성 시각: 2026-10-28T00:03:40.246343+09:00 (Asia/Tokyo)

사용자 `go`에 따라3시간 출력·5GiB 디스크 여유 하한·18시간 실행 한도를 적용했다. 단일 fresh실행이며 재시도·cold restart·ARK300/150 자동 대기는 없다. 원래 초기장에서 시작한5일720스텝이다.

실행 시작: 2026-10-28T00:00:25.466066+09:00. 관리기PID63634, MPI launcher63635, WRF63637, 읽기 전용 관찰기63651. 시각 상한은 2026-10-28T18:00:25.653896+09:00이며 자원·수치 오류가 있으면 이전에 중단될 수 있다. 현재 직접 읽은 수용 main-step 로그는2/720개, 모델시간1200초다. 완주·정확도·성능 수용을 주장하지 않는다.

## 고정 자료와 변경

- 기반소스 main739638f + 기존 두P2 로컬 수정. 생산 C++/Fortran은 채택된 fe03ba5 모델 소스와 차이 없음.
- 실행파일SHA30b0ac22f264c3597790d21e05b98d1b4803683cc12c02640e0f767520bd40bc, 공통입력SHAc3e56611954a5f9c1de107b7c86ecef0a5ff0ea5456cf94f26abfaf31e9bf912.
- NML의 유일한 차이는 history_interval360→180분.100km,40×80×64,h600,5일,동일 방정식/경계/허용오차/필터 유지.
- ManifestSHA bb6e6a4c609af98075abfc09960a82c0fb4170210a21bacd7cc5a2f574f50833. 관리기39b04a3ab74d7d693ccbd446057d6a1a23bda0def85ef4eb5aea3f12e892351d, 분석기823ed95d2b312eb7e9ec3719dd13cc6d84f84ab0c87439ffd0895d21f9dd15c3, 관찰기6ea5e1d6718aa9ff9660c2951b139d374ba15e4618882c842a4bfe2b589082bf.
- JVP 계산은 유지하며 단계 CSV·상세JVP출력·telemetry·자료동화 그래프 기록은OFF다. 현재 단계CSV 0개, JVP DIAG 0건.

## 초기 상태와 비교

초기화 이후21개 상태·기본장 배열 비교가 통과했고, t=0 NetCDF SHA7a9c52880ee5491126f4ea76ddc4c1c045416c8a4af562feb99f590d5b3aef50가 보존 RK3 기준과 같다. 실행 전체의41개3시간 출력은 관리기가 직접 검증한다. 관찰기는 공통6시간 시각만 선택한 symlink view(최종21개)로 중간 시각에는 보존 RK3 h600과, 최종 시각에는 h600/h300/h150 모두와 비교한다.6h/24h/36h/5day 체크포인트를 두었다. 합성41개 입력에서 2/5/7/21개 view 선택 검사를 통과했으며 모델 시험으로 세지 않는다.

Green 검토는 실제 launch 이후 완료한 읽기 전용 점검으로서 사전 승인이 아니다. Red 사전 설정 점검 및 실행 중 읽기 점검도 수행했다. 작은 frozen execution corpus의 Graphify AST를 갱신·해시 연결했다. 전체 호출 완전성이나 수치 정확성 증명은 아니다.

## 운영 근거와 유보

이전 정리 후 다른 디스크 사용으로 여유가3.3GiB까지 줄었다. 이미 ZIP member/복원 SHA를 검증한272개 개별zst 사본2,760,797,022바이트를 추가 정리하고4개보존ZIP 해시를 다시 확인했다. 추가 삭제 목록은 [정리 영수증](/Users/yhlee/AD-SDIRK3-review-evidence/cleanup-202610272228/additional_redundant_zst_cleanup_20261027.json)에 있다. 앞선 정리 보고서의 zst보존은 당시 상태이며, 이후 이 중복 사본을 정리했다. 자료는 ZIP에 그대로 있고 원본 예측장·로그·입력·실행파일은 보존했다.

운영체제 idle sleep 방지만 `caffeinate -i -w63634`로 관리기 수명 동안 요청했다. 영구 전원 설정은 변경하지 않았다.

NML max_krylov_iter=100이나 출력된 실효 max_krylov=50인 기존 설정 경로 차이를 기록했다. 이전 중단 실행 및 이전 quiet6h도 실효값50이며 CONFIG EFFECTIVE 줄이 모두 같다. 원인을 확정하거나 실행 중 수정하지 않았다.

새RK3 실행·모델 재빌드·회귀시험 반복은 없다. 현재3시간 출력은 보존RK3의6시간 출력과 I/O 조건이 달라 동일 정확도 속도 우위로 해석하지 않는다. 기존 작은 수반·복원 완료는 유지한다.

[현재 진행](/Users/yhlee/AD-SDIRK3-review-evidence/ark600-3hour-continuous-20261027/progress.json), [시작 상태](/Users/yhlee/AD-SDIRK3-review-evidence/ark600-3hour-continuous-20261027/start_status_receipt.json), [초기 일치](/Users/yhlee/AD-SDIRK3-review-evidence/ark600-3hour-continuous-20261027/runs/ark_h600/initial_gate.json), [실행 규약](/Users/yhlee/AD-SDIRK3-review-evidence/ark600-3hour-continuous-20261027/manifest.json), [실효 설정 대조](/Users/yhlee/AD-SDIRK3-review-evidence/ark600-3hour-continuous-20261027/effective_config_comparison.json).
