# ARK600 첫 6시간 — 실제 실행과 원배열 근거

[보고서](../../(202610241427)_standard_ark_six_hour_screen.md)는 공식 규모의 같은 초기장에서 ARK를 600초 간격으로 6시간 적분한 결과다. 36스텝·108개 암시적 단계가 수렴했고 종료 코드는 0이다. 5일 적분·정확도 예산 합격·동일 정확도 속도 우위를 주장하지 않는다.

- [summary.json](summary.json): 네 경로의 6시간 장 비교와 한계
- [field_metrics.csv](field_metrics.csv): 물리 소유 영역의 RMS·최대차·평균차
- [costs.csv](costs.csv): 같은 6시간 RSL main-step 구간의 비용
- [solver_telemetry.json](solver_telemetry.json): Newton 검사·보정·AD/FD JVP 집계
- [W 예측장 그림](W_six_hour_comparison.png): 같은 색 범위의 두 예측장, 별도 색 범위의 차이장과 연직 RMS
- [evidence.zip](evidence.zip), [archive_receipt.json](archive_receipt.json): 원배열·로그·설정·분석기·해시와 CRC 검증

ZIP에는 **실제 native NetCDF 8개**(ARK600와 RK3 600/300/150초의 시작·6시간), 공통 wrfinput, 네 비교 경로의 namelist, ARK 원시 로그·종료 관찰·실행 영수증, RK3 6시간 타이밍 발췌와 원본 출처, 분석·실행기 소스와 Graphify 바인딩을 포함한다. namelist 파일은 공식 원본과 ARK 6시간본의 원실행 디렉터리 사본까지 총 6개다. 동일 배열이어도 파일 메타데이터가 다를 수 있어 t=0 파일을 모두 보존했다. 전체 5일 출력과 실행파일·라이브러리는 포함하지 않는다. 실행파일 SHA는 원래 실행·빌드 영수증에서 확인할 수 있다.

`MANIFEST.json`의 SHA-256·크기와 ZIP CRC를 확인한 뒤 별도 디렉터리에 해제한다. Python에 NumPy·netCDF4가 있는 환경에서 아래 명령은 **모델 실행 없이** 포함된 배열로 정량 비교를 재계산한다.

```sh
python3 tools/benchmark_wrf_accuracy_cost.py \
  --runs-json analysis/portable_data_only_plan.json \
  --reference rk3_h600 --reference-check rk3_h300 --out replay.json
```

이 data-only 계획은 원래 상대경로 계획에서 실행파일의 필수 해시 검사를 제거해 선언 메타데이터로 옮겼다. 실행파일이 없는 묶음으로 기존 배열을 재분석하기 위한 것이며, 실제 모델 실행 검증을 완화한 것이 아니다. 원래 strict 계획과 실행파일 해시는 그대로 보존했다. `portable_data_only_plan_derivation.json`에 정확한 변경을 기록했다. 동봉 데이터로 재계산해 비교·시간민감도·파동·수지 등 9개 섹션이 원래 분석과 정확히 같음을 확인했으며, `verification/offline_reanalysis_receipt.json`에 남겼다. 원본 strict 계획을 그대로 실행하려면 기록된 실행파일도 별도로 필요하다.

핵심 ARK600−RK3 600 표만 직접 차감하려면 다음을 사용한다.

```sh
python3 analysis/recompute_endpoint.py \
  data/runs/ark_h600/wrfout_d01_0001-01-01_06:00:00 \
  data/runs/rk3_h600/wrfout_d01_0001-01-01_06:00:00
```

Matplotlib도 설치돼 있으면 같은 두 경로를 `analysis/plot_W_six_hour.py`에 넘겨 W 그림을 재생성할 수 있다. 위상 정렬·진폭 보정 없이 최대차가 있는 native k=48 단면과 전체 소유 연직 RMS를 그린다.

U 중복 주기면과 W·PH 고정 하단을 제외하고 V 물리 벽과 W·PH 동적 상단은 포함한다. 단위는 U/V/W m/s, T K(온위 섭동), PH m²/s², MU·P+PB Pa다. 파동 표의 P는 압력 섭동이며 총압과 구분한다. RSL 시간은 적분 로그 구간이고, ARK 전체 wall time 2,318.04초와 동일한 측정량은 아니다. 이번 약 485배 비용비에는 같은 6시간의 RSL 시간끼리만 사용했다.

분석 원본 CSV 바이트와 해시는 ZIP에 그대로 유지한다. 저장소에서 바로 읽는 CSV 사본만 줄바꿈을 LF로 통일했다. 전체 원자료·실행파일·외부 캐시는 `/Users/yhlee/AD-SDIRK3-review-evidence/standard-forward-comparison-20261024/`에 보존한다. 새 RK3 실행이나 MPI 재현 시험은 수행하지 않았다.
