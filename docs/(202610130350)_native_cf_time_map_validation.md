# PR303: native map and lower CF forwarding validation

기록 시각: 2026-10-13 03:51 JST (+0900)
최종 갱신: 2026-10-13 07:13 JST (+0900)

이 기록은 PR303 source-60 고정 궤적 진단과 원본 초기조건 240초 forecast를 CF 수정 전후로 분리한다. 실제 CF 값의 forwarding을 반영한 post-CF source-60 30초 run/map과 post-CF 240초 forecast가 완료됐다. 세부 범위는 아래에 각각 적고, source-profile 대 integrated-C++ U/V strict RHS gate의 실패와 RK3 기준 자격 미달은 그대로 유지한다.

## 확인된 하단 CF 입력 불일치

저장된 Fortran pressure-gradient 호출의 `cf1/cf2/cf3`는 `(2.0955762863, -1.7350879908, 0.6395117044)`였고, 같은 C++ 생산 경로는 기본값 `(0.5, 0, 0)`을 사용했다. 차이는 반올림 오차가 아니다. Fortran `module_initialize_ideal.F:791–807`의 실제 생성식은 `cf1=fnp(2)+cof1`, `cf2=fnm(2)-cof1-cof2`, `cf3=cof2`이며, 첫 수직 pressure face는 `dpn(1)=0.5*(cf1*p1+cf2*p2+cf3*p3)`로 만든다. NH U/V pressure-gradient term 4는 `rdnw(k)*(dpn(k+1)-dpn(k))`를 사용하므로 이 하단 보간 계수는 실제 항의 입력이다. 호출자 `module_em.F:780–837`은 같은 `cf`를 pressure-gradient 루틴에 넘긴다. 호출 ABI에 세 값이 없어서 C++ 기본값과 Fortran 실제값이 달랐고, 이는 저장된 한 설정의 P2 경계 입력 불일치로 확인됐다. 근거와 보존 캡처는 [CF 원인 감사](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/red_cf_bottom_boundary_scope_ko.md>)와 해당 감사에 적힌 Fortran PGF 캡처에 있다.

최소 수정은 Fortran 실제 계수 세 개를 기존의 checked zero-copy ABI를 따라 C++ solver의 해당 tile 계수로 전달하고 검증하는 것이다. 현재 소스 스냅샷은 이 전달 경로를 포함한다. 빌드 영수증은 24-member C++ archive를 새로 만들고 Fortran `module_implicit_sdirk3` 및 `solve_em`을 다시 컴파일·연결했으며, archive member/object 대응이 확인됐다고 기록한다. 선택된 ABI 시험은 `BASE_STATE_CHECKED_ABI: PASS`, full-tile 계약 시험은 `Full tile step contracts passed`로 끝났다. 이 증거는 [CF bridge 빌드 영수증](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_build/pr303_cf_bridge_build_receipt.json>), [ABI 시험 로그](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_build/cf_bridge_test_wrapper/run_base_state_checked_abi_final.log>) 및 [full-tile 로그](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_build/cf_bridge_test_wrapper/run_full_tile.log>)에 있다. 빌드된 새 WRF 실행 파일 SHA256은 `aecd20f900ca922775944df380ecbeb8b63655c7390e8a8a0faac9f821f95ef3`이다. 이 빌드 영수증의 `model_runs`와 `native_runs`는 모두 0이다.

컴파일이 만든 `module_implicit_sdirk3.f90` 및 `.bb` 파일은 생성 산출물이다. Fortran의 authoritative source는 `module_implicit_sdirk3.F`이며, 생성물의 존재를 별도 생산 소스 변경으로 세지 않는다.

## CF 전달 후 source-60 30초 run과 2-step map

이 short map의 input은 원래 240초 시작 파일 e71이 아니라, WRFOUT 00:01에서 만든 corrected source-60 input fc37(`fc37c8c0…d14df`)이다. 반대로 240초 forecast는 원래 시작 input e71(`e71b730a…dd2a9`)을 쓴다. 두 증거를 서로 바꾸어 사용하지 않는다.

새 실행 파일 `aecd20f9…f95ef3` 및 C++ archive `b04b827f…e077159`를 사용했다. 로그는 `native_set=1`, `phi_adv_z=1`, `v_sca_adv_order=3`과 실제 FP32 CF `(2.0955762863, -1.7350879908, 0.6395117044)`를 기록하며, native30 검증은 원래 fc37 source-60 초기 입력, `USE_INPUT_W=T`, 같은 입력의 Fortran PGF 캡처 재사용을 확인한다. 반환 코드 0, 30초 / 15초 간격 두 model step, solver statistics의 세-stage 합계 6, 실패 0이었다. WRFOUT 두 파일의 세 timestamp는 `00:00:00`, `00:00:15`, `00:00:30`이고 raw history states는 모두 유한하다. 독립 검증은 `completed_native30_with_reused_same_input_ft_pgf` 상태다. 캡처 runner의 자체 상태 문자열은 C++ route에서 생성하지 않는 선택적 Fortran observer sidecar를 요구해 failed로 남았지만, 이 파일 누락은 독립 native30 run/selector 검증의 반환 코드나 통과 기록을 바꾸지 않는다. 자세한 값은 [native30 독립 검증](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_execution/post_cf/native30/actual_native30_validation.json>)과 [run receipt](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_execution/post_cf/native30/run_receipt.json>)에 있다.

동일 source-60 상태의 2-step retained/replay VJP map은 endpoint와 VJP 재생이 정확히 일치했고, 적용 대상 stage 여섯 개가 수렴했다. `ε={0.02,0.01,0.005}`의 PH/THM central FD 상대오차는 각각 약 `1.74e-8`, `3.0e-11–4.1e-11`로 기존 `<5e-4` 기준 아래이며 모든 시험 폭은 같은-stage hard-sign branch-safe였다. PH Taylor 비율은 `3.93–4.08`, roundoff floor 위여서 `[3.5,4.5]`를 통과했다. THM Taylor 비율은 `3.43–5.45`이고 모두 roundoff floor 아래여서 결과는 계속 **미결**이다. 0-step baseline Full RHS strict gate에서는 source-profile C++ 대 WRF-integrated C++의 U/V owner만 실패했다(U 41/4096 mismatch, 최대 `5.82e-11 m/s²`; V 133/3840, 최대 `1.46e-11 m/s²`); W/PH/THM/MU owners는 비트 단위로 일치했다. 이 비교는 Fortran/C++ parity가 아니다. 이 구분과 수치는 [post-CF source60 profile map](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_execution/post_cf/source60_profile_map.json>)에 있다.

## CF 이전에 닫힌 source-60 2-step map의 제한된 결과

이전 native `phi_adv_z=1`, `v_sca_adv_order=3` source-60 진단은 보존된 동일 상태에서 두 단계의 전진, 재생, VJP를 수행했다. endpoint 및 VJP 재생은 정확히 일치했고, 여섯 적용 가능한 implicit stage가 수렴했다. 다만 엄격한 0-step Full RHS 비교는 source-profile C++와 WRF 연결 production C++ 호출 문맥 사이의 U/V owner에서 실패했다. PH/W/THM/MU는 일치했으며 U/V 차이는 작지만 비트 단위 동일성은 아니다. 이 검사는 Fortran과 C++ RHS 동등성 시험이 아니므로 이를 통과한 것으로 바꾸지 않는다.

선택한 source-60 고정 map, 혼합 PH/THM 방향, 그리고 같은 hard-sign branch 안에서의 중앙 FD는 기존 `<5e-4` VJP/FD 상대오차 기준을 PH와 THM 모두 통과했다. 시험한 `ε={0.02,0.01,0.005}`에서 PH 상대오차는 약 `1.74e-8`, THM은 `2.7e-11`부터 `3.6e-11`이었다. 동일 stage의 active `Omega` 부호 검사에는 active zero가 없었고 모든 시험 폭에서 branch-safe였다. PH Taylor remainder의 인접 폭 비율은 `3.93–4.08`로 기존 `[3.5,4.5]` 기준과 roundoff-floor 요건을 충족했다. THM Taylor remainder 비율은 약 `3.39–4.54`였지만 remainder가 roundoff floor 위에 있지 않아 **미결**이다. 따라서 THM FD 일치만으로 Taylor 기준까지 통과했다고 하지 않는다. 이 결과는 이 source-60 상태, 선택한 mixed direction/cotangent, 두 단계의 고정 discrete map에만 한정된다. 원자료는 [재처리 진단 JSON](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_execution/source60_native_profile/diagnostic_2step_reprocessed.json>)이며, source-60 입력 게이트 판정은 [Red의 범위 기록](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/red_acceptance/pr303_uv_exact_gate_decision_ko.md>)에 있다.

## CF 이전 원본-input 240초 관측

최초 240초 실행은 원래 `em_b_wave` 입력 e71(`e71b730a…dd2a9`), 16개 15초 model step, 원래 60초 history 설정으로 수행됐다. 실제 selector 로그는 `native_set=1, phi_adv_z=1, v_sca_adv_order=3`이지만, 당시 실행 파일의 하단 CF 값은 Fortran 실측값으로 전달되지 않고 C++ 기본 `(0.5,0,0)`이었다. 그러므로 이것은 현재 CF 수정의 forecast 검증이 아니라 **CF 누락 상태에서의 사전 관측**이다.

세 개 WRFOUT 파일에서 다섯 `Times` 레코드 `00:00, 00:01, 00:02, 00:03, 00:04`를 읽었다. 초기 여덟 raw state field는 h=1.875, h=3.75, h=0.9375 RK3 및 두 예전 h=15 경로와 모두 정확히 일치했다. 240초 시점의 사전 실행과 각 reference 사이 RMS 차이는 다음과 같다.

| Field | 사전 CF 실행 대 RK3 h=1.875 | 사전 CF 실행 대 예전 PR296 ARK h=15 |
|---|---:|---:|
| U | `1.35073e-2 m/s` | `6.98e-7 m/s` |
| V | `3.57192e-3 m/s` | `4.55e-7 m/s` |
| W | `3.10140e-4 m/s` | `1.60303e-5 m/s` |
| PH | `2.56629e-1 m²/s²` | `2.84313e-3 m²/s²` |
| T | `1.11423e-4 K` | `1.20064e-4 K` |
| MU | `5.04621e-1 Pa` | `4.88281e-4 Pa` |

예전 PR296 ARK 실행의 저장된 namelist에는 `PHI_ADV_Z=1`, `V_SCA_ADV_ORDER=3`이 표시되지만, 당시 source에는 native selector setter가 없고 RSL에도 `native_set=1`이 없다. 따라서 그 파일은 직접적인 과거 출력 비교자료일 뿐, active native 1/3 reference로 취급하지 않는다. V3의 h=0.9375 대 h=1.875 기준 자격은 이미 24개 field-time 항목 모두 실패했고 이 문서에서도 그대로 미자격이다. 새 native-map 결과와 비교해 h=1.875 출력까지의 T 거리는 32.43% 줄었고 W 거리는 4.21% 늘었지만, 이는 미자격 과거 궤적까지의 **거리 변화**이지 정확도 개선/저하 판정이 아니다. 원자료는 [240초 관측 JSON](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_metric/pr303_postfix_240_observation.json>) 및 [source/hash binding](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_metric/pr303_postfix_240_source_hash_binding.json>)이다.

## CF 전달 후 원본-input 240초 forecast

원본 e71 입력과 같은 namelist/hash를 사용한 새 실행은 실제 CF triplet과 native 1/3 selector를 기록했고, 반환 코드 0으로 완료됐다. 16개 nominal 15초 model step을 수행했으며 solver 통계 `Steps=48, Failed=0`은 세 implicit stage를 합산한 수이지 48개 forecast step이 아니다. 세 WRFOUT 파일에 다섯 실제 history time `00:00/01:00/02:00/03:00/04:00`이 들어 있으며, 모든 raw history state field가 유한하고 초기 여덟 raw state fields는 기존 RK3 h=3.75/1.875/0.9375 및 두 과거 h=15 run과 정확히 일치한다. 전체 run의 wall time은 `8.280 s`(1 MPI rank, 1 OMP thread)였으며 이는 기록값일 뿐 비용/속도 게이트가 아니다. 실행 provenance는 [post-CF 240 receipt](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_execution/post_cf/postfix_240_e71/run_receipt.json>)이고, output metric는 [post-CF 240 observation](</Users/yhlee/AD-SDIRK3-review-evidence/native-time-map-20261012/green_metric/pr303_postcf_240_observation.json>)에 있다.

저장된 출력 전체에서 `T+300` 최소 `302.2531 K`, `MU+MUB` 최소 `89088.633 Pa`, `diff(PH+PHB)/g` 최소 layer depth `942.6494 m`였고 모든 state fields는 유한했다. `h=1.875` RK3 reference와 240초 시점 RMS 거리는 U `1.35073e-2 m/s`, V `3.57193e-3 m/s`, W `3.10141e-4 m/s`, PH `2.56629e-1 m²/s²`, T `1.11423e-4 K`, MU `5.04621e-1 Pa`다. 기준 궤적은 기존 검사에서 미자격이므로 이 차이는 같은 입력의 **관측 비교**다. V3 table/context로 candidate pass/fail 또는 예측 정확도를 주장하지 않는다.

동일 saved initial condition, timestep 및 output times에서 CF 이전 forecast와 post-CF forecast의 물리 owner-field endpoint RMS 변화는 U `7.40e-9 m/s`, V `1.94e-7 m/s`, W `3.64e-9 m/s`, PH `1.95e-5 m²/s²`, T `0 K`, MU `0 Pa`였다. 차이는 post-CF output minus pre-CF output의 실제 history states에서 직접 계산했다. 이는 이 forecast pair의 CF 전달 영향 관측이며, reference accuracy나 일반화된 CF 영향 추정은 아니다.

## 남은 목표

다음 단일 우선 목표는 별도 RK3 reference qualification이다. 다만 여기서 다른 미해결 과학 한계까지 끝난 것은 아니다. source-profile C++ 대 integrated-C++ U/V strict bit gate는 여전히 실패했고, THM Taylor remainder는 roundoff floor 아래여서 미결이다. 이것은 Fortran/C++ 전체 RHS나 전체 WRF map의 동등성을 입증하지 않으며, 해당 source-60 입력과 선택한 map 이외의 일반 WRF 정확도도 보장하지 않는다. V3의 h=0.9375 대 h=1.875 검사는 24개 field-time 항목 모두 미통과여서 reference는 계속 미자격이다. 이번 사후 분석은 새 reference 또는 추가 refinement를 수행하지 않았고, 후속 기준선 자격 검토가 끝나기 전까지 240초 결과는 관측 차이로만 취급한다.

V3 RK3 reference는 미자격 상태로 유지한다. 이 문서는 프로젝트 전역 정확도 목표, 240초 forecast 합격, 성능/비용 우위, 전체 WRF 수반 또는 모든 입력에서의 CF 동등성을 주장하지 않는다. 회전파 결론은 이전 종료 상태 그대로다.
