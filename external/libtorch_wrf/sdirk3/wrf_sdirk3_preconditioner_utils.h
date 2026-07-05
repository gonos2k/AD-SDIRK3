#ifndef WRF_SDIRK3_PRECONDITIONER_UTILS_H
#define WRF_SDIRK3_PRECONDITIONER_UTILS_H

#include <torch/torch.h>
#include <vector>
#include <complex>
#include "wrf_sdirk3_preconditioner_advanced.h"

namespace wrf {
namespace sdirk3 {

/**
 * Preconditioner Utilities and Analysis Tools
 */

// =============================================================================
// Spectral Analysis for Preconditioner Design
// =============================================================================

class PreconditionerSpectralAnalysis {
public:
    struct SpectralInfo {
        std::vector<float> eigenvalues_real;
        std::vector<float> eigenvalues_imag;
        float condition_number;
        float spectral_radius;
        float clustering_factor;  // How well eigenvalues are clustered
    };
    
    /**
     * Analyze the spectrum of the preconditioned operator M^{-1}A
     */
    static SpectralInfo analyze_preconditioned_spectrum(
        const torch::Tensor& A_sample,
        AdvancedPreconditioner* preconditioner,
        int n_samples = 100);
    
    /**
     * Estimate effectiveness of preconditioner using power method
     */
    static float estimate_condition_number(
        const std::function<torch::Tensor(const torch::Tensor&)>& apply_A,
        AdvancedPreconditioner* preconditioner,
        int n_iterations = 50);
    
    /**
     * Analyze eigenvalue clustering
     */
    static float compute_clustering_factor(const std::vector<std::complex<float>>& eigenvalues);
    
private:
    static std::pair<float, torch::Tensor> power_iteration(
        const std::function<torch::Tensor(const torch::Tensor&)>& apply_op,
        const torch::Tensor& initial_guess,
        int n_iterations);
    
    static std::pair<float, torch::Tensor> inverse_power_iteration(
        const std::function<torch::Tensor(const torch::Tensor&)>& apply_op,
        AdvancedPreconditioner* preconditioner,
        const torch::Tensor& initial_guess,
        int n_iterations);
};

// =============================================================================
// FFT-based Preconditioner Components
// =============================================================================

class FFTPreconditionerComponents {
public:
    /**
     * Compute FFT-based approximation to Helmholtz operator inverse
     * (I - c²Δt²∇²)^{-1}
     */
    static torch::Tensor apply_helmholtz_inverse_fft(
        const torch::Tensor& r,
        float c_sound,
        float dt,
        float gamma,
        float dx, float dy, const torch::Tensor& dz);
    
    /**
     * Apply periodic Poisson solver using FFT
     */
    static torch::Tensor solve_poisson_fft(
        const torch::Tensor& rhs,
        float dx, float dy, const torch::Tensor& dz);
    
    /**
     * Setup eigenvalues for discrete Laplacian
     */
    static torch::Tensor compute_laplacian_eigenvalues(
        int nx, int ny, int nz,
        float dx, float dy, const torch::Tensor& dz,
        bool periodic_x = false,
        bool periodic_y = false);
    
private:
    static torch::Tensor apply_dct_3d(const torch::Tensor& input, bool inverse = false);
    static torch::Tensor apply_dst_3d(const torch::Tensor& input, bool inverse = false);
};

// =============================================================================
// Incomplete Factorization Preconditioners
// =============================================================================

class IncompleteFactorization {
public:
    /**
     * ILU(0) factorization for block-structured matrices
     */
    class ILU0Preconditioner : public AdvancedPreconditioner {
    private:
        // Sparse matrix storage (CSR format)
        torch::Tensor values_;
        torch::Tensor row_ptr_;
        torch::Tensor col_idx_;
        
        // ILU factors
        torch::Tensor L_values_;
        torch::Tensor U_values_;
        torch::Tensor diagonal_;
        
        int n_rows_;
        int nnz_;
        
    public:
        ILU0Preconditioner(const torch::Tensor& A_values,
                          const torch::Tensor& row_ptr,
                          const torch::Tensor& col_idx);
        
        torch::Tensor apply(const torch::Tensor& r) override;
        void update(const torch::Tensor& state, float dt, float gamma) override;
        size_t get_memory_usage() const override;
        
    private:
        void compute_ilu0_factorization();
        torch::Tensor forward_substitution(const torch::Tensor& b);
        torch::Tensor backward_substitution(const torch::Tensor& y);
    };
    
    /**
     * Modified ILU with level-based fill-in
     */
    class MILUPreconditioner : public ILU0Preconditioner {
    private:
        float drop_tolerance_ = 1e-4f;
        int max_fill_level_ = 1;
        
    public:
        MILUPreconditioner(const torch::Tensor& A_values,
                          const torch::Tensor& row_ptr,
                          const torch::Tensor& col_idx,
                          float drop_tolerance,
                          int max_fill_level);
        
    private:
        void compute_milu_factorization();
    };
};

// =============================================================================
// Polynomial Preconditioners
// =============================================================================

class PolynomialPreconditioner : public AdvancedPreconditioner {
private:
    int degree_;
    std::vector<float> coefficients_;
    torch::Tensor reference_matrix_;
    
    enum PolynomialType {
        CHEBYSHEV,
        NEUMANN,
        LEAST_SQUARES
    } poly_type_;
    
public:
    PolynomialPreconditioner(int degree, PolynomialType type = CHEBYSHEV);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override { return sizeof(float) * coefficients_.size(); }
    
private:
    void compute_chebyshev_coefficients(float lambda_min, float lambda_max);
    void compute_neumann_series(const torch::Tensor& A_diag);
    torch::Tensor evaluate_polynomial(const torch::Tensor& r);
};

// =============================================================================
// Deflation-based Preconditioners
// =============================================================================

class DeflationPreconditioner : public AdvancedPreconditioner {
private:
    torch::Tensor deflation_vectors_;  // Columns are deflation vectors
    torch::Tensor E_;  // E = A * Z
    torch::Tensor coarse_matrix_;  // E^T * Z
    torch::Tensor coarse_inverse_;
    
    std::unique_ptr<AdvancedPreconditioner> base_preconditioner_;
    int n_deflation_vectors_;
    
public:
    DeflationPreconditioner(
        std::unique_ptr<AdvancedPreconditioner> base_precond,
        int n_vectors = 10);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override;
    
    /**
     * Add deflation vector (e.g., from previous solve)
     */
    void add_deflation_vector(const torch::Tensor& vec);
    
private:
    void setup_deflation_space(const torch::Tensor& state);
    void update_coarse_operators();
    torch::Tensor apply_deflation(const torch::Tensor& r);
    torch::Tensor apply_projection(const torch::Tensor& v);
};

// =============================================================================
// Domain Decomposition Preconditioners
// =============================================================================

class DomainDecompositionPreconditioner : public AdvancedPreconditioner {
public:
    struct SubdomainInfo {
        int id;
        int i_start, i_end;
        int j_start, j_end;
        int k_start, k_end;
        std::vector<int> neighbor_ids;
        torch::Tensor local_matrix;
        std::unique_ptr<AdvancedPreconditioner> local_solver;
    };
    
private:
    std::vector<SubdomainInfo> subdomains_;
    int overlap_size_ = 1;
    
    enum DDMethod {
        ADDITIVE_SCHWARZ,
        MULTIPLICATIVE_SCHWARZ,
        RESTRICTED_ADDITIVE_SCHWARZ,
        OPTIMIZED_SCHWARZ
    } dd_method_;
    
public:
    DomainDecompositionPreconditioner(
        int nx, int ny, int nz,
        int n_subdomains_x, int n_subdomains_y, int n_subdomains_z,
        DDMethod method = ADDITIVE_SCHWARZ,
        int overlap = 1);
    
    torch::Tensor apply(const torch::Tensor& r) override;
    void update(const torch::Tensor& state, float dt, float gamma) override;
    size_t get_memory_usage() const override;
    
private:
    void setup_subdomain_decomposition(int nx, int ny, int nz,
                                      int nsx, int nsy, int nsz);
    torch::Tensor apply_additive_schwarz(const torch::Tensor& r);
    torch::Tensor apply_multiplicative_schwarz(const torch::Tensor& r);
    void exchange_interface_values(torch::Tensor& local_solutions);
};

// =============================================================================
// Preconditioner Performance Monitor
// =============================================================================

class PreconditionerMonitor {
public:
    struct PerformanceMetrics {
        float avg_apply_time_ms;
        float avg_update_time_ms;
        float memory_usage_mb;
        float iteration_reduction_factor;
        float condition_number_estimate;
        int total_applies;
        int total_updates;
        
        // Convergence history
        std::vector<float> residual_norms;
        std::vector<int> iteration_counts;
    };
    
    /**
     * Monitor and analyze preconditioner performance
     */
    static void monitor_preconditioner(
        AdvancedPreconditioner* preconditioner,
        const std::string& name);
    
    /**
     * Compare multiple preconditioners
     */
    static void compare_preconditioners(
        const std::vector<std::pair<std::string, AdvancedPreconditioner*>>& preconditioners,
        const torch::Tensor& test_problem);
    
    /**
     * Generate performance report
     */
    static void generate_report(
        const std::unordered_map<std::string, PerformanceMetrics>& metrics,
        const std::string& output_file);
    
    /**
     * Adaptive preconditioner selection based on metrics
     */
    static std::string select_best_preconditioner(
        const std::unordered_map<std::string, PerformanceMetrics>& metrics,
        float memory_constraint_mb = -1.0f);
    
private:
    static std::unordered_map<std::string, PerformanceMetrics> metrics_database_;
};

// =============================================================================
// Preconditioner Factory with Auto-tuning
// =============================================================================

class AutoTunedPreconditionerFactory {
public:
    struct ProblemCharacteristics {
        float reynolds_number;
        float mach_number;
        float aspect_ratio;
        float cfl_number;
        bool has_strong_advection;
        bool has_strong_stratification;
        bool is_hydrostatic;
        int grid_size;
    };
    
    /**
     * Create optimally tuned preconditioner based on problem characteristics
     */
    static std::unique_ptr<AdvancedPreconditioner> create_auto_tuned(
        const ProblemCharacteristics& characteristics,
        int nx, int ny, int nz,
        float dx, float dy, const torch::Tensor& dz);
    
    /**
     * Run auto-tuning experiments
     */
    static std::unordered_map<std::string, float> auto_tune_parameters(
        AdvancedPreconditioner* preconditioner,
        const torch::Tensor& test_state,
        const std::function<torch::Tensor(const torch::Tensor&)>& apply_operator);
    
private:
    static std::string select_preconditioner_type(
        const ProblemCharacteristics& characteristics);
    
    static std::unordered_map<std::string, float> optimize_parameters(
        const std::string& precond_type,
        const ProblemCharacteristics& characteristics);
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_PRECONDITIONER_UTILS_H