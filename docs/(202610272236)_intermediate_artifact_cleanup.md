# 중간 산출물 정리

공개 실행 근거: [ARK600 디스크 중단 자료](evidence/ark600_disk_stop_20261028/README.md). 아래 호스트 절대경로는 당시 로컬 원자료의 위치다.
작성 시각: 2026-10-27T22:36:15.228141+09:00 (Asia/Tokyo)

사용자 요청: “중간에 필요하지 않은데 중단산출물로 남겨둔것 정리”. 불필요한 중복 사본만 삭제했다. 과거 실패를 성공으로 바꾸거나 고유 실행 근거를 제거하지 않았다.

## 정리 결과

- 과거 solver-controlled/fine-pair 실행의 중복 비압축 단계 CSV 272개: 8,175,170,288바이트 삭제.
- RK3 PR305 증거 ZIP과 모든 파일이 일치하는 staging 압축 해제 사본 216개: 251,376,483바이트 삭제. 동일 canonical 압축 해제 사본과 ZIP은 보존.
- 총 488개, 8,426,546,771바이트(8.43GB, 약7.85GiB). 실제 디스크 여유는 삭제 직전1.96GiB에서 삭제 직후9.81GiB로 증가했다. 다른 작업의 동시 디스크 사용 때문에 관측 여유 변화량은 삭제 논리 바이트와 조금 다를 수 있다.

## 확인·보존

각 CSV는 보존 ZIP의 해당 zstd member SHA와 복원 후 크기·SHA-256을 현재 비압축 파일과 직접 대조했다(272/272). RK3 staging은 보존 ZIP과 정확한 상대경로 집합 및216개 파일 내용이 같았다. 삭제 전488개 파일에 열린 핸들이 없었다. 압축 ZIP4개 및 별도 zstd 자료는 보존했다. 삭제 후4개 ZIP의 SHA가 그대로임을 재확인했다.

채택 WRF 실행파일, 공통 입력, RK3 h600/h300/h150의 모든5일 출력, ARK 중단 실행의7개 원본 출력 등72개 파일의 삭제 전후 크기·SHA가 일치한다. RSL·실행 receipt·실패 기록·원래 사용자 저장소·현재 수정 worktree는 보존했다. 출처나 중복 여부가 불확실한 scratch와 다른 프로젝트의 폴더는 삭제하지 않았다.

fine-pair conditional32건의 추정 zstd 경로 불일치는 이번 정리 계획에 실제 경로와 SHA를 기록해 해결했다. 과거 manifest는 변경하지 않았다. 최종 검증은 추정 경로에 의존하지 않고 보존 ZIP member에서 직접 복원했다.

## 재현과 다음 상태

삭제한 loose CSV가 다시 필요하면 [삭제 파일 목록](/Users/yhlee/AD-SDIRK3-review-evidence/cleanup-202610272228/deleted_files.jsonl)의 retained_archive와 [검증 계획](/Users/yhlee/AD-SDIRK3-review-evidence/cleanup-202610272228/green_plan.json)의 zip_member를 사용해 `unzip -p ARCHIVE MEMBER | zstd -dc > ORIGINAL_PATH`로 복원할 수 있다. 같은 내용의 중복 staging도 보존 ZIP에서 복원 가능하다.

[삭제 영수증](/Users/yhlee/AD-SDIRK3-review-evidence/cleanup-202610272228/deletion_receipt.json), [272개 복원 검증](/Users/yhlee/AD-SDIRK3-review-evidence/cleanup-202610272228/fresh_zip_decompression_all272.json), [핵심 파일 보존 검사](/Users/yhlee/AD-SDIRK3-review-evidence/cleanup-202610272228/after_preservation_check.json), [ZIP 보존 검사](/Users/yhlee/AD-SDIRK3-review-evidence/cleanup-202610272228/after_archive_check.json)를 남겼다. Green의 중복 확인 및 Red의 삭제 대상 검토를 거쳤다.

이번 작업은 디스크 정리이며 모델 실행·재빌드·새 RK3 비교·시험 재실행·수치식/허용오차 변경은 수행하지 않았다. ARK6005일·ARK30036시간 및 정확도/비용 수용은 미완료다. 승인된18시간 한도와 현재 실행 manifest의20GiB 하한은 변경하지 않았고, 제안한3시간 출력·5GiB 하한은 아직 적용하지 않았다. 새 모델 실행은 시작하지 않았다.
