# PR308: 미세 시간간격 쌍의 W 궤적과 풀이 허용오차 대조

기록 시각: 2026-10-15 10:03:24 JST (+0900)
작업트리: `/private/tmp/pr308-fine-pair-w-trajectory-20261015`
증거 디렉터리: `/Users/yhlee/AD-SDIRK3-review-evidence/fine-pair-w-trajectory-20261015`

## 판정 범위

같은 입력과 실행 파일로 수행한 두 native ARK mode-3 계산의 첫 15초를 비교했다. `h=0.9375 s`는 16개, `h=0.46875 s`는 32개 매크로 단계를 완료했고 두 실행 모두 반환 코드 0, 실패 0, 연속한 accepted-step CSV를 기록했다. 두 실행의 초기 여섯 상태가 정확히 같고, 17개 공통 시각을 비교했다. 물리 W owner 4,096개 중 `t=0` W는 두 경로 모두 정확히 0이다.

이 자료는 관측된 두 trajectory의 차이를 설명한다. 더 작은 간격이 참해에 가깝다는 뜻, 시간 차수·Richardson 수렴, 프로젝트 정확도 합격, 고유 주파수·위상, 또는 W 차이의 인과 원인을 입증하지 않는다. 미리 고정한 17시각 관찰 기준는 한 번도 넘지 않았다. 이 관찰 기준은 정확도 수용 검사가 아니다.

## 실행과 공통 초기 상태

두 run은 같은 executable SHA `5bf90cd95e6d5ac4a5a76f73ec407d9ee6e91c1b6919488df20c9df09a2a7769`, C++ archive SHA `dd87b49aa448a88e13ba2047fd75b433d785f112ab485483ae439ba5035f32c5`, Fortran archive SHA `11e6b42861a85d1ecfd4774c4272da71cb46e41a38c57c24d28b525cee09ad15`, implementation source SHA `a4d65886b5281c28a1b81a3b62b74ab0a763706c311cb7887d1eecaaaf88b076`, 그리고 `wrfinput_d01` SHA `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`를 공유했다. 두 namelist는 timestep 분수만 다르고 나머지 고정 설정이 같으며, 각 run은 MPI 1 rank, OpenMP 1 thread다. 실제 runtime readback은 native mode 3, `diffusion_option=2`, `kdamp=0.2`, `khdif=1000`, `kvdif=10`, Newton/Krylov tolerance `1e-7`, effective EWT `1e-6`, mass-coordinate mode 1과 현재-W buoyancy를 기록한다.

공통 초기상태 gate는 packed `(U,V,W,PH,T,MU)` 24,531개 값의 FP64 표현까지 bitwise 일치함을 확인했다. 두 `t=0` NetCDF의 U/V/W/PH/T/THM/MU/MUB 여덟 필드도 각각 정확히 같다. 초기 물리 W 배열은 4,096 owner에서 정확히 0이다. `t=15 s` NetCDF는 각 경로의 projected FP64 endpoint를 FP32로 내린 값과 정확히 일치한다. CSV manifest는 coarse 16개와 fine 32개 파일의 SHA를 모두 확인했다. Run index와 공통 초기 gate는 `green_execution/pr308_fine_pair_run_index.json` (SHA `9810724d76aa7bacdb630def9323c1b981966814b6207f34f992de9505b5b539`) 및 `green_execution/common_initial_state_gate.json` (SHA `e29ab2047e94ca6ebf994bb480b28020a63676a475d1656171da195edc6bfbb4`)에 있다.

## W trajectory

분석은 하단 고정 face와 수평 alias를 제외하고 dynamic top을 포함한 동일 4,096개 physical W owner를 썼다. M0 dry-dual-mass metric과 `e1` 방향은 fine-pair 결과를 보기 전에 PR307 첫 단계의 실제 Full W RHS로 고정했다. Basis manifest SHA는 `afc377620a427102be81d8a460d79ae7d72a32dc5503a80d14f7bf0e7aa8debe`이며, `e1`의 M0 가중 RMS는 1이다.

각 fine endpoint는 coarse endpoint와 같은 시간을 갖는다. 시각은 CSV `step_seq`와 기록된 실제 dt의 exact rational 값으로 만들었고 WRFOUT의 두 시각만으로 중간 궤적을 보간하지 않았다. 공통 17개 시각은 0에서 15초까지 0.9375초 간격이다. 전체 owner W 배열의 fine-minus-coarse RMS 차이 중 최댓값은 `2.70515469e-7 m/s` (`t=15 s`); 이 시각의 최대 절댓값 차이는 `1.36256e-6 m/s`, M0 가중 RMS 차이는 `2.23218e-7 m/s`, 고정 `e1` projection 차이는 `−5.16449e-8 m/s`다. 고정 descriptive trigger `epsilon_W/3=2.5981466e-6 m/s`를 넘은 공통 시각은 없다. 이 수치는 두 계산의 관측 차이만 나타내며 threshold 아래라는 이유로 어느 계산도 qualified reference가 되지 않는다.

`green_metric/fine_pair_w_trajectory_analysis.json` (SHA `4fc214cdc9f1ee920e359943b1d36e561812e27f6f88587afd2fb80447a04569`)와 Red의 원 CSV 독립 검산 `red_provenance/pr308_independent_csv_primary_trajectory.json` (SHA `27adc990652ac64ce69e8691de60dd643cc7231e8bd312b1909b1574bd50da00`)는 exact-time mapping, accepted CSV provenance, fixed projection 및 t=0/t=15 publication을 독립적으로 묶는다.

## Solver residual accounting과 제한된 대조

Fine 경로 첫 macrostep (`t=0.46875 s`)의 stage 3/4 scaled Newton residual은 각각 `7.7124156e-8`과 `4.0531170e-8`; 둘 다 runtime Newton tolerance `1e-7` 이하다. 두 단계는 최초 잔차 검사(iteration 0)에서 보정 없이 종료됐다. 기록의 `newton_iterations=1`은 이 최초 검사를 센 값이며, Newton 보정을 한 번 수행했다는 뜻이 아니다. Krylov iteration은 0이다. Stage 4 returned-K equality는 관측되지 않았다. 이 누락은 별도로 표시하며 same-accepted-Y `FI`와 `G=Y−B−h*a_ii*FI(Y)` 측정 자체를 무효화하지 않는다.

Red가 각 fine macrostep에서 계산한 직접 W increment residual 합계는 최대 한 단계 `4.3723212e-7 m/s`, 32단계 local contributions의 합 `4.3723283e-7 m/s`다. 공식은 각 단계의 `dt * sum_{implicit stages 2..4} |b_i| * RMS_W(K_i−FI_i(Y_i))`다. 이 값은 측정된 stage residual이 같은 매크로 increment에 직접 기여할 수 있는 크기를 accounting한 값이다. 이후 비선형 stage map이 과거 차이를 어떻게 증폭·감쇠하는지 계산하지 않으므로 최종 시간오차의 전파 상한으로 부르지 않는다.

### Newton 허용오차 한 가지를 바꾼 대조 결과

위 첫 미세 스텝의 잔차를 근거로, 별도 사전등록 후 동일한 `h=0.46875 s`, 입력·실행파일·0–15초 구간에서 `sdirk3_newton_tol`만 `1e-7`에서 `1e-9`로 바꾼 **단 한 번의 대조를 완료**했다. Krylov `1e-7`, 상대 Newton 기준 0, 유효 EWT `1e-6`과 나머지 설정은 유지됐다. 32개 스텝·96개 암시적 단계가 실패 없이 끝났고 fallback·retry·soft-continue 카운터는 모두 0이었다. 입력 또는 생산 기본값을 변경한 실험이 아니다.

첫 미세 스텝의 3·4단계는 각각 Newton 2회·Krylov 13회로 진행됐고, W의 `G` RMS는 `5.0120e-15`, `4.9181e-15 m/s`로 줄었다. 32스텝의 직접 잔차 기여 합은 `4.3723283e-7`에서 `7.3088617e-13 m/s`로 줄었다. 이 합의 전파오차 상한 해석은 여전히 금지한다.

| 공통 시각 | 기본 fine−coarse W RMS | 엄격한 Newton fine−coarse W RMS | 변화 |
|---:|---:|---:|---:|
| 0.9375초 | `2.4887185e-7 m/s` | `3.2138305e-8 m/s` | 87.09% 감소 |
| 15초 | `2.7051547e-7 m/s` | `4.6656761e-7 m/s` | 72.47% 증가 |

허용오차 변경이 만든 차이와 원래 fine−coarse 차이의 M0 cosine은 0.9375초에서 `−0.996375`, 15초에서 `+0.345660`이다. 초기 차이를 거의 반대로 되돌렸지만, 15초의 차이를 없애지는 않았다. 따라서 이 짧은 구간에서도 기본 fine−coarse 차이를 순수한 시간적분 오차로 취급하거나, 초기 풀이잔차 하나로 전 구간을 설명할 수 없다. 엄격한 계산이 더 정확한 참해라고 단정하지 않으며, 생산 tolerance 변경이나 240초 재실행도 수행하지 않았다.

별도 대조의 원자료와 통계는 `green_execution/h0p46875_0_15s_newton_tol1e-9/run_receipt.json`, `red_precision_newton1e9_control_analysis.json`에 보존한다. 기준 두 실행의 protocol과 결과는 이 대조로 덮어쓰지 않는다.

## 압력·CF 연속성 및 RK3 기준해 제한

같은 accepted-Y pressure/buoyancy work timeline은 별도 진단으로 보존했다. 15초에서 tableau-weighted cumulative net pressure-plus-μ-buoyancy work는 coarse `6.66332144e7 J`, fine `6.64692671e7 J`다. 이는 physical-work source contribution이며 닫힌 전체 대기 에너지 수지나 인과 설명이 아니다. m/s의 고정 W projection과는 차원도 다르다. 세부 시계열은 `green_pressure/fine_pair_pressure_work_time_series.json` (SHA `e661808c11f5b590f3ee0d57c317dfb8430ba0c2c78ba0c401f36a2bf286ba3c`)에 있다.

이 보고서는 이미 확인된 same-state pressure 생성/항 분해와 input CF1/2/3 native forwarding 결론을 다시 열지 않는다. 관련 자료는 [PR298 pressure-gradient 감사](<(202610121034)_pr298_u_pressure_gradient_and_plan_contract.md>)와 [CF 전달 및 시간맵 검증](<(202610130350)_native_cf_time_map_validation.md>)에 있다. 이번 run은 post-CF executable을 사용했고, pressure work와 W trajectory는 별도 수량으로 함께 기록했을 뿐 pressure가 두 trajectory 차이의 원인이라고 주장하지 않는다.

기존 same-setup RK3 비교는 [PR306 common-filter reference 기록](<(202610140710)_common_filter_reference_and_initial_w.md>) 및 [PR305 RK3 EMDIV/수용 경계](<(202610132029)_rk3_emdiv_cause_and_acceptance.md>)에 보존돼 있다. PR306의 `h=0.46875` reference는 네 W checkpoint 중 네 개가 실패해 24행 qualification reference가 아니다. 따라서 이번 fine-pair 차이는 RK3를 truth로 둔 오차가 아니며, 두 ARK 간격의 pairwise sensitivity로만 취급한다. PR305의 stock RK3 filter 기여 분석도 그 기록된 RK3 조건에 한정된다. 이 보고서는 그 결과를 ARK에 이식하지 않는다.

## 실행·로그 기록에서 구분한 사항

첫 coarse 실행은 native 반환 코드 0이었지만, 최초 harness가 파일명을 문자열 순서(`1,10,…,2`)로 검사해 실패로 표시했다. 숫자 단계 순서로 재검사해 고쳤고, 최초 실패 receipt를 보존했다. 이를 이유로 모델을 다시 실행하지 않았다.

Newton 대조의 일부 solver-entry 로그는 `dt=0.5`, `newton_tol=0.0`을 출력했다. 소스의 스트림 정밀도 유지에 따른 표시였으며, 96개 실제 수렴 판정 헤더의 `atol/effective_tol=1e-9`와 32개 CSV의 직접 설정 metadata로 실제 설정을 확인했다. 로그 숫자 표시를 수치 설정 변화로 해석하지 않았고 생산 로그 코드도 바꾸지 않았다. 근거는 `green_rhs/newton_tol_entry_format_audit.json`이다.

## 재현 근거와 상태

고정 분석 protocol SHA `3620ba8d9846b97da435aac635fc4f846ad5342c0cbdc2b37842fb5a317845d9`, frozen basis manifest, run index, 2개 per-run receipt, coarse/fine CSV manifest, Green trajectory, Green pressure-work 및 Red raw CSV 분석이 모두 위 E308 디렉터리에 보존돼 있다. 초기상태 gate SHA `e29ab2047e94ca6ebf994bb480b28020a63676a475d1656171da195edc6bfbb4`. 실행 간 입력·실행 파일·소스와 carry count가 receipts에 고정돼 있다.

생산 Fortran/C++·ABI·tableau·물리 계수는 변경하지 않았고 재빌드도 하지 않았다. 새 도구 `tools/analyze_fine_pair_w_trajectory.py`는 기존 accepted-step 분석기를 재사용하며 실제 48개 CSV를 분석했다. 17개 공통 시각과 48개 직접 잔차 지표는 별도 Red 구현과 대조했다. Python 구문 검사와 Graphify의 해당 파일·의존 소스 해시 확인도 완료했다.

PR306의 20/24, W 네 시각 미충족과 15초 후보의 정확도 수용 보류는 그대로다. 이번에는 새 240초 적분, split-explicit RK3 재실행 또는 실행시간 우위 비교를 하지 않았다. 기준 문제·예산·초기조건·소유 영역을 바꾸지 않았다. 후속 검증은 초기 풀이 정확도와 이후 시간해상도 영향을 분리해야 하며, 이 세 짧은 실행만으로 60–240초 실패의 원인을 확정하지 않는다.

최종 수치 갱신: 2026-10-15T10:10:41+09:00

핵심 배열과 NumPy 재계산 절차는 [저장소의 작은 검산 자료](evidence/pr308_fine_pair_w/README.md)에 함께 보관했다. 대형 외부 ZIP 없이도 W 차이의 주요 수치를 확인할 수 있다.
