# 선택 수용 쌍의 전체 W 잔차 평가오차와 저장 입력 norm 차이

작성: 2026-11-10T19:43:00.092367+09:00.
기준 main3d4f7857(PR347), native source `d05130c0f30b5ce0397b2090a7103bf3aba7fcb3`.
Green·Red: GPT-6.1-Terra medium. 기본 OFF accepted-step 진단만 확장했다.
수치식·수용조건·dt600·ARK 계수는 바꾸지 않았다.

## 완료 범위와 남은 것

| 항목 | 결과 | 범위 |
|---|---|---|
| native B/K/R와 실제 fixed C | 직접 저장·검사 | 기존 수용 쌍 및 native Q 입력 digest와 대응 |
| 전체 W 잔차 평가오차 | 조건부 상계 확보 | 정의한 고정 binary B,a,C와 K의 해당 두 상태 |
| 저장된 전체 native R와 실제 metric의 norm 차이 nu | 상계 확보 | exact integer 제곱합·dyadic sqrt, backend gamma 미사용 |
| 다른 residual 블록의 평가오차 | 미완료 | U/V·NH4·PH·T·MU 등은 새 reference로 인증하지 않음 |
| 전체 e_i·정확한 merit 감소·Stage3 수렴·E/T/A/F | 미완료 | W와 nu가 작다는 이유로 나머지를0으로 놓지 않음 |

## 1. 무엇을 목표 실수 잔차로 정의했는가

저장된 B,a,K와 fixed C의 binary 값을 정확한 실수 입력으로 정의한다.
B는 stage base, a는 실제 stage coefficient, K는 rate vector다. 이 입력들이 만들어지기
**이전**의 stage assembly·grid/background initialization 오차는 이번 목표 밖이다.
반면 **평가 안의 B+aK 형성, EOS/upstream alb, Mdot, hybrid mass·비율·변환 및 K−F는
모두 실수식으로 재계산**한다. Rounded native Y나 native Mdot를 target 입력으로 고정하지 않는다.

\[
 Y=Q_{in}(B+aK),\qquad G_W^*(K)=K_W-Q_W F_W^*(Y;C).
\]

단위 map·dry WRFParity profile에서 M=mu+MUB, L_k=c1f_k*M+c2f_k다.
현재 projected U/V에서 source face 평균과 주기 seam·V wall을 사용해 coupled flux를 만들고,

\[
 D_m=\sum_k\left(-\frac1{rdnw_k}\right)
       [rdx\,\Delta_x CU_k+rdy\,\Delta_y CV_k]
\]

를 재계산한다. MU parity 경로가 실제 true인지 검사해 mean/clamp 대체 경로를 제외한다.
Source와 같은 원래 EOS 및 이상 alb(theta_base,p_base)를 방향 반올림으로 감싼다.
W의 coupled force는 내부층 g[-rdn_k(p_k−p_{k−1})−c1f_k*mu], top은
 g[2*top_rdnw*p_top−c1f_top*mu], coupled bottom은0이다. 최종 real W rate는
(G_k−w_k*c1f_k*D_m)/L_k다.

**바닥 coupled force의0을 최종 W rate의0으로 대신하지 않았다.** Product-rule 항은 바닥에도
남으며, Q는 수평 alias/copy와 V normal wall을 처리하고 W의 vertical bottom을 강제로0으로
만들지 않는다. 참조 코드와 제조검사가 nonzero bottom product를 확인한다.
Post-pack implicit divergence damping은 실제 켜져 있으나 source는 W를 그대로 repack한다.
이를 damping 전체 OFF로 잘못 분류하지 않았다. Advection/Coriolis/diffusion/Rayleigh는 explicit,
HEVI와 implicit W damping은 실제 지원 profile에서 OFF, mode3 fast callback은 Fphys를 더하지 않는다.

이 조건의 전체 W 수식을 평가하므로 이전 F_int의 native ratio/계수 형성 remainder와 state
formation을 W에 대해 다시 남기지 않는다. 다만 **고정 입력 B/a/C가 생성되기 전의 오차**, 다른
profile/숨은 context, 다른 RHS와 native norm은 별도 범위다. PDE/전체 시간지도의 정확도 정리가 아니다.

## 2. 부족했던 정보를 한 번의 관측에 저장

이전 run에는 EOS 입력과 계수 요약만 있고 full B/K/R, U/V/W state가 없어 full W를 직접
비교할 수 없었다. 기존 EOS TLS와 기본 OFF macro를 재사용해 exclusive w_reference 디렉터리에
B,K0,K1,R0,R1 전체 vector, 실제 theta_base,c1f,c2f, mass-flux RDNW의9개 NPY를 저장했다.
원 dtype/shape/data FNV, 실제 layout과 여섯 블록 Sinv min/max를 기록한다. 전체 가중치 배열은
저장하지 않는다. 추가 RHS/JVP/전처리 호출은 없다. 복제·검사·IO의 실제 비용은 있으므로
관측 run을 성능 비교에 사용하지 않는다.

Mass RDNW는 reciprocal 전에 실제 getter 출력에서 읽고 EOS snapshot이 context를 교체할 때도
preserve한다. Actual unit maps·dry qv/cqw·mode/parity/sanitize/transport/damping 조건을 검사한다.
pre-PGF W force가0인지와 source branch도 확인한다. 새로 포착한 theta_base는 재구성 후보가
아니라 실제 입력이며 이전 FNV-only 조건을 이 새 target에서는 직접 데이터로 대체했다.

Native Q 입력 연계는 **별도 검증**이다. NumPy의 FP64 multiply 다음 add로 native formed Y를
재구성하고 수평 Q/V normal walls/odd ghost signed zero를 적용해 실제 u/v/mu/MUB/w consumer
hash 및 EOS ph/t/mu bytes와 대조했다. 이것이 통과해도 그 rounded Y를 MPFR target으로 쓰지 않는다.
독립 reference는 여전히 exact B+aK를 계산한다.

## 3. 빌드·native 실행·입력의 연결

Pressure capture context layout 변경 때문에 dependency로 확인한5개 consumer를 matching
header/macros로 clean rebuild했다. Tile/Newton/pressure_stage_probe/native_pressure_reference/
v_primary_probe가 대상이며, 다른26개 archive member는 byte 동일하다. 기존 Fortran1270개 artifact,
18개 dylib entry, SDK/compiler/OS/linker가 일치했다. Fortran/C ABI·generated files는 바꾸지 않았다.
Helper assertions 활성 compile/run0과 단일 archive/map 직접 Tile/Newton method 출처465/466을 확인했다.

원 archive: `39eda6200136f045d3c316566cf28154f6793d20bdca45f046a00a9d545456fa`.
원 executable: `255356ecce70acfd7d41ab00f61dd3545aeb7d59f7a2cfcd82ea43a85e62c602`.

사전 source/analysis393개 pin을 고정한 h600 native1회는 exit86, errors=[]였다.
기존 base/accepted K/R·S/ref/context와16 EOS 파일이 일치했고 새9개 배열도 실제 hash와 대응했다.
profile은 unit maps,dry qv/cqw present zero, paritytrue,sanitize0,HEVI/transport/W dampingoff였다.
Stage2 CONVERGED 로그만 있고 Stage3 첫 수용 갱신 뒤 종료했으며 완료 timestep0이다.
초기21장·계수의 archived A9/RK3 비교는 통과했다. 새 RK3·endpoint 예보장·공정한 속도 비교는 없다.
rank RSS2097954816bytes로 등록4GiB 이하였고 native timeout/디스크 조건도 통과했다.

## 4. W 전체 잔차의 직접 구간 비교

MPFR256 RNDD/RNDU로 exact Q(B+aK), 현재 mass continuity, EOS와 W 변환을 계산한 뒤
저장된 native R_W와 **직접** 비교한다. 그러므로 native W arithmetic·입력 및 계수 형성·
backend mass sum·마지막 residual 뺄셈 오차가 이 정의한 W residual의 차이에 함께 포함된다.
Native kernel의 연산 수나 gamma bound를 추정해 넣지 않았다. MPFR와 source/입력 대응의
신뢰 가정 및 guard가 성립하는 profile에 대한 조건부 pointwise 상계다.

| 상태 | W residual physical Linf 상계(m/s²) | physical L2 상계 | 가중 수학적 W RMS 상계 |
|---|---:|---:|---:|
| before | 5.348845419454588e-13 | 4.968558225608559e-11 | 2.3907853519229967e-13 |
| accepted | 5.433428941262796e-13 | 4.9482251846932426e-11 | 2.4285918370791415e-13 |

전체 W 원소215865개씩(aliases 포함)을 검사했다. 실제 W inverse-weight 최대1과
sqrt(Nw/N)의 위쪽 MPFR 값으로 mathematical RMS 상계를 얻었다. 이 열은 native norm routine의
오차가 아니다. 엄격한 숫자는 RNDU binary64와 exact hex이며 decimal을 exact-rational upper로
다시 해석하지 않는다. 다른 K·stage·profile 전체에 같은 상계를 일반화하지 않는다.

Noninteger exponent 제조 grid의 초기 planning timing은3.31s였고, sign-definite power의
monotone endpoint 선택(8→2 pow calls)과 fixed ideal-alb cache 후0.29s였다. Four-corner 원래
구간과 optimized 구간의 exact MPFR endpoint 동등성을 검사했다. 이 추정은 full-grid 보증이
아니었다. 실제1회 reference는63.21s/종료0, input pin불변으로 완료했다. Rusage52MB는
wrapper 기록이며 C++ child peak memory 상계로 주장하지 않는다. 이 시간은 WRF 속도비가 아니다.

Reference launcher의 최초 Python parse typo는 등록된 계산을 시작하기 전에 고쳤고 원문·오류를
보존했다. Native/MPFR 알고리즘·판정 조건을 바꾼 실패 실험 대체가 아니다. 계산은 각각1회다.

## 5. Native norm 차이 nu는 정수 산술로 별도 제한

저장된 전체 R0/R1와 실제 여섯 metric block의 양의 min/max를 사용한다. Binary64 각 수의
integer ratio로 제곱합을 정확히 누적하고 block별 가중치 제곱으로 아래/위 sum을 계산한다.
현재 모든 블록 min=max여서 실제 stored metric이 결정된다. Nonuniform 경우에도 extrema로
norm interval을 얻는 방법이며 가중치 전체를 임의의 상수로 대체하는 계산이 아니다.

Squared norm q에 대해 m=isqrt(floor(q*2^512)), [m,m+1]/2^256(정확 제곱이면 동일 endpoint)가
sqrt enclosure를 준다. Reported native RMS를 exact binary number로 놓고 두 끝점까지의 거리
최댓값으로 nu를 제한한다. File SHA와 실제 data-byte FNV, residual pairing, 여섯 연속 block의
완전한 N cover 및 H=I를 검사했다. Backend operation count·reduction 방식·sqrt ULP를 가정하지 않았다.

| 상태 | norm·scaling·반환 변환 차이 nu 상계 |
|---|---:|
| before | 7.583206822826292e-11 |
| accepted | 7.827930118980624e-18 |

이것은 고정된 **stored R/S**의 mathematical norm과 실제 반환값의 차이다. W를 포함한
물리 residual의 모든 평가오차 epsG와 구분한다. E_i를 얻을 때 epsG+nu로 한 번만 합산해야 한다.

## 6. W 블록에 한정한 엄격한 감소의 보조 결론

Stored R_W의 mathematical norm도 같은 정수 제곱합과 dyadic sqrt로 감쌀 수 있다.
Full-W 평가오차 epsilon_W와 역삼각부등식을 사용하면 target W merit의 구간을 얻는다.
이 계산은 전체 native RMS를 사용하지 않으므로 global nu를 다시 더하지 않는다.

| 상태 | target W merit 하한 근삿값 | 상한 근삿값 |
|---|---:|---:|
| before | 6.054728407556887e-5 | 6.0547284553725934e-5 |
| accepted | 1.0546185884616899e-9 | 1.0551043068291057e-9 |

정확한 Fraction에서 before 하한−accepted 상한은 양수(약6.0546228971262035e-5)다.
따라서 **정의한 fixed-input/profile의 이 두 상태에서 W 블록 mathematical merit는 엄격히
감소했다**는 조건부 결론을 얻는다. 이것은 전체 residual merit의 감소나 Stage3 반복 수렴이 아니다.
Manifest의 packet origin, 두 endpoint, 실제 positive weight와 reference/data hash를 다시
검사했고 Red가 다른 integer grouping으로 분리 margin을 독립 계산했다.

## 7. 검증과 다음 미완료 조건

Source path·term completeness와 Q indices를 Green/Red가 독립 검토했다. 제조검사는 continuity,
periodic seam/V-wall, nonzero bottom product, interior/top pressure 부호, nonzero K formation,
잘못된 residual/domain/profile 및 EOS/mass RDNW bit 차이를 겨냥했다. Normalizer의 signed zero,
producer digest 검증도 통과했다. Norm 제곱합은 subnormal을 포함한 exact 값과 sqrt enclosure를
검사했다. Red는 실제 norm을 다른 정수 grouping으로 재계산해 같은 nu를 얻었다.
MPFR 숫자 postreview는 코드·데이터/65층 출력의 검토이며 별도 독립 구현의 전수 재실행은 아니다.

정의한 고정 B/a/C에서 **W 잔차 평가오차와 stored-input norm gap**은 채웠다. U/V primary·NH4,
PH·T·MU 등 다른 블록의 epsG가 없어 전체 e0/e1나 merit 감소는 아직 인증하지 않는다.
과거 P1 NO-GO와 완료/미완료 범위는 바꾸지 않는다. Stage3 반복, JVP/forcing·inverse/Lipschitz,
시간적분·W 잡음·실용 비용·5일 E/T/A/F는 별도 미완료다.

[재현 및 입력 정의](evidence/w_error_20261110/README.md) ·
[증거 manifest](evidence/w_error_20261110/manifest.json).
