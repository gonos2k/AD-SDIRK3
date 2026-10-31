# A5: 같은 native 방향의 V-primary 기여 분리

작성: 2026-11-01T01:54:53.849073+09:00.
기반 main: d24e6b359930b854f3a5dc2e7d280b823b790d53.
Green/Red: GPT-6.1-Terra medium.

## 단일 목표와 닫은 범위

A4의 수평평균P0 NO-GO는 닫아 두며 반복하거나 기준2배를 낮추지 않는다. 이번에는 같은
h600의 실제 first stage2/Newton0에서 기존v와Az에 실제z=P0⁻¹v를 연결한다.
새 분할·감쇠·Newton/선형 허용오차·참조 pressure inverse는 바꾸지 않는다.

동일한P0의 순방향 작용으로 e=v−Az=(v−P0z)+(P0−A)z를 전 packed 행에서 계산하고,
물리 블록과 S로 되돌린 scaled 좌표를 별도 검증한다. inverse reconstruction relative norm은
1e-8 이하여야 하며 각 블록 closure는128*FP64eps*해당block데이터 scale로 판정한다.
V의 물리 소유 내부1..79면만 기여 판정에 쓰고 walls/aliases를 별도 보고한다. W의 동적 상단은
전체 지표에 포함한다. native Q는PH0/W0를 보존하므로 P0의 Qhat로 바꾸지 않는다.

원래 EOS/helper와 같은 호출에서 실제 cqv, 질량 분모/변환, 경계/분기 정보를 고정한다.
순수 native Q/평균/face-mass 함수를 재사용하며 불변 문맥과 수명을 검사한다.
고정점의 V-primary만 원래 horizontal_pgf_primary와 같은 변환 순서로 독립 평가한다.
mu perturbation 때 state-dependent face masses와 분모도 다시 계산한다.
세 해석항은 isolated FWAD(유한차분 fallback 없음)와 중앙차분으로 검사한다.
AD는 per-point1e-12m/s³+1e-9|AD|, FD는 같은 절대값+1e-5|AD|를 기준으로 하고
FD 폭[0.01,0.001,0.0001]초 중 두 폭 이상이 통과해야 한다. 원식 재현의 torch::equal은
정확한 수치값 일치이며 signed zero를 구분하는 byte equality라고 부르지 않는다.

## 실제 기여의 사전 판단

m=(P0−A)z의 V 성분과 c=a*Jprimary_native(z)−(z−P0z)_V를 같은 방향에서 비교한다.
정규화된신호||mV||/||vV||≥1e-8을 요구하고, 다른검증이 통과한 경우
||mV−c||²/||mV||²≤0.5를 만족할 때만 다음 보정 검토로 진행한다.
계수를 맞추거나 내적 최적계수를 실제 보정에 넣지 않는다. cosine·gain·직교성은 설명용이다.
NH4·질량 product-rule·Omega는 원인으로 섞지 않고 미포함 경로로 남긴다.
기여를 설명하지 못하면 이 primary 후보를 종료하고 항을 임의로 누적하지 않는다.

## 검증과 자원

코드·ABI·집중검사 후 사전등록한 native P0 한 스텝만 수행할 계획이다.
기존 A4 B0의 input/NML/source-context·v/Az·174숫자끝값은 변경 비간섭 근거로 재사용한다.
A5는 항상cost_eligible=false로 두고 A4 속도비나5일 안정성으로 해석하지 않는다.
추가 전체 RHS/A/M 호출은 하지 않으며, 이미 계산한 triplet과 읽기전용P0 forward 및
isolated EOS/PGF·Q만 사용한다. 원래 선형계 수렴·단계·guard/FD fallback 판정은 유지한다.
원래 AD에 필요한 JVP는 유지하고 자료동화용 궤적은 저장하지 않는다.

원시 상태·z·S·EOS/기하를 한 점의 self-describing 바이너리에 먼저 원자적으로 저장하고,
성공한 isolated 작용은 별도 작은 파일에 저장한다. primary 검증 실패 때도 실제 데이터는
남겨 offline 수정에 재사용한다. 단 P0 forward 자체의 예외나 OOM은 원시 저장에 앞설 수 있다.
문맥 callback은 저장된 함수가 아니라 고정된live source를 호출하는 짧은 수명 객체이며
실제 projection/분기 불일치·불완전 capture는 부적격으로 분리한다.
FP64 데이터는 진단에 필요한 한 점만 보존하고 후처리에서 무손실 압축한다.
새 native 실행·RK3 재실행은 이 문서 작성 시 하지 않았다. 보존된 RK3와의 예측장 그림을
별도 비교하며 이 방향의 rate-vector를 후반24–36시간 예측 잡음이라고 부르지 않는다.
