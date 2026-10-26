# ARK600 5일 실행 중단 — 부분 36시간 자료와 관리기 후속 조치

작성 시각: 2026-10-26 09:48:19 JST (UTC+09:00)

## 상태

단일 `ark_h600` 5일 실행은 2026-10-25 16:50:09.377558 JST에 끝났다. `forecast_receipt.json`의 종료 사유는 `processes()`가 `/bin/ps -axo pid=,ppid=,rss=,stat=,lstart=` 호출에서 3초 시간 초과를 낸 뒤 상위 예외 처리로 전달된 것이다. 관리기의 `finally`가 소유한 launcher에 `TERM`을 보냈고, 종료코드는 1, 정리 오류는 0, 남은 소유 프로세스는 없었다. 수치적 발산으로 판정된 실행은 아니다.

기록된 완료 outer step은 251/720 (34.8611%)이며 모델 시간은 150,600초, 즉 41시간 50분이다. 18시간 제한은 소진되지 않았다. `progress_last_running_snapshot.json`의 250-step 실행 중 스냅샷은 보존했고, 마지막으로 남은 `progress.json`의 250-step 상태는 원본을 바꾸지 않은 `forecast_receipt.json`을 기준으로 251-step 실패 종료로 정정했다. 최종 성공 표식은 없고 `aborted_markers`는 0이다. `cost_eligible=false`, `accuracy_pass_claim=false`, `five_day_complete=false`를 유지한다.

영수증에는 경과 wall time 30,584.036694초(약 8시간 29분 44초), monotonic time 17,304.618597초(약 4시간 48분 25초), 차이 13,279.418097초(약 3시간 41분 19초)가 기록돼 있다. 차이의 원인을 시스템 수면이나 시각 보정으로 단정하지 않는다.

## 저장된 비교 자료

이미 저장된 0–36시간 7개 history frame만 후처리했다. Green의 독립 검토에서 초기 상태 gate, history 파일 해시, 유한 필드와 양의 물리량 검사가 통과했다. 이 범위는 저장된 frame에만 해당하며 내부의 저장되지 않은 모든 시각이나 41시간 50분까지의 모든 상태를 인증하지 않는다.

RK3 `h=600 s`는 비교 기준이지 정확해가 아니다. ARK와 RK3의 W RMS 차이는 24시간에서 `4.376337489e-4 m/s`, 36시간에서 `2.464239036e-3 m/s`다. 방법 간 거리이므로 시간 오차의 합격 판정으로 해석하지 않는다. 36시간까지 저장된 ARK frame의 최소값은 총온위 258.837097 K, 건조 기둥 질량 88,939.260986 Pa, 층 깊이 214.968433 m, 총압 11,079.638451 Pa다.

6시간 후처리에서 같은 구현 비교를 timestep 감도 비교 API에 연결한 호출 오류는 비교 호출을 바로잡아 저장 자료만으로 복구했다. 모델을 재시작하지 않았고 solver 방정식이나 수락 기준을 바꾸지 않았다. 이는 특정 후처리 호출의 오류이며 전체 solver 결함으로 일반화하지 않는다.

## 다음 실행용 관리기 보완과 검증

원래 실행과 입력, executable, source manifest, 종료 영수증은 변경하지 않는다. 원본과 분리한 `tools/run_standard_wrf.py`와 다음 실행용 관찰기 사본에 세 항목을 보완했다.

1. 일시적인 process-tree 표본 시간 초과를 모델 실패와 구분하고, 재시도는 제한한다. 독립적인 wall-time, 디스크, 프로세스 생존 감시는 유지하며 지속적인 감시 불능은 fail-closed로 처리한다.
2. 종료 시 wrapper 통계를 누적 행 전체 개수가 아니라 마지막 누적 행 `2160/0`과 실패 행 부재로 검증한다. 현재 RSL에는 `300/0`, `600/0` 누적 행이 먼저 출력되므로 `[(2160,0)]` 단일행 조건은 정상적인 장기 실행에서 오거부할 수 있다. 이는 이번 중단 원인은 아니다.
3. observer가 종료될 때 `progress.json`을 terminal 상태로 갱신한다.

관리기의 모의 프로세스·시계·조회 오류와 관찰기 종료 상태를 다루는 표준 라이브러리 고장 주입 21개 검사가 통과했다. 한두 번의 조회 timeout 후 복구, 3회 연속 누락 시 제한 중단, 조회 중 시간/디스크 상한, 잘못된 표본, 실제 Popen 종료 우선, 소유 프로세스 정리와 타 PID 신호 금지, 누적 통계와 종료 progress 갱신을 확인했다. WRF/MPI를 실제 시작한 시험은 아니다. 새 모델 실행·재시작·생산 수치 회귀·재빌드·성능 시험은 수행하지 않았다. 이 ARK 실행 중단은 이전의 MPI launcher RC1 조사와 별개이며, 그 원인을 설명하지 않는다.

## 증거 위치와 고정 정보

- Worktree: `/private/tmp/standard-forward-comparison-20261024`, branch `codex/ark600-five-day-20261025`, HEAD `fe03ba54522ce8e3ba27596a3ccd8095d4c0c47c`, tree `f81a2f6cc8407881752a8794c07af3d5713343b2`.
- Evidence root: `/Users/yhlee/AD-SDIRK3-review-evidence/ark600-five-day-20261025`.
- 종료/진행: `runs/ark_h600/forecast_receipt.json`, `progress.json`, `progress_last_running_snapshot.json`, `controller_followup_findings.json`.
- Partial analyses: `analysis/86400/comparison.json`, `analysis/129600/comparison.json`, `analysis/green_partial_24_36h_review.json`.
- 6시간 후처리 복구 기록: `postprocess_correction.json`.
- `manifest.json` SHA-256: `dd6472e686a63ddff1fc101696689ef47b84246932ee44283fcbd574eafc6ae6`.
- `forecast_receipt.json` SHA-256: `42b9be36a5035bfefc091b80a2fe6dcc6083614177eac5e5fc25986d66213cde`.
- `controller_followup_findings.json` SHA-256: `1b3523a27622f103fff8b3d958b4680d7dbf8600026515df9f7bab74c850513f`.

공개 근거: [실행 기록·부분 거리·재계산 배열과 제어기 검사](evidence/ark600_five_day_interrupted/README.md). 개별 배열 파일은 최대 1.8 MB 미만으로 분리했고, 원본 FP32 NetCDF에서 FP64로 승격한 뒤 차감한 소유점 차이와 RK3 초기 대비 변화만 담는다. 내부 FP64 궤적이나 전체 상태 원배열이 아니다. `recompute_partial_metrics.py`로 14행의 RMS/최대차/편향/기준 변화/소유점 수를 재계산한다.

KG check: this worktree has neither `wiki/` nor `graphify-out/`. No wiki was initialized and no graph was rebuilt. The existing execution and review receipts remain the source evidence.

최종 갱신 시각: 2026-10-26 10:07:17 JST (UTC+09:00)
