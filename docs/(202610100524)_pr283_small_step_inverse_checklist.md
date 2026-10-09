# PR282 후속: 작은 시간간격의 실제 역문제

Local timestamp: 2026-10-10T05:24:43.420925+09:00.
Base main: 4615c2e06551e743b27ab3ccd7925a82e6f23c30. Original dirty worktree remains untouched.

## 닫힌 항목 유지

- [x] PR282 미사용 RHS Tensor 보유 수정 및 960스텝 forward-only 관측 시간민감도 0.1σ 진단 기준 충족.
- [x] PR282 최종 HEAD 필수 CI37939869822 성공 확인. PR merge checkout 5c043033/tree5fae2b0는 HEADde86a48/tree5fae2b0와 같은 소스이며 artifact SHA8d5dabd634dceb97cfe09a2fdefce8476c44dccc7ae838d82d9c2ffc63194657 확인. 새 모델 실행은 아님.
- [x] Graphify 기존 13개 source mirror를 새 worktree와 해시 대조 후 재사용. runtime dispatch/generated scratch/context numeric 추출 공백 유지.

## 이번 실행 범위

- [ ] 같은 literal initial state와 Bcan(8480×4), 105 physical observations, σ=3e-4, 150/300초 고정.
- [ ] 작은 memory-bounded gradient 경로 선택: 기존 단계·FP64 전달·관측전치를 재사용. 독립 Green/Red 검토 전 구현하지 않음.
- [ ] 짧은 retained/replayed 전방 및 초기 수반 동등성 대조. 재계산의 입력·context·출력·경계·dt가 맞는지 확인하고 실패는 보존.
- [ ] 같은 h=0.3125, 960스텝 전방과 gradient 평가. h5 gradient를 다른 시간간격의 gradient로 재사용하지 않음.
- [ ] 최대 1–2회 실제 Armijo 수용 갱신으로 동일 목적함수 감소 확인. 작은 fixed backtracking cap; 재최적화로 새 관측을 만들지 않음.
- [ ] 계산된 gradient의 분석 변화·예측·비용 영향과 불확실성을 함께 기록. 완전한 Eg 또는 수렴 인증이 없으면 명시적으로 열린 상태 유지.
- [ ] Green/Red 최종 source/evidence 검토 및 영향 범위 검증.

960스텝 전체 retained 메모리는 측정되지 않았습니다. 기존 120스텝 2,413,544KiB에서 약18GiB로 외삽한 값은 설계 추정치이며 지원 메모리 인증이 아닙니다. 일반 checkpoint/Hessian framework, 새로운 damping/δ/허용오차 조정, 배포·광범위한 정리는 선행하지 않습니다. 실제 WRF 또는 동일 setup RK3 비교는 이번 작업에서 아직 수행하지 않았고, PR282 production source의 기존 검증으로 구분합니다.
