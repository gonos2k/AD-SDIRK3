# WRF Implicit Scheme Timestep 분석 보고서

> **HISTORICAL RECORD (dated snapshot).** Solver/API descriptions in this
> document (e.g. the pre-FGMRES GMRES/BiCGSTAB era) reflect the code as of
> the date of this record, not the current contract. For the current state
> (FGMRES solver, MPI support boundary, CTest inventory) see the repository
> root `README.md` and `external/libtorch_wrf/sdirk3/README.md`.


**작성일**: 2025년 6월 15일  
**테스트 케이스**: em_quarter_ss (1/4 scale supercell)  
**시뮬레이션 시간**: 30분  
**도메인**: 42 × 42 × 41 격자점, dx=dy=2000m  

## 1. 실험 개요

### 목적
Implicit scheme의 수치적 안정성을 활용하여 더 긴 timestep으로 시뮬레이션을 수행하고, 성능 향상을 정량적으로 평가

### 실험 설정
- **Explicit scheme**: dt = 6초 (기준선)
- **Implicit scheme**: dt = 12, 18, 24, 30초
- **Implicit 설정**: θ = 0.6, solver = BiCGSTAB, tolerance = 1.0e-6

## 2. 성능 분석 결과

### 2.1 실행 시간 비교

| Scheme    | dt (초) | 총 timestep 수 | 평균 시간/step (초) | 총 실행 시간 (초) | 상대 성능 |
|-----------|---------|----------------|---------------------|-------------------|-----------|
| Explicit  | 6       | 300            | 0.0190              | 5.70              | 100%      |
| Implicit  | 12      | 150            | 0.0197              | 2.96              | 51.8%     |
| Implicit  | 18      | 100            | 0.0313              | 3.13              | 54.9%     |
| Implicit  | 24      | 75             | 0.0399              | 2.99              | 52.5%     |
| Implicit  | 30      | 60             | 0.0451              | 2.71              | **47.5%** |

### 2.2 Timestep당 오버헤드 분석

| dt (초) | Timestep 오버헤드 | 오버헤드 증가율 |
|---------|-------------------|-----------------|
| 6       | 기준선            | 0%              |
| 12      | +0.0007초         | +3.7%           |
| 18      | +0.0123초         | +64.7%          |
| 24      | +0.0209초         | +110.0%         |
| 30      | +0.0261초         | +137.3%         |

## 3. 주요 발견사항

### 3.1 성능 이득
- **최대 52.5% 실행 시간 단축** (dt=30초 사용 시)
- Timestep 크기가 5배 증가해도 안정적 실행
- 전체 timestep 수 감소로 인한 I/O 오버헤드 감소

### 3.2 Implicit Solver 오버헤드
- Timestep이 클수록 solver 반복 횟수 증가
- dt=30초에서도 수렴성 유지 (tolerance 1.0e-6 달성)
- 개별 timestep 오버헤드가 증가하지만 전체 성능은 향상

### 3.3 안정성 평가
```
dt=6  (Explicit): CFL_max ≈ 0.8-0.9
dt=12 (Implicit): CFL_max ≈ 1.6-1.8
dt=18 (Implicit): CFL_max ≈ 2.4-2.7
dt=24 (Implicit): CFL_max ≈ 3.2-3.6
dt=30 (Implicit): CFL_max ≈ 4.0-4.5
```

모든 경우에서 수치적 안정성 유지됨

## 4. 최적 Timestep 선택 가이드

### 4.1 성능 우선
- **추천**: dt = 24-30초
- 최대 성능 이득을 원할 경우
- 약 50% 실행 시간 단축 가능

### 4.2 정확도 우선
- **추천**: dt = 12-18초
- Solver 오버헤드 최소화
- 여전히 45-48% 성능 향상

### 4.3 균형잡힌 선택
- **추천**: dt = 18초
- 적절한 성능 향상 (45% 시간 단축)
- 중간 수준의 solver 오버헤드

## 5. Implicit Scheme의 장점

1. **CFL 제약 완화**
   - Explicit: CFL < 1.0 제약
   - Implicit: CFL > 4.0에서도 안정

2. **계산 효율성**
   - Timestep 수 감소로 전체 계산량 감소
   - Runge-Kutta 스텝 수 감소

3. **I/O 효율성**
   - History 출력 횟수 감소
   - Restart 파일 작성 빈도 감소

4. **확장성**
   - 고해상도 시뮬레이션에서 더 큰 이득 예상
   - 장기간 적분에 유리

## 6. 제한사항 및 고려사항

1. **메모리 사용량**
   - Implicit solver를 위한 추가 메모리 필요
   - Jacobian 행렬 저장 공간

2. **초기 설정 복잡도**
   - Solver 파라미터 튜닝 필요
   - θ 값 선택의 중요성

3. **물리 과정과의 상호작용**
   - 일부 물리 과정은 작은 timestep 선호
   - 구름 미세물리, 난류 과정 등

## 7. 권장사항

### 7.1 운영 환경
```
&dynamics
 time_step = 18,                    ! 균형잡힌 선택
 use_implicit_scheme = .true.,
 implicit_acoustic = .true.,
 theta_implicit = 0.6,              ! Crank-Nicolson 근사
 implicit_solver_type = 2,          ! BiCGSTAB
 implicit_solver_tol = 1.0e-6,
 implicit_solver_max_iter = 10,
/
```

### 7.2 테스트 프로토콜
1. 짧은 기간 테스트로 최적 dt 확인
2. CFL 수 모니터링
3. Solver 수렴성 확인
4. 물리량 검증

## 8. 결론

WRF Implicit scheme은 timestep을 5배까지 증가시켜도 안정적으로 작동하며, 실행 시간을 최대 52.5% 단축시킬 수 있습니다. 이는 특히 장기간 시뮬레이션이나 앙상블 예측에서 큰 계산 자원 절약을 가능하게 합니다.

권장 timestep은 사용 목적에 따라:
- **최대 성능**: dt = 24-30초
- **균형잡힌 선택**: dt = 18초
- **보수적 접근**: dt = 12초

---

**참고**: 이 분석은 em_quarter_ss 이상화 사례를 기반으로 하며, 실제 사례나 다른 도메인 구성에서는 결과가 다를 수 있습니다.