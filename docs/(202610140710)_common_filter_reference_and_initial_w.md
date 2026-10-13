# PR306 공통 native-ARK 기준군과 초기 W 조사

작성: 2026-10-14 07:10:53 JST (+09:00)
대상 worktree: PR306 W306
기준 HEAD: `5dc69a35823a5af8f6d5e24978933a25ee2b7081` (main parent `134f02d4297b3e39f17098311d62691cabdca5bb`, `tree=bbb46283d595f9d390d8c3bc77982c1feb61843b`)

## 판정

같은 native mode-3 ARK 경로에서 `h=0.9375 s`와 `h=0.46875 s`의 240초 차이를 기존 고정 V3 예산으로 확인했다. 24개 기준 행 가운데 20개가 통과했고, W가 네 checkpoint 모두 실패했다. 최악 행은 `t=240 s`: W RMS 차이 `1.21260777e-5 m/s`, 고정 `epsilon_W/3=2.59814662e-6 m/s`, 비율 `4.6672`다. 따라서 `h=0.46875 s`도 이 named-case 경험적 기준의 qualified reference가 아니다. `h=15, 7.5, 3.75, 1.875, 0.9375 s` 후보와 `h=0.46875 s`의 차이는 모두 산출했지만, 기준이 미자격이므로 형식 판정은 보류한다.

더 큰 간격 쌍 두 개도 별도 실패로 유지한다. `h=7.5` 대 `3.75 s`와 `h=3.75` 대 `1.875 s`는 각각 20/24를 통과했고 네 W 행이 실패했다. 기준 숫자·mask·checkpoint는 세 phase에서 바꾸지 않았다. V3 예산은 root가 제안한 named-case 기준이며 프로젝트 전체에서 사용자가 채택한 목표가 아니다. 이 결과는 참오차 상계, Richardson 추정, 형식 차수 또는 stock RK3와의 적분법만의 정확도 판정이 아니다.

## 실행군, 설정, 출처

총 9개의 새 native 실행이 있다. 여섯 240초 적분은 모두 같은 `a5` executable·소스·e71 입력을 사용했고, native ARK mode 3, uninterrupted FP64 state carry를 유지하며 timestep만 바꿨다. 실행별 namelist diff도 timestep 삼중항 외 설정이 같음을 확인한다. 실행당 MPI 1 rank, OMP 1 thread였다. 출력은 60초 간격으로 다섯 정확한 `Times`를 남겼다. 여섯 run receipt에서 출력은 유한했고, `MU+MUB>0`, `PH+PHB`의 높이 방향 증가는 유지됐다. solver 실패 수는 모두 0이며 carry publication 수는 각 run의 macro-step 수와 일치한다.

| 간격 h (s) | Macro steps | Solver stages | Whole-run wall (s) | Host / internal carry |
|---:|---:|---:|---:|---:|
| 15 | 16 | 48 | 9.06 | 1 / 15 |
| 7.5 | 32 | 96 | 16.12 | 1 / 31 |
| 3.75 | 64 | 192 | 29.74 | 1 / 63 |
| 1.875 | 128 | 384 | 58.27 | 1 / 127 |
| 0.9375 | 256 | 768 | 108.67 | 1 / 255 |
| 0.46875 | 512 | 1536 | 223.24 | 1 / 511 |

Wall 값은 단일 실행의 whole-process timing일 뿐 속도·비용 게이트나 integrator-only 비교가 아니다. h=15 현재 실행은 PR304 보관 결과와 여덟 raw field × 다섯 시각, 총 40개 배열 모두 bitwise 일치했다. 이 parity gate로 예전 PR304 결과를 같은 executable/source family에서 재사용할 수 있음을 확인했다. 여섯 receipt의 출처·시각·단계·carry 수 consistency check도 통과했다. h=15 및 h=3.75의 원 unified-exec process return code는 보존되지 않았고, 이 둘은 성공 marker와 완성된 검증 출력으로 기록된다. 다른 네 적분은 return code 0이 receipt에 남아 있다.

세 추가 15초 probe는 A(debug 1, observer off), B(debug 2, observer off), C(debug 2, trace on) 조건이다. 각각 한 macro step과 세 solver stage를 수행했다. 세 조건의 0초와 15초 여덟 raw field는 모두 bitwise 같았고, C에만 diagnostic sidecar가 생성됐다. 이는 계측 on/off 영향 확인이지 추가 forecast reference가 아니다.

W306 HEAD의 tracked authoritative source 3,388개는 W305 source base와 일치한다. 실행 executable SHA-256은 `a5b231b279309c3268771bc0d2746b7716fa9daafa106bd40249e3ca10421d49`, C++ archive는 `b04b827fc194a363fbf9cf6e618f4b3d51b1dbf3dea10b33b6bb8de99e077159`, Fortran archive는 `11e6b42861a85d1ecfd4774c4272da71cb46e41a38c57c24d28b525cee09ad15`다. W306 main과 비교한 tracked source tree에는 authoritative mismatch가 없다. 실행에 연결된 선택 source SHA는 `module_small_step_em.F=037f706d…`, `solve_em.F=888b81bc…`, `wrf_sdirk3_tile_unified_impl.cpp=76a40d51…`, `wrf_sdirk3_interface.h=6121e11e…`이며 전체 바인딩은 source-tree audit와 Graphify receipt에 있다. 실행 소스는 W305 build receipt에 연결돼 있다. PR306에서 production source 또는 ABI를 바꾸거나 새로 빌드하지 않았다. Graphify receipt는 호출 관계 탐색과 source hash binding 기록이며 수치 증명의 대체물이 아니다.

## 실제 mode-3 경로

유효 설정은 `TIME_INTEGRATION_SCHEME=6`, IMEX mode 3, native `ARK324L2SA`, `split_explicit=F`, `SDIRK3_INTERNAL_FP64=T`, `SDIRK3_INTERNAL_FP64_STATE_CARRY=T`다. `Full` RHS는 실제 `Fast+Slow` stage 경로로 구성된다. `SMDIV=0.1`, `EMDIV=0.01`, `EPSSM=0.1`은 namelist/RSL에 보이는 값이지만 split-explicit acoustic substep 자식이 비활성이어서 이번 ARK map에 적용되는 stock WRF acoustic filter가 아니다. 따라서 stock split-explicit RK3와 같은 EMDIV 필터를 가진 통합법만의 비교라고 부르지 않는다.

`SDIRK3_IMPLICIT_DIVERGENCE=T`, `SDIRK3_KDAMP=0.2`, `DIFF_OPT=2`, `KHDIF=1000`, `KVDIF=10`은 이 native ARK family의 공통 stage 설정이다. `DAMP_OPT=0`, `W_DAMPING=0`이다. bridge가 처음 설정한 grid `kdamp`는 `0.01`이고, `unifiedStep`는 stage RHS 평가 전에 config의 `kdamp=0.2`를 `grid_info`로 동기화한다. 초기화 로그의 `0.01`은 이 stage 동기화 전 값이다. 직접 stage-time coefficient readback은 남아 있지 않아 `0.2`는 실행 소스 경로로 확인한 값으로 구분한다. C++ native transport selector는 여섯 run 모두 `native_set=1, phi_adv_z=1, v_sca_adv_order=3`이다.

## 고정 기준과 세 reference phase

V3 budget은 stock RK3 응답에 기반해 고정된 root-proposed named-case 수치다. 네 checkpoint `60/120/180/240 s` 각각에서 여섯 물리 field의 RMS를 비교한다. 같은 숫자를 각 checkpoint에 적용하고, reference eligibility는 24개 모든 행에서 `RMS <= epsilon/3`이어야 한다. PH label의 기존 metadata erratum을 적용해 계산은 높이 대신 geopotential `PH`(`m²/s²`)로 수행한다.

| Field 및 owner mask | Count | 단위 | 고정 epsilon (240초) | Reference limit epsilon/3 |
|---|---:|---|---:|---:|
| U, 주기 X 중복면 제외 | 4096 | m/s | 7.744417040064484e-4 | 2.5814723466881614e-4 |
| V, 양쪽 물리 Y 벽 포함 | 4352 | m/s | 1.0122007244795247e-4 | 3.374002414931749e-5 |
| W, 하단 고정면 제외·상단 포함 | 4096 | m/s | 7.794439856797330e-6 | 2.598146618932444e-6 |
| PH, 하단 고정면 제외·상단 포함 | 4096 | m²/s² | 1.0249750539744869e-2 | 3.416583513248290e-3 |
| T 기준값 `T+300` | 4096 | K | 3.619571391542998e-5 | 1.2065237971809995e-5 |
| MU 기준값 `MU+MUB` | 256 | Pa | 1.459090045756119e-2 | 4.863633485853729e-3 |

| Fine / coarse sensitivity 쌍 | 통과 행 | 실패 행 | 최악 W 행 |
|---|---:|---|---|
| 3.75 / 7.5 s | 20/24 | W 네 시각 | 240 s: RMS 2.92282e-5, 11.25× epsilon/3 |
| 1.875 / 3.75 s | 20/24 | W 네 시각 | 60 s: RMS 1.09690e-5, 4.22× epsilon/3 |
| 0.46875 / 0.9375 s | 20/24 | W 네 시각 | 240 s: RMS 1.21261e-5, 4.67× epsilon/3 |

최종 pair의 candidate arithmetic은 h=15/7.5/3.75/1.875초에서 각각 20/24 행 통과였고 h=0.9375초에서 21/24였다. 각 간격의 W 실패 행을 제외한 다른 다섯 field는 통과했다. 이는 reference가 통과했을 때 적용할 epsilon threshold와의 산술 비교일 뿐이다. reference가 계속 미자격이므로 어떤 candidate도 합격 또는 불합격으로 adjudicate하지 않는다. 과거 stock Fortran RK3의 V3 qualification 실패 역시 별도 기록이며 native ARK pair와 섞지 않는다. ARK에는 stock RK3의 acoustic filter 적용 경로가 없기 때문이다.

## W 차이와 초기 압력 진단

입력 W는 전 영역 정확히 0이다. 같은 owned W 격자에서 차분 방향을 `D2=W(3.75)-W(7.5)`, `D3=W(1.875)-W(3.75)`로 두면 D3/D2 RMS 비율은 `0.962, 0.444, 0.446, 0.313`(60–240초), cosine은 `0.326, 0.559, 0.535, 0.822`다. 더 작은 h에서 W 차이 크기는 후반에 감소하지만 네 시각 내내 reference limit보다 크고, 방향도 일정한 scalar ratio가 아니다. 수직 DCT index와 top/internal slice는 이 자료에서 공간 모양만 묘사한다. 이들은 acoustic 고유주파수나 시간 주파수의 측정이 아니므로, 빠른 신호가 분해됐다고 결론내리지 않는다.

초기 `t=0` same-call Full W acceleration 진단에서는 C++가 실제로 재구성한 pressure를 쓴 pressure-plus-buoyancy 예측과 Full W tendency의 cosine이 `0.9980567`이었다. Full RHS RMS는 `1.09772e-5 m/s²`, pressure-plus-buoyancy 예측은 `1.09362e-5 m/s²`, 나머지 `Full−pressure/buoyancy` RMS는 `6.84301e-7 m/s²`(Full RMS의 약 6.23%)였다. dynamic top 잔차는 약 `2.65e-13 m/s²`; interior remainder는 남겨 두었으며 W transport/cross-stress를 0으로 놓지 않았다. Raw input NetCDF P를 그대로 대입한 예측 RMS는 `1.18285e-10 m/s²`로 훨씬 작았다. 이 차이는 startup에서 `calc_p_rho_wrf`가 pressure를 재진단하는 경로를 보여 준다. 이는 한 초기 Full RHS를 분해한 결과지 240초 W 차이의 원인은 아니다.

초기 수직 간격과 음속으로부터 얻은 보수적인 acoustic 외피는 `omega_bound≈0.734 rad/s`다. 그러면 마지막 pair의 `h*omega_bound`는 약 `0.688`과 `0.344`다. ARK324 fast-only 선형 증폭 분석은 h가 클수록 고주파 acoustic 모드를 더 감쇠하고 위상을 늦출 수 있음을 보인다. 이는 W timestep 민감도와 양립하는 가설이지 실제 비선형 W 스펙트럼·인과관계 증거가 아니다. 최초 accepted stage에서 같은 state의 W Fast/Slow RHS 및 pressure work를 비교하는 검사가 다음 표적이다. input pressure를 강제하거나 초기조건을 바꾸지 않으며, 추가 blind timestep ladder도 하지 않는다.

## 검증 범위와 다음 작업

여섯 실행은 새 V2 receipt, 정확한 input/NML/executable/source/archive hashes, 출력 hashes, carry count와 admissibility로 묶었다. 세 15초 debug/observer probe는 수치 출력 parity 확인 용도다. 기존 최소 C++ core CI 결과는 별도의 C++ 회귀 근거이며 이번 WRF Fortran 통합을 검증하지 않는다. 이번 보고서의 native WRF 근거는 아홉 개 로컬 실행이다; 새로운 CI 범위나 확장 요구를 뜻하지 않는다. 이번 결과로 WRF 전체 함수의 진실값, 모든 입력에서의 정확도, 안정성 전역 보장, 예측력 또는 성능 우위를 주장하지 않는다.

## 근거 파일

아래 JSON은 raw executable과 대형 WRFOUT/RSL binary를 포함하지 않는 committed audit set이다. 실행별 raw files/sidecars는 별도 evidence archive와 그 manifest/hash로 확인한다.

- [최종 h=0.46875/0.9375 24행 게이트 및 후보 arithmetic](evidence/pr306_common_filter_reference/native_ark_final_reference_pair_analysis.json)
- [기존 h=1.875/3.75 phase](evidence/pr306_common_filter_reference/native_ark_h1875_reference_analysis.json)
- [기존 h=3.75/7.5 phase](evidence/pr306_common_filter_reference/native_ark_v3_fixed_budget_analysis.json)
- [여섯 run summary 및 h=15 PR304 40/40 parity](evidence/pr306_common_filter_reference/ark3_native_runs_and_h15_parity.json)
- [per-run V2 receipt index](evidence/pr306_common_filter_reference/ark3_native_run_receipts_index.json) 및 [6-run receipt consistency check](evidence/pr306_common_filter_reference/ark3_native_run_receipt_consistency_v2.json)
- [세 초기 RHS debug/observer probe 분석](evidence/pr306_common_filter_reference/initial_rhs_probe_three_run_analysis.json)
- [초기 C++ pressure-W force 분석](evidence/pr306_common_filter_reference/initial_cpp_w_pressure_force_analysis.json)
- [효과 설정 감사](evidence/pr306_common_filter_reference/ark3_mode3_effective_context_audit.json), [source-tree hash binding](evidence/pr306_common_filter_reference/source_tree_match_audit.json), [Graphify pre-edit navigation/source binding](evidence/pr306_common_filter_reference/pr306_preedit_graph_binding_receipt.json) 및 [최종 source graph binding](evidence/pr306_common_filter_reference/pr306_final_source_graph_binding.json)
- [Red의 최종 reference gate 재계산](evidence/pr306_common_filter_reference/native_ark_h09375_h046875_gate_red.json) 및 [committed-analysis hash manifest](evidence/pr306_common_filter_reference/committed_analysis_manifest.json)
- [최종 Graphify 소스 바인딩](evidence/pr306_common_filter_reference/pr306_final_source_graph_binding.json), [초기 압력 독립 Red 검산](evidence/pr306_common_filter_reference/red_precision_initial_pressure_audit_ko.md), [최종 수용 문서 감사](evidence/pr306_common_filter_reference/pr306_final_acceptance_audit_ko.md), [완료 범위 감사](evidence/pr306_common_filter_reference/red_hook_pr306_closure_ko.md)
- 대용량 raw WRFOUT/RSL/probe bundle은 별도 E306 로컬 evidence archive `/Users/yhlee/AD-SDIRK3-review-evidence/common-filter-reference-20261014/AD-SDIRK3_PR306_common_filter_reference_evidence.zip`에 둔다. 이 ZIP은 GitHub의 committed JSON bundle과 별개다. 59,698,325 bytes·265개 ZIP 항목이며 SHA-256은 `a8af3926c0d34e1c20c01bd9004e784a675d763f9affdf57a43917ecfa78a47e`다. CRC·모든 payload 해시·새 경로 압축해제 후 배열 대조와 portable 압력 재계산은 통과했다. [패키징 검증 영수증](evidence/pr306_common_filter_reference/AD_SDIRK3_PR306_common_filter_reference_evidence_bundle_receipt.json)의 `all_gates_pass`는 자료 보존·재현 검사이며, 실패한 20/24 정확도 기준을 통과로 바꾸지 않는다. 압축 안 문서는 패키징 시점 스냅샷이고 이 해시·영수증 문단은 패키징 후 추가했다.

기준 데이터의 stock RK3 비교 기록은 [(202610132029)_rk3_emdiv_cause_and_acceptance.md](./(202610132029)_rk3_emdiv_cause_and_acceptance.md)에 남아 있다. 이번 native-ARK reference screen과는 별도다.
