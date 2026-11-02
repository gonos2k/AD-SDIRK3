# 비교 실행 전 캡처 연결 수정

작성: 2026-11-02T11:18:04.548829+09:00.

첫 source11b71a6b의 사전 등록1회는 input 단계에서 종료89로 실패했다.
V-primary preparation callback이 비어 있었으며 native JVP0회, M0회, 풀이0회였다.
시간 bracket은 consistent였지만 수치 비교에 진입하지 않았으므로 원래 runner
INCONCLUSIVE/cost_decision=INCONCLUSIVE를 그대로 보존한다.

원인은 기존 A5 캡처가 `selected_base()`이면 생략되도록 작성된 데 있다.
이 규칙은 Stage4 관측기의 큰 자료를 줄이기 위한 것이었으나 새 matched 관측기가
Stage2를 선택하면서 필요한 준비 콜백도 함께 생략했다. 방정식이나 prepared AD 실패가 아니다.

선택된 Stage2의 active EOS base에서는 A5 캡처를 허용하고, 선택된 Stage4에서는
이전처럼 생략한다. null/unselected/non-base 경로도 이전 동작을 유지한다.
정책은 감사 헤더 한 함수에 두며, Tile의 한 조건만 이를 사용한다.
이는 이후 Stage2 관측기에도 적용되는 일반 캡처 정책이며 특정 결과값을 우회하지 않는다.
원래 `capture_active`, failed/duplicate 검사는 유지한다.

새 소스와 별도 디렉터리/규약에서 추가 native 시도1회만 허용한다.
기존 실패 기록은 재작성하지 않고 새 등록에 해시로 연결한다. 동일 바이너리를
유리한 시간값이 나올 때까지 반복하는 재시도가 아니다. h600, 같은 입력/NML,
연산자·모델·수렴조건·2배 비용 선별·시간·자원 기준은 바꾸지 않는다.
누락 캡처의 predicate 시험과 재빌드 후 실행하며 새 결과도 독립 판정한다.
