#ifndef NEWTON_KRYLOV_SOLVER_H
#define NEWTON_KRYLOV_SOLVER_H

#include <torch/torch.h>
#include <functional>
#include <atomic>  // OPT Pass33+: For diagnostic sampling counter
#include "wrf_sdirk3_coefficients.h"

namespace wrf {
namespace sdirk3 {

// ============================================================================
// OPT Pass33+: GMRES WORKSPACE FOR BUFFER REUSE IN NEWTON-KRYLOV SOLVER
// ============================================================================
// Pre-allocate H (Hessenberg) and g (residual) tensors to eliminate repeated
// allocations during GMRES restart iterations within Newton steps.
//
// USAGE:
//   NewtonGMRESWorkspace ws;
//   ws.ensureCapacity(gmres_restart, tensor_options);
//   auto x = gmres_solve(matvec, b, x0, max_iter, tol, restart, &ws);
// ============================================================================

/**
 * @brief Pre-allocated workspace for gmres_solve function
 *
 * OPT Pass33+: Eliminates repeated tensor allocations in GMRES restart loop.
 */
struct NewtonGMRESWorkspace {
    torch::Tensor H;    // Hessenberg matrix [restart+1, restart]
    torch::Tensor g;    // Residual vector [restart+1]
    int64_t max_restart = 0;
    torch::TensorOptions cached_opts;

    /**
     * @brief Ensure workspace has sufficient capacity
     *
     * @param restart GMRES restart dimension
     * @param opts Tensor options (dtype, device)
     * @return true if reallocation occurred
     */
    bool ensureCapacity(int64_t restart, const torch::TensorOptions& opts) {
        bool same_size = (restart <= max_restart);
        bool same_opts = (cached_opts.dtype() == opts.dtype() &&
                         cached_opts.device() == opts.device());

        if (same_size && same_opts && H.defined()) {
            // Reuse: zero the buffers
            H.zero_();
            g.zero_();
            return false;
        }

        // Allocate new buffers
        max_restart = restart;
        cached_opts = opts;
        H = torch::zeros({restart + 1, restart}, opts);
        g = torch::zeros({restart + 1}, opts);
        return true;
    }

    /**
     * @brief Reset workspace for new GMRES iteration (zero without realloc)
     */
    void reset() {
        if (H.defined()) H.zero_();
        if (g.defined()) g.zero_();
    }

    /**
     * @brief Clear workspace to free memory
     */
    void clear() {
        H = torch::Tensor();
        g = torch::Tensor();
        max_restart = 0;
        cached_opts = torch::TensorOptions();
    }

    /**
     * @brief Get approximate memory usage in bytes
     */
    size_t memoryUsage() const {
        size_t total = 0;
        if (H.defined()) total += H.nbytes();
        if (g.defined()) total += g.nbytes();
        return total;
    }
};

/**
 * @brief Thread-local GMRES workspace for Newton-Krylov solver
 * OPT Pass33+: Use in parallel regions for lock-free buffer access
 */
inline NewtonGMRESWorkspace& getThreadLocalNewtonGMRESWorkspace() {
    thread_local NewtonGMRESWorkspace tls_workspace;
    return tls_workspace;
}

// Options for Newton-Krylov solver
struct NewtonKrylovOptions {
    int max_newton_iter = 10;
    float newton_tol = 1e-6f;
    int gmres_restart = 30;
    int gmres_max_iter = 100;
    float gmres_tol = 1e-4f;
    bool use_preconditioner = false;
    bool verbose = false;

    // Halo width for JVP masking (prevents artificial gradients from ghost cells)
    // IMPORTANT: Callers MUST set this to g_sdirk3_config.halo_width or appropriate value
    // Default 0 disables masking - this is INCORRECT for WRF usage
    int halo_width = 0;
};

// Newton-Krylov solver for implicit SDIRK stages
class NewtonKrylovSolver {
public:
    explicit NewtonKrylovSolver(const NewtonKrylovOptions& options = {}, int mu_size = 0);

    // OPT Pass33+: Explicit non-copyable/non-movable due to std::atomic member
    NewtonKrylovSolver(const NewtonKrylovSolver&) = delete;
    NewtonKrylovSolver& operator=(const NewtonKrylovSolver&) = delete;
    NewtonKrylovSolver(NewtonKrylovSolver&&) = delete;
    NewtonKrylovSolver& operator=(NewtonKrylovSolver&&) = delete;

    // Solve implicit stage equation: K = F(U_n + dt*sum(a_ij*K_j))
    torch::Tensor solve(
        const torch::Tensor& U_n,          // State at time n
        const torch::Tensor& K_prev,       // Previous stage derivatives
        const std::function<torch::Tensor(const torch::Tensor&)>& compute_rhs,
        float dt,                          // Time step
        double gamma,                      // Diagonal coefficient
        int stage                          // Stage number (1, 2, or 3)
    );
    
    // Get solver statistics
    void get_stats(int& newton_iters, int& gmres_iters, float& residual) const;
    void reset_stats();
    
private:
    NewtonKrylovOptions options_;
    int total_newton_iters_;
    int total_gmres_iters_;
    float last_residual_;
    int mu_size_;  // Size of mu component (nx*ny) to avoid perturbing
    int halo_width_;  // Width of halo region to mask in JVP

    // OPT Pass33+: Diagnostic sampling counter for per-iteration verbose output
    // Reduces D2H overhead when debug_level >= 2 and verbose is enabled
    mutable std::atomic<uint64_t> newton_diag_call_counter_{0};

    // Helper function to compute stage state
    torch::Tensor compute_stage_state(
        const torch::Tensor& U_n,
        const torch::Tensor& K_prev,
        float dt,
        int stage
    );
};

// GMRES solver for linear systems
// OPT Pass33+: Added optional workspace parameter for buffer reuse
torch::Tensor gmres_solve(
    const std::function<torch::Tensor(const torch::Tensor&)>& matvec,
    const torch::Tensor& b,
    const torch::Tensor& x0,
    int max_iter,
    float tol,
    int restart,
    NewtonGMRESWorkspace* workspace = nullptr
);

} // namespace sdirk3
} // namespace wrf

#endif // NEWTON_KRYLOV_SOLVER_H