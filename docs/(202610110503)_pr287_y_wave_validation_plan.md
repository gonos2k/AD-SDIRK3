# 비영 Y/V 한 모드의 연구용 역문제

작성 시각: 2026-10-11 05:03:06 JST
기준: main 2e548c01cdd32a26fe58da59111cc761ba99a822. 원래 사용자 작업트리는 변경하지 않고 별도 worktree를 사용한다.

현재 X-only 기준 사례의 terminal 독립 미분은 실제 18개 호출과 Green/Red 재계산을 완료했다. Richardson–수반 L2 차이는 1.6783e-8로 종료 기준 여유보다 충분히 작다. 경험적 기준 사례는 닫고, formal Eg 상한은 별도 범위로 남긴다.

이번 확장은 기존 건조·고정계수·단일 tile 배경에서 Y 의존성과 V만 추가한다. Periodic X의 m=1, symmetric Y의 첫 벽 모드를 사용하며 scalar/U/W/PH/온위/MU는 cos Y, V는 Y면의 sin Y와 정확한 양끝 0을 따른다. 기존 1D 원식의 수직/EOS 행을 그대로 사용하고 U_eff=u+Ky/(iKx)v와 Vdot=(iKy/Kx)Udot로 실제 C-grid Y 발산·압력 기울기를 확장했다. 기존 33성분 기준은 변경하지 않았다.

독립 41성분 operator의 한 sub-Nmax 결합 모드에서 두 시간 quadrature를 만들었다. 주파수는 약0.002568938 rad/s, W 공통 진폭은0.01 m/s다. 150·300초의105개 물리 위치 H_BG는 rank2/조건수 약5.126이다. Native 입력은 정지 descriptor base+B@절대 제어값이며, truth=(0.78,-0.36), 시작=(0.25,0.15)다. Source H_BG/sigma로 만든 inverse Gram은 탐색 metric일 뿐 prior가 아니다.

체크리스트:
- [x] Source Y벽 parity, ky0 exact 환원, eigenpair/시간 quadrature, 관측 rank 확인.
- [x] 실제 local native RHS 두 방향·세 폭 및 여섯 블록 대조. V벽/U중복면/하단W·PH 규칙 확인.
- [x] Full/preflight-only mock main, F821·문법·actionlint·Green/Red source 검토. Mock은 native 계산이 아니다.
- [ ] 같은 Linux 실행파일의 descriptor·RHS·16스텝 replay preflight.
- [ ] 같은960×0.3125초 전방·재생수반·실제 비용 Armijo/BFGS 초기장 추정.
- [ ] 수용된 분석장의 미사용 예측과 U/V/W/PH/온위/MU 상태 비교.
- [ ] 실제 artifact Green/Red 독립 재계산 및 완료 범위 검토.

실제 최적화는 최대3회 갱신과4회 backtracking으로 제한한다. 매 수용점에서 fresh gradient를 계산하며 strict 비용 감소만 수용한다. 계산 종료 기준에 도달하지 않으면 incomplete로 남기고 과장하지 않는다.

자동 CI는 기존5개 CTest,16스텝 replay,metadata consumer 및 문법 검사만 유지한다. 장시간 실험은 명시적 수동 실행이다. 배포·설치·전체 시험·새 플랫폼을 추가하지 않는다.

생산 C++/Fortran 및 native 시험 CPP는 변경하지 않았다. 새 WRF 실행이나 같은 설정 RK3 필드/시간 비교는 수행하지 않았으며, 적합한 PR#283 회귀 근거를 새 실행과 구분해 재사용한다.
