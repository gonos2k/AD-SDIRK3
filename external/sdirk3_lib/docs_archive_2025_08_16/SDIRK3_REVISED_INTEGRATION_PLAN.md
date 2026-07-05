# SDIRK3 수정된 통합 계획 (Revised Integration Plan)
## libtorch_wrf/sdirk3 기반 최적화 및 검증 전략

**Date**: 2024-01-16  
**Version**: 2.0 (Revised)  
**Critical Update**: libtorch_wrf/sdirk3는 이미 올바른 (j,k,i) 메모리 레이아웃을 구현하고 있음

---

## 🔄 계획 수정 사유

### 새로운 발견사항
1. **libtorch_wrf/sdirk3는 이미 올바른 (j,k,i) 레이아웃 사용**
   - `wrf_memory_layout.h`에서 확인
   - Fortran (i,k,j) ↔ C++ tensor[j,k,i] 변환 정확히 구현
   - 스태거드 그리드 올바르게 처리

2. **WRF-Fortran 인터페이스 정교하게 구현됨**
   - 메모리 레이아웃 변환 불필요
   - 제로카피 뷰 이미 최적화

3. **주요 작업 방향 변경**
   - ~~메모리 레이아웃 마이그레이션~~ → **버그 수정 및 최적화**
   - ~~두 코드베이스 병합~~ → **libtorch_wrf/sdirk3 개선**

---

## 🎯 수정된 목표

### Primary Goals
1. **버그 수정**: 알려진 및 잠재적 버그 해결
2. **성능 최적화**: em_b_wave 수행 속도 개선
3. **정확도 검증**: em_b_wave 결과 정확도 확보
4. **Gradient Flow 보존**: .item() 제거 완료

### Success Metrics
- ✅ em_b_wave 12시간 안정적 실행
- ✅ RK3 대비 3배 큰 timestep (dt=180s)
- ✅ 총 실행시간 50% 단축
- ✅ 질량/에너지 보존 오차 < 1e-10

---

## 📋 현황 분석

### libtorch_wrf/sdirk3 강점 ✅
```cpp
// 이미 구현된 핵심 기능들
✓ 올바른 메모리 레이아웃 (j,k,i)
✓ WRF-Fortran 인터페이스
✓ Newton-Krylov 솔버
✓ WSM6 마이크로피직스
✓ 타일 병렬화
✓ 헤일로 교환
✓ 제로카피 뷰
```

### 확인된 이슈 ❌
1. **일부 .item() 사용 잔존**
   - GMRES 솔버: 일부 수정 완료
   - Newton 솔버: 추가 수정 필요
   - 플럭스 계산: 벡터화 기회

2. **잠재적 버그**
   - 수렴 조건 체크
   - 경계 조건 처리
   - 물리 텐던시 커플링

3. **성능 병목**
   - 불필요한 메모리 복사
   - 비효율적인 루프 구조
   - 캐시 미스 패턴

---

## 🛠️ 작업 계획 (4주 단축)

### Phase 1: 버그 수정 및 검증 (Week 1-2)

#### 1.1 .item() 제거 완료
```cpp
// 남은 파일들 수정
✓ wrf_sdirk3_jvp_autograd.cpp (완료)
⏳ wrf_sdirk3_newton_solver.cpp
⏳ wrf_sdirk3_unified_rhs.cpp
⏳ wrf_sdirk3_preconditioner.cpp

// 수정 패턴
// OLD: if (norm.item<float>() < tol)
// NEW: if ((norm < tol).all().item<bool>())
```

#### 1.2 수렴 조건 버그 수정
```cpp
// Newton solver convergence fix
class NewtonSolver {
    bool check_convergence(const torch::Tensor& residual) {
        // 상대 오차와 절대 오차 모두 체크
        torch::Tensor rel_error = residual.norm() / initial_residual.norm();
        torch::Tensor abs_error = residual.norm();
        
        torch::Tensor converged = (rel_error < rel_tol) | (abs_error < abs_tol);
        return converged.all().item<bool>();
    }
};
```

#### 1.3 경계 조건 검증
```bash
# 경계 조건 테스트 스크립트
cd /Users/yhlee/dWRF/external/libtorch_wrf/sdirk3
./test_boundary_conditions.sh
```

### Phase 2: em_b_wave 환경 구축 (Week 2)

#### 2.1 테스트 케이스 준비
```bash
#!/bin/bash
# setup_em_b_wave.sh

# WRF 테스트 디렉토리 생성
mkdir -p ${WRF_ROOT}/test/em_b_wave_sdirk3
cd ${WRF_ROOT}/test/em_b_wave_sdirk3

# namelist 설정
cat > namelist.input << EOF
&time_control
 run_days                = 0,
 run_hours               = 12,
 run_minutes             = 0,
 run_seconds             = 0,
 time_step               = 180,  ! 3x larger than RK3
 time_step_fract_num     = 0,
 time_step_fract_den     = 1,
 max_dom                 = 1,
 s_we                    = 1,
 e_we                    = 41,
 s_sn                    = 1,
 e_sn                    = 41,
 s_vert                  = 1,
 e_vert                  = 41,
/

&dynamics
 rk_ord                  = 3,
 use_sdirk3              = .true.,
 sdirk3_max_newton       = 4,
 sdirk3_newton_tol       = 1e-6,
 sdirk3_gmres_restart    = 30,
 sdirk3_use_autograd     = .false.,  ! Start with FD
/

&physics
 mp_physics              = 0,  ! No microphysics for initial test
 ra_lw_physics           = 0,
 ra_sw_physics           = 0,
 sf_sfclay_physics       = 0,
 sf_surface_physics      = 0,
 bl_pbl_physics          = 0,
 cu_physics              = 0,
/
EOF
```

#### 2.2 초기 조건 생성
```bash
# ideal.exe를 사용한 초기 조건 생성
./ideal.exe

# 초기 파일 확인
ncdump -h wrfinput_d01 | grep -E "U|V|W|T|P"
```

### Phase 3: 성능 최적화 (Week 3)

#### 3.1 프로파일링
```cpp
// performance_profiler.cpp
class PerformanceProfiler {
    struct Timings {
        double newton_solve;
        double gmres_solve;
        double rhs_computation;
        double physics_tendencies;
        double halo_exchange;
    };
    
    void profile_timestep() {
        auto start = high_resolution_clock::now();
        
        // Newton solver
        auto t1 = time_section([&]() { newton_solver.solve(); });
        
        // GMRES 
        auto t2 = time_section([&]() { gmres.solve(); });
        
        // RHS
        auto t3 = time_section([&]() { compute_rhs(); });
        
        report_timings(t1, t2, t3);
        identify_bottlenecks();
    }
};
```

#### 3.2 캐시 최적화
```cpp
// Cache-friendly loop ordering
// Optimize for j-k-i layout
#pragma omp parallel for collapse(2)
for (int j = jts; j <= jte; j++) {
    for (int k = kts; k <= kte; k++) {
        #pragma omp simd
        for (int i = its; i <= ite; i++) {
            // Computation with optimal cache access
            tensor[j][k][i] = compute_tendency(j, k, i);
        }
    }
}
```

#### 3.3 벡터화
```cpp
// Vectorized flux computation
torch::Tensor compute_flux_vectorized(const torch::Tensor& u) {
    // Use tensor operations instead of loops
    auto u_left = u.index({Slice(), Slice(), Slice(0, -1)});
    auto u_right = u.index({Slice(), Slice(), Slice(1, None)});
    
    // Vectorized computation
    auto flux = 0.5 * (u_left + u_right) * 
                torch::where(u_left + u_right > 0, u_left, u_right);
    
    return flux;
}
```

### Phase 4: em_b_wave 검증 (Week 4)

#### 4.1 정확도 테스트
```python
# validate_em_b_wave.py
import netCDF4 as nc
import numpy as np
import matplotlib.pyplot as plt

def validate_accuracy():
    """em_b_wave 정확도 검증"""
    
    # Reference solution (RK3)
    ref = nc.Dataset('wrfout_rk3_d01_2024-01-16_12:00:00')
    
    # SDIRK3 solution
    test = nc.Dataset('wrfout_sdirk3_d01_2024-01-16_12:00:00')
    
    # Compare key variables
    variables = ['U', 'V', 'W', 'T', 'P', 'PH']
    
    for var in variables:
        ref_data = ref.variables[var][:]
        test_data = test.variables[var][:]
        
        # Compute errors
        abs_error = np.abs(test_data - ref_data)
        rel_error = abs_error / (np.abs(ref_data) + 1e-10)
        
        print(f"{var}:")
        print(f"  Max absolute error: {np.max(abs_error):.2e}")
        print(f"  Max relative error: {np.max(rel_error):.2e}")
        print(f"  RMS error: {np.sqrt(np.mean(abs_error**2)):.2e}")
        
        # Check tolerance
        assert np.max(rel_error) < 1e-3, f"{var} accuracy test failed"
    
    # Conservation checks
    check_conservation(test)
    
    print("✅ All accuracy tests passed!")

def check_conservation(dataset):
    """질량 및 에너지 보존 검증"""
    
    times = dataset.variables['Times'][:]
    mu = dataset.variables['MU'][:]  # Column mass
    
    # Total mass at each time
    total_mass = np.sum(mu, axis=(1,2))
    
    # Check mass conservation
    mass_drift = (total_mass[-1] - total_mass[0]) / total_mass[0]
    print(f"Mass drift: {mass_drift:.2e}")
    assert abs(mass_drift) < 1e-10, "Mass conservation violated"
    
    # Energy conservation (simplified)
    # KE + PE + IE = constant
    u = dataset.variables['U'][:]
    v = dataset.variables['V'][:]
    w = dataset.variables['W'][:]
    theta = dataset.variables['T'][:]
    
    ke = 0.5 * (u**2 + v**2 + w**2)
    total_ke = np.sum(ke * mu[:,:,None,:], axis=(1,2,3))
    
    energy_drift = (total_ke[-1] - total_ke[0]) / total_ke[0]
    print(f"Energy drift: {energy_drift:.2e}")
    assert abs(energy_drift) < 1e-3, "Energy conservation violated"
```

#### 4.2 성능 측정
```bash
#!/bin/bash
# benchmark_em_b_wave.sh

echo "Running performance benchmark..."

# RK3 baseline
time mpirun -np 4 ./wrf_rk3.exe 2>&1 | tee rk3_timing.log
RK3_TIME=$(grep "Total time" rk3_timing.log | awk '{print $3}')

# SDIRK3 
time mpirun -np 4 ./wrf_sdirk3.exe 2>&1 | tee sdirk3_timing.log
SDIRK3_TIME=$(grep "Total time" sdirk3_timing.log | awk '{print $3}')

# Calculate speedup
python3 -c "
rk3 = $RK3_TIME
sdirk3 = $SDIRK3_TIME
speedup = rk3 / sdirk3
print(f'Speedup: {speedup:.2f}x')
print(f'Target: 1.5x')
if speedup >= 1.5:
    print('✅ Performance target achieved!')
else:
    print('⚠️ Performance below target')
"
```

#### 4.3 안정성 테스트
```bash
# Long integration test (10 days)
cat > namelist.input.long << EOF
&time_control
 run_days                = 10,
 run_hours               = 0,
 time_step               = 180,
/
EOF

mpirun -np 4 ./wrf.exe
python3 check_stability.py wrfout_d01_*
```

---

## 📊 검증 메트릭

### 필수 통과 기준
| Metric | Target | Priority |
|--------|--------|----------|
| em_b_wave 완주 | 12시간 | CRITICAL |
| 질량 보존 | < 1e-10 | CRITICAL |
| 에너지 보존 | < 1e-3 | HIGH |
| 상대 오차 | < 1e-3 | HIGH |
| 속도 향상 | > 1.5x | MEDIUM |

### 성능 목표
```yaml
Timestep: 180s (3x RK3)
Wall time: < 50% of RK3
Memory usage: < 110% of RK3
Cache hit rate: > 85%
Parallel efficiency: > 80%
```

---

## 🔧 디버깅 도구

### 1. Convergence Monitor
```cpp
// convergence_monitor.h
class ConvergenceMonitor {
    void log_iteration(int iter, double residual) {
        if (verbose) {
            std::cout << "Newton iter " << iter 
                     << ": res = " << std::scientific 
                     << residual << std::endl;
        }
        
        if (residual > 1e10) {
            std::cerr << "WARNING: Divergence detected!" << std::endl;
            dump_state("divergence_state.nc");
        }
    }
};
```

### 2. Tensor Validator
```cpp
// tensor_validator.h
bool validate_tensor(const torch::Tensor& t, const std::string& name) {
    if (torch::any(torch::isnan(t)).item<bool>()) {
        std::cerr << "NaN detected in " << name << std::endl;
        return false;
    }
    if (torch::any(torch::isinf(t)).item<bool>()) {
        std::cerr << "Inf detected in " << name << std::endl;
        return false;
    }
    return true;
}
```

---

## 📅 타임라인 (4주)

| Week | Tasks | Deliverables |
|------|-------|--------------|
| 1 | 버그 수정, .item() 제거 | Fixed codebase |
| 2 | em_b_wave 환경 구축 | Test ready |
| 3 | 성능 최적화 | Optimized code |
| 4 | 최종 검증 | Validation report |

---

## ✅ 체크리스트

### Week 1
- [ ] .item() 완전 제거
- [ ] Newton 수렴 버그 수정
- [ ] 경계 조건 검증
- [ ] 단위 테스트 통과

### Week 2
- [ ] em_b_wave 설정
- [ ] 초기 조건 생성
- [ ] 첫 실행 성공
- [ ] 기본 검증

### Week 3
- [ ] 프로파일링 완료
- [ ] 병목 지점 해결
- [ ] 캐시 최적화
- [ ] 벡터화 구현

### Week 4
- [ ] 정확도 검증
- [ ] 성능 목표 달성
- [ ] 장기 안정성 확인
- [ ] 최종 보고서

---

## 🚨 위험 관리

| Risk | Mitigation |
|------|------------|
| em_b_wave 실패 | 단순 케이스부터 점진적 테스트 |
| 성능 목표 미달 | 프로파일링 기반 최적화 |
| 수렴 문제 | Adaptive tolerance 구현 |
| 메모리 부족 | Gradient checkpointing |

---

## 📝 최종 산출물

1. **최적화된 코드베이스**
   - 버그 수정 완료
   - .item() 제거 완료
   - 성능 최적화 완료

2. **em_b_wave 검증 보고서**
   - 정확도 결과
   - 성능 벤치마크
   - 보존 특성 분석

3. **사용자 가이드**
   - namelist 설정
   - 최적 파라미터
   - 트러블슈팅

---

**결론**: libtorch_wrf/sdirk3는 이미 견고한 기반을 가지고 있으므로, 버그 수정과 최적화에 집중하여 4주 내에 em_b_wave 테스트를 통과하는 production-ready 코드를 완성할 수 있습니다.