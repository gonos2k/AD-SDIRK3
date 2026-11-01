# 압력 직접 소거: 국소 선별 통과, 전체 시간은 미해결

작성: 2026-11-01T12:22:28.830176+09:00.
기반 main `6d84ae9b`; native 소스 `baacbb4b`; Green·Red: GPT-6.1-Terra medium.

## 판정

같은 P1를 X–연직 압력 소거와 독립 V Schur로 구현해 **첫 선형점 제한 선별은 통과**했다.
준비 포함14.4088→11.7174초, 첫 JVP23→14다. 그러나 **전체 스텝 RSL은152.3707→
194.93518초로 늘었다. 전체 전방 가속은 확인되지 않았다.** 국소2배 목표도 미달이다.
코드는 default-off 연구 경로로 보존하며, B·장기 실행·생산 채택은 하지 않는다.
A4 NO-GO와 A7 INCONCLUSIVE/미채택·퇴역 기록은 그대로 유지한다.

## 같은 근사계의 직접 소거

P0의 U/W/μ/t/PH행은 유지하고 V행을 `V−aJprimary=bV`로 대체하는 같은 P1다.
`Axz=I+a²αβLx−a²gEφGz`를 사용해
`p_base=Axz^-1[bp−aβDx(bu)+agEφ(bw)]`, `p(V)=Axz^-1[−aβDy V]`를 구한다.
`bp=Eφ(bφ−φ0)+Et bt+Eμ bμ`이며 알려진 physical Φ0는 base에 넣는다.
V Schur의 homogeneous/base를 분리하고 모든 필드를 기존 소거식으로 복원한다.
마지막 결과 `b+E(recovered−Rb)`는 원래 벽·alias·하단 보완 값을 유지한다.

압력 부분풀이는 기존 λY=0 인자를 재사용하는 X FFT+연직 Thomas다. Y 변환과 전체 P0
역산/작용은 내부에서 호출하지 않는다. 부분풀이 자체가 무료라는 뜻은 아니다.
Primary는 물리 φ/T/μ core만 받아 내부 V를 돌려주며, 같은 분석식을 packed oracle과 공유한다.
실제 native A는 바꾸지 않았고 참조계·축약계의 SPD나 native A=P1은 가정하지 않았다.

V basis202240점, pressure cache204800점이며 내부 전체 packed1080491점 basis는 없다.
외부 FGMRES는 기존 전체 상태다. cache backing/임시 배열이 있어 이 원소 수 감소를 전체
메모리/시간 감소율로 부르지 않는다. V metric은 `sqrt(Σ_alias S_alias^-2)`로 실제 확장과
같은 norm을 구성한다. 4회/no-restart/실제 상대잔차0.01이며 새 V Schur 우변에 대한 값이다.
A7 force-Schur와 같은 정확도 조건이라고 가정하지 않는다. 최종 native A의1e-7과
Newton1e-9/상대0은 유지했다. 정상 예산 소진과 invalid/자원 실패는 별도로 분류했다.

## 검증과 실제 실행

구성요소5종(코어 primary, 직접 Schur, 기존 pressure/model/adapter)이 통과했다.
독립 작은 전체 P1 행렬 대조·소거/복원, 비영 PH0·벽/alias/보완, alias별 다른 S,
압력만의 V forcing, zero primary, Budget/Invalid 경로와 기존 AD oracle 대응을 확인했다.
작은 계약시험을 표준600초 성능이나 전체 물리 정확도 근거로 확대하지 않는다.
개발 중 test/Direct-header 변경에 따른 partial build 정지는 보존했고, 최종 바뀐 객체만
재빌드했다. primary가 쓰지 않는 Direct ABI 변경은 별도 scope로 분리해 기록했다.

2개의 새 native case는 같은100km·질량40×80×64·h600·mode3·FP64 carry·EW/reuse OFF,
같은 초기장·필터·감쇠·outer budget/metric/허용오차로 각1회 수행했다. 각각 초기장에서
한 스텝이며 연속20분이나 장기 계산이 아니다. retry0, actual Torch intra/inter 각1이었다.
Ctrl source7개는 fresh(6구성요소+Newton), 24개는 영향 없는 source/header/object를 검증해
재사용했다. 정확한31-member 아카이브·헤더·Fortran/CFFI·동적 라이브러리·양쪽 링크를 확인했다.
후보는 같은 소스에서 Newton의 compile macro만 추가했다. 215/240개 pin을 독립 검증했다.

Control은 과거05f의174개 숫자 배열·v/z/Az/S와 바이트 일치했다. 양쪽 모두 rc0·한 스텝·
3개 암시적 단계·wrapper3/0, 유한/물리 허용 상태, 실제 native 잔차와 끝값8eps를 통과했다.
후보 단계 residual은 약6.3e-14였고 여섯 소유점 최대차는 U/T에서0, V7.45e-9m/s,
W5.82e-11m/s, PH1.49e-8m²/s², MU2.38e-7Pa였다. 실제 같은 v/U/context/S는 일치했다.
직접 경로에 P0 base가 없으므로 이전의 base_z 기록 검사는 재사용하지 않았다.

## 첫점 양성 선별의 정확한 범위

|판단량|P0 control|직접 P1|
|---|---:|---:|
|전체 scaled 첫 차이 norm|0.00268792|0.00262346|
|내부 V 소유점 L2|9.61521e-5|1.03476e-5|
|packed μ L2, Pa/s|1.20087e-4|1.19198e-4|
|packed scaled PH L2|1.51565e-4|2.85558e-4|
|첫 Arnoldi/JVP|21/23|13/14|
|스텝 전체 JVP|231|208|
|선택점 전체 P0 역산|21|0|
|X–연직 부분풀이|0|65|
|준비 포함 첫 구간 초|14.408770333|11.717400083|

13개 직접 M 적용은 내부 Arnoldi52회, XZ65회, core78회, 최종 packed 확장13회였다.
매 적용이 cap4 내에서 수렴했고 최대 실제 축약잔차0.00387721이었다. 별도 첫 P1 직접
검사는 scaled 상대4.1837e-7, non-V8.1231e-14였다. native A 잔차는 기존 FGMRES가
실제로 계산해 저장한 것이며 독립 재평가라고 부르지 않는다. W상단/내부 비증가와 V감소,
전체 차이 비증가·실제 field/work/cost 조건을 모두 통과해 **SCREEN_POSITIVE**다.

첫시간 감소18.6787%, 비율1.22969배다. 이는 첫점에서 사전 선언한 유효 단일 대조이지
일반 속도 인증은 아니다. 2배 목표는 별도 false이며 옛 A4 기준을 낮추지 않았다.
JVP 전체 감소9.9567%에서 첫 계 감소9회 외의14회는 두 번째 raw Newton 계48→34의
변화다. 이를 모두 직접 preconditioner의 보편적인 반복 감소로 해석하지 않는다.

전처리 wrapped M은 오히려2.12478→3.45657초로 늘었다. native A는8.68729→5.76088초,
나머지(준비 포함)는3.59670→2.49994초였다. JVP는 A에, physical M은 wrapped M에
포함되므로 중복 합산하지 않는다. 이전 A7 중첩 시간과 곱하거나 직접 속도비를 만들지 않는다.

## 전체 스텝 관측은 불리하다

RSL main time은152.37070→194.93518초(+42.56448초)다. 후보의5번째 선형계(stage4,
Newton0)는 JVP32회로 같지만 계측시간이12.7189→45.072031초였다. 직접 후보는 첫
stage2/Newton0에만 적용된다. 이후 입력/선형화점이 직접 캡처되지 않았으므로 같은 operator였다는
주장도, 특정 앱·swap·스케줄러가 원인이라는 주장도 하지 않는다.

기존 rank rusage는 control real161.46/user91.82/sys5.05초, 후보206.32/88.86/4.25초다.
CPU 합은96.87→93.11초, real−CPU 산술 잔여는64.59→113.21초다. 이것은 기다림이나
특정 자원의 독립 계측이 아니다. RSL과 rank 시간창도 달라 더하거나 빼서 phase를 만들지 않는다.
기존 로그만으로 원인을 분리하지 못했으므로 전체 실행시간 개선은 미확인으로 남긴다.
새 host profiler·전역 ps·모델 재실행은 하지 않았다.

## 그림·자료·남은 항목

[첫점 비용](evidence/pressure_v_direct_20261101/first_cost_stack.png)과
[전체 스텝 문맥](evidence/pressure_v_direct_20261101/whole_step_context.png)을 함께 봐야 한다.
[V/μ/PH rate 지도](evidence/pressure_v_direct_20261101/rate_defects.png),
[모든 scaled 블록](evidence/pressure_v_direct_20261101/scaled_blocks.png),
[실제 RK3/control/후보 W](evidence/pressure_v_direct_20261101/forecast_context.png)를 공통색 범위로 확인했다.
Rate는 임의 정규화된 Newton 방향이며 예측장·대기 에너지가 아니다. PH/T 작용 차이는
여전히 커지고 W가 후보 전체 scaled 차이 제곱의 약98.59%다. 이를 후반 noise 원인으로 부르지 않는다.
RK3는 같은 초기/10분 보관 결과를 재사용했고 새 RK3·MPFR·이전 중첩 후보·장기 run은 하지 않았다.

같은 전체 scalar 압력계를 매번 정확히 풀어야 한다고 가정하던 비용 구조는 바꿨지만,
현재 코드는 default-off 제한 연구 후보로만 유지한다. 6–12스텝/B/5일 확대는 승인하지 않았다.
전체 native 공간항 정합성, 물리 오차 예산,24–36시간 W 잡음,5일·300/150초 민감도와
실용 RK3 비용은 계속 미완료다. 유일·유효 출력은 보존하고 정확히 같은6파일만 읽기 전용
하드링크로 정리했다(114635027 논리바이트, 물리 APFS 확보량 아님).
[manifest](evidence/pressure_v_direct_20261101/manifest.json)에 실행/검산/그림/원본 해시를 고정했다.
