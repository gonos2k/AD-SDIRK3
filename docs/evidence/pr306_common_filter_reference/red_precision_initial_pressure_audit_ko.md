# PR306 t=0 CPP W 압력항 독립 검토

Local timestamp: 2026-10-14 06:38:00 +0900
대상: `/private/tmp/pr306-common-filter-reference-20261014`, HEAD `5dc69a35823a5af8f6d5e24978933a25ee2b7081`. 오프라인 capture 재계산과 source 대조만 했다. 모델·빌드·생산 소스 변경은 없다.

## 검증

기존 분석기 `green_pressure/analyze_initial_cpp_w_pressure_force.py`를 기존 t=0 Full/PGF sidecar에 재실행했다. 재생성 JSON은 저장 분석과 동일한 SHA256 `93a44ee1…d476fd9`로 일치한다. Full과 PGF의 U,V,W,PH,T,MU 입력 상태는 모두 float32 투영 뒤 bitwise 동일했고, 입력 `MU`도 캡처 상태와 일치한다. 따라서 재구성은 다른 상태를 섞지 않았다.

C++ W 내부층 원식은 pressure difference와 column-mass 항의 결합이다(소스 [interior PGF](</private/tmp/pr306-common-filter-reference-20261014/external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp:21208)); top은 `2*|RDNW|*p_lower` 형태의 별도 경계식이다([dynamic-top PGF](</private/tmp/pr306-common-filter-reference-20261014/external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp:21604)). Fortran 기준 식도 같은 interior/top 구조다([module_big_step_utilities_em.F](</private/tmp/pr306-common-filter-reference-20261014/dyn_em/module_big_step_utilities_em.F:2491)). dry e71 입력에서는 `QVAPOR=0`, `MAPFAC_M=1`, `C1F=1`, `C2F=0`; W는 t=0에 0이므로 product-rule의 `-W·Mdot/M` 항은 0이다. 중력 상수는 source의 `9.81f`를 반영해 `g=9.8100004196167 m/s²`로 사용했다. 해당 물리화에서 face layer mass `L_f`는 평균 `89091.93 Pa`이고, 예측 가속도는 `g*(pressure-gradient − C1F*MU)/L_f`다.

물리 W 소유점은 배열 `(j,k,i)=(16,16,16)`, 총 4096개다. 고정 하단 face `k=0`은 제외하고 동적 top `k=16`은 포함했다: interior `k=1..15` 3840개, top 256개. 같은-call C++ 진단 압력 sidecar `p_pert_mass`로 계산한 압력+column-mass 항은 실제 Full W RMS `1.09772334e-5 m/s²`에 대해 `1.09361965e-5 m/s²`였고, cosine은 `0.9980567`이다. 차이 residual은 RMS `6.84301420e-7 m/s²` (actual의 약 6.23%), max `2.90457e-6`; residual은 수직 이류, mixing/cross-stress 등 다른 W 항을 포함할 수 있으므로 압력항만으로 전부 설명한다고 보지 않았다. 동적 top에서는 실제/예측 RMS가 각각 `8.0308564e-6`/`8.0308563e-6`, residual RMS `2.65e-13 m/s²`로 거의 일치한다.

비교용으로 raw input NetCDF P를 같은 식에 넣으면 예측 RMS가 `1.18285e-10 m/s²`, actual과의 cosine은 `−0.0365`다. 이는 C++ Full RHS가 입력 P를 그대로 압력항에 사용하지 않고 실제 Full call에서 재진단한 `p_pert_mass`를 사용한다는 source/sidecar 관계와 부합한다. raw P 결과를 actual pressure prediction으로 바꾸어 부르지 않았다.

## 범위

이 검증은 **한 번의 첫 Full 진단 호출(t=0)**에서 pressure+column-mass W 부분항을 독립 재구성한 결과다. 그 시점의 실제 W RHS 대부분과 방향이 일치한다는 증거이며, residual은 별도 물리항으로 남겨둔다. 이는 240초 h-family의 누적 W 차이에 대한 원인 분해가 아니고, 시간 적분 중 압력 오차가 얼마만큼 기여하는지 확정하지 않는다. h-family 검토와 혼합해 “240초 W 차이가 압력/EOS 때문”이라고 주장할 수 없다.

재실행 산출물(임시 경로): `/tmp/red_precision_initial_cpp_w_pressure_force_recheck.json`, SHA256 `93a44ee1…d476fd9`. 입력 sidecar SHA256: Full `b55f4537…c9c3ce07`, PGF operands `af37bae3…dc5fdd`. 분석기 SHA256 `d06e01bb…25f03103`.
