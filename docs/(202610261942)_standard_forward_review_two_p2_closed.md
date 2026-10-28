# 표준 전방 비교: 두 P2 해소와 남은 실행 항목

공개 실행 근거: [ARK600 디스크 중단 자료](evidence/ark600_disk_stop_20261028/README.md). 아래 호스트 절대경로는 당시 로컬 원자료의 위치다.
작성 시각: 2026-10-26T19:42:24.314262+09:00 (Asia/Tokyo)

기준 main: `739638f0edeb52ab6105450754275f7aa87cdc78`. PR #323의 CI action 변경만 반영한 뒤 격리 worktree에서 수정했다. 원래 작업 폴더의 기존 변경, 생산 수치식, 실행파일, 허용오차, 감쇠, 입력장과 보존 증거는 수정하지 않았다. 아래는 로컬 수정이며 새 PR/CI 결과를 주장하지 않는다.

## 체크리스트

- [x] R1: 기존 `wrfout_d01*`, `wrfoutReady_d01*`, `wrfrst_d01*`가 하나라도 남아 있으면 실행 전 거부. 파일 삭제 없이 `forecast_intent`/Popen 이전에 차단하며 입력·namelist만 있는 폴더는 통과한다.
- [x] R2: 재계산 로더에서 NPZ 크기와 SHA-256을 검사하고, 해시한 동일 바이트를 로딩한다. 두 배열 각각의 선언 형상·FP64·유한값을 확인한다. CLI 오류는 비영 종료하며 부분 CSV를 내보내지 않는다.
- [x] R3: 기존 24·36시간 파생 배열의 내적·층별·X/Y 파수 진단을 확보했다. 아래 해석은 수치 참값에 대한 정확도 판정이 아니다.
- [ ] R4: 동일 설정 ARK300의 36시간 표적 대조. 새 실행 없음.
- [ ] R5: ARK600 연속 5일 완주, 600/300/150 민감도와 정확도·비용 수용. 새 실행 없음.

## 검증

`python3 -B tools/test_run_standard_wrf.py`: 관리기 모의시험 23개 통과. 과거 세 출력 패턴별 차단·무삭제·Popen 미호출과 새 입력 폴더 경로를 실제 main 진입으로 확인했다. WRF 실행 시험은 아니다.

`python3 -B docs/evidence/ark600_five_day_interrupted/test_recompute_partial_metrics.py`: 8개 통과. 정상·해시 변조·크기·두 배열 각각의 형상/자료형/NaN/Inf 오류와 CLI 실패를 확인했다. 원본 NPZ 14개의 해시·크기가 모두 manifest와 일치한다. 이 환경에서 수정 전후 14행 CSV는 바이트 일치하며, 공개 CSV와는 기존 NumPy 반올림 차이를 포함해 수치 열이 1e-12 이내다.

Green/Red 교차 검토: 추가 지적 없음. Graphify는 관리기/시험과 배열 로더/시험의 작은 corpus만 갱신했고, 소스 해시를 연결했다. 관리기는 작은 AST 추출, 로더는 두 파일 그래프를 갱신했다. [소스/추출물 해시 연결](/Users/yhlee/AD-SDIRK3-review-evidence/main739638f-followup-20261026/graphs/binding.json)을 보존했다. 전체 저장소 호출 완전성을 보장하지 않으며 그래프는 수치 정합성 증명이 아니다. `git diff --check` 통과.

## 보존 배열로 확인한 사실

같은 초기장에 대해 R=RK3(t)-X(0), D=ARK(t)-RK3(t), A=R+D이다. 새 검증 로더를 거친 내적은 [inner_products.csv](/Users/yhlee/AD-SDIRK3-review-evidence/main739638f-followup-20261026/inner_products.csv)에 기록했다. 36시간 W의 mean(D*R)는 -1.68137493794e-9 (m/s)^2, RMS(A)=0.003188510976 m/s, RMS(A)/RMS(R)=1.575175375, cosine(A,R)=0.634589439다. 항등식 잔차는 약3.4e-21 (m/s)^2다. 단순 감쇠 모형과 다른 크기·패턴 차이가 존재한다.

기존 공간 진단에서 36시간 차이 제곱노름의 X평균 성분은 W96.62%, V92.19%, T97.33%였다. Y 경계에 맞춘 DCT-II(W/T)·DST-I(V) 분해에서 Y방향 파장 4격자 이하 성분은 각각99.58%,94.46%,97.85%였다. 이는 질량가중 에너지가 아니며 원인·수치 발산·어느 방법의 정확성도 확정하지 않는다. Y 분석은 지도에서 줄무늬를 관찰한 뒤 수행한 탐색 진단이다.

[기존 X/층별 지표](/Users/yhlee/AD-SDIRK3-review-evidence/ark600-continuous-20261026/diagnostics/summary_metrics.csv), [층별 표](/Users/yhlee/AD-SDIRK3-review-evidence/ark600-continuous-20261026/diagnostics/per_z.csv), [Y 파수 지표](/Users/yhlee/AD-SDIRK3-review-evidence/ark600-continuous-20261026/diagnostics/y_modal_summary.csv), [공간 그림](/Users/yhlee/AD-SDIRK3-review-evidence/ark600-continuous-20261026/diagnostics/mean_x_spatial_maps.png), [Y 파수 그림](/Users/yhlee/AD-SDIRK3-review-evidence/ark600-continuous-20261026/diagnostics/y_modal_energy.png)을 재사용했다. 동일 영역에서 보존된 RK3h150과 직접 비교한 36시간 W 거리는0.002469755233 m/s이다. RK3h150을 정확해로 간주하지 않는다.

## 디스크와 출력 간격

현재 20GiB 기준은 실행 규약의 남은 디스크 하한이며 출력 용량 추정이 아니다. 실제 보존 RK3의 5일/6시간 출력21개 총합은105,714,184바이트(약106MB)다. 기존 비초기 파일 평균을 적용하면 3시간41개는 약209MB, 1시간121개는 약624MB다. 압축률이 달라질 수 있으며 로그·임시 파일·시스템 사용량은 별도다. 현재 출력은6시간 간격이므로1/3시간 간격은 출력 빈도를 높인다. 자원·출력 규약을 묵시적으로 바꾸거나 보존 자료를 삭제하지 않았다.

승인된18시간 실행 상한은 유지한다. 현재 새 모델 실행은 시작하지 않았고20GiB 잔여 공간 기준이 충족되지 않았다. 재실행 시 이 수정 관리기의 소스 해시와 선택한 출력/자원 규약을 다시 고정해야 한다. 이 운영 조건을 새로운 수치 수용 조건으로 해석하지 않는다.

새 모델 적분·재빌드·RK3 비교 실행·성능 측정·GitHub CI 실행은 수행하지 않았다. 기존 중단 실행은 계속 five_day_complete=false, accuracy_pass_claim=false, cost_eligible=false이며 완료된 수반·복원·순열 검사를 다시 열지 않는다.
