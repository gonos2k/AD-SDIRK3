# PR #287 비영 Y/V 한 모드 검증 체크리스트

작성 시각: 2026-10-10 23:40:08 JST
기준 main: 2e548c01cdd32a26fe58da59111cc761ba99a822. 원래 사용자 작업트리는 건드리지 않고 별도 worktree에서 작업한다. PR #286 terminal 실험은 실제18개 호출과 Green/Red 재계산을 마쳤으며 경험적 정확도 수준의 기존 기준 사례는 종료했다. Formal 4D Eg 상한은 별도 미완료 범위다.

- [x] 기존 33성분 source RHS의 수직/EOS식을 그대로 재사용한 41성분 source-coordinate Y/V 확장. U_eff=u+Ky/(iKx)v는 X+Y 발산을 정확히 합치며 Vdot=(iKy/Kx)Udot는 실제 Y 압력기울기 부호를 반영한다. 연속파 각도회전 또는 단순sqrt(kx²+ky²) 대체를 쓰지 않는다.
- [x] Periodic X/symmetric Y의cos-scalar, sin-V C-grid parity와ky0기준exact환원 확인. V양끝벽0, U주기alias, 고정하단W/PH를 pack에 적용한다.
- [x] 같은 건조·고정계수·Coriolis0배경의한결합모드와두시간quadrature QA/QB. Wmax=.01 m/s로공통정규화하고 가장큰양의 sub-Nmax 주파수 .002568938 rad/s를선택한다. H_BG 관측행렬rank2/조건수5.126을확인했다. 관측truth는독립linear-source전파와actualPH높이표본이며native비선형trajectorytruth가아니다.
- [x] 기존native--quadratic-probe를재사용해실제RHS의2quadrature/3폭/6블록을비교했다. 대조는작은공통방향scale1e-3로수행해finite smooth-upwind를미소선형계수와혼동하지않는다. Local source/exe/descriptor/CSV 근거는receipt에기록했다.
- [ ] 같은실행파일16stepgate와Linuxpreflighthelper실행. 직접CI실행은아직전이다.
- [ ] 같은두제어변수의실제300초FP64전방–재생수반–strictcostArmijo초기장최적화. sourcewhitenedH_BG/sigma의2×2metric만초기척도로사용하며prior항이없다. 실제literal시작은descriptor평형base+B@(.25,.15), truth(.78,-.36)이며bareB나기존저장분석장을배경으로쓰지않는다.
- [ ] 수용분석장에서미사용시각예측과U/V/W/PH/온위/MU소유상태를평가한다.관측비용개선과전체상태/예측개선을구분한다.
- [ ] Green/Red팀의최종소스·원자료·수치·완료범위감사.

자동CI는기존5개CTest·16스텝재생·metadataconsumer·문법검사로유지한다. 필요한Y모드실험만수동실행하며배포·설치·전체시험·플랫폼확대를추가하지않는다.

현재생산C++/Fortran코드와native시험CPP는변경하지않았다. 새WRF모델실행이나같은설정의RK3필드/시간대조도수행하지않았다. 적합한PR#283회귀를새실행과구분해재사용한다. 새physics옵션/Hessian/checkpoint/API는선행조건으로추가하지않는다.
