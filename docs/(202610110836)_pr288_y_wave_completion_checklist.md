# Y/V 장시간 역문제와 미사용 예측의 완료 조건

작성 시각: 2026-10-11 08:36:37 JST

PR #287의 병합 main은 `9803c3d0cf011975c1a1d7ee462863f2aba29277`이며 producer `730c86bb5d06889746ea232efe8aa9c162ec2c4e`와 같은 tree다. 별도 worktree에서 작업하며 기존 사용자 작업트리를 변경하지 않는다.

- [x] 기존 X-only terminal 네 방향의 동일 실행파일 FD–수반 결과 판독: 경험적 기준 사례 종료. 엄밀한 전체 Eg 상한은 별도다.
- [x] 새 Y/V Linux preflight: run `38082312334`, artifact `11680668029`, SHA-256 `6adc4205730e4afde22aa786a5ff6646ca4aa0d866a02f94cfa11a0e8238f45a`. Descriptor·16스텝 replay·두 RHS 방향만 실행했다.
- [x] 최소 연구 CI: run `38082312640`, artifact `11681126390`, SHA-256 `d1f7c36bf2ee7f958d4445bccf74b5fa10173cab39c99cf4b085d15e13a8675a`. 다섯 CTest와 짧은 replay가 통과했다.
- [x] 같은 두 절대 제어계수·관측·R·960×0.3125초에서 실제 목적함수와 수반 계산. Manual run `38095566773`을 시작했다. 실제 두 갱신 후 계산 종료 기준을 충족했다.
- [x] 실제 비용 Armijo 갱신과 각 수용점의 fresh gradient 확인. 제한 횟수 종료와 계산상 수렴을 구분한다.
- [x] 실제 시작·수용 분석장의 300초 checkpoint에서 600·900초의 미사용 W와 V 포함 전체 소유 상태 비교.
- [x] 실제 실행자료의 Green/Red 독립 재계산과 최종 완료 범위 기록.

독립 선형 source truth와 105개 고정 물리 위치를 유지한다. 새 모드는 Coriolis가 없는 발산파로, 일반 회전성 흐름 또는 모든 U/V 초기조건을 복원하는 시험이 아니다. 미사용 예측은 상태만 전달하는 cold restart이며 중단 없는 warm 궤적의 비트 재현을 주장하지 않는다.

자동 CI는 기존 다섯 CTest·16스텝 replay·metadata 소비와 문법 검사만 유지한다. 장시간 역문제·미사용 예측은 수동 연구 실행이다. 배포·설치·플랫폼 시험을 추가하지 않는다.

생산 C++·Fortran 및 native 시험 CPP는 변경하지 않았다. 새 `test/em_b_wave` 실행과 동일 설정 RK3 필드·runtime 비교는 수행하지 않았으며 이전 적합 회귀 근거를 새 실행과 구분해 유지한다.

최종 근거: `(202610111001)_pr288_y_wave_inverse_forecast_closure.md`. 역문제 아홉 native 호출과 미사용 예측 네 호출, 실제 Green/Red 검산을 완료했다.
