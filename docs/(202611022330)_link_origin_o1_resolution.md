# Native 링크 귀속 정정과 O1 계약 정의

작성: 2026-11-02T23:30:41.445603+09:00.
기준 main `eca8f6fcbc91c42bcea9bda4375ae6cd333a238c`(PR340).
생산 소스 `fcd58bc9aa77818e98fdb21f54aa375d66e3139d`.
Green·Red: GPT-6.1-Terra medium.

## 한 항목씩 해소한 결과

| 항목 | 처리 | 판정 |
|---|---|---|
| L1 이전 실행 코드 귀속 | PR340 실행파일 SHA가 PR339 실행파일과 정확히 일치 | c7 native 검증 주장 철회 |
| L2 중복 archive 후보 | Darwin LDFLAGS의 SDIRK3 -L/-l 제거, LIB_BUNDLED 명시 archive 유지 | owning 설정·make 확장·실제 링크 확인 |
| L3 실제 object 귀속 | 실제 ld probe/version, 전체 map, 직접 Newton·Tile 메서드 file ID 확인 | 현재 단일 archive 귀속 확인 |
| L4 최신 코드 h600 회귀 | 새 binary 한 스텝, 원래 조건·보관 출력 비교 | 아래 범위 통과 |
| R2 종료 문구 | no accepted update와 같은 iteration의 raw/modified/effective/entry 상태 | 문구·주석 수정 및 컴파일; 종료 event/수치/분기 유지 |
| O1 함수 계약 | 실제 base·callback·projection·문맥·공간·metric·JVP를 소스에 대응 | 정의 완료; 고정성·미분 동일성은 O2/O3 |

## 링크 문제와 정정

PR340의 executable SHA `8bd1e2e3fa63f9233360d7d97dd3689ff3d505142bab73da75b02826e99b5c32`는
이전 PR339 파일의 실제 SHA와 같다. 새 archive 검사만으로 최신 코드가 실행됐다고 한
판단은 잘못됐다. 이전 실행·출력 관측과 집중시험은 보존하지만 그 native 실행을 c7
회귀 인증으로 사용하지 않는다. 정확한 과거 archive 추출 과정까지 역추적했다고 하지는 않는다.

`arch/configure.defaults`의 Darwin SDIRK3 프로필은 LDFLAGS에 먼저 -L/-l을 넣고,
LIB_BUNDLED에 archive를 다시 명시했다. `main/Makefile`의 여덟 실행파일 링크 recipe는
같은 LIB를 사용한다. 앞의 SDIRK3 후보만 제거하고 Torch/MPI/Fortran flags와 뒤 archive는
유지했다. make 확장에서 후보 하나를 확인했으며 각 실행파일을 따로 빌드하지는 않았다.
보관된 Fortran object·라이브러리는 재사용했고 전체 WRF clean rebuild나 새 RK3는 없었다.

현재 archive SHA `978d2a81e0153d71185fa48d2d98412f5355b3aa3459ef924e3ac31253d0ced4`,
실행파일 SHA `79a1f00887f889d8ac944f168b040e9e2ab7f4a5f8bba345c5764d8b6d9a2185`다.
Apple ld `ld-27106.1`의 실제 driver invocation을 보존했다. map file ID465는 Tile object,
466은 Newton object이며 실제 `solveImplicitStage`와 `Impl::solve_stage_impl` 본체가
각각 그 member에서 왔음을 확인했다. lambda/cold clone만 확인한 것이 아니다.
전체 map은 gzip(약0.8MB)으로 제공하며 원본 SHA도 함께 둔다.

## 보완한 native 회귀의 범위

A9/P0 연구 구성·MPI1·Torch1/1·100km·40×80×64·h600 한 스텝을 완료했다.
stage2/3/4는 각 Newton3회, scaled RMS는 6.296993e-14 / 6.345785e-14 /
6.344006e-14, wrapper3/0이다. 174개 endpoint 숫자 배열은 보관된 **이전 바이너리의**
A9 출력과 bitwise 일치했다. 초기21장/계수는 signed-zero 동등 비교를 통과했고,
여섯 비교장 유한값·저장된 양성 지표를 확인했다. 현재 raw/direction 진단도 로그에 6회 나타났다.

보관 RK3와 RMS 거리는 U 3.148492e-4m/s, V 2.031948e-4m/s,
W 6.909943e-4m/s, PH 0.3963487m²/s², T 2.922344e-4K, MU 0.3137946Pa다.
이는 두 방법 사이의 거리이며 정확해 오차·잡음 개선이 아니다. RSL146.37157초와 보관
RK3의0.55700초는 서로 다른 실행 시점·관측 조건의 기록으로만 남기며 공정한 속도비로
해석하지 않는다. rank RSS2,084,651,008바이트, 등록된 자원 한도를 통과했다.

이전101/210/12 집중시험은 새 archive를 직접 링크한 유효 근거로 재사용했고 반복하지 않았다.
이번 CPP 변경은 진단문구/주석뿐이며 Newton object만 재컴파일하고 나머지30member는
기존 archive와 byte 비교했다. zero-update 종료 분기를 이번 정상 native 실행에서
실제로 통과했다고 주장하지 않는다. raw runner 결과와 별도 PASSED postvalidation은
구분해 보존했다. 기본 native 전체 구성·다중 rank·장시간·속도 수용 인증은 아니다.

## O1과 다음 순서

[실제 mode3 함수 계약](design/mode3_residual_operator_contract.md)은 실제 stage base B와
Y=B+aK, G_K=K−F_C(Y), G_Y=aG_K의 조건을 정의한다. F_phys의 slow 소유와 실제 boundary
projection, packed/full-halo의 차이, reference/omega/cache를 C에 포함한다.
Newton의 masked scaled RMS와 GMRES의 unmasked relative L2/조건부 D objective를
구분한다. passing halo mask만으로 1-D Krylov reductions가 같은 mask를 쓴다고
가정하지 않는다. 이러한 차이는 곧 구현 오류의 증명은 아니며, 허용 공간에서의 정합성을
확인해야 한다는 남은 의무다.

O1은 소스 정의와 조건부 대수 관계까지다. 고정 문맥·JVP와 실제 residual 미분의 동일성,
제약 공간의 작용은 O2/O3/O4에 남겼다. 이어 E의 에너지/가역성, T의 실제 시간 지도,
A의 오차 예산, F의 전방·W 잡음·5일·전체 비용을 검증한다. P1 NO-GO를 다시 열지 않는다.

[증거 manifest](evidence/link_origin_o1_20261102/manifest.json).
Graphify는 검증된39파일 corpus를 재사용/갱신했다(AST861nodes·2738edges, semantic LLM0).
Make 관계는 별도 source/make 확장으로 확인했으며 AST·소스 검토를 수치 증명으로 쓰지 않았다.
