# 비영 Coriolis 한 모드의 연구 범위 확장

작성 시각: 2026-10-11 11:05:25 JST

PR #288은 main `67ec1ed394a3ae3379c4b30df933c93a9cd19e6c`에 병합됐고 HEAD `9126d906f5f6644763b8b397a63845818de3d34d`와 같은 tree다. 기존 X-only 및 무회전 Y/V 기준은 경험적으로 종료 상태를 유지한다. 별도 worktree의 `codex/pr289-coriolis-wave`에서 다음 물리 자유도 하나만 추가한다.

상수 f=1e-4/s, e=0인 traditional f-plane을 목표로 한다. 건조·평탄·단위 지도계수·고정 계수·X 주기·Y 대칭벽·단일 CPU tile과 105개 물리 W 관측, 동일 R 및 150·300초 관측시각을 유지한다. 습윤·지형·MPI·새 관측망·일반 초기장 제어를 함께 추가하지 않는다.

- [x] Canonical native Coriolis helper와 authoritative Fortran 호출·변환·경계 소스 검토.
- [x] 기존 41성분 cos/sin 부분공간이 f≠0에서 닫히지 않음을 확인.
- [x] 편집 전 Graphify 42개 source binding 검증. Header-only·Fortran USE/BIND(C) 추출 공백은 실제 소스로 확인했다.
- [x] 균일 배경에서 회전 연산자의 가중 전치 관계를 유도하고, 변수 질량에 대한 주장을 분리.
- [ ] 시험 전용 default-off f 선택과 replay에 필요한 동일 문맥 전달, 기본값 배열 parity 및 짧은 실제 replay 검증.
- [ ] Full-Y 484성분 선형 source, f=0 환원, 실제 native RHS와 회전항의 보간·부호·가중 일 대조.
- [ ] 한 관성중력파의 두 진폭·위상 계수와 같은 전방–수반의 실제 역문제.
- [ ] 수용 분석장의 미사용 시각 예측과 V·와도 포함 상태 비교.
- [ ] 실제 artifact Green/Red 독립 검산, 최종 source graph 갱신과 최소 연구 CI.

## 방정식과 제한

Y 질량점마다 기존 33성분 source column을 두고, 11개 내부 V면의 8성분을 추가하면 복소 자유도는 12×33+11×8=484다. V의 양끝 벽은 고정 0으로 제외한다. 각 column에서 `u_eff=u+Dy(v)/(iKx)`로 기존 원식의 연속방정식·Omega 경로를 재사용하고, V 기압경도력은 실제 전체 Y 차분으로 구성한다. 회전항은 X의 staggered 평균 기호와 Y 평균 행렬 및 그 전치를 사용한다. 연속 파수식에 f²를 더하는 방식은 사용하지 않는다.

고정 f에서 기존 X 반사·복소켤레의 standing-pair 식은 Coriolis 부호를 바꾸므로 재사용하지 않는다. 한 복소 고유모드 q와 iq를 실수 packed X-phase 방향으로 사용한다. 관측으로 보이지 않는 균형/회전 영공간을 모든 제어변수로 풀어 놓지 않고, 알려진 한 모드의 두 계수만 추정한다.

Canonical helper는 `Ru=alpha_u U`, `Rv=alpha_v V`에 회전 보간을 적용한다. 단위 지도계수·상수 f에서 coupled momentum의 평균 연산자는 전치 쌍이며, 균일 고정 배경의 물리적 운동에너지 회전 기여가 상쇄된다. **공간적으로 다른 질량까지 같은 물리 에너지 중립성을 주장할 수는 없다.** 직접 2×2 계산에서 질량 `[[1,3],[2,4]]`, 독립 U(0,0)=1과 내부 V(1,0)=1일 때 f=1의 물리적 회전 일은 -0.125, momentum-square 회전 일은 0이다. 이는 원래 Fortran 보간을 따르는 성질이며 새 C++ 결함으로 분류하거나 생산식에 임의 보정을 넣지 않는다. 유한진폭 실험에서는 이 제한을 유지한다.

## 구현·검증 상태

시험 C++에는 descriptor/RHS probe/physical inverse에서만 사용할 trailing `--rotating-wave`를 추가한다. 기존 solver의 f 필드 경로를 선택하며 e/sina=0, cosa=1을 유지한다. 실제 FP32 f 값을 metadata에 기록한다. 생산 옵션·Fortran ABI·RHS·solver 설정을 변경하지 않는다. 기존 역문제의 Armijo/BFGS는 evaluator만 전달해 재사용하며 허용오차나 관측을 조정하지 않는다.

기본 controller의 기존 mock은 새 evaluator 전달 후에도 통과했다. 이 mock은 실제 native 실행이 아니다. 초기 검토와 독립 stencil 산식은 수행했지만 새 장시간 회전 역문제는 아직 실행하지 않았다. 새 WRF em_b_wave 및 동일 설정 RK3 필드/runtime 비교도 수행하지 않았고 이전 적합 회귀와 구분한다. 자동 CI는 다섯 CTest·짧은 replay·metadata·문법 검사인 기존 최소 연구 범위를 유지한다.
