# 여섯 선형계 CPU/경과시간과 다섯 번째 입력 관측

작성: 2026-11-01T18:18:26.346413+09:00.
Native 소스 `5dc7fc7c`; 기반 main `fe906dee`; Green·Red: GPT-6.1-Terra medium.

## 확인한 것과 남은 것

같은 구간을 계측해 CPU·wall 및 wall−CPU 산술 잔여의 변화를 기록했다. 다섯 번째의
JVP/RHS32회와 AD선택32/FD0/내부fallback0은 같았지만 실제 Ueval·단계 참조·tangent/Jv는
미세하게 달랐다. 구간 내 reference 재바인딩·version 변화는 없었다. 과거45.072초의 JVP
표본은 새 측정에서 재현되지 않았다. 특정 앱·swap·스케줄러나 하나의 코드 결함을 원인으로
확정하지 못했다. 더 많은 전체 명령어를 수행해 느려졌다는 단순 설명도 전체 계수와 맞지 않는다.

Direct P1는 기본 OFF로 보존했고 새 solver/식/감쇠/시간간격/허용오차를 추가하지 않았다.
A8 국소 SCREEN_POSITIVE와 전체 속도 미확인, W 잡음·5일 목표는 그대로다.

## 최소 관측 구현

기본 OFF compile macro가 Newton/Tile에만 들어간다. 기존31개 core의 두 CPP를 새로
컴파일하고29개는 source/header/object를 확인해 재사용했다. 두 arm은 같은 관측 Tile를
공유하고 Newton의 Direct flag만 다르다. 기존 클래스/EOS 구조·Fortran/CFFI ABI는 바꾸지 않았다.

실제6개 GMRES 시작/종료에 steady wall와 getrusage(RUSAGE_SELF)의 user/system CPU를
읽었다. 같은 선택 JVP 구간에도 CPU/wall을 읽으며 A/최종 wrapped M 호출·경과시간,
중첩·RHS mode·backend thread·outer AD/FD와 internal fallback을 연결했다. CPU는 모든
rank thread 합이며 wall보다 클 수 있다. wall−CPU를 대기나 특정 원인으로 부르지 않는다.
JVP는 A의 포함 항이고 A가 M 안에 중첩되면 additive remainder를 만들지 않는다.

다섯 번째 실제 stage4/Newton0의 base F 및 첫 실제 Arnoldi JVP에서 참조를 관측한다.
U_ref_stage_를 실제 reference update 뒤·원래 RHS 평가 전에 읽고, U/F/K/b/x0/S/Sinv,
EOS/base/maps/metrics·topology/BC/flags·owner/version을 기록했다. scratch 변경은 별도
표시하며 모든 transitive cache/backend identity를 보장하지 않는다. 네 FP64 NPY만 추가했다:
Ueval, 실제 첫 JVP의 Uref, physical tangent, Jv. 실제 수치 계산을 다시 평가하지 않았다.
clone은 구간 내 overhead, hash/NPY/JSON은 구간 밖이다. 기존 첫점 A4/A5 workload는 유지했다.

## 실제 실행·스택·검증기 기록

고정 control P0→Direct P1 각1회,100km·질량40×80×64·600초·mode3·FP64 carry,
EW/reuse OFF·고정 선형1e-7·Newton1e-9/상대0·같은 damping/filter로 수행했다.
각각 초기장에서 한 스텝이며 연속20분·6–12스텝·장기 계산이 아니다. retry는0이다.
둘 다 rc0·3단계·wrapper3/0·유한/물리 허용 상태·actual Torch1/1을 충족했다.
각 arm174숫자 배열은 관측 코드 없는 같은 arm의 A8 보관 끝값과 바이트 일치했다.
관측기가 다른 수치 궤적을 만든 근거는 없으나 FP32 공개 끝값의 동일성만으로 모든 내부
FP64/전체 operator의 동일성을 증명한다고 표현하지 않는다.

빌드 마지막 검사에서 과거 SDK27.0과 현재27.2의 libSystem/libc++ current-version
load metadata가 달라 중단한 기록을 보존했다. 경로·compatibility와 minOS26.0은 같고,
현재 host26.7에서 호환됨을 확인해 **모델 실행 전** 별도 current-SDK binding을 승인했다.
현재 두 arm의 compiler/SDK/연결 라이브러리·LC_BUILD_VERSION은 같았다. 과거 정확한
compiler hash는 미확정이고, 과거 타이밍의 원인 비교에는 이 차이가 남는다.

OFF 모델 종료 후 원래 Python검증기는 `positive` import 누락으로 INCONCLUSIVE를
기록했다. 이 원본은 수정하지 않았다. 같은 helper와 같은 모든 gate를 저장된 출력에만
적용한 별도 validator가 PASSED를 기록했다. 추가 모델·기준 완화 없이 ON의 control
연결을 그 receipt와 원본에 동시에 고정했다. ON은 DIAGNOSTIC_COMPLETED로 직접 완료했다.

## 다섯 번째 같은 구간의 실제 값

|측정량|OFF|ON|
|---|---:|---:|
|선형계 JVP/RHS 횟수|32/32|32/32|
|outer AD/FD · 내부fallback|32/0 ·0|32/0 ·0|
|선형계 경과시간 s|7.723425|14.302400|
|같은 구간 process CPU s|7.720556|11.669290|
|wall−CPU 산술 잔여 s|0.002869|2.633110|
|포함 JVP 경과시간 s|5.311856|9.915317|
|포함 JVP CPU s|5.310078|8.181528|

선형계 wall 증가6.578974초는 CPU 증가3.948734와 산술 잔여 증가2.630240초로 나뉜다.
JVP만의 wall 증가는4.603462초이고 CPU2.871450, 산술 잔여1.732012초다.
이 세 포함 시간들을 서로 더하지 않는다. 각 증가의 원인·실행하지 않은 시간 종류는 미확정이다.
단순히 JVP 횟수 또는 FDfallback이 늘어서 지연됐다는 설명은 이번 관측에는 맞지 않는다.

Ueval 최대차는 U8.79e-12m/s,V3.26e-11m/s,W3.72e-13m/s,PH2.19e-10m²/s²,
T1.21e-12K,MU2.32e-10Pa였다. Uref에도 비슷한 작은 차이가 있으며 tangent/Jv도
바이트 동일하지 않았다. S/Sinv·topology·BC·포착한 geometry는 같았지만 EOS/F/K/b와
state/reference는 달랐다. 이 작은 차이가 지연을 만들었다는 결론도, operator가 같으니
외부 원인이라는 결론도 하지 않는다. source가 같은 Tile라도 입력/전체 분기는 같다는 증명이 아니다.

## 전체 rank 실행 관측

|측정량(전체 process, 다섯 번째 구간 아님)|OFF|ON|
|---|---:|---:|
|rank real s|50.65|120.00|
|user+sys CPU s|49.69|86.29|
|retired instructions|343262476142|317047047237|
|cycles|100216842870|104747395002|
|involuntary switches|978|59119|
|max RSS B|2032926720|2150039552|
|peak footprint B|1496172728|1662552296|

전체 명령어 수는 약7.6% 줄었는데 CPU는약73.7% 늘었다. 더 많은 전체 retired instructions
하나만으로 CPU 증가를 설명할 수 없으며 실행 조건/효율의 차이도 분리해야 한다.
cycles/CPU는 실제 frequency의 독립 계측이 아니고 switch 수는 특정 원인이나 다섯 번째의
인과 계수가 아니다. RSS/footprint 중 유리한 값만으로 메모리 개선도 주장하지 않는다.
과거와 현재 SDK/실행시점이 달라 과거45초 JVP spike를 소급 설명하지 않는다.

## 그림·단계 종료

[6구간 CPU/wall](evidence/linear_delay_audit_20261101/solve_timing.png),
[실제 포착 W와 참조 및 차이](evidence/linear_delay_audit_20261101/fifth_W_states.png),
[전체 rank 보조 계수](evidence/linear_delay_audit_20261101/aggregate_runtime.png)를 확인했다.
포착한 W는 Newton 평가/참조 상태이며 최종 예보나24–36시간 noise 성장 그림이 아니다.
RK3는 기존 초기/끝 참조로 재사용했으며 새 RK3/장기 계산은 하지 않았다.

관측기 검사는 두 TU TLS·clock/nesting·AD 값/접선 보존·NPY·EOS/파일/owner 오류·
callable-copy exception의 원래 함수 보존을 통과했다. Graphify는 같은 main tree26source
캐시를 검증한 뒤 관측 source를 갱신했다. TLS/callback/Fortran 간선은 불완전하며 source/
수치 근거를 따른다. 의미 추출 LLM token0이다.

이번 범위는 같은 구간 계측과 한 diagnostic block까지 종료한다. 원인 단일화, 같은 상태의
전체 Tile 대응 연산, 실행조건별 재현성, 전체 가속·W noise·5일 정확도는 미완료로 남긴다.
현재 Raw/Direct 비용을 다른 run과 곱하거나 새 solver/감쇠·한도를 추가하지 않았다.
원본 실패·current SDK binding·별도 validation 및 유효 raw는 보존했고 정확한 중복만 정리했다.
[manifest](evidence/linear_delay_audit_20261101/manifest.json)에 모든 공개/원본 hash를 고정한다.
