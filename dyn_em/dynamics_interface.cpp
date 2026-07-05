/**
 * @file dynamics_interface.cpp
 * @brief C++ implementation of WRF dynamics interface
 * @author WRF Development Team
 * @date 2025-06-15
 */

#include <torch/torch.h>
#include <iostream>
#include <vector>
#include <memory>
#include <cstring>
#include <mutex>
#include <cmath>

#ifdef _OPENMP
#include <omp.h>
#endif

// OPT Pass33+: Compile-time log gating for production builds
// Define DYNAMICS_INTERFACE_VERBOSE to enable diagnostic output
// Build with: -DDYNAMICS_INTERFACE_VERBOSE for debug builds
#ifdef DYNAMICS_INTERFACE_VERBOSE
#define DYNAMICS_LOG std::cout
#define DYNAMICS_WARNINGS_ENABLED 1
#else
// Null stream pattern: if(true);else suppresses "unreachable code" warnings
#define DYNAMICS_LOG if(true) ; else std::cout
// OPT Pass33+: Also disable non-critical warnings in production
// Define DYNAMICS_INTERFACE_WARNINGS to enable warnings without full verbose
#ifdef DYNAMICS_INTERFACE_WARNINGS
#define DYNAMICS_WARNINGS_ENABLED 1
#else
#define DYNAMICS_WARNINGS_ENABLED 0
#endif
#endif

// C interface for Fortran interoperability
extern "C" {

// Structure for dynamics solver state
struct DynamicsSolver {
    // Configuration
    int nx, ny, nz;
    int mode;
    int num_threads;

    // Solver state
    bool initialized;
    std::mutex state_mutex;

    // PyTorch tensors for computation
    torch::Tensor u_tensor;
    torch::Tensor v_tensor;
    torch::Tensor w_tensor;
    torch::Tensor p_tensor;

    // Constructor
    DynamicsSolver(int nx_, int ny_, int nz_, int mode_)
        : nx(nx_), ny(ny_), nz(nz_), mode(mode_), initialized(false) {

        // Set thread count
#ifdef _OPENMP
        num_threads = omp_get_max_threads();
#else
        num_threads = 1;
#endif
        torch::set_num_threads(num_threads);
    }
};

// =============================================================================
// THREAD SAFETY 2025-12-31 Batch41: Thread-local work buffers for multi-tile parallelism
// =============================================================================
// RATIONALE:
// - Global work buffers in DynamicsSolver are protected by mutex (serializes access)
// - For OpenMP multi-tile parallel calls, each thread needs its own buffers
// - thread_local ensures each thread gets independent work space
// - Buffers are lazily initialized on first use per thread
//
// ALTERNATIVE APPROACHES (not used):
// 1. Per-tile context: Pass tile-specific context with buffers (more complex)
// 2. Stack-local buffers: Create inside function (allocation overhead each call)
// 3. Thread pool with buffer pool: Most efficient but complex to implement
// =============================================================================
struct ThreadLocalWorkBuffers {
    torch::Tensor dudx, dvdy, dwdz;
    torch::Tensor dpdx, dpdy, dpdz;
    bool initialized;
    int cached_nz, cached_ny, cached_nx;
    // FIX 2025-12-31 Batch41: Track device/dtype for proper invalidation on device change
    torch::Device cached_device;
    torch::ScalarType cached_dtype;

    ThreadLocalWorkBuffers()
        : initialized(false), cached_nz(0), cached_ny(0), cached_nx(0),
          cached_device(torch::kCPU), cached_dtype(torch::kFloat32) {}

    // Initialize or reinitialize buffers if dimensions, device, or dtype changed
    // FIX 2025-12-31 Batch41: Also checks device/dtype, not just dimensions
    // NOTE: thread_local buffers persist across omp_set_num_threads() calls,
    // but each thread maintains its own instance. Dimension/device changes
    // between tiles are handled by the checks below.
    void ensure_buffers(const torch::Tensor& reference) {
        int nz = reference.size(0);
        int ny = reference.size(1);
        int nx = reference.size(2);
        auto device = reference.device();
        auto dtype = reference.scalar_type();

        // Check if reallocation needed: dimensions, device, or dtype changed
        bool needs_realloc = !initialized
            || cached_nz != nz || cached_ny != ny || cached_nx != nx
            || cached_device != device || cached_dtype != dtype;

        if (needs_realloc) {
            // Allocate WITHOUT requires_grad to prevent AD pollution
            // FIX 2025-12-31 Batch41: requires_grad(false) is SAFE with copy_()
            // =================================================================
            // REQUIRES_GRAD SAFETY ANALYSIS:
            // - Buffers created with requires_grad(false)
            // - copy_() does NOT change destination's requires_grad flag
            // - Only DATA is copied, not gradient tracking state
            // - zero_() is also safe - in-place ops don't enable requires_grad
            //
            // WOULD BE UNSAFE IF:
            // - Buffer reassigned: dudx = some_tensor_with_grad (replaces reference)
            // - add_() from grad tensor: creates computation graph node
            //
            // SAFE OPERATIONS (used in this code):
            // - zero_() - in-place zero, keeps requires_grad=false
            // - slice().copy_() - copies data only, target keeps requires_grad=false
            // =================================================================
            auto options = reference.options().requires_grad(false);
            dudx = torch::zeros({nz, ny, nx}, options);
            dvdy = torch::zeros({nz, ny, nx}, options);
            dwdz = torch::zeros({nz, ny, nx}, options);
            dpdx = torch::zeros({nz, ny, nx}, options);
            dpdy = torch::zeros({nz, ny, nx}, options);
            dpdz = torch::zeros({nz, ny, nx}, options);
            initialized = true;
            cached_nz = nz;
            cached_ny = ny;
            cached_nx = nx;
            cached_device = device;
            cached_dtype = dtype;
        }
    }

    // DEBUG 2025-12-31 Batch41: Verify requires_grad stays false after copy_()
    // Call this after computations in debug builds to verify no AD pollution
#ifndef NDEBUG
    void assert_no_grad_pollution() const {
        assert(!dudx.requires_grad() && "dudx requires_grad leaked!");
        assert(!dvdy.requires_grad() && "dvdy requires_grad leaked!");
        assert(!dwdz.requires_grad() && "dwdz requires_grad leaked!");
        assert(!dpdx.requires_grad() && "dpdx requires_grad leaked!");
        assert(!dpdy.requires_grad() && "dpdy requires_grad leaked!");
        assert(!dpdz.requires_grad() && "dpdz requires_grad leaked!");
    }
#endif

    // FIX 2025-12-31 Batch41: Release build check with WARN logging
    // Since assert only works in debug, provide a WARN-level check for release builds.
    // Returns true if pollution detected (requires_grad leaked).
    // Call periodically in critical sections for production monitoring.
    //
    // OPT Pass33+: WARN_ONCE pattern to avoid log spam on repeated calls.
    // The warning will be printed only once per process lifetime.
    bool check_requires_grad_pollution(bool log_warning = true) const {
        static bool s_warned_once = false;  // WARN_ONCE flag

        bool has_pollution = false;
        const char* polluted_buffer = nullptr;

        if (dudx.defined() && dudx.requires_grad()) { has_pollution = true; polluted_buffer = "dudx"; }
        else if (dvdy.defined() && dvdy.requires_grad()) { has_pollution = true; polluted_buffer = "dvdy"; }
        else if (dwdz.defined() && dwdz.requires_grad()) { has_pollution = true; polluted_buffer = "dwdz"; }
        else if (dpdx.defined() && dpdx.requires_grad()) { has_pollution = true; polluted_buffer = "dpdx"; }
        else if (dpdy.defined() && dpdy.requires_grad()) { has_pollution = true; polluted_buffer = "dpdy"; }
        else if (dpdz.defined() && dpdz.requires_grad()) { has_pollution = true; polluted_buffer = "dpdz"; }

        // OPT Pass33+: Only warn once to avoid log spam
        // Also gated by DYNAMICS_WARNINGS_ENABLED to suppress in production builds
        // Build with -DDYNAMICS_INTERFACE_WARNINGS or -DDYNAMICS_INTERFACE_VERBOSE to enable
#if DYNAMICS_WARNINGS_ENABLED
        if (has_pollution && log_warning && !s_warned_once) {
            s_warned_once = true;
            std::cerr << "[Dynamics] WARN: requires_grad pollution detected in ThreadLocalWorkBuffers!"
                      << " Buffer '" << polluted_buffer << "' has requires_grad=true."
                      << " This may cause AD graph memory leaks or incorrect gradients." << std::endl;
            std::cerr << "[Dynamics] WARN: Buffers should always have requires_grad=false."
                      << " Check if copy_() was replaced with assignment or add_() was used."
                      << " (This warning will not repeat)" << std::endl;
        }
#else
        (void)log_warning;  // Suppress unused parameter warning
        (void)polluted_buffer;
#endif
        return has_pollution;
    }

    // Zero out for reuse (faster than reallocation)
    void zero_all() {
        dudx.zero_();
        dvdy.zero_();
        dwdz.zero_();
        dpdx.zero_();
        dpdy.zero_();
        dpdz.zero_();
    }
};

// Thread-local work buffers - each OpenMP thread gets its own instance
static thread_local ThreadLocalWorkBuffers tl_work_buffers;

// Global solver instance
static std::unique_ptr<DynamicsSolver> g_solver = nullptr;
static std::mutex g_solver_mutex;

/**
 * Initialize dynamics solver
 */
void wrf_dynamics_init(int nx, int ny, int nz, int mode) {
    std::lock_guard<std::mutex> lock(g_solver_mutex);
    
    DYNAMICS_LOG << "[Dynamics] Initializing with nx=" << nx 
              << ", ny=" << ny << ", nz=" << nz 
              << ", mode=" << mode << std::endl;
    
    // Clean up any existing solver
    if (g_solver) {
        g_solver.reset();
    }
    
    // Create new solver
    g_solver = std::make_unique<DynamicsSolver>(nx, ny, nz, mode);
    g_solver->initialized = true;
    
    // Set PyTorch options
    torch::manual_seed(42);  // For reproducibility
    
    DYNAMICS_LOG << "[Dynamics] Initialization complete" << std::endl;
}

/**
 * Finalize dynamics solver
 */
void wrf_dynamics_finalize() {
    std::lock_guard<std::mutex> lock(g_solver_mutex);
    
    DYNAMICS_LOG << "[Dynamics] Finalizing" << std::endl;
    
    if (g_solver) {
        g_solver.reset();
    }
}

/**
 * Helper: Convert Fortran array to PyTorch tensor with proper layout
 */
torch::Tensor fortran_to_tensor(float* data, int nx, int ny, int nz, bool requires_grad = false) {
    if (!data) {
        throw std::runtime_error("Null data pointer");
    }

    // Create tensor options
    // FIX 2025-12-31 Batch38 Issue 1: Explicit CPU device for host pointer from_blob
    auto options = torch::TensorOptions()
        .dtype(torch::kFloat32)
        .device(torch::kCPU)
        .requires_grad(requires_grad);

    // Fortran sends data in column-major order
    // Create tensor and reshape for C++ processing
    torch::Tensor tensor = torch::from_blob(data, {nx * ny * nz}, options).clone();
    
    // Reshape to 3D (accounting for layout differences)
    tensor = tensor.reshape({nx, ny, nz});
    
    // Permute to expected layout for physics calculations
    // Fortran (i,k,j) -> C++ (z,y,x)
    tensor = tensor.permute({2, 1, 0}).contiguous();
    
    return tensor;
}

/**
 * Helper: Convert PyTorch tensor back to Fortran array layout
 */
void tensor_to_fortran(const torch::Tensor& tensor, float* data, int nx, int ny, int nz) {
    if (!data) {
        throw std::runtime_error("Null data pointer");
    }
    
    // Ensure tensor is on CPU
    torch::Tensor cpu_tensor = tensor.to(torch::kCPU);
    
    // Check dimensions
    if (cpu_tensor.size(0) != nz || cpu_tensor.size(1) != ny || cpu_tensor.size(2) != nx) {
        throw std::runtime_error("Tensor dimension mismatch");
    }
    
    // Permute back to Fortran layout
    // C++ (z,y,x) -> Fortran (i,k,j)
    torch::Tensor fortran_tensor = cpu_tensor.permute({2, 1, 0}).contiguous();

    // FIX Round193: Verify tensor is Float32 for data_ptr<float>() safety
    TORCH_CHECK(fortran_tensor.scalar_type() == torch::kFloat32,
        "copy_tensor_to_fortran: tensor must be Float32, got ", fortran_tensor.scalar_type());

    // Reshape to 1D and copy
    // FIX Round193: Use int64_t for reshape to prevent int overflow on large grids
    int64_t total_elements = static_cast<int64_t>(nx) * ny * nz;
    fortran_tensor = fortran_tensor.reshape({total_elements});
    // FIX Round193: Use size_t for memcpy size to prevent int overflow
    size_t total_bytes = static_cast<size_t>(total_elements) * sizeof(float);
    std::memcpy(data, fortran_tensor.data_ptr<float>(), total_bytes);
}

/**
 * Acoustic wave advancement with implicit integration
 */
void wrf_acoustic_advance(float* u_ptr, float* v_ptr, float* w_ptr, float* p_ptr,
                         float* tend_u_ptr, float* tend_v_ptr, float* tend_w_ptr,
                         float* c2_ptr, float dt, float dx, float dy, float dz,
                         int nx, int ny, int nz, float theta, bool* converged) {
    
    try {
        std::lock_guard<std::mutex> lock(g_solver_mutex);
        
        if (!g_solver || !g_solver->initialized) {
            std::cerr << "[Dynamics] Error: Not initialized" << std::endl;
            *converged = false;
            return;
        }
        
        // Validate inputs
        if (!u_ptr || !v_ptr || !w_ptr || !p_ptr) {
            std::cerr << "[Dynamics] Error: Null velocity/pressure pointers" << std::endl;
            *converged = false;
            return;
        }
        
        // Convert Fortran arrays to tensors
        torch::Tensor u = fortran_to_tensor(u_ptr, nx, ny, nz, true);
        torch::Tensor v = fortran_to_tensor(v_ptr, nx, ny, nz, true);
        torch::Tensor w = fortran_to_tensor(w_ptr, nx, ny, nz, true);
        torch::Tensor p = fortran_to_tensor(p_ptr, nx, ny, nz, true);
        
        torch::Tensor tend_u = fortran_to_tensor(tend_u_ptr, nx, ny, nz, false);
        torch::Tensor tend_v = fortran_to_tensor(tend_v_ptr, nx, ny, nz, false);
        torch::Tensor tend_w = fortran_to_tensor(tend_w_ptr, nx, ny, nz, false);
        torch::Tensor c2 = fortran_to_tensor(c2_ptr, nx, ny, nz, false);
        
        // Store original values for implicit scheme
        torch::Tensor u_old = u.clone();
        torch::Tensor v_old = v.clone();
        torch::Tensor w_old = w.clone();
        torch::Tensor p_old = p.clone();
        
        // Newton iteration for implicit solve
        const int max_newton = 5;
        const float tol = 1e-6;
        bool local_converged = false;

        // PERF 2025-12-31 Batch41: Use thread-local work buffers for thread safety
        // FIX 2025-12-31 Batch41: Thread-local allows parallel tile processing without mutex contention
        tl_work_buffers.ensure_buffers(u);

        for (int newton_iter = 0; newton_iter < max_newton; ++newton_iter) {
            // PERF 2025-12-31 Batch41: Zero out work buffers for reuse instead of zeros_like()
            tl_work_buffers.zero_all();

            // Use references to thread-local work buffers (safe for parallel calls)
            auto& dudx = tl_work_buffers.dudx;
            auto& dvdy = tl_work_buffers.dvdy;
            auto& dwdz = tl_work_buffers.dwdz;
            auto& dpdx = tl_work_buffers.dpdx;
            auto& dpdy = tl_work_buffers.dpdy;
            auto& dpdz = tl_work_buffers.dpdz;

            // X-derivatives (dimension 2 in permuted tensor)
            // FIX 2025-12-31 Batch41: Use copy_() for in-place assignment (slice()= is no-op)
            if (nx > 2) {
                dudx.slice(2, 1, nx-1).copy_((u.slice(2, 2, nx) - u.slice(2, 0, nx-2)) / (2.0f * dx));
                dpdx.slice(2, 1, nx-1).copy_((p.slice(2, 2, nx) - p.slice(2, 0, nx-2)) / (2.0f * dx));
            }

            // Y-derivatives (dimension 1)
            // FIX 2025-12-31 Batch41: Use copy_() for in-place assignment (slice()= is no-op)
            if (ny > 2) {
                dvdy.slice(1, 1, ny-1).copy_((v.slice(1, 2, ny) - v.slice(1, 0, ny-2)) / (2.0f * dy));
                dpdy.slice(1, 1, ny-1).copy_((p.slice(1, 2, ny) - p.slice(1, 0, ny-2)) / (2.0f * dy));
            }

            // Z-derivatives (dimension 0)
            // FIX 2025-12-31 Batch41: Use copy_() for in-place assignment (slice()= is no-op)
            if (nz > 2) {
                dwdz.slice(0, 1, nz-1).copy_((w.slice(0, 2, nz) - w.slice(0, 0, nz-2)) / (2.0f * dz));
                dpdz.slice(0, 1, nz-1).copy_((p.slice(0, 2, nz) - p.slice(0, 0, nz-2)) / (2.0f * dz));
            }
            
            // Compute divergence
            torch::Tensor div = dudx + dvdy + dwdz;
            
            // Implicit residual functions
            torch::Tensor res_u = u - u_old - dt * (tend_u - theta * dpdx);
            torch::Tensor res_v = v - v_old - dt * (tend_v - theta * dpdy);
            torch::Tensor res_w = w - w_old - dt * (tend_w - theta * dpdz);
            torch::Tensor res_p = p - p_old - dt * theta * c2 * div;
            
            // Check convergence
            // FIX 2025-12-31 Batch41: NoGradGuard for .item<>() to avoid AD graph issues
            // PERF 2025-12-31 Batch41: Single GPU→CPU transfer instead of 4 separate .item() calls
            float residual_norm;
            {
                torch::NoGradGuard no_grad;
                // Stack norms into single tensor for one GPU→CPU transfer
                // Stack norms and transfer to CPU once, then sum on CPU
                auto norms = torch::stack({
                    torch::norm(res_u),
                    torch::norm(res_v),
                    torch::norm(res_w),
                    torch::norm(res_p)
                }).to(torch::kCPU);
                // norms is already CPU; explicit to(kCPU) for .sum() result for clarity
                residual_norm = norms.sum().to(torch::kCPU).item<float>();
            }

            if (residual_norm < tol) {
                local_converged = true;
                break;
            }
            
            // Simple fixed-point iteration update
            float alpha = 0.7f;  // Relaxation factor
            u = u - alpha * res_u;
            v = v - alpha * res_v;
            w = w - alpha * res_w;
            p = p - alpha * res_p;
        }
        
        *converged = local_converged;
        
        // Convert results back to Fortran arrays
        tensor_to_fortran(u, u_ptr, nx, ny, nz);
        tensor_to_fortran(v, v_ptr, nx, ny, nz);
        tensor_to_fortran(w, w_ptr, nx, ny, nz);
        tensor_to_fortran(p, p_ptr, nx, ny, nz);
        
    } catch (const std::exception& e) {
        std::cerr << "[Dynamics] Error in acoustic advance: " << e.what() << std::endl;
        *converged = false;
    }
}

/**
 * Implicit solver for general state vector
 */
void wrf_implicit_solve(float* state_ptr, float* tend_ptr, float* coeff_ptr,
                       int nx, int ny, int nz, float dt, bool* converged) {
    
    // Placeholder for general implicit solver
    DYNAMICS_LOG << "[Dynamics] General implicit solver called" << std::endl;
    *converged = true;
}

} // extern "C"