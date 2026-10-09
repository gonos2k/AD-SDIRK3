# PR281 후속 체크리스트 및 검증 결과

Local timestamp: 2026-10-09T22:50:26.130463+09:00.

생산 수정 검증 소스 D2 `d2ed2d6a3af84c76cf7912cbebd3197b90753adc`; 시험 전용 분리 수반 소스 E218 `e218c8d495cdab91914122169f2815eb99cd77ec`. 원래 dirty worktree는 수정하지 않았습니다. 이전 보고서는 당시 조사 상태이며 이 보고서가 최신 상태입니다.

- [x] 같은 literal fine 초기장·105개 물리 관측·R·150/300초를 고정하고 h=2.5,1.25,0.625,0.3125 대조.
- [x] 마지막 두 전방의 관측 RMS 차이 0.025565752σ/0.042917698σ로 사전 0.1σ 진단 기준 만족. 엄밀한 참해 오차 상한·새 시간차수·정지점 인증은 아님.
- [x] 미사용 `t_w_work_`가 보유하던 계산 그래프 격리 및 삭제. 지배방정식·경계·허용오차 변경 없음.
- [x] matching-header 재빌드·Mac 30×10 CSV byte parity. 같은 setup RSS 604,880,896→141,475,840 B. Linux 960×0.3125 forward-only RSS 161,572 KiB, 354.71초. 전체 960스텝 수반 메모리 측정은 아님.
- [x] fresh em_b_wave 동일 입력 15초/240초, 48/48단계·174변수 유한·양의 질량/층두께·세 출력 byte-identical. 보관 RK3 필드·시간 비교는 `(202610092012)_pr282_wrf_regression_fresh_archive.md`에 기록. 정확도/성능 향상 주장 없음.
- [x] D2 전체 필수 CPU CI37923001929 성공. LastTest.log 128개 + PhysicalInverse 1개, expected/actual 129개 합집합 일치: 129개 모두 새 실행, 128통과·MPS 1skip·0실패·0재사용. core ZIP SHA37d87d917e9652492abcf92d5143708514a836c89b9817d00749efd03afc3fc2. reuse receipt의 generic interpretation과 달리 eligible=false이며 실제 재사용은 없음.
- [x] 네 제어축 두 폭의 기존 FD를 재사용하고 E218 한 궤적에서 150/300초 초기 수반을 분리. 새 FD·재최적화 없음. 진단37938871531 성공, ZIP SHAe86b4eac6e723b488190d13aa0ca0aa01ebc7e93386d3fd950c9141e6bc95b5a.
- [x] 분리 수반의 합과 합산 수반: full-state 상대차 2.7850e-12, 네 제어공간 차이 2.48763e-11. 이는 현재 실행파일의 내부 정합성 검사.
- [ ] 전체 네 제어 gradient 오차 Eg 확보. 시각별 기여 상쇄 비율 5.06310e6; 재사용 FD의 폭 변화가 남고 특히 150초에서 차이가 커짐. FD는 이전 소스와의 진단이며 어느 쪽의 오류로 확정하지 않음.
- [ ] Eg를 포함한 최적화 종료 판단과 시간오차가 분리된 초기장·미사용 시각 예측 회복.

분리 제어 gradient: 150초 [-1.6584560484,0.9058188022,12.8810593432,-4.7472979947], 300초 [1.6584511047,-0.9058164910,-12.8810597607,4.7472980798]. 합산 norm 5.47389e-6는 정확도 인증이 아닙니다. 기존 FD와 각 시각의 L2 차이는 폭0.005/0.0025에서 각각 150초 9.69395e-6/2.33958e-5, 300초 1.30143e-6/5.06322e-6입니다. 시간별 비교로 불확실성의 위치는 좁혔으나 모델 응답 오차 상한은 없습니다.

Valgrind 217개 미초기화 조건 경고는 미해결이며 clean verdict로 사용하지 않습니다. Graphify의 runtime dispatch/generated scratch/context numeric extraction gaps를 유지했고 source mirror를 확인했습니다. Green/Red 독립 검토를 실시했습니다. 생산 RHS 결함을 추가로 확정하지 않았으며 unsupported EOS 수정·허용오차 완화·새 Hessian/checkpoint 체계는 도입하지 않았습니다. 병합하지 않습니다.
