# PR308 fine-pair W trajectory: independent Red audit

Local timestamp: 2026-10-15 10:00:32 +0900  
대상: 기존 h=.9375 s (16 steps) / h=.46875 s (32 steps), 0–15 s native ARK runs. 48 accepted-step CSV를 읽었으며, 모델 추가 실행·빌드·production source edit는 없다. 계산 결과는 [analysis JSON](</Users/yhlee/AD-SDIRK3-review-evidence/fine-pair-w-trajectory-20261015/red_precision_fine_pair_analysis.json>), raw derived arrays는 [NPZ](</Users/yhlee/AD-SDIRK3-review-evidence/fine-pair-w-trajectory-20261015/red_precision_fine_pair_arrays.npz>), 재현 코드는 [audit script](</Users/yhlee/AD-SDIRK3-review-evidence/fine-pair-w-trajectory-20261015/red_precision_fine_pair_audit.py>)에 저장했다.

## 실행·시간·owner 검증

두 receipt 모두 completed/return code 0, expected 16/32 CSV, 연속 `step_seq`, exact `dt`, accepted flag 전부 true, solver failed step 0을 기록한다. 16-step run의 RSL solver count는 implicit 48, fine run은 96이다. Carry 기록은 각각 host/internal/publication `1/15/16`, `1/31/32`; run 내 연속 CSV의 input state는 앞 step의 projected endpoint와 각각 15/31번 exact-equal이다. 두 실행은 같은 executable `5bf90cd9…a2a7769`, C++ archive `dd87b49a…5f32c5`, Fortran archive, W308 source hash와 e71 input `e71b730a…00dd2a9`를 사용한다.

공통 시각은 run 시작 t=0과 accepted step 번호·CSV의 exact dt에서 `t_j=j·0.9375` 및 fine index `2j`로 구성했다. 두 NetCDF receipt는 t=0/15 s만 제공하므로 중간 시각을 NetCDF에서 보간하지 않았다. 분석 W mask는 internal `[j,k,i]` 17×17×17 저장에서 `j=0..15, i=0..15, k=1..16`만 사용한다. 고정 bottom 제외, dynamic top 포함, 총 4096 owner (interior 3840 + top 256)다. 두 초기 W는 FP32 view에서 bitwise 동일하고 exact zero다. M0/e1은 frozen E307 NPZ를 hash 확인 후 그대로 사용했으며, `e1`의 M0-weighted RMS는 1이다.

실제 runtime metadata는 두 run에서 동일하게 `kdamp_config=kdamp_grid=.2`, `newton_tol=krylov_tol=1e-7`, `effective_ewt_rtol=1e-6`, mass-coordinate mode 1, effective WRF Omega/ww-cp on, top lid off, physics vector exact zero로 기록됐다.

## 수치 결과

17개 common times 모두에서 W trajectory difference `D_W=fine−coarse`의 unweighted RMS가 고정 protocol의 descriptive `ε/3=2.5981466e-6 m/s`보다 작았다. 이 trigger는 reference qualification이나 truth bound가 아니다. `D_W` RMS는 t=.9375 s에서 `2.48872e-7`, t=7.5 s에서 `1.20713e-7`, t=15 s에서 `2.70515e-7 m/s`였다. t=15 최대 절대 차이는 `1.36256e-6 m/s`; M0-weighted RMS는 `2.23218e-7 m/s`다. M0 방향 projection `p_delta`는 t=.9375 s의 `−1.01445e-7`에서 t=12.1875 s의 `+2.22307e-7`, t=15 s의 `−5.16449e-8 m/s`로 부호가 바뀐다. 이 15초 구간과 17개 표본은 하나의 진동 주파수/위상을 특정하기에 충분하지 않다.

각 implicit stage에서 직접 계산한 accepted-Y defect는 `G_i,W=Y_i,W−B_i,W−(dt·a_ii)F_I,W(Y_i)` (m/s), accepted method residual은 `R_i,W=K_{fast,i,W}−F_I,W(Y_i)` (m/s²)다. stage 1은 explicit seed여서 Newton defect가 정의되지 않아 직접 solver bound에서 제외했다. Fixed physical 4096 owners에서 각 run의 최대 per-step stage RMS와 local increment sum은 다음과 같다.

| Run | `max_stage RMS(G_W)` | `max_stage RMS(R_K,W)` | `Σ_steps [dt Σ_{i=2..4}|b_i| RMS(R_i,W)]` |
|---|---:|---:|---:|
| h=.9375 s | `1.44026e-14 m/s` | `3.52466e-14 m/s²` | `8.22801e-13 m/s` |
| h=.46875 s | `1.57258e-7 m/s` | `7.69693e-7 m/s²` | `4.37233e-7 m/s` |

Fine run의 직접 defect 합은 거의 전부 첫 macro step에서 왔다: step 1의 local contribution `4.3723212e-7 m/s`; 이후 31 step의 합은 약 `7.10e-13 m/s`다. 그 첫 step에서 stage 3/4의 final scaled Newton residual은 `7.7124e-8` / `4.0531e-8`, both below actual `1e-7` threshold. Coarse 첫 step의 local sum은 `4.61845e-14 m/s`다.
첫 fine macro의 accepted-point detail은 stage 2 `K−FI` RMS/max `2.38830e-14 / 8.79945e-14 m/s²`, stage 3 `7.69693e-7 / 6.60097e-6 m/s²`, stage 4 `4.23942e-7 / 3.06981e-6 m/s²`다. 이에 대응하는 `G_W` RMS/max는 각각 `4.87959e-15 / 1.79784e-14`, `1.57258e-7 / 1.34866e-6`, `8.66167e-8 / 6.27200e-7 m/s`다. Stage 3/4에서 Newton은 1 iteration 후 Krylov 0으로 멈췄고 scaled residual은 `7.71242e-8`, `4.05312e-8`로 실제 `1e-7` 기준 아래였다. 이들은 fine-pair 첫 상태의 observed solver defects이며, h15 E307의 다른 `G_W`와 섞지 않는다. 1e-9 single-fine tolerance control이 실행되면 baseline CSV를 다시 읽지 않고 frozen JSON/NPZ를 재사용해 stage 3/4 residual, `D_control=W_control−W_coarse`, `D_baseline + (W_control−W_baseline_fine)` 벡터 항등식, fixed M0/e1 norm/projection/cosine을 대조할 [별도 offline 비교 스크립트](</Users/yhlee/AD-SDIRK3-review-evidence/fine-pair-w-trajectory-20261015/red_precision_newton1e9_control_audit.py>)를 준비했다. control이 baseline delta와 정렬되어도 solver tolerance가 trajectory 차이의 유일 원인이라는 뜻은 아니다.
첫 fine macro의 stage 3/4는 Newton iteration 1에서 accepted됐고 Krylov iteration은 0이었다. Stage 3의 accepted-Y `G_W` RMS/max는 `1.57258e-7 / 1.34866e-6 m/s`, accepted `K−FI` RMS/max는 `7.69693e-7 / 6.60097e-6 m/s²`; scaled final Newton residual은 `7.71242e-8`이다. Stage 4는 `G_W` RMS/max `8.66167e-8 / 6.27200e-7 m/s`, accepted `K−FI` `4.23942e-7 / 3.06981e-6 m/s²`; scaled residual `4.05312e-8`다. `G/a`와 `K−FI` 일치는 같은 stage equation의 두 표현이며 별도 독립 오라클/증거 두 개로 세지 않는다. Stage 4 Newton-returned-K snapshot은 없지만 checked `Y`와 same-point `FI`는 exact state/digest gate를 통과한다; 따라서 accepted method `G/R`는 측정 가능하고 returned-K parity는 미관측으로 둔다.

17개 common-time trajectory 차이와 양쪽 run의 누적 local defect 합을 비교하면 t=15 s의 `D_W RMS=2.70515e-7 m/s`, pair local sum=`4.37234e-7 m/s`다. 이 합은 각 step의 직접 tableau residual increment만 더한 진단이며 stage/step 이후의 nonlinear trajectory amplification을 포함하지 않으므로 global state-error bound가 아니다. 국소 solver defect가 trajectory difference의 원인이라고 결론내리지 않는다.

Accepted-Y point flag를 확인한 implicit 144 stage records 모두에서 `checked_state==Y`, digest 일치, FI와 Full RHS call argument가 Y, FE checked state가 Y였다. `F_full−F_I−F_E`는 captured W owner에서 max abs `4.11e-20 m/s²` 아래다. 별도 내부 context `U_ref_stage`는 FI/Full에서 B와 일치하고 Y와는 다르며, slow/FE에서는 Y와 일치한다. 따라서 closure 숫자는 실제 captured primitive 관계로만 보고한다. Stage 4 `Newton returned_K` snapshot이 없는 경우도 있어 returned-K parity라고 부르지 않는다; accepted-Y G와 final-method K−FI는 checked Y/FI로 직접 계산했다. 이 결과는 stage residual `G/a`와 `K−FI`가 같은 식의 두 표현임을 점검하는 closure이지, 두 개의 독립적인 solver 증거는 아니다.

## 해석 경계

이 audit은 짧은 구간의 actual trajectory divergence, accepted method defect, 그리고 tableau direct increment를 나란히 측정했다. `ε/3`은 descriptive trigger뿐이며 pass/fail 기준이 아니다. Richardson/order, 정확도·truth bound, frequency/phase, 또는 물리 원인 주장을 하지 않는다. Negative quadratic/work 값은 W-only quadrature defect일 수 있으므로 energy damping truth로 읽지 않으며, 여기서는 전체 에너지 closure를 계산하지 않았다. `D_W`와 local defect 누적치의 크기 비교도 propagation을 생략한 진단이다.
