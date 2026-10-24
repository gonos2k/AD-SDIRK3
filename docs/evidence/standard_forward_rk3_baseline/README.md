# 표준 em_b_wave RK3 기준 근거 — 부분 완료 기록

[실행 보고서](../../(202610241316)_standard_forward_rk3_baseline_and_ark_pending.md)는 공식 규모·5일·600/300/150초의 RK3 기준 실행 3개를 기록한다. 완주한 ARK 실행은 아직 없으므로 RK3–ARK 비교 완료, ARK 안정성 실패, 동일 정확도 속도 우위를 주장하지 않는다.

- [summary.json](summary.json): 실제 분석 결과와 해석 한계
- [costs.csv](costs.csv): 종료 코드·스텝 수·전체 wall time·표본 RSS
- [timestep_sensitivity.csv](timestep_sensitivity.csv): RK3 내부 간격 차이
- [evidence.zip](evidence.zip): 실행·빌드·초기장·설정 영수증, 원시 RSL/런처/종료 관찰 로그, 분석 CSV와 그림, 분석기·검사·실행기 소스, Graphify 출처 기록
- [archive_receipt.json](archive_receipt.json): ZIP의 SHA-256·크기·CRC 및 내부 해시 검증 결과

ZIP의 `MANIFEST.json`은 각 payload의 SHA-256과 크기를 기록한다. ZIP 자체의 해시는 별도 archive receipt에 둔다. `analysis_rk3/rk3_analysis.json.gz`를 풀면 원래 전체 분석 JSON이 된다. 원래 postprocess receipt는 압축 전 JSON 해시를 보존한다. 기존 영수증의 절대경로는 과거 출처이며 재작성하지 않았다.

전체 native NetCDF·실행파일·라이브러리는 다음 로컬 경로에 보존하며 ZIP에 포함하지 않았다.

```
/Users/yhlee/AD-SDIRK3-review-evidence/standard-forward-comparison-20261024/
```

원배열 재분석은 해당 디렉터리의 세 `runs/rk3_h*` 출력이 필요하다. 기존 분석기의 `--runs-json analysis_rk3/runs_plan.json`, `--reference rk3_h150`, `--reference-check rk3_h300`와 새 출력 경로를 사용한다. 다른 장비에서는 계획의 로컬 경로를 해당 원자료 위치로 바꿔야 한다. ZIP만으로 저장된 수치·해시·CSV·그림을 점검할 수 있지만, 전체 원배열 차감을 반복할 수는 없다.

단위는 U/V/W m/s, T K(건조 온위 섭동), PH m²/s², MU·총압 Pa다. T에 동일한 300 K를 더해도 차이는 같다. 총압은 P+PB이며 파동 CSV의 P는 압력 섭동의 mode-1 진단이다. h150은 정확해가 아닌 비교 대상이고, W의 세분화 차이는 단조 감소하지 않는다. 정확도 합격 문턱은 아직 정하지 않았다.

중지한 `ark_h600` 자료도 그대로 포함했다. RHS_COUNT가 매 RHS 전체 입력 변환·해시를 수행한다는 측정 문제 때문에 수동 중지한 실행으로, 원래 종료 코드 1을 보존한다. 이 실행은 성능 표본이 아니다. `forecast_manifest_lean.json`과 lean runner는 **미실행 준비 자료**이며 성공 근거가 아니다.

`verification/final_local_checks.json`은 분석기 합성 NetCDF 검사 9개와 Ruff 결과다. 이는 native ARK 5일 시험을 대신하지 않는다. Graphify는 Python 분석기의 소스 탐색용이며 Fortran·C++ 계산이나 수치 정확성의 증명이 아니다.
