# 실행 전 계약 — 하나의 prepared A에서 P0/P1 비교

작성: 2026-11-02T09:35:03.637080+09:00.
기반 main9292c6c8(PR337 병합). Green·Red: GPT-6.1-Terra, medium.

## 질문과 연산자

표준 em_b_wave 100km·40×80×64·600초·FP64·MPI1rank·실제 Torch1/1에서
첫 stage2/Newton0의 같은 b,x0,S와 **하나의 PreparedJvp 객체가 정의한 A**를 공유한다.
A(x)=Sinv*(physical−J(a*physical)), physical=S*x, a=dt*gamma이며 원래의 곱셈 순서를 유지한다.
기존 reuse_linearization 옵션을 Tile 생성부터 켠다. 실행 중 전역 옵션을 바꾸지 않는다.
두 군 모두 공통의 reverse-over-reverse 표현을 쓰므로, 기존 native-FWAD 커널의 비용비나
과거 감소율의 곱을 보고하는 실험이 아니다. 원래 시간맵·h600·감쇠·공간식·허용오차는 유지한다.
NAMELIST는 #337 입력에서 reuse_linearization 한 항목만 true로 바꾸고 env도 일치시킨다.

## 고정된 유한 실행

- native 시도1회, 재시도0. 한 프로세스 안의 A0(P0)–B0(P1)–B1(P1)–A1(P0)이다.
  네 개의 독립 예보 실행이나 표본4개의 독립 통계로 취급하지 않는다.
- 네 M은 같은 captured EOS/기하/raw 상태에서 각각 새로 만든다. 원래 mutable M wrapper의
  latch/counter를 재사용하지 않는다. P1은 기존 DirectPressureV와 owned PreparedPrimary의
  apply_core만 쓰며 전체 Tile RHS를 호출하지 않는다. 생산 physical norm ratio guard
  [1e-6,100]을 같은 FP32 norm 방식으로 확인하고, 사건 발생 시 identity로 바꾸지 않고 실패한다.
- 같은 restart/max-restarts와 FP32 표현1e-7 tolerance, packed 1D IdentityS/nohalo/no refinement
  지원 경로만 사용한다. x0는 각 solve에 복제한다. 원래 Newton 최종1e-9를 완주한 시험은 아니다.
- 모델 네 개의 준비는 tape 전에 한다. tape는1회 준비하고 네 solve 동안 유지한다.
  RHS interleave, tape 오류/소유권 변화, nonfinite, FD fallback은 terminal 실패이며 복구·재준비하지 않는다.
- 종료88은 제한 진단 완료,89는 실패다. 정상 코드0/예보 완료로 만들거나 원래 적분을 재개하지 않는다.
  대시간스텝 및 Newton stage 완료는0이다.

## 수치 확인의 정확한 범위

원래 native forward-AD는 tape 생성 전 세 방향, tape 파기 후 각 해 및 각 arm의 두 Arnoldi
누적 조합에 적용한다: native15회, tape 준비 RHS1회다. 세 scaled 방향은 normalized b,
전역 인덱스의 ±1 교대, sin(.017i)+.5cos(.013i) 정규화이며 난수 선택은 없다.
각 native 호출은 RHS1회·fallback0 및 원래 F와 torch::equal 수치 일치를 요구한다
(부호 있는0은 같게 처리하며 byte 비교라는 뜻이 아니다).

같은 native 참조 세 방향으로 tape 작용을 준비 후1회, 각 arm 전후에 확인한다(총27회).
각 global 및 block 비교는 **elementwise allclose(rtol=1e-10,atol=1e-12)**이다.
실제 Arnoldi의 Z와 A(Z)만 sum 및 alternating sum으로 누적해, tape 파기 후 같은 native A와
대조한다. 실제 Arnoldi 수와 누적 수가 같아야 하고, 두 입력은 비영·유한이며 |cos|<1−1e-12다.
전체 basis나 전역 Jacobian을 저장하지 않는다. 조합 검사는 사용한 방향들에 대한 근거이며
모든 방향의 native Jacobian 동등성이나 전체 Tile 직렬화를 증명하지 않는다.

각 해에는 native r=b−A(x)를 새로 계산하고 scaled L2 상대값이 **같은 applied tolerance**보다
작아야 한다. 물리 좌표 L2와 여섯 block norm도 따로 보고하되, 섞인 단위의 전역 norm을
대기 에너지·예측장 정확도로 부르지 않는다. 고정 입력·ref/관측 문맥의 owner/version과
경계의 전체 지문을 검사한다. callback type 지문은 callback payload 전체 증명은 아니다.

## 비용 계산과 종료 결정

CPU는 process user+system, 경과는 steady clock이며 준비·A·M·풀이·native truecheck를 기록한다.
공통 snapshot/prepare 및 tape 비용은 실제로1회 측정하되 **단독 후보당 전액 charge**한다.
charged_total = common + tape + 해당 M setup + 해당 solve + 해당 native residual.
이는 실제 전체 ABBA 경과시간의 분할·합계가 아닌, 공통 연산자를 쓰는 후보별 **모형 비용**이다.
solve 안의 guard/sketch 시간은 포함되며, 이를 뺀 accounted_total도 별도 제시한다.
차감값은 무계측 실행을 직접 측정한 값이 아니고 cache 조건도 바뀔 수 있다.
세 native preprobe,27 tape 검증,8 mixture replay 및 경계 지문은 별도 진단시간으로 보고하고
위 모형 비용에서 제외한다. native residual4회는 각각 해당 arm에 charge한다.

2배 선별 기준을 낮추지 않는다. A0/B0와 A1/B1의 **CPU·경과 모두**에서 charged와 accounted
두 정의 모두2배 이상이고 모든 수치·시간·자원 조건이 성립할 때만 제한 COST_LOCAL_GO다.
그 외 유효 비용 평가는 NO_GO, 유효성 실패는 INCONCLUSIVE다. 어느 경우도 생산 채택이나
B(6–12스텝)·C(5일) 자동 승인이 아니다. 과거 A4 NO-GO/A8 제한/A9/A10 판정은 보존한다.

## 실행기·자원

각 끝점의 monotonic_before_ns/realtime_ns/monotonic_after_ns를 보존한다.
endpoint span<=10ms와, resolution을 포함한 gap interval 전체가 (−1s,+1s) 안일 때 적격이다.
10ms는 기존1s 기준의1%인 읽기 불확실성 예산으로 실행 전에 고정한다. 넓은 bracket 또는
경계 중첩은 unresolved이지 실제 시계 변경의 증거가 아니다. 과거 #337에 소급 적용하지 않는다.
stdout/stderr 네 stream의 수렴 기록은 같은 tuple의 최대 multiplicity로 병합한다.
시간스텝 ID 없는 동일 tuple의 stream 분산은 구분할 수 없으므로 이 종료형0-stage 범위로 제한한다.

실행 전후 디스크5GiB, rank 사후 최대RSS4GiB, MPI360초/launcher390초 한도이며
실시간 전체 메모리 보장이나 반복 ps/lsof 폴링은 없다. 최소 env만 전달하고 실제 스레드를 확인한다.
실행 소스/컴파일러/SDK/Fortran·라이브러리/입력·스크립트 해시는 빌드·실행 전에 고정한다.
기존 RK3 T0 초기장만 재사용한다. 새 RK3 적분·예측장·장기·작은 h 비교는 하지 않는다.
