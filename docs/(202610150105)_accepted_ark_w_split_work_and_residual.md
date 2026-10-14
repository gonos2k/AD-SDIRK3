# PR307 실제 수용 ARK 단계의 W 분할·일률·적분된 일·풀이잔차

작성: 2026-10-15 01:05:32 JST (+0900)

## 결과와 범위

원래 e71 초기장과 native ARK mode 3에서 15초 한 스텝의 실제 수용된 네 단계를 기록했다. 암시적 2–4단계에서 직접 측정한 같은 상태의 W Full−FI−FE는 RMS 약 10⁻²¹ m/s²이며, 단계 식 잔차와 실제 W 증가분을 별도로 확인했다. 이 스텝에서 큰 W 분할 불일치나 큰 단계 식 잔차는 관찰하지 않았다. 초기 압력·부력의 가진과 시간맵의 W 전용 이차 결함을 정량화했지만, PR306의 240초 W 시간민감도를 완전히 설명한 것은 아니다. 기존 기준해 20/24, W 네 시각 미충족과 후보 정확도 판정 보류는 유지한다.

이번 변경은 기존 기본 비활성 `WRF_SDIRK3_STAGE_OPERAND_DIAG`를 재사용한 관찰 확장과 분석기다. 새 설정 키·Fortran·공개 C ABI·물리식·초기조건·tableau·오차 예산은 변경하지 않았다. 기록은 실제 수용 단계와 macro 결과를 확인하고 projection 뒤에 저장하며 추가 RHS 호출을 만들지 않는다. 최초 명시적 단계에는 Newton 풀이와 Newton 잔차를 만들지 않는다.

## 실행과 출처

실행 당시 기준 HEAD는 `192c9149fc1b5968755654a328f350d43604687f`였다. 관찰 소스는 그 worktree의 수정 상태에서 빌드했고, 검증한 바이트가 현재 코드 commit `18abbe9a2cd4b32c68d1b3bbf0d64ea0040d48fc`와 일치한다. 실행 당시 HEAD 자체의 바이너리라고 표현하지 않는다.

- 입력 SHA-256: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`.
- private header SHA-256: `a3e0df6ffb493142e328b6312ec991bfdee854c6aef33ab45d1dc2358891884e`.
- 구현 SHA-256: `a4d65886b5281c28a1b81a3b62b74ab0a763706c311cb7887d1eecaaaf88b076`.
- 최종 CSV SHA-256: `0ed70e465081f10472376931be881d916f2ed6ac7cfa3bfa592e2f920fb4e91b`.

설정은 MPI 1 rank·OMP 1 thread, h=15초, 종료 15초, debug 2, 원래 native 수송 옵션과 하단 CF다. 최종 ON/OFF 모두 한 macro step·세 암시적 풀이·실패 0회, host 입력 1회·FP64 carry publication 1회다. 현재 grid/config kdamp는 직접 기록한 0.2이며 Newton/Krylov 설정은 1e−7, effective EWT rtol은 1e−6이다. raw `wrf_omega_ww_cp=0`과 effective WRFParity mode 1의 true를 구분한다. blend=1, current-W buoyancy=true, W damping=0, Rayleigh 적용 없음이다.

기존 C++ 24개 객체를 matching header로 재빌드했다. 최초 이름공간 오류의 실패 로그는 보존했고 수치 증거에서 제외했다. 이후 관찰 메타데이터 수정은 header를 유지하여 영향받은 구현 객체만 다시 컴파일했으며, 아카이브의 24개 구성원과 나머지 객체 바이트를 확인했다. Fortran archive `11e6b428…`·configure·`.mod`·관련 객체와 link stack은 그대로 재사용했다.

최종 새 실행파일 OFF는 보존된 A5 debug-2 OFF 결과와 여덟 필드·두 시각, 16개 배열이 비트 동일하다. 새 OFF/ON도 16개 배열이 비트 동일하며, 양쪽 RHS 카운터는 begin=3, end=65, delta=62로 같다. 새 모델 실행은 초기 관찰 버전 OFF/ON 두 번과 최종 v2 OFF/ON 두 번, 총 네 번의 15초 실행이다. 새 240초·추가 시간간격 세분화·solver tolerance 대조는 수행하지 않았다. 단일 wall time을 속도 우위의 근거로 사용하지 않는다.

## 같은 상태의 W와 실제 증가분

packed mass 가로 형상은 17×17이지만 물리 소유 영역은 16×16이다. W는 j/i=0..15, k=1..16의 4096개 점을 사용하며 고정 하단만 제외하고 동적 상단을 포함한다. 중복·padding 행과 열을 물리셀로 세지 않는다.

Fast와 Slow는 생산 알고리즘의 서로 다른 기여다. 방법이 쓰는 K와 실제 체크 호출의 FI(Y)를 분리한다. 기존 debug-2 Full 호출도 그대로 보존한다. FI/Full의 입력은 accepted Y와 정확히 일치하고, 그 보조 `U_ref_stage_`는 Bi와 일치한다. Slow/FE의 평가 입력과 보조 참조는 모두 Yi다. 이 보조 참조를 평가점 Bi라고 오해하지 않는다. 해당 WRFParity·blend·current-W 정책에서 W에 대한 참조 차이의 역할은 소스와 실제 W 분할 비교로 한정해 확인했다.

| 단계 | G_W RMS (m/s) | G_W 최대 절대값 (m/s) | Full−FI−FE W RMS (m/s²) |
|---:|---:|---:|---:|
| 1 | 해당 없음: 명시적 | 해당 없음 | 독립 Full 미측정 |
| 2 | 8.59879e−10 | 1.28126e−8 | 1.66e−21 |
| 3 | 1.96352e−11 | 2.84422e−10 | 1.61e−21 |
| 4 | 8.31395e−11 | 9.46858e−10 | 9.60e−22 |

G=Y−B−h·a_ii·FI(Y)는 속도 단위의 accepted-stage 식 잔차다. K−FI는 가속도 단위로 별도 기록한다. 이 작은 잔차는 역연산자의 증폭까지 포함한 참오차 상한이 아니다. 4단계의 Newton returned-K snapshot은 없어 반환 K와 후처리 K의 동등성은 미관측으로 남긴다. 그러나 같은 Y의 FI·Full·압력 관측과 accepted 방법 K는 확인됐으므로 그 단계의 G와 일률은 유효하다.

실제 W 증가분 RMS는 1.03022054e−4 m/s다. 암시적 가중 증분 RMS는 1.03083696e−4, 명시적 증분은 1.03857653e−5 m/s이며 두 벡터를 원소별로 조합한다. 생산 순서의 full-tableau 재생은 raw endpoint와 정확히 같고, E/I를 나누어 조합한 차이는 RMS 2.51196e−20 m/s다. raw와 projected endpoint W도 해당 소유 영역에서 같다. 단계 RMS를 단순 합산하지 않는다.

## 압력 일률과 적분된 일, 한 스텝의 W 이차 항등식

실제 FI 호출의 pressure/buoyancy 성분과 질량변환을 읽었다. 내부면의 pressure·μ-buoyancy 산식은 포착 성분과 최대 약 2.2e−19 m/s² 차이로 맞는다. 포착 성분 배열에 없는 동적 상단은 같은 호출의 p′·MU·질량계수와 소스 경계식으로 유도했으며, 직접 측정한 상단 성분이라고 부르지 않는다. 그 상단을 평가에서 제외하거나 0으로 취급하지 않는다.

일률은 W, 시간에 대해 가중 적분한 일은 J로 구분한다. Pressure+buoyancy의 단계별 영역 일률은 0, +8.20137, +6.14721, +6.12725 MW다.

물리 dry dual-cell 질량과 지도계수 면적을 사용한다. Lf(Pa)를 kg로 취급하지 않는다. 초기 W=0이면 순간 일은 0이지만 pressure+buoyancy 가속도 RMS는 약 1.09362e−5 m/s²로 비영이다. 내부 단계는 균일한 시간표본이 아니므로 단계 번호로 파동 주파수를 추정하지 않는다.

네 단계의 tableau source-work는 pressure +1407.746 MJ, μ-buoyancy −1351.313 MJ, 합 +56.4331 MJ다. 합 중 동적 상단은 +7.0190 MJ다. 두 독립 구현의 성분 일 적분은 약 3.3e−7 J 이내로 일치한다. 이는 수직 섭동압력·부력 성분의 일이며 전체 대기 에너지 예산이 아니다.

별도로 초기 질량 가중 M0를 고정한 W 전용 이차 항등식을 계산했다. endpoint W 에너지 증가는 +51.494558 MJ, tableau의 accepted K+FE 일은 +55.716542 MJ, 정의한 RK 이차 결함 ΔE−P_method는 −4.221984 MJ다. 독립 대수 항등식의 차이는 약 1.49e−8 J다. 2–4단계의 같은 Y 독립 Full과 명시적 1단계의 실제 FI+FE 조합을 사용한 일은 방법 K+FE 일과 +253.461 J 차이가 나며, Newton의 특정 반환-K 동등성을 가정하지 않는다. endpoint 질량으로 바꾼 W 에너지 변화는 −0.2471 J다.

4단계 Y4의 W 에너지 +51.830609 MJ는 최종 endpoint의 +51.494558 MJ와 다르다. ARK의 마지막 단계 상태를 최종 가중 조합과 동일시하지 않는다. 음의 이차 결함은 이 한 스텝의 고정 W 질량 노름 진단이다. 전체 에너지 소산율·정확해 오차·240초 W 실패의 지배 원인을 확정하지 않는다.

## 수정한 관찰 계약과 남은 작업

최초 관찰 분석에서 두 공백을 수정했다. packed 17×17과 physical 16×16을 혼동한 소유 영역은 명시적인 v2 owner 메타데이터와 입력 파일의 차원으로 대조한다. 또 4단계의 returned-K가 없는 상태를 관측된 K 변화처럼 취급해 FI/P/Y availability를 막던 조건을 분리했다. 직접 checked-Y 일치를 accepted-state G/일률의 근거로 쓰고, Newton 반환-K 증거의 미확인은 별도로 보존한다. v1 CSV·실행파일·소스·실패/이전 영수증은 덮어쓰지 않았다. 빌드 후 추가한 미사용 device guard 제안도 별도 보존하고 실제 검증한 소스로 복원했다.

이 한 스텝에서 확인한 큰 initial W 가진과 음의 RK W 결함은 다음 조사 방향을 시간적분의 감쇠·위상과 연결한다. 다만 긴 궤적이나 실제 고유주파수를 관찰하지 않았으므로 전체 W 시간민감도는 열린 항목이다. 추가 실행이 필요하면 이미 보존한 입력에서 한 가지 질문을 구별하는 짧은 시간순 endpoint 대조를 먼저 설계한다. tolerance·감쇠·초기조건·노름을 결과에 맞춰 바꾸지 않는다. 완료된 입력·CF·수송·회전파 검증을 재개하지 않는다.

기존 최소 C++ CI의 회귀 범위와 이번 native WRF 실행은 별도로 기록한다. 새 배포·플랫폼·CI 확대는 조건으로 추가하지 않는다. stock RK3와의 이전 field/runtime 비교는 PR305·306 보고서를 재사용하며, 서로 다른 필터 정책의 계산을 이번 정확도 기준으로 섞지 않는다. 관측창 수반·별도 시험 경로 U/V 차이·THM Taylor 미결은 기존 범위대로 남긴다.

## 근거

- [실제 v2 단계·잔차·일률 분석](evidence/pr307_accepted_ark_w/actual_accepted_stage_w_analysis_v2.json)
- [독립 압력 일률 재계산](evidence/pr307_accepted_ark_w/actual_stage_pressure_work_v2.json), [교차검증](evidence/pr307_accepted_ark_w/independent_pressure_work_crosscheck.json)
- [고정 M0 이차 항등식](evidence/pr307_accepted_ark_w/frozen_w_energy_identity_v2.json)
- [독립 Red raw/provenance 검증](evidence/pr307_accepted_ark_w/pr307_v2_independent_raw_validation.json)
- [matching C++/Fortran link 영수증](evidence/pr307_accepted_ark_w/pr307_v2_build_receipt.json), [최종 ON 실행](evidence/pr307_accepted_ark_w/on_run_receipt.json)
- [같은 실행에서 v2 metadata 의미 검토](evidence/pr307_accepted_ark_w/v2_semantic_review.json), [commit/Graphify 바인딩](evidence/pr307_accepted_ark_w/pr307_committed_source_binding_receipt.json)
- [선별 원자료 hash manifest](evidence/pr307_accepted_ark_w/analysis_manifest.json)

대형 원자료·실행파일·CPP archive·CSV와 재계산 스크립트는 E307 `/Users/yhlee/AD-SDIRK3-review-evidence/accepted-ark-w-stage-20261014`에 보존한다. 휴대용 ZIP `AD_SDIRK3_PR307_accepted_ark_w_stage_evidence.zip`은 125,351,917바이트이며 payload 171개와 manifest를 포함한다. SHA-256은 `a4057d5d99831ba5538416d737218bc262c22e843cff53bc8365fd95fc2733e1`이다. CRC·모든 payload 해시·새 경로 압축해제 후 native 배열 대조와 세 분석기의 재계산이 통과했다. [패키징 영수증](evidence/pr307_accepted_ark_w/AD_SDIRK3_PR307_accepted_ark_w_stage_package_receipt.json)의 통과는 자료 보존·재현 검사이며 기존 정확도 기준해의 20/24를 통과로 바꾸지 않는다. ZIP 안 문서는 패키징 시점 스냅샷이고 이 해시 문단과 영수증 링크는 패키징 후 추가했다.

최종 [수용 범위 Red 감사](evidence/pr307_accepted_ark_w/pr307_final_acceptance_audit_ko.md)와 [종료 범위 Red 감사](evidence/pr307_accepted_ark_w/pr307_closing_scope_ko.md)는 내용·수치·범위 검토를 통과했다. Graphify source binding과 독립 raw/압력 분석의 Green·Red 근거는 위 manifest에 연결했다.
