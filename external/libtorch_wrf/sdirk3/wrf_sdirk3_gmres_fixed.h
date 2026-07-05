#ifndef WRF_SDIRK3_GMRES_FIXED_H
#define WRF_SDIRK3_GMRES_FIXED_H

#include <torch/torch.h>
#include <vector>
#include <functional>
#include <cmath>
#include "wrf_sdirk3_state_vector.h"

namespace wrf {
namespace sdirk3 {

// ============================================================================
// OPT Pass33+: GMRES WORKSPACE FOR BUFFER REUSE
// ============================================================================
// Pre-allocate H (Hessenberg matrix) and g (residual vector) tensors to avoid
// repeated allocations during Newton-GMRES iterations. These are small CPU
// tensors but allocation overhead adds up over many GMRES calls.
//
// USAGE:
//   GMRESWorkspace ws;
//   ws.ensureCapacity(max_krylov_dim);
//   auto x = GMRESSolverFixed::solve(A_times_v, b, x0, config, precond, &ws);
// ============================================================================

/**
 * @brief Pre-allocated workspace for GMRES solver
 *
 * OPT Pass33+: Eliminates repeated tensor allocations in GMRES iterations.
 * H and g are small CPU tensors (~4KB total for default krylov_dim=30).
 */
struct GMRESWorkspace {
    torch::Tensor H;    // Hessenberg matrix [krylov_dim+1, krylov_dim] CPU
    torch::Tensor g;    // Residual vector [krylov_dim+1, 1] CPU
    torch::Tensor y;    // Solution coefficients [krylov_dim, 1] CPU
    int64_t max_krylov_dim = 0;

    /**
     * @brief Ensure workspace has sufficient capacity
     *
     * @param krylov_dim Maximum Krylov subspace dimension
     * @return true if reallocation occurred
     */
    bool ensureCapacity(int64_t krylov_dim) {
        if (krylov_dim <= max_krylov_dim && H.defined()) {
            // Reuse: zero the buffers
            H.zero_();
            g.zero_();
            y.zero_();
            return false;
        }

        // Allocate new buffers on CPU
        auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        max_krylov_dim = krylov_dim;
        H = torch::zeros({krylov_dim + 1, krylov_dim}, cpu_opts);
        g = torch::zeros({krylov_dim + 1, 1}, cpu_opts);
        y = torch::zeros({krylov_dim, 1}, cpu_opts);
        return true;
    }

    /**
     * @brief Reset workspace for new GMRES iteration (zero without realloc)
     */
    void reset() {
        if (H.defined()) H.zero_();
        if (g.defined()) g.zero_();
        if (y.defined()) y.zero_();
    }

    /**
     * @brief Clear workspace to free memory
     */
    void clear() {
        H = torch::Tensor();
        g = torch::Tensor();
        y = torch::Tensor();
        max_krylov_dim = 0;
    }

    /**
     * @brief Get approximate memory usage in bytes
     */
    size_t memoryUsage() const {
        size_t total = 0;
        if (H.defined()) total += H.nbytes();
        if (g.defined()) total += g.nbytes();
        if (y.defined()) total += y.nbytes();
        return total;
    }
};

/**
 * @brief Thread-local GMRES workspace for parallel regions
 * OPT Pass33+: Use to avoid workspace allocation contention
 */
inline GMRESWorkspace& getThreadLocalGMRESWorkspace() {
    thread_local GMRESWorkspace tls_workspace;
    return tls_workspace;
}

// FIX 2025-12-27: Helper to safely extract scalar from tensor (handles GPU sync + AD)
// This avoids repeated GPU->CPU sync by explicitly transferring once
namespace {
inline float safe_scalar(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.to(torch::kCPU).item<float>();
}
inline float safe_norm(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.norm().to(torch::kCPU).item<float>();
}
} // anonymous namespace

/**
 * GMRES Solver with Givens Rotation (FIXED)
 * According to approved design v6.0
 *
 * FIX Round151: DATA_PTR<FLOAT>() SAFETY
 * All data_ptr<float>() calls in this file operate on H and g tensors that are
 * explicitly created on CPU (lines 171-173: cpu_opts with .device(torch::kCPU)).
 * This guarantees safe pointer access without GPU sync issues.
 *
 * CRITICAL FIXES APPLIED:
 * - Use index_put_ instead of tensor[]
 * - Proper Givens rotation implementation
 * - No lstsq calls - use back substitution
 * - NOTE: GMRES requires scalar values for convergence checks; GPU sync is unavoidable
 *   but NoGradGuard is used to prevent AD graph construction
 *
 * OPT Pass33+ WORKSPACE ALLOCATION NOTE:
 * H and g tensors are allocated fresh each gmres_iteration() call. This is acceptable:
 * - H [krylov_dim+1, krylov_dim] ≈ 31x30 = 3.6KB for default restart=30
 * - g [krylov_dim+1, 1] ≈ 124 bytes
 * - Both are CPU tensors with fast allocation
 * - Main computational cost is in StateVector operations and A_times_v, not workspace
 * - If profiling shows workspace allocation as bottleneck, consider passing pre-allocated
 *   workspace as optional parameter to gmres_iteration()
 */
class GMRESSolverFixed {
public:
    struct Config {
        int max_iter = 100;
        int restart = 30;
        float tol = 1e-6;
        bool verbose = false;
    };
    
    struct GivensRotation {
        float c, s;

        GivensRotation(float a, float b) {
            if (b == 0) {
                c = 1.0f;
                s = 0.0f;
            } else if (std::abs(b) > std::abs(a)) {
                float tau = -a / b;
                s = 1.0f / std::sqrt(1.0f + tau * tau);
                c = s * tau;
            } else {
                float tau = -b / a;
                c = 1.0f / std::sqrt(1.0f + tau * tau);
                s = c * tau;
            }
        }

        // FIX 2025-12-30 Batch28 Issue 4: Use data_ptr for CPU tensor operations
        // H is [krylov_dim+1, krylov_dim], contiguous on CPU
        // Pointer access: H[i, j] = H_ptr[i * n_cols + j]
        void apply(torch::Tensor& H, int i, int j) {
            float* H_ptr = H.data_ptr<float>();
            int64_t n_cols = H.size(1);
            float h1 = H_ptr[i * n_cols + j];
            float h2 = H_ptr[(i + 1) * n_cols + j];

            H_ptr[i * n_cols + j] = c * h1 - s * h2;
            H_ptr[(i + 1) * n_cols + j] = s * h1 + c * h2;
        }

        // FIX 2025-12-30 Batch28 Issue 4: Use data_ptr for CPU tensor operations
        // g is [krylov_dim+1, 1], contiguous on CPU
        // For [N, 1] shape, g[i, 0] = g_ptr[i]
        void apply_to_vector(torch::Tensor& g, int i) {
            float* g_ptr = g.data_ptr<float>();
            float g1 = g_ptr[i];
            float g2 = g_ptr[i + 1];

            g_ptr[i] = c * g1 - s * g2;
            g_ptr[i + 1] = s * g1 + c * g2;
        }
    };
    
    /**
     * Solve Ax = b using GMRES with Givens rotation
     *
     * @param A_times_v Function computing matrix-vector product
     * @param b Right-hand side
     * @param x0 Initial guess
     * @param config GMRES configuration
     * @param preconditioner Optional preconditioner
     * @param workspace Optional pre-allocated workspace (OPT Pass33+)
     */
    static StateVector solve(
        std::function<StateVector(const StateVector&)> A_times_v,
        const StateVector& b,
        const StateVector& x0,
        const Config& config,
        std::function<StateVector(const StateVector&)> preconditioner = nullptr,
        GMRESWorkspace* workspace = nullptr) {
        
        StateVector x = x0.clone();
        StateVector r = b - A_times_v(x);
        
        // Apply preconditioner if provided
        if (preconditioner) {
            r = preconditioner(r);
        }

        // AUTOGRAD DESIGN: GMRES is a linear solver used inside Newton iteration.
        // Gradients are computed via implicit differentiation at Newton convergence,
        // NOT by differentiating through the iterative GMRES process.
        // All .item() calls below are intentionally outside the AD graph.
        torch::NoGradGuard no_grad;  // Entire GMRES solve is non-differentiable

        // FIX 2025-12-27: Add .to(kCPU) before .item() to avoid GPU sync overhead
        float beta = r.norm().to(torch::kCPU).item<float>();
        if (beta < config.tol) {
            return x;  // Already converged
        }

        float b_norm = b.norm().to(torch::kCPU).item<float>();
        if (b_norm < 1e-12) b_norm = 1.0f;

        // OPT Pass33+: Ensure workspace capacity if provided
        if (workspace) {
            workspace->ensureCapacity(config.restart);
        }

        // Main GMRES iteration
        for (int outer = 0; outer < config.max_iter / config.restart; ++outer) {
            auto [x_new, converged] = gmres_iteration(
                A_times_v, x, r, beta, b_norm,
                config.restart, config.tol,
                preconditioner, workspace);

            x = x_new;
            if (converged) {
                return x;
            }

            // Compute new residual for restart
            r = b - A_times_v(x);
            if (preconditioner) {
                r = preconditioner(r);
            }
            beta = r.norm().to(torch::kCPU).item<float>();
        }

        return x;
    }

private:
    static std::pair<StateVector, bool> gmres_iteration(
        std::function<StateVector(const StateVector&)> A_times_v,
        const StateVector& x0,
        const StateVector& r0,
        float beta,
        float b_norm,
        int krylov_dim,
        float tol,
        std::function<StateVector(const StateVector&)> preconditioner,
        GMRESWorkspace* workspace = nullptr) {

        // Arnoldi vectors
        std::vector<StateVector> V;
        V.reserve(krylov_dim + 1);
        V.push_back(r0 * (1.0f / beta));

        // OPT Pass33+: Use workspace if provided, otherwise allocate fresh
        // FIX 2025-12-27: Explicitly create H and g on CPU to avoid repeated GPU->CPU transfers
        auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        torch::Tensor H, g;

        if (workspace && workspace->H.defined() && workspace->max_krylov_dim >= krylov_dim) {
            // Reuse workspace buffers (already zeroed by ensureCapacity or previous reset)
            H = workspace->H.slice(0, 0, krylov_dim + 1).slice(1, 0, krylov_dim);
            g = workspace->g.slice(0, 0, krylov_dim + 1);
            // Zero the used portions (in case workspace wasn't reset)
            H.zero_();
            g.zero_();
        } else {
            // Allocate fresh (no workspace or insufficient capacity)
            H = torch::zeros({krylov_dim + 1, krylov_dim}, cpu_opts);
            g = torch::zeros({krylov_dim + 1, 1}, cpu_opts);
        }
        g.index_put_({0, 0}, beta);
        
        // Store Givens rotations
        std::vector<GivensRotation> rotations;
        rotations.reserve(krylov_dim);
        
        StateVector x = x0.clone();
        int iter;
        
        for (iter = 0; iter < krylov_dim; ++iter) {
            // Arnoldi process
            StateVector w = A_times_v(V[iter]);
            if (preconditioner) {
                w = preconditioner(w);
            }

            // FIXED: Modified Gram-Schmidt with index_put_
            // FIX 2025-12-27: h_ij comes from GPU dot product, transfer once to CPU
            for (int i = 0; i <= iter; ++i) {
                auto h_ij = w.dot(V[i]);
                float h_ij_val = safe_scalar(h_ij);  // Single GPU->CPU transfer
                H.index_put_({i, iter}, h_ij_val);   // H is already CPU
                w = w - h_ij_val * V[i];
            }

            auto h_norm = w.norm();
            float h_norm_val = safe_scalar(h_norm);  // Single GPU->CPU transfer
            H.index_put_({iter + 1, iter}, h_norm_val);  // H is already CPU

            // Check for breakdown
            if (h_norm_val < 1e-12) {
                iter++;
                break;
            }

            V.push_back(w * (1.0f / h_norm_val));

            // FIXED: Apply previous Givens rotations
            // FIX 2025-12-30 Batch28 Issue 4: rotations[i].apply now uses data_ptr internally
            for (int i = 0; i < iter; ++i) {
                rotations[i].apply(H, i, iter);
            }

            // FIXED: Compute and apply new Givens rotation
            // FIX 2025-12-30 Batch28 Issue 4: Use data_ptr for CPU tensor H
            float* H_ptr = H.data_ptr<float>();
            int64_t H_cols = H.size(1);
            float h_iter = H_ptr[iter * H_cols + iter];
            float h_iter_plus = H_ptr[(iter + 1) * H_cols + iter];

            GivensRotation rot(h_iter, h_iter_plus);
            rot.apply(H, iter, iter);
            rot.apply_to_vector(g, iter);
            rotations.push_back(rot);

            // Check convergence
            // FIX 2025-12-30 Batch28 Issue 4: Use data_ptr for CPU tensor g
            float* g_ptr = g.data_ptr<float>();
            float residual = std::abs(g_ptr[iter + 1]);
            if (residual / b_norm < tol) {
                iter++;
                break;
            }
        }

        // FIX 2025-12-30 Batch28 Issue 4: Get pointers once for back substitution
        // H is [krylov_dim+1, krylov_dim], g is [krylov_dim+1, 1]
        float* H_ptr = H.data_ptr<float>();
        float* g_ptr = g.data_ptr<float>();
        int64_t H_cols = H.size(1);

        // FIXED: Back substitution (not lstsq)
        // FIX 2025-12-30 Batch28 Issue 4: Use data_ptr for all CPU tensor ops
        torch::Tensor y = torch::zeros({iter, 1}, cpu_opts);
        float* y_ptr = y.data_ptr<float>();

        for (int i = iter - 1; i >= 0; --i) {
            y_ptr[i] = g_ptr[i];

            for (int j = i + 1; j < iter; ++j) {
                y_ptr[i] -= H_ptr[i * H_cols + j] * y_ptr[j];
            }

            // v20.14 r49-fix: Guard against near-zero diagonal (matches newton_solver.cpp:940)
            float h_diag = H_ptr[i * H_cols + i];
            y_ptr[i] = (std::abs(h_diag) > 1e-10f) ? y_ptr[i] / h_diag : 0.0f;
        }

        // Reconstruct solution
        // FIX 2025-12-30 Batch28 Issue 4: Use data_ptr for y
        for (int i = 0; i < iter; ++i) {
            x = x + y_ptr[i] * V[i];
        }

        float final_residual = std::abs(g_ptr[iter]);
        bool converged = (final_residual / b_norm < tol);
        
        return {x, converged};
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_GMRES_FIXED_H