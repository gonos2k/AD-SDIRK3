# PR283: 작은 시간간격의 실제 갱신 검증

Local timestamp: 2026-10-10T07:24:21.766518+09:00.
Run37992048928 producer 8aeee9e5524a2ad33d71828ef266a9bfbcceef2c; native test source8e852669862f8ece581e1fccfef546aa993fef4e10e9f0dea10deb0a83624df7; executable receipt5f232add9d964b59f1af29402a534285a5d433d73e8f8e03dc8bf25cd9988bc0. Artifact11647667397 ZIP SHAad31d82325c780230ff816d4c70340f9b3dfc09abce14f6bd8725448e3c69728, digest/CRC verified. Executable bytes are not in the artifact; its reported SHA matches the short gate receipt, not an independent binary rehash.

## 이번에 닫힌 체크리스트

- [x] 같은 literal 초기장8480성분과 Bcan(4방향), 같은105physical관측·R·150/300초, h0.3125/N14/K12/EW1 고정.
- [x] 같은 Linux 실행파일의 짧은16스텝 gate 통과 및 원래 fixture context exact match. Mac gate와 구분.
- [x] 작은 시간간격의 실제 두 gradient 계산. 매번960step exact replay,961detachedendpoints,121CarriedState snapshots,120reverse8-stepwindows; 모든block endpoint차이0 및 predictor/digest restore flags1. 전체960tape를 보관한 결과는 아님.
- [x] 전방–gradient 경로 연결: 첫 수용 후보의 checkpoint·관측예측·관측자료·보간구간·비용은 그 점에서 다시 계산한 gradient 호출과 정확히 같음.
- [x] 실제 두 Armijo 갱신 수용. 둘다alpha1, 각localdelta stepnorm0.05, 음의gᵀp 및 엄격한 실제 비용 감소.
- [x] Green/Red 독립 원자료 검산, source/profile/input/context/obs/브래킷·물리허용 검사 확인. 실제FD를 새로 계산하거나 관측을 다시 만들지 않음.
- [x] snapshot 소유권/digest contract22/22 및 fresh WRF48stage/174finite/positivefullmass-thickness/세출력byteidentity. 동일설정 보관RK3 비교는 (202610100556)_pr283_wrf_regression_fresh_archive.md.
- [ ] 최종점의 전체 네 제어 gradient 오차Eg·정지점/완전한최적화 종료 인증.
- [ ] 초기 대기상태 복원 및 동화에 쓰지 않은 이후 예측의 개선을 독립적으로 평가.

| 구간 | 실제 J | 계산 gradient norm | alpha | gᵀp |
|---|---:|---:|---:|---:|
| 시작점 |62.04886966417989|2176.1156553|1|−12.9814942583|
| 첫 수용점 |49.81793161435095|1998.7436030|1|−11.5842008825|
| 두 번째 수용점 |38.99185831181373|미평가|—|—|

독립 잔차식 비용차이는 −12.2309380498,−10.8260733025이며 기록값 차이와1e-10내에서 일치합니다. 비용감소율37.1594%; 관측W RMSE2.30618e-4→1.82816e-4m/s. 이는 같은 native 합성 목적함수 개선이며 실제 기상 초기장/예보 정확도 개선율은 아닙니다. 최종delta는 [0.00330072434,−0.000677293978,−0.02604463357,−0.09648988385]; 과거 절대truth계수와 직접 빼지 않습니다.

Gradient호출1302.917/1280.367초, peak425116/425864KiB(최대약416MiB); forward후보476.544/463.637초, peak161628/161192KiB. 네 native호출 합3523.466초(58.724분). 비교하지 않은 다른컴파일러/정확도/전체WRF 성능 향상으로 해석하지 않습니다.

## 재현 및 증거 제한

생산 방정식·물리계수·관측·허용오차를 바꾸지 않았습니다. 기존 CarriedState의 두 stage predictor 저장/복원과 제한된 test-only block replay만 추가했습니다. Last-gradient 기준을 더 조이지 않고 같은 작은시간간격의 실제 갱신을 먼저 연결했습니다. 전체Eg는 독립 상한이 없고 마지막 수용점gradient를 추가 계산하지 않았으므로 수렴 완료를 주장하지 않습니다.

기존 artifact의 source_sha256 목록은 tools/test_native_wave_refinement.py를 기록하고 실제 inverse driver 자체의 파일hash는 누락했습니다. 실제 producer git blob의 driver SHA는 65d4c77266ccc7801ea8ae14189317befb5607c78c99b213adbcdea31638d45e로 별도 복원했습니다. 이후 기록에는 Path(__file__).resolve() 한 줄을 추가해 selfhash를 포함합니다. 숫자·알고리즘을 바꾸지 않았고 완료한 artifact를 수정하거나 장시간실험을 재실행하지 않았습니다.

이 run은 수동 진단 job 성공이며 전체 required CI129개 성공을 의미하지 않습니다. 최종 PR의 필수CI는 별도로 확인합니다. Original dirty worktree untouched; no merge.
