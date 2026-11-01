# 같은 P1의 압력 직접 소거와 독립 V Schur 후보

작성: 2026-11-01T11:12:49.995186+09:00.
기반 main `6d84ae9b71c65b1059fa6eb8b44b58afd33e9fb4` (PR333 병합).
Green·Red: GPT-6.1-Terra medium. 원래 A7 INCONCLUSIVE/미채택·원복은 유지한다.

## 단일 질문과 수식

같은 `P1=P0−aC`, `a=600γ`를 더 낮은 비용으로 적용할 수 있는가를 확인한다.
P0의 U/W/질량/온위/PH 행은 그대로이며 V행만 기존 검증된 native primary다.
V가 주어지면 `Axz=I+a²αβLx−a²gEφGz`에서 압력을 구한다.
`p_base=Axz^-1[bp−aβDx(bu)+agEφ(bw)]`, `p(V)=Axz^-1[−aβDy V]`다.
여기서 `bp=Eφ(bφ−φ0)+Et bt+Eμ bμ`다. X FFT와 연직 Thomas는 기존
참조 압력의 λY=0 인자를 재사용하고 Y 변환과 전체 P0 역산을 하지 않는다.
기준 β와 동적 상단 인자를 그대로 쓰며 SPD·모드 분리가능한 native A를 가정하지 않는다.

`V−aJprimary(core(p(V),V;0)) = bV+aJprimary(core(p_base,0;b))`를 풀고
모든 변수를 기존 복원식으로 복원한다. 물리 Phi0의 알려진 값은 base에만 포함한다.
마지막 packed 결과는 `b+E(recovered−Rb)`로 만들어 비정규 wall/alias/하단 보완을 보존한다.
내부 matvec은 physical core φ/T/μ와 독립 V만 다루며 P0 전체 작용도 호출하지 않는다.
공유 소거·열역학·차분·primary 식은 소유 컴포넌트 한 곳에 유지했다.

## 내부 표현과 판정

V basis는79×64×40=202240점이며 pressure cache는80×64×40점이다.
전체 packed1080491점 basis를 내부에서 사용하지 않는다. V metric은 실제 확장행렬과 S로
`weight_j=sqrt(Σ_alias 1/S_alias²)`를 계산하여 `||S^-1 E_V v||`와 대응한다.
최대4 Arnoldi, restart0, 실제 V Schur 우변 대비 잔차<=0.01이다.
숫자가 같아도 A7 force-Schur와 다른 미지수·우변·metric이므로 내부 반복 정확도를 같다고
표현하지 않는다. 실제 물리 V행 잔차로 승인하고 Givens 추정값만으로 성공 처리하지 않는다.
정상적인4회 예산 소진과 invalid/context/비유한/특이 평가, 자원 실패를 구분한다.
후자는 INCONCLUSIVE, 유효 예산 소진은 이 bounded 후보의 NO-GO다. 모두 usable x가 없다.

원래 A·ARK·공간차분·600초·감쇠/필터·outer FGMRES/S/guard와 최종 Newton1e-9/상대0,
고정 선형1e-7을 유지한다. 원래 A의 실제 잔차·여섯 소유점 끝값8eps·상태 허용성과
전체 작업량·준비 포함 비용으로 판단한다. post-point 전체 P1 잔차도 별도 계산한다.
첫 P1 scaled 상대잔차<=0.01 및 non-V<=1e-8, 첫 native A 차이·W 상단/내부 비증가,
V 내부 감소, 첫 JVP 엄격감소·전체 JVP 비증가와 첫 구간 비용 감소를 선별 조건으로 둔다.
이는 새 후보의 제한 선별이며 옛 A4의2배 기준을 낮추거나 B 확대를 승인하는 것이 아니다.
2배 국소 목표 충족 여부는 별도 보고하고, 이번에는 어떤 결과에서도 B/장기 run을 자동 시작하지 않는다.

## 검증·실행 범위

5개 컴포넌트 검사(새 core primary·직접 Schur·기존 pressure/model/adapter)가 통과했다.
작은 독립 전체 P1 행렬과 소거/복원, 비영 PH0·V벽·alias/보완·scale별 alias가중치,
압력만의 V forcing, 영 primary, cap/invalid failure를 확인했다. 기존 AD와 새 core 표현도
두 방향에서 대조했다. 작은 계약시험을 표준 h600 효율 근거로 확대하지 않는다.
이전 MPFR·차분 캠페인과 미채택 중첩 모델은 다시 실행하지 않는다.

현재 WRF/MPI 모델 실행·새 RK3 비교는 아직 없다. frozen source와 runner/input/build를
SHA로 사전 고정한 뒤 P0 control→직접 후보 각1회, 독립 초기장의600초 한 스텝만 수행한다.
retry/추가 후보/내부 tolerance·cap 조절/장기 실행은 없다. invalid identity/자원 실패는
INCONCLUSIVE로 보존하고, 유효 수치/작업/비용 실패는 NO-GO로 종료한다.
같은 실제 v/U/context/S를 비교한다. 직접 경로에는 P0 base가 없으므로 A7의 base_z 기록
동일성 검사를 재사용하지 않는다. 실제 wrapper의 반환 z를 원래 Arnoldi에서 직접 캡처한다.
실제 Torch intra/inter thread 수를 읽어 기록하고 두 case에서 같은 값을 요구한다.
MPI360초, rank 사후 RSS4GiB, 실행 전후 disk5GiB, clock gap1초 이내로 고정한다.
상세 iteration 로그·주기적인 호스트 프로세스/디스크 조사는 추가하지 않는다.

Graphify는 검증된 PR333 tree와23개 source SHA가 같은 캐시를 재사용해 시작했고,
변경 후 의존관계를 갱신한다. C++ closure/TLS/Fortran 간선은 불완전하며 소스로 확인한다.
코드 의미 추출 LLM token0. 원래 W잡음·전체 공간항 정합성·5일 정확도·실용 RK3 비용은 미완료다.
