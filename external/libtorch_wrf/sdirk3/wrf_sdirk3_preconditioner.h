#ifndef WRF_SDIRK3_PRECONDITIONER_H
#define WRF_SDIRK3_PRECONDITIONER_H

#include <torch/torch.h>
#include <memory>
#include <vector>

namespace wrf {
namespace sdirk3 {

/**
 * WRF SDIRK-3 전처리기 구현
 * 
 * 파일명: wrf_sdirk3_preconditioner.h
 * 목적: Newton-Krylov 수렴 가속을 위한 물리 기반 전처리기
 */

/**
 * 음향-중력파 분리 전처리기
 * Acoustic-Gravity wave decoupling preconditioner
 */
class AcousticGravityPreconditioner : public WRFPreconditioner {
public:
    AcousticGravityPreconditioner(
        int nx, int ny, int nz,
        float dx, float dy, float* dz,
        float c_sound, float N_freq,
        float dt, double gamma
    );
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, double gamma) override;
    
private:
    // Grid dimensions
    int nx_, ny_, nz_;
    float dx_, dy_;
    std::vector<float> dz_;
    
    // Physical parameters
    float c_sound_;      // Sound speed
    float N_freq_;       // Brunt-Väisälä frequency
    
    // Precomputed operators
    torch::Tensor acoustic_inverse_;
    torch::Tensor gravity_inverse_;
    torch::Tensor coupling_correction_;
    
    // Helper methods
    void compute_acoustic_block();
    void compute_gravity_block();
    void compute_coupling_terms();
};

/**
 * 다중격자 전처리기 (Multigrid)
 * V-cycle multigrid for elliptic pressure equation
 */
class MultigridPreconditioner : public WRFPreconditioner {
public:
    struct MultigridOptions {
        int n_levels;           // Number of multigrid levels
        int n_pre_smooth;       // Pre-smoothing iterations
        int n_post_smooth;      // Post-smoothing iterations
        int coarse_solver_iter; // Iterations on coarsest grid
        float omega;            // Relaxation parameter
        bool use_weighted_restriction;
    };
    
    MultigridPreconditioner(
        int nx, int ny, int nz,
        const MultigridOptions& options
    );
    
    torch::Tensor apply(const torch::Tensor& r) override;
    
private:
    MultigridOptions options_;
    
    // Grid hierarchy
    struct GridLevel {
        int nx, ny, nz;
        torch::Tensor restriction_op;
        torch::Tensor prolongation_op;
    };
    std::vector<GridLevel> levels_;
    
    // Multigrid operations
    torch::Tensor smooth(const torch::Tensor& u, const torch::Tensor& f, 
                        int level, int n_iter);
    torch::Tensor restrict_residual(const torch::Tensor& r, int level);
    torch::Tensor prolongate_correction(const torch::Tensor& e, int level);
    torch::Tensor v_cycle(const torch::Tensor& f, int level);
};

/**
 * 불완전 LU 분해 전처리기
 * ILU(0) for general sparse systems
 */
class ILUPreconditioner : public WRFPreconditioner {
public:
    ILUPreconditioner(
        const torch::Tensor& jacobian_pattern,
        float drop_tolerance = 1e-4
    );
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, double gamma) override;
    
private:
    // Sparse matrix storage (CSR format simulation)
    torch::Tensor L_values_;
    torch::Tensor U_values_;
    torch::Tensor diagonal_;
    
    // Sparsity pattern
    torch::Tensor row_ptr_;
    torch::Tensor col_idx_;
    
    // Forward/backward substitution
    torch::Tensor forward_solve(const torch::Tensor& b);
    torch::Tensor backward_solve(const torch::Tensor& y);
};

/**
 * 물리 기반 분할 전처리기
 * Physics-based splitting preconditioner
 */
class PhysicsSplittingPreconditioner : public WRFPreconditioner {
public:
    enum SplittingType {
        HORIZONTAL_VERTICAL,    // Horizontal-vertical splitting
        FAST_SLOW,             // Fast-slow mode splitting
        VARIABLE_BASED         // Variable-based (u,v,w,p,θ) splitting
    };
    
    PhysicsSplittingPreconditioner(
        int nx, int ny, int nz,
        SplittingType type,
        const std::map<std::string, float>& physics_params
    );
    
    torch::Tensor apply(const torch::Tensor& r) override;
    
private:
    SplittingType type_;
    std::map<std::string, float> params_;
    
    // Component solvers
    std::unique_ptr<WRFPreconditioner> horizontal_solver_;
    std::unique_ptr<WRFPreconditioner> vertical_solver_;
    std::unique_ptr<WRFPreconditioner> fast_mode_solver_;
    std::unique_ptr<WRFPreconditioner> slow_mode_solver_;
    
    // Splitting operations
    std::pair<torch::Tensor, torch::Tensor> split_horizontal_vertical(
        const torch::Tensor& r);
    std::pair<torch::Tensor, torch::Tensor> split_fast_slow(
        const torch::Tensor& r);
};

/**
 * 적응적 전처리기
 * Adaptive preconditioner that switches based on convergence
 */
class AdaptivePreconditioner : public WRFPreconditioner {
public:
    AdaptivePreconditioner(
        std::vector<std::unique_ptr<WRFPreconditioner>> preconditioners
    );
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, double gamma) override;
    
    // Adaptation control
    void update_convergence_history(float residual_norm);
    void select_best_preconditioner();
    
private:
    std::vector<std::unique_ptr<WRFPreconditioner>> preconditioners_;
    int current_preconditioner_;
    
    // Performance tracking
    struct PreconditionerStats {
        int n_applications;
        float avg_reduction_factor;
        float recent_performance;
    };
    std::vector<PreconditionerStats> stats_;
};

/**
 * 주파수 영역 전처리기
 * Frequency-domain preconditioner using FFT
 */
class SpectralPreconditioner : public WRFPreconditioner {
public:
    SpectralPreconditioner(
        int nx, int ny, int nz,
        float dx, float dy, float dz,
        float dt, double gamma
    );
    
    torch::Tensor apply(const torch::Tensor& r) override;
    
private:
    // FFT plans
    int nx_, ny_, nz_;
    float dx_, dy_, dz_;
    
    // Precomputed spectral response
    torch::Tensor spectral_response_;
    
    // FFT operations
    torch::Tensor forward_fft_3d(const torch::Tensor& r);
    torch::Tensor inverse_fft_3d(const torch::Tensor& r_hat);
    void compute_spectral_response(float dt, double gamma);
};

/**
 * 전처리기 공장 클래스
 */
class PreconditionerFactory {
public:
    enum PreconditionerType {
        NONE,
        JACOBI,
        BLOCK_JACOBI,
        ACOUSTIC_GRAVITY,
        MULTIGRID,
        ILU,
        PHYSICS_SPLITTING,
        SPECTRAL,
        ADAPTIVE
    };
    
    static std::unique_ptr<WRFPreconditioner> create(
        PreconditionerType type,
        const std::map<std::string, float>& params,
        int nx, int ny, int nz
    );
    
    // WRF namelist에서 전처리기 설정 읽기
    static PreconditionerType parse_namelist_option(int option_value);
};

/**
 * 전처리기 성능 모니터
 */
class PreconditionerMonitor {
public:
    void start_timing();
    void end_timing();
    
    void record_iteration(float residual_before, float residual_after);
    
    struct PerformanceMetrics {
        float avg_time_ms;
        float avg_reduction_factor;
        int total_applications;
        float effectiveness;  // 0-1 score
    };
    
    PerformanceMetrics get_metrics() const;
    void reset();
    
private:
    std::vector<float> timings_;
    std::vector<float> reduction_factors_;
    std::chrono::high_resolution_clock::time_point start_time_;
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_PRECONDITIONER_H