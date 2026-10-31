# A5 기여 분리와 V행 보정 후보: NO-GO

작성: 2026-11-01T04:28:19.702319+09:00.
관측 소스: `05f28d1`; 후보 실행 소스: `112f3449`; 수치 hook 원복: `796f36c9`.
Green·Red는 GPT-6.1-Terra medium을 사용했다.

## 결과와 범위

같은 실제 선형화점과 방향 z에서 V-primary와 참조 P0의 차이를 분리했다.
단위계수 작용은 그 방향의 V 모델 차이 제곱을 약98.84% 줄였다. 그러나 이를
역작용의 V행에만 더한 후보는 전체 scaled 첫 방향 차이를36.5% 키웠고,
첫 선형계 JVP도 줄이지 못했다. 후보는 **NO-GO로 종료**하고 수치 hook를 원복했다.
검증된 prepared 선형 작용과 진단은 보존한다. 운영 성능·후반 W 문제는 미해결이다.

## 실험 기록

모든 native 실행은 같은 입력 `c3e566…`, 100km, 질량격자40×80×64, h600초,
mode3, FP64 carry, reuse OFF, Newton1e-9/상대0, 고정 선형 FP32(1e-7)을 사용했다.
네 번 모두 독립 초기장의 한 스텝이며, 연속40분이나5일 계산이 아니다.

|실행|모델 결과|진단·선별 결과|
|---|---|---|
|fe1 관측|종료0, 한 스텝 완료|float/double 기록기 오류로 INCONCLUSIVE|
|05f 기록기 수정|종료0, z와 문맥 확보|원래 FP64 차분1/3 통과, INCONCLUSIVE 유지|
|112f 무보정 제어|종료0, 기존 상태·끝값 일치|CONTROL_COMPLETED|
|112f 단일 V행 후보|종료0, 수렴·8eps 통과|전체 차이·첫 작업량 기준 미달, NO-GO|

기록기 수정은 상수의 명시적 `std::vector<double>` 변환뿐이다. 옛 결과·사전규약·pin을
수정하지 않았다. 집중 검사는 실제 bound P0의 run(), 두 파일 역독해, 상수9개의
FP64 값과 순서, overwrite, projection/cqv 거부, 두 방향 FWAD, 선형성, 계수 보유와
V-only nilpotence를 matching archive에서 통과했다.

## 같은 방향의 분해와 차분 검증

실제 FGMRES의 scaled v,z,Az와 S를 저장했다. 같은 P0의 forward action으로
`e=v−Az=(v−P0z)+(P0−A)z`를 물리·scaled 좌표의 모든 packed 행에서 확인했다.
Scaled 역산 잔여의 상대 norm은3.7984e-14다. 같은 호출의 EOS·실제 cqv·질량 변환·기하와
native Q를 사용했으며 원식의 primal 값도 재현했다. `torch::equal`은 signed zero를
구분하지 않는 정확 수치 일치이고, byte 비교는 원시 벡터·입력·끝값에서 별도로 수행했다.

해석 세 항 합과 source FWAD의 오차 norm은5.0138e-22다. 원래 FP64 중앙차분은
[.01,.001,.0001]초에서 오차 norm이[8.81e-12,8.95e-11,8.82e-10]으로 커졌고,
2/3 조건을 실패했다. 이 값은 미분의 섭동 폭이며 모델 time_step을 바꾼 것이 아니다.
원래 실행의 INCONCLUSIVE와 FD flags는 그대로 남긴다.

별도로 사전등록한 offline MPFR160-bit 검증은 같은 자료·세 폭·허용오차를 유지했다.
변하는 face mass와 분모를 재계산하고 coupled helper와 물리 변환을 원래 순서로 평가했다.
폭마다202240개 V 내부점에서 실패0, 최대 절대오차3.06e-23, L2 오차4.54e-22였다.
미리 정한47개 표본의256-bit 확인도 모두 통과했다. 실행시간은20.24초다.
이는 중간 반올림에 대한 독립 근거이며, 원래 FP64 실행의 통과나 모델 정밀도 변경이 아니다.
독립 NumPy의 세 작용도 저장된 값과 일치했다.

`m=(P0−A)z`, `c=a(Jprimary−Jref)z`에서 단위계수 제곱비는0.0116284,
cosine은0.99417이다. 맞춘 gain은 사용하지 않았다. 직접 δΦ항의 signed projection이
큰 부분을 설명한다. 이 결과는 한 점·한 방향에 한정되며 NH4·질량 product-rule·Omega나
24–36시간 예보 잡음의 인과를 대신하지 않는다.

## 단일 보정의 전체 효과

Prepared 계수는 선택된 Newton점에서 한 번 준비하고 각 실제 P0 방향에 적용했다.
`δz_V=aJprimary(z)−(z−P0z)_V`만 더했으며 첫 방향을 재사용하지 않았다.
원래 A·FGMRES·허용오차·외부 S변환·guard와 다른 다섯 Newton 선형계는 유지했다.
고정된 제어→후보 각1회(max2)를 재시도 없이 수행했다. 추가 A/RHS shadow 호출은 없다.

|지표|제어 P0|V행 후보|
|---|---:|---:|
|전체 packed scaled 첫 차이 norm|0.00268792|0.00366784|
|물리 소유 내부 V L2|9.61521e-5|1.03686e-5|
|packed μ행 L2 (Pa/s)|1.20087e-4|2.44494e-3|
|packed Φ행 scaled L2|1.51565e-4|5.45396e-4|
|첫 선형계 JVP|23|23|
|한 스텝 JVP 합계|231|217|
|준비 포함 첫 구간 ms|6044.34|3648.48|

전체 차이 비증가와 첫 JVP 엄격감소 조건을 실패해 **NO-GO**다. V 내부 차이 감소,
W 상단·내부 비증가, 수렴·초기상태·물리 허용성·guard·끝값8eps 조건은 통과했다.
단일 쌍의 시간과 전체 JVP 감소만으로 원인 가속이나 전체 예보 속도를 주장하지 않는다.
첫 차이 norm은 조건수·수렴의 필요충분조건이 아니라 사전 고정한 보수적 선별 지표다.
실제 backend thread 수는 독립 측정하지 않았고 MPI1rank·OMP/BLAS 환경값1을 고정했다.

Physical M은 wrapped M의 포함 항, 기존 JVP 시간은 scaled A의 포함 항이므로 중복 합산하지
않는다. 준비·실제 적용·첫 방향 복사 비용은 포함했고, 구간 뒤 관찰·직렬화 시간은 별도다.
원래 FGMRES가 저장한 최종 실제 잔차는 같은 허용오차를 통과했으나 독립 재평가는 아니다.

## 질량 행의 증가를 설명한 원래 작용

실제 δz는 V성분만 바뀌었다. 고정 질량에서 native continuity는
`Jμ(δV)=msfty Σ_k[msftx*dnw_k*rdy*Δ_y(H_v δV/msfvx)]`, `dnw=−1/rdnw_abs`다.
따라서 `e_new,μ=e_old,μ+aJμδV`다. 이 예측과 실제3321개 packed μ행의 상대 L2 차이는
4.0e-16, 최대 절대차는1.22e-19였다. 이는 같은 방향의 μ행 변화를 설명하며,
전역 질량누출이나 Φ 증가의 모든 항을 입증한 것은 아니다.

참조 역산은 μ·온위·Φ를 V와 결합해 복원한다. 그 뒤 V만 바꾸면 그 연동을 반영하지
못한다. 외부 solve는 원래 A를 수렴시켜 끝값 screen을 통과했지만 preconditioner의
작용 차이는 다른 행으로 전파됐다. 다음 설계는 source-consistent V 작용을 연속방정식·
압력·지오포텐셜과 결합한 역작용에서 다뤄야 한다. 이번 후보에 다른 항을 계속 더하지 않았다.

## 그림·코드 상태·남은 작업

[세 항 기여](evidence/pressure_a5_20261101/signed_components.png),
[모든 scaled 행](evidence/pressure_a5_20261101/scaled_blocks.png),
[V 개선과 μ/Φ 악화](evidence/pressure_a5_20261101/rate_defects.png)를 확인했다.
[실제 RK3·제어·후보 W](evidence/pressure_a5_20261101/forecast_context.png)는
보존된 RK3와 같은10분에서 native 높이와 공유 scale로 따로 비교했다.
방향 벡터는 임의 정규화된 Newton rate이며 실제 예측장·대기 에너지가 아니다.

미채택 수치 hook는 원복했다. 현재 Newton은05f 본문과 byte가 같고 퇴역 macro의
fail-fast prefix만 있다. 후보112f source/exe/단계/원시자료와 검증된 PreparedPrimary·
진단을 보존한다. Fortran/Registry/CFFI는 바꾸지 않았고 matching C++ 객체·header·
archive30멤버·양쪽 link를 확인했다. 새 모델이나 다른 보정·5일 run은 하지 않는다.
같은 벡터·끝값 파일은 SHA 확인 후 read-only hard link로 중복 저장을 줄였으며
기존 경로와 pin은 유지했다. 고유한 한 점 문맥은 유효 분석 근거로 보존한다.

A5의 이번 기여 분리·단일 후보 평가는 끝났다. 실용 비용, 연속6–12스텝 선별,
5일·300/150초 민감도, 물리 정확도 예산, 전체 공간 연산자 정합성, 후반 W 잡음은 미완료다.
[manifest](evidence/pressure_a5_20261101/manifest.json)에 원본/공개 해시와 실험별 상태를 기록했다.
