#ifndef WRF_SDIRK3_FULL_PHYSICS_H
#define WRF_SDIRK3_FULL_PHYSICS_H

#include <torch/torch.h>
#include <functional>
#include <map>
#include <string>

namespace wrf {
namespace sdirk3 {

// ============================================================================
// OPT Pass33+: PHYSICS TENDENCY WORKSPACE FOR BUFFER REUSE
// ============================================================================
// In physics tendency calculations, zeros_like() is called repeatedly for
// zero-valued tendency components (e.g., u_tend=0, v_tend=0 when only w changes).
// Pre-allocating these buffers eliminates allocation overhead in hot paths.
//
// USAGE:
//   PhysicsTendencyWorkspace ws;
//   for (int step = 0; step < nsteps; ++step) {
//       ws.ensureCapacity(state, opts);
//       // Use ws.zero_u, ws.zero_v, etc. instead of zeros_like(state.u)
//       auto tend = compute_tendency_with_workspace(state, ws);
//   }
// ============================================================================

/**
 * @brief Pre-allocated workspace for physics tendency computations
 *
 * OPT Pass33+: Eliminates repeated zeros_like() calls in hot paths.
 * Each buffer is zeroed via zero_() on reuse (cheaper than allocation).
 */
struct PhysicsTendencyWorkspace {
    // Zero-valued buffers matching state variable shapes
    torch::Tensor zero_u;      // [ny, nz, nx+1] for u-stagger
    torch::Tensor zero_v;      // [ny+1, nz, nx] for v-stagger
    torch::Tensor zero_w;      // [ny, nz+1, nx] for w-stagger
    torch::Tensor zero_p;      // [ny, nz, nx] mass points
    torch::Tensor zero_mu;     // [ny, nx] column
    torch::Tensor zero_theta;  // [ny, nz, nx] mass points

    // Cached dimensions and options for validation
    int64_t cached_ny = 0;
    int64_t cached_nz = 0;
    int64_t cached_nx = 0;
    torch::TensorOptions cached_opts;  // FIX: Track dtype/device for proper reuse

    /**
     * @brief Ensure workspace has sufficient capacity
     *
     * @param ny Y dimension
     * @param nz Z dimension
     * @param nx X dimension
     * @param opts Tensor options (dtype, device)
     * @return true if reallocation occurred
     */
    bool ensureCapacity(int64_t ny, int64_t nz, int64_t nx,
                        const torch::TensorOptions& opts) {
        // FIX: Check dimensions AND dtype/device to prevent misuse
        bool same_dims = (ny == cached_ny && nz == cached_nz && nx == cached_nx);
        bool same_opts = (cached_opts.dtype() == opts.dtype() &&
                         cached_opts.device() == opts.device());

        if (same_dims && same_opts && zero_u.defined()) {
            // Reuse: zero the buffers
            zero_u.zero_();
            zero_v.zero_();
            zero_w.zero_();
            zero_p.zero_();
            zero_mu.zero_();
            zero_theta.zero_();
            return false;
        }

        // Reallocate
        cached_ny = ny;
        cached_nz = nz;
        cached_nx = nx;
        cached_opts = opts;  // FIX: Cache dtype/device

        zero_u = torch::zeros({ny, nz, nx + 1}, opts);
        zero_v = torch::zeros({ny + 1, nz, nx}, opts);
        zero_w = torch::zeros({ny, nz + 1, nx}, opts);
        zero_p = torch::zeros({ny, nz, nx}, opts);
        zero_mu = torch::zeros({ny, nx}, opts);
        zero_theta = torch::zeros({ny, nz, nx}, opts);

        return true;
    }

    /**
     * @brief Template version using WRFFullState dimensions
     */
    template<typename StateT>
    bool ensureCapacityFromState(const StateT& state) {
        auto ny = state.u.size(0);
        auto nz = state.u.size(1);
        auto nx = state.u.size(2) - 1;  // u is [ny, nz, nx+1]
        return ensureCapacity(ny, nz, nx, state.u.options());
    }

    /**
     * @brief Clear workspace to free memory
     */
    void clear() {
        zero_u = torch::Tensor();
        zero_v = torch::Tensor();
        zero_w = torch::Tensor();
        zero_p = torch::Tensor();
        zero_mu = torch::Tensor();
        zero_theta = torch::Tensor();
        cached_ny = cached_nz = cached_nx = 0;
        cached_opts = torch::TensorOptions();  // FIX: Reset cached options
    }

    /**
     * @brief Get approximate memory usage in bytes
     */
    size_t memoryUsage() const {
        size_t total = 0;
        if (zero_u.defined()) total += zero_u.nbytes();
        if (zero_v.defined()) total += zero_v.nbytes();
        if (zero_w.defined()) total += zero_w.nbytes();
        if (zero_p.defined()) total += zero_p.nbytes();
        if (zero_mu.defined()) total += zero_mu.nbytes();
        if (zero_theta.defined()) total += zero_theta.nbytes();
        return total;
    }
};

/**
 * @brief Thread-local physics tendency workspace
 * OPT Pass33+: Use in parallel regions for lock-free buffer access
 */
inline PhysicsTendencyWorkspace& getThreadLocalPhysicsWorkspace() {
    thread_local PhysicsTendencyWorkspace tls_workspace;
    return tls_workspace;
}

/**
 * WRF SDIRK-3 Full Physics Interface
 * 
 * 파일명: wrf_sdirk3_full_physics.h
 * 목적: 음향파, 중력파, 대류 등 전체 물리과정 통합
 * 
 * Implicit 특성을 활용하여 모든 파동 모드 제어
 */

// Physics mode flags
enum WRFPhysicsMode {
    WRF_PHYSICS_ACOUSTIC    = 0x01,  // 음향파
    WRF_PHYSICS_GRAVITY     = 0x02,  // 중력파
    WRF_PHYSICS_BUOYANCY    = 0x04,  // 부력
    WRF_PHYSICS_CORIOLIS    = 0x08,  // 코리올리
    WRF_PHYSICS_ADVECTION   = 0x10,  // 이류
    WRF_PHYSICS_DIFFUSION   = 0x20,  // 확산
    WRF_PHYSICS_FULL        = 0xFF   // 전체 물리
};

/**
 * WRF 전체 물리과정 상태 벡터
 * Extended state for full dynamics
 */
struct WRFFullState {
    // Prognostic variables
    torch::Tensor u, v, w;          // 3D wind components
    torch::Tensor ph, phb;          // Perturbation and base geopotential
    torch::Tensor t, theta;         // Temperature and potential temperature
    torch::Tensor p, pb;            // Perturbation and base pressure
    torch::Tensor mu, mub;          // Column mass (perturbation and base)
    torch::Tensor qv, qc, qr;       // Moisture variables
    
    // Diagnostic variables
    torch::Tensor rho;              // Density
    torch::Tensor alt;              // Inverse density
    
    // Grid metrics
    torch::Tensor fnm, fnp;         // Coriolis parameters
    torch::Tensor rdx, rdy;         // Inverse grid spacing
    torch::Tensor msftx, msfty;     // Map scale factors
};

/**
 * WRF 물리 상수 및 격자 정보
 */
struct WRFPhysicsConfig {
    // Grid parameters
    float dx, dy;                   // Horizontal grid spacing
    float* dz_w;                    // Vertical spacing at w points
    float* dz_u;                    // Vertical spacing at u points
    int nx, ny, nz;                 // Domain dimensions
    
    // Physical constants
    float g;                        // Gravity (9.81 m/s²)
    float cp;                       // Specific heat (1004.5 J/kg/K)
    float cv;                       // Cv = Cp - Rd
    float rd;                       // Gas constant for dry air (287 J/kg/K)
    float rv;                       // Gas constant for water vapor
    float p0;                       // Reference pressure (100000 Pa)
    float t0;                       // Reference temperature (270 K)
    float gamma;                    // Cp/Cv
    
    // Time integration
    float dt;                       // Model time step
    float acoustic_dt;              // Acoustic time step
    int time_step_sound;            // Sound steps per RK step
    
    // Physics options
    int physics_mode;               // Bitmask of active physics
    bool use_theta_m;               // Use moist theta
    bool use_terrain;               // Include terrain effects
    int diff_opt;                   // Diffusion option
    int km_opt;                     // Eddy coefficient option
    
    // Implicit solver options
    float implicit_gravity_fac;     // Gravity wave implicit factor (0-1)
    float implicit_acoustic_fac;    // Acoustic wave implicit factor (0-1)
    float epssm;                    // Off-centering for vertical sound waves
};

/**
 * 전체 물리과정 RHS 계산
 */
class WRFFullPhysicsRHS {
public:
    WRFFullPhysicsRHS(const WRFPhysicsConfig& config);
    
    /**
     * 전체 경향성 계산
     * @param state 현재 상태
     * @return 시간 경향성 [nu, nz, ny, nx] where nu = 변수 개수
     */
    torch::Tensor compute_tendency(const WRFFullState& state);
    
    /**
     * 개별 물리과정 경향성
     */
    torch::Tensor compute_acoustic_tendency(const WRFFullState& state);
    torch::Tensor compute_gravity_tendency(const WRFFullState& state);
    torch::Tensor compute_advection_tendency(const WRFFullState& state);
    torch::Tensor compute_coriolis_tendency(const WRFFullState& state);
    torch::Tensor compute_diffusion_tendency(const WRFFullState& state);
    torch::Tensor compute_buoyancy_tendency(const WRFFullState& state);
    
    /**
     * 진단 변수 업데이트
     */
    void update_diagnostics(WRFFullState& state);
    
    /**
     * 경계 조건 적용
     */
    void apply_boundary_conditions(WRFFullState& state);
    
private:
    WRFPhysicsConfig config_;
    
    // 유한차분 연산자
    torch::Tensor ddx(const torch::Tensor& f, float dx);
    torch::Tensor ddy(const torch::Tensor& f, float dy);
    torch::Tensor ddz_w(const torch::Tensor& f, int k);  // w-grid
    torch::Tensor ddz_u(const torch::Tensor& f, int k);  // u-grid
    
    // 보간 연산자
    torch::Tensor interp_u_to_w(const torch::Tensor& f);
    torch::Tensor interp_w_to_u(const torch::Tensor& f);
    
    // 지형 효과
    torch::Tensor compute_metric_terms(const WRFFullState& state);
};

/**
 * Implicit-Explicit (IMEX) 분할
 */
class WRFIMEXSplitting {
public:
    /**
     * 빠른 모드와 느린 모드 분리
     */
    static void split_fast_slow_modes(
        const WRFFullState& state,
        const WRFPhysicsConfig& config,
        WRFFullState& fast_state,
        WRFFullState& slow_state
    );
    
    /**
     * Implicit 처리할 항목 선택
     */
    static torch::Tensor extract_implicit_terms(
        const torch::Tensor& full_tendency,
        const WRFPhysicsConfig& config
    );
    
    /**
     * Explicit 처리할 항목 선택
     */
    static torch::Tensor extract_explicit_terms(
        const torch::Tensor& full_tendency,
        const WRFPhysicsConfig& config
    );
};

/**
 * 선형화된 연산자 (Newton 반복용)
 */
class WRFLinearizedOperator {
public:
    WRFLinearizedOperator(
        const WRFFullState& base_state,
        const WRFPhysicsConfig& config
    );
    
    /**
     * 선형화된 연산자 적용: L(δu) = (I - dt*γ*J)δu
     */
    torch::Tensor apply(const torch::Tensor& delta_u);
    
    /**
     * Jacobian-vector product: J*v
     */
    torch::Tensor jacobian_vector_product(const torch::Tensor& v);
    
    /**
     * 전처리기 업데이트
     */
    void update_preconditioner();
    
private:
    WRFFullState base_state_;
    WRFPhysicsConfig config_;
    
    // 선형화 계수
    std::map<std::string, torch::Tensor> linearization_coeffs_;
};

/**
 * 안정성 분석 도구
 */
namespace stability_analysis {
    
    /**
     * 최대 안정 시간간격 계산
     */
    double compute_max_stable_dt(
        const WRFFullState& state,
        const WRFPhysicsConfig& config,
        double safety_factor = 0.9
    );
    
    /**
     * 각 파동 모드의 CFL 수 계산
     */
    struct CFLNumbers {
        double acoustic_cfl;
        double gravity_cfl;
        double advective_cfl;
        double diffusive_cfl;
        double max_cfl;
    };
    
    CFLNumbers compute_cfl_numbers(
        const WRFFullState& state,
        const WRFPhysicsConfig& config
    );
    
    /**
     * 증폭 인자 계산
     */
    double compute_amplification_factor(
        double z,  // z = λ * dt
        const WRFPhysicsConfig& config
    );
    
    /**
     * 에너지 보존 검증
     */
    struct EnergyComponents {
        double kinetic_energy;
        double potential_energy;
        double internal_energy;
        double total_energy;
    };
    
    EnergyComponents compute_energy(
        const WRFFullState& state,
        const WRFPhysicsConfig& config
    );
}

/**
 * WRF 제어 인터페이스
 */
class WRFControlInterface {
public:
    /**
     * Namelist 파라미터로부터 설정 읽기
     */
    static WRFPhysicsConfig read_config_from_namelist(
        const std::map<std::string, float>& namelist_params
    );
    
    /**
     * WRF Registry 변수와 동기화
     */
    static void sync_with_registry(
        WRFFullState& state,
        void* grid_ptr  // WRF grid structure pointer
    );
    
    /**
     * 진단 정보 출력 (rsl.out)
     */
    static void write_diagnostics(
        const WRFFullState& state,
        const stability_analysis::CFLNumbers& cfl,
        const stability_analysis::EnergyComponents& energy,
        int timestep
    );
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_FULL_PHYSICS_H