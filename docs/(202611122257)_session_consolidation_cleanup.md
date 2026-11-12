# 현재 세션 통합과 고아 산출물 정리

정확한 현지 시각: 2026-11-12T22:57:10.507343+09:00

기준 커밋: `2d4d8e29dfdfad94412244eb4cb2b61255a0e495`, 기존 PR349.
이번 변경은 현재 SDIRK3 세션의 파일 정리 기록이다. 생산 수치 소스·ABI·tolerance와
기존 수치 결론은 변경하지 않았다.

## 정리한 범위

- 이 세션에서 만든 깨끗한 임시 체크아웃 5개를 제거하고 각 브랜치·커밋을 보존했다.
  파일시스템 여유 공간의 관측값은 약13.06GiB→15.06GiB였다. 이 차이는 다른 I/O의
  영향도 가능한 관측값이며 배타적인 allocated-byte 측정은 아니다.
- 외부에 분리돼 있던133개 파일을 현재 worktree의 `.session/archive`로 이동하고,
  크기·SHA-256·mode를 대조했다. 과거 영수증의 절대경로와 원본 내용은 수정하지 않았다.
- Git blob과 일치하는 중복27개를 공개 canonical 파일에 연결했다. 두 graph는
  gzip 압축 바이트와 복호화한 원본의 hash를 구분해 보존했다.
- 재생성 가능한 Python bytecode2개와 Graphify AST/index95개를 제거했다.
  중복과 캐시의 logical bytes는 총15,804,709, 약15.07MiB다.
- 현재 색인은36개 파일에 대응한다. 공개 canonical27개와 고유 private9개다.
  최초133개 파일 색인과 폐기한 캐시의 inventory는 private history에 보존했다.

## 통합한 위치와 재현

코드·공개 증거와 private 자료는 기존 `residual-blocks-20261110` worktree 하나에 있다.
현재 local 색인은 `.session/index.json`, 고유 실행파일·로그·이력 script는
`.session/archive`다. `.session`의 실행파일·캐시를 Git에 추가하지 않는다.
공개 source와 기존 입력 bundle은 그대로 재현에 이용할 수 있다.

과거 launcher에 적힌 절대경로는 이력이며, 이동 후 실행 가능성을 뜻하지 않는다.
[기존 portable replay](evidence/scalar_blocks_20261110/README.md)를 사용하거나,
색인의 Git ref에서 해당 source를 복원한다. worktree는 원래 저장소의 Git object
store를 공유한다. 다른 세션의 자료·브랜치는 이번 정리 대상에서 제외했다.
Git/PR 병합, 브랜치 삭제나 전역 worktree prune은 수행하지 않았다.

## 검증과 남은 작업

현재36개 파일의 크기·SHA·mode,27개 pinned Git blob 및 복호화 hash, 고유9개
archive inventory, 최초 색인의 hash, 보존한5개 브랜치의 커밋을 확인했다.
Green/Red가 해당 범위를 독립 검증했다. 공개 receipt는 결과와 원본 색인의 hash를
기록하며, private 원본 전체를 제공하거나 삭제 전 상태를 독립 재현하는 자료는 아니다.

새 테스트·WRF/RK3/MPFR 실행·native rebuild·예보장·성능 비교는 수행하지 않았다.
기존 같은 설정의 RK3 비교는 원래 범위에서 재사용하고, 수치 보증을 확대하지 않는다.
W/μ/T 평가오차의 기존 결과는 유지한다. PH/U/V, 전체 merit 감소, Stage3 반복과
시간/예보 수렴은 미완료다. 다음 수치 작업은 기존 체크리스트에 따른다.

[공개 정리 기록](evidence/session_cleanup_20261112/receipt.json).
