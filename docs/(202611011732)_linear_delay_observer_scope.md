# 후속 선형계 지연: 같은 구간 CPU/경과시간 관측

작성: 2026-11-01T17:32:36.669920+09:00.
기반 main `fe906dee3c6fb19ab6fd33dd090c251cb41ee8a3` (PR334 병합).
Green·Red: GPT-6.1-Terra medium.

## 단일 질문

PR334의 stage4/Newton0 JVP32회 구간은12.7189→45.072031초였지만 CPU가 따로
계측되지 않아 연산과 실행 지연을 구분하지 못했다. 기존 DirectP1(default-off)·600초·
원래 공간항/감쇠/최종 수렴조건을 고정하고 같은6개 실제 선형계의 두 시간을 관측한다.
새 solver·보정·tolerance/cap 조절·장기 실행을 하지 않는다. PR334 국소 SCREEN_POSITIVE와
전체 RSL152.37→194.94초, 기존 A4/A7 결과는 소급 변경하지 않는다.

## 관측 범위

새 compile macro는 기본 OFF이며 Newton/Tile 두 파일만 관측한다. 기존 EOS 구조/ABI나
Fortran/CFFI·solver 설정을 바꾸지 않는다. steady clock과 getrusage(RUSAGE_SELF)의
user/system CPU를 같은 실제 GMRES/FGMRES 시작/종료에 읽는다. CPU는 rank 프로세스
전체 thread 합이므로 wall보다 클 수 있고 wall−CPU는 대기나 특정 원인 계측이 아니다.

기존 JVP 선택 구간의 wall/CPU, A·최종 wrapped M 호출/경과시간, 중첩/overlap,
actual backend thread, outer AD/FD 선택과 내부 FWAD→FD fallback 차이를 기록한다.
RHS mode/call 수와 반환된 잔차·budget·success를 연결하며 추가 A/M/RHS/JVP는 없다.
관측 실패는 ineligible로만 표시하고 원래 callable/반환값은 유지한다.

다섯 번째 실제 stage4/Newton0에 실제 바인딩된 Uref·reference-update/BC/기하·base/EOS·
스케일/입력 문맥을 확인한다. 첫 실제 Arnoldi의 Ueval/Uref/물리 tangent/Jv만4개 FP64
NPY로 저장한다(합64MiB 이하). 기존 첫점 A4/A5 계산·파일도 그대로 유지하여 allocator/
진단 workload 변경을 지연 원인과 섞지 않는다. 전체 raw 합192MiB 이하로 제한한다.
복제는 시간 구간 안의 overhead로, hash·NPY쓰기·JSON은 구간 밖으로 기록한다.
무한한 전체 Tile identity나 transitive cache 재생을 보장하지 않는다.

## 검증과 실행 규약

두 TU의 TLS 공유·시계/중첩·기존 forward AD값/접선·NumPy FP64 roundtrip·EOS/캡처
실패·기존파일 거부·owner/version 변화·callable copy 예외의 원래 계산 보존을 검사했다.
초기 컴파일의 ADL 충돌과 개발 중 input-drift 정지는 보존하며 최종 동일 header 검사를 통과했다.
이는 native validation은 아니다. 아직 WRF/MPI 실행이나 새 RK3 비교는 수행하지 않았다.

이후 frozen source·input/NML·31객체/헤더/라이브러리·runner SHA를 고정하고 control P0→
DirectP1 각1회, 같은100km·질량40×80×64·600초 독립 초기장 한 스텝을 수행한다.
No retry/추가 후보/느려질 때까지 재실행은 없다. 각 arm의 원래174숫자 배열과 비교해
observer noninterference를 요구하고,6구간 clock/counter·선택5번째 raw/context를 검증한다.
입력·reference·분기·fallback 차이는 발견으로 보고하며 같다고 가정하지 않는다.
시계 실패·캡처 미완료·필수 문맥 불안정은 INCONCLUSIVE로 남기고 clock유효값도 별도 보존한다.
새로운 전체 시간의 개선·악화를 gate로 소급 골라서는 안 된다.

MPI360초, postrun rank RSS4GiB, pre/post disk5GiB, actual Torch1/1, clock gap1초 이내,
새선형계 경과시간의 clone overhead1% 이내를 고정한다. 상세iteration/global ps/디스크 주기
샘플러·새 대형 profiler는 없다. RK3는 보관 자료를 재사용하며6–12스텝/5일은 시작하지 않는다.
관측이 과거 지연을 재현하지 않으면 재현되지 않았다고 종료하고 특정 앱/swap/스케줄러로
원인을 단정하지 않는다. 같은 U만으로 전체 연산자가 같다고 주장하지 않는다.

Graphify는 이전26개 source SHA와 merged tree의 같은 캐시를 검증해 사용했고 변경 corpus를
갱신한다. C++ closure/TLS/Fortran 간선은 불완전하며 소스로 확인한다. 의미 추출 LLM token0.
