# strict ARK 240초 재계산 자료

고정한 두 시간간격 `0.9375/0.46875 s`, Newton 절대 기준 `1e-9`의 0–240초
연속 계산이다. **공식 결과는 22/24**이며 W의 180·240초가 미충족이다.
60·120초의 여섯 변수는 모두 충족한다. 전체 기준해 적격성과 `h=15 s` 후보의
공식 정확도 수용은 보류한다.

## 작은 자료로 판정 재계산

Python과 NumPy만 있으면 된다. 원래 NetCDF, WRF 실행파일과 큰 단계 CSV는
다음 계산에 필요하지 않다.

```sh
python3 docs/evidence/pr310_strict_reference_240s/recompute.py
```

필수 입력은 이 폴더의 `recompute.py`, `protocol_final.json`,
`published_fields_and_W.npz`, `manifest.json` 네 파일이다. 명령의 정상 종료는
재계산 성공을 뜻하며, 출력의 `official_pass_count=22`가 기준 판정이다.
24/24 통과로 해석하지 않는다.

NPZ의 `{field}_t{seconds}_{coarse,fine}`은 공개 FP32 NetCDF를 FP64로 읽어
이미 물리 소유점만 선택한 배열이다. T에는 300 K, MU에는 MUB를 **이미 더했다**.
같은 변환을 다시 적용하지 않는다. U의 X 중복면과 W/PH의 고정 하단을 제외하고,
V의 두 Y벽과 W/PH의 동적 상단은 포함한다. 소유점 수는 U/V/W/PH/T/MU 순서로
4096/4352/4096/4096/4096/256이다.

`coarse_W_internal_projected_kji`와 `fine_W_internal_projected_kji`는 각각
257/513개의 내부 FP64 W 끝값이며 단위는 m/s다. 공통 시각은 coarse의 `j`와
fine의 `2j`, 정확한 시간은 `j*15/16 s`다. 같은 파일의 고정 M0/e1은 별도
질량가중 진단용이며 공식 V3의 무가중 RMS를 대체하지 않는다.

## 확인할 파일

- `official_24row_screen.csv`: 네 평가시각의 여섯 변수 판정과 물리 단위.
- `common_time_W_257.csv`: 내부 W의 무가중 RMS 및 고정 M0/e1 진단.
- `accepted_step_solver_summary.csv`: 실제 단계의 잔차·분할·종료 기록.
  명시적 첫 단계에는 Newton G가 없으며, count=2는 초기 검사 후 한 번 보정이다.
- `numerical_summary.json`: 전체 판정과 풀이·상태 연결·양의 물리량 요약.
- `compact_derivation_receipt.json`: 원래 compact 자료의 해시와 보존·제외한 배열.
- `coarse_run_receipt.json`, `fine_run_receipt.json`: 실행파일·입력·source·출력 해시.
- `coarse_original_run_receipt.json`, `receipt_metadata_correction_audit.json`:
  실제 NetCDF 시각은 그대로이고 관리 도구의 예상 시각만 정정했음을 대조한다.
- `protocol_final.json`, `root_launch_binding.json`: 실행 전 고정한 기준과 범위.
- `stock_rk3_secondary.json`: 보존 stock RK3와의 보조 거리. 필터 정책이 달라
  정확해 비교나 속도 우위로 해석하지 않는다.

공식 판정은 `RMS(fine−coarse) <= epsilon_X/3`다. 다른 단위의 RMS를 더해
전체 오차를 만들지 않는다. 120초 평가점의 통과는 그 이전 모든 기록시각의
통과가 아니다. 내부 W의 최대 차이는 170.625초의 `4.55712645847e-6 m/s`다.

## 원자료와 범위

원래 NetCDF와 실행 로그는 외부 근거 폴더
`/Users/yhlee/AD-SDIRK3-review-evidence/strict-reference-240s-20261020`에 보존한다.
모든 원시 단계 CSV를 영구 보관하는 묶음이 아니다. 전체 W 끝값과 스칼라 요약,
원시 파일별 SHA·크기, 선택한 다섯 모델 스텝의 상세 단계 자료를 보존했다.
제외한 전체 단계 배열을 무손실 보존했다고 주장하지 않는다.

`raw_csv_retention_manifest.json`은 비선택 임시 CSV 763개(22,884,674,379바이트)를
제거하고 다섯 파일(147,043,773바이트)을 재해시해 남긴 사실을 기록한다.
`green_red_agreement.json`과 `independent_comparison_scope.json`은 두 독립 분석과
Root 직접 NetCDF 검산의 범위를 구분한다. Red의 직렬화 오류와 한 번의 오프라인
복구도 별도 영수증에 남겼다. 원래 입력·압력·감쇠·예산을 바꾸거나 추가 모델을 실행하지 않았다.
이번 자료는 해당 건조 사례의 시간민감도 검사이며 참오차 상한, 수렴 차수,
전체 관측창 수반 또는 프로젝트 전체 수용의 증거가 아니다.
