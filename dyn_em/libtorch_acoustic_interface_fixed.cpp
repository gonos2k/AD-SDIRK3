#include <torch/torch.h>
#include <iostream>
#include <vector>
#include <memory>
#include <cstring>
#include <mutex>

// OPT Pass33+: Compile-time log gating for production builds
// Define LIBTORCH_ACOUSTIC_VERBOSE to enable diagnostic output
// Build with: -DLIBTORCH_ACOUSTIC_VERBOSE for debug builds
#ifdef LIBTORCH_ACOUSTIC_VERBOSE
#define ACOUSTIC_LOG std::cout
#else
// Null stream pattern: if(true);else suppresses "unreachable code" static analysis warnings
// while still allowing compiler to optimize away the entire statement
#define ACOUSTIC_LOG if(true) ; else std::cout
#endif

extern "C" {

// LibTorch Acoustic Wave Processing Interface for WRF
// Fixed version with proper memory management and array layout handling

struct AcousticTensorState {
    torch::Tensor u_tensor;
    torch::Tensor v_tensor;
    torch::Tensor p_tensor;
    torch::Tensor ru_tend_tensor;
    torch::Tensor rv_tend_tensor;
    torch::optim::Adam* optimizer;
    bool initialized;
    std::mutex state_mutex;  // Thread safety
    
    AcousticTensorState() : optimizer(nullptr), initialized(false) {}
    
    ~AcousticTensorState() {
        std::lock_guard<std::mutex> lock(state_mutex);
        if (optimizer) {
            delete optimizer;
            optimizer = nullptr;
        }
    }
};

// Global state for acoustic processing
static std::unique_ptr<AcousticTensorState> g_acoustic_state = nullptr;
static std::mutex g_state_mutex;  // Global state protection

// Initialize LibTorch acoustic processing
void libtorch_acoustic_initialize(int mode) {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    ACOUSTIC_LOG << "[LibTorch Acoustic C++] Initializing with mode: " << mode << std::endl;
    
    // Clean up any existing state
    if (g_acoustic_state) {
        g_acoustic_state.reset();
    }
    
    g_acoustic_state = std::make_unique<AcousticTensorState>();
    g_acoustic_state->initialized = true;
    
    // Set PyTorch to use deterministic algorithms for reproducibility
    torch::manual_seed(42);
    
    ACOUSTIC_LOG << "[LibTorch Acoustic C++] Initialization complete" << std::endl;
}

// Helper function to safely create tensor from Fortran data
torch::Tensor create_tensor_from_fortran(float* data, int nx, int ny, int nz, bool requires_grad = false) {
    if (!data) {
        throw std::runtime_error("Null data pointer provided");
    }

    // Fortran sends data in column-major order (nx, ny, nz)
    // But C++ PyTorch expects row-major {nz, ny, nx}
    // We need to transpose the data properly

    // FIX 2025-12-31 Batch38 Issue 1: Explicit CPU device for host pointer from_blob
    auto options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU).requires_grad(requires_grad);

    // Create tensor with Fortran memory layout
    torch::Tensor tensor_fortran = torch::from_blob(data, {nx, ny, nz}, options).clone();
    
    // Permute to C++ expected layout: (nx,ny,nz) -> (nz,ny,nx)
    torch::Tensor tensor_cpp = tensor_fortran.permute({2, 1, 0}).contiguous();
    
    return tensor_cpp;
}

// Helper function to copy tensor back to Fortran data
void copy_tensor_to_fortran(const torch::Tensor& tensor, float* data, int nx, int ny, int nz) {
    if (!data) {
        throw std::runtime_error("Null data pointer provided");
    }
    
    // Ensure tensor is on CPU and contiguous
    torch::Tensor tensor_cpu = tensor.to(torch::kCPU).contiguous();
    
    // Check dimensions
    if (tensor_cpu.size(0) != nz || tensor_cpu.size(1) != ny || tensor_cpu.size(2) != nx) {
        throw std::runtime_error("Tensor dimensions mismatch");
    }
    
    // Permute from C++ layout (nz,ny,nx) back to Fortran layout (nx,ny,nz)
    torch::Tensor tensor_fortran = tensor_cpu.permute({2, 1, 0}).contiguous();

    // FIX Round193: Verify tensor is Float32 for data_ptr<float>() safety
    TORCH_CHECK(tensor_fortran.scalar_type() == torch::kFloat32,
        "tensor_to_fortran_cpu: tensor must be Float32, got ", tensor_fortran.scalar_type());

    // Copy data
    // FIX Round193: Use size_t to prevent int overflow on large grids (>2^31 elements)
    size_t total_bytes = static_cast<size_t>(nx) * ny * nz * sizeof(float);
    std::memcpy(data, tensor_fortran.data_ptr<float>(), total_bytes);
}

// Advanced acoustic wave processing with automatic differentiation
void libtorch_acoustic_advance_uv_ad(
    float* u_data, float* v_data, float* p_data,
    float* ru_tend_data, float* rv_tend_data,
    int nx, int ny, int nz, float dt, float rdx, float rdy, int mode) {
    
    try {
        std::lock_guard<std::mutex> lock(g_state_mutex);
        
        if (!g_acoustic_state || !g_acoustic_state->initialized) {
            std::cerr << "[LibTorch Acoustic] Error: Not initialized" << std::endl;
            return;
        }
        
        // Validate inputs
        if (!u_data || !v_data || !p_data || !ru_tend_data || !rv_tend_data) {
            std::cerr << "[LibTorch Acoustic] Error: Null pointer(s) provided" << std::endl;
            return;
        }
        
        if (nx <= 0 || ny <= 0 || nz <= 0) {
            std::cerr << "[LibTorch Acoustic] Error: Invalid dimensions" << std::endl;
            return;
        }
        
        ACOUSTIC_LOG << "[LibTorch Acoustic C++] Processing " << nx << "x" << ny << "x" << nz 
                  << " grid with mode " << mode << std::endl;
        
        // Create tensors with proper memory layout conversion
        torch::Tensor u = create_tensor_from_fortran(u_data, nx, ny, nz, true);
        torch::Tensor v = create_tensor_from_fortran(v_data, nx, ny, nz, true);
        torch::Tensor p = create_tensor_from_fortran(p_data, nx, ny, nz, true);
        torch::Tensor ru_tend = create_tensor_from_fortran(ru_tend_data, nx, ny, nz, false);
        torch::Tensor rv_tend = create_tensor_from_fortran(rv_tend_data, nx, ny, nz, false);
        
        // Store original values for stability check
        torch::Tensor u_orig = u.clone();
        torch::Tensor v_orig = v.clone();
        
        switch (mode) {
            case 1: { // Basic Automatic Differentiation
                ACOUSTIC_LOG << "[LibTorch Acoustic C++] Applying Mode 1: Basic AD" << std::endl;

                // Compute pressure gradients using automatic differentiation
                auto dpdx = torch::zeros_like(p);
                auto dpdy = torch::zeros_like(p);

                // Central difference with AD (accounting for C++ tensor layout)
                // In C++ layout: dim 2 is x, dim 1 is y, dim 0 is z
                // FIX 2025-12-31 Batch41: Use copy_() for in-place assignment (slice()= is no-op)
                // PERF 2025-12-31 Batch41: Cache repeated slices - views into p, not clones
                if (nx > 2) {
                    auto p_x_hi = p.slice(2, 2, nx);      // p(i+1)
                    auto p_x_lo = p.slice(2, 0, nx-2);    // p(i-1)
                    dpdx.slice(2, 1, nx-1).copy_((p_x_hi - p_x_lo) / (2.0 * rdx));
                }
                if (ny > 2) {
                    auto p_y_hi = p.slice(1, 2, ny);      // p(j+1)
                    auto p_y_lo = p.slice(1, 0, ny-2);    // p(j-1)
                    dpdy.slice(1, 1, ny-1).copy_((p_y_hi - p_y_lo) / (2.0 * rdy));
                }

                // Update velocities with gradient information
                u = u + dt * (ru_tend - 0.1 * dpdx);
                v = v + dt * (rv_tend - 0.1 * dpdy);

                break;
            }
            
            case 2: { // Gradient-based Optimization
                ACOUSTIC_LOG << "[LibTorch Acoustic C++] Applying Mode 2: Gradient Optimization" << std::endl;
                
                // Create parameter list for optimization
                std::vector<torch::Tensor> parameters = {u, v};
                
                // Setup or update optimizer
                if (!g_acoustic_state->optimizer) {
                    g_acoustic_state->optimizer = new torch::optim::Adam(parameters, 
                        torch::optim::AdamOptions(0.001));  // Reduced learning rate
                } else {
                    // Update parameters in existing optimizer
                    g_acoustic_state->optimizer->param_groups()[0].params() = parameters;
                }
                
                // Optimization loop
                const int max_iter = 3;  // Reduced iterations for stability
                for (int iter = 0; iter < max_iter; ++iter) {
                    g_acoustic_state->optimizer->zero_grad();
                    
                    // Define loss function (acoustic wave energy)
                    auto kinetic_energy = 0.5 * (u.pow(2) + v.pow(2)).mean();
                    auto pressure_energy = 0.5 * p.pow(2).mean();
                    auto loss = kinetic_energy + pressure_energy;
                    
                    // Add regularization to prevent instability
                    auto regularization = 0.01 * ((u - u_orig).pow(2).mean() + (v - v_orig).pow(2).mean());
                    loss = loss + regularization;
                    
                    loss.backward();
                    
                    // Gradient clipping for stability
                    torch::nn::utils::clip_grad_norm_(parameters, 1.0);
                    
                    g_acoustic_state->optimizer->step();
                    
                    if (iter == 0 || iter == max_iter - 1) {
                        // FIX 2025-12-31 Batch41: NoGradGuard + to(kCPU) for diagnostic
                        float loss_val;
                        {
                            torch::NoGradGuard no_grad;
                            loss_val = loss.to(torch::kCPU).item<float>();
                        }
                        ACOUSTIC_LOG << "[LibTorch Acoustic C++] Iteration " << iter
                                  << ", Loss: " << loss_val << std::endl;
                    }
                }

                break;
            }

            case 3: { // Full Computational Graph Optimization
                ACOUSTIC_LOG << "[LibTorch Acoustic C++] Applying Mode 3: Full Graph Optimization" << std::endl;
                
                // Compute CFL condition using tensors
                auto cfl_u = torch::abs(u) * rdx * dt;
                auto cfl_v = torch::abs(v) * rdy * dt;
                auto max_cfl = torch::max(torch::max(cfl_u), torch::max(cfl_v));

                // FIX 2025-12-31 Batch41: NoGradGuard + to(kCPU) for diagnostic
                float cfl_value;
                {
                    torch::NoGradGuard no_grad;
                    cfl_value = max_cfl.to(torch::kCPU).item<float>();
                }
                float adaptive_dt = dt;
                
                if (cfl_value > 0.8f) {
                    adaptive_dt = dt * 0.8f / cfl_value;
                    ACOUSTIC_LOG << "[LibTorch Acoustic C++] Adaptive dt: " << adaptive_dt 
                              << " (CFL: " << cfl_value << ")" << std::endl;
                }
                
                // Advanced pressure gradient computation with optimized coefficients
                auto dpdx = torch::zeros_like(p);
                auto dpdy = torch::zeros_like(p);
                
                // Use optimized finite difference coefficients
                // FIX 2025-12-31 Batch41: Use copy_() for in-place assignment (slice()= is no-op)
                // PERF 2025-12-31 Batch41: Cache repeated slices to reduce kernel launches
                // =============================================================================
                // VIEW SHARING NOTE (2025-12-31 Batch41):
                // These cached slices are VIEWS (not clones) - they share storage with p.
                // This is INTENTIONAL and CORRECT:
                // - Views avoid memory allocation overhead
                // - The slices are used immediately in arithmetic, then discarded
                // - No .clone() needed since we only READ from these slices
                //
                // FIX 2025-12-31 Batch41: VIEW INVALIDATION SAFETY ANALYSIS
                // VERIFIED SAFE: p is never modified between view creation and use.
                // Code path:
                //   1. p created from Fortran data (line 132)
                //   2. dpdx/dpdy zeros_like(p) - reads p's metadata only
                //   3. p_x_*, p_y_* slices created (views into p)
                //   4. Slices used immediately in dpdx/dpdy computation
                //   5. u, v updated - p NOT modified
                //   6. End of computation - views go out of scope
                //
                // WOULD BE UNSAFE IF:
                // - p.copy_(...) or p = p + ... between steps 3 and 4
                // - p passed to function that modifies it between slice creation and use
                // - Multi-threaded access where another thread modifies p
                //
                // RECOMMENDATION: If p modification is ever added, either:
                // 1. Move modification after view usage, OR
                // 2. Add .clone() to cached slices that need isolation
                // =============================================================================
                if (nx > 4 && ny > 4) {
                    // 4th order accurate gradients - cache slices (views, not clones)
                    float c1 = 1.125f, c2 = -0.041667f;

                    // Cache X-direction slices (views into p)
                    auto p_x_p1 = p.slice(2, 3, nx-1);   // p(i+1)
                    auto p_x_m1 = p.slice(2, 1, nx-3);   // p(i-1)
                    auto p_x_p2 = p.slice(2, 4, nx);     // p(i+2)
                    auto p_x_m2 = p.slice(2, 0, nx-4);   // p(i-2)

                    // Cache Y-direction slices (views into p)
                    auto p_y_p1 = p.slice(1, 3, ny-1);   // p(j+1)
                    auto p_y_m1 = p.slice(1, 1, ny-3);   // p(j-1)
                    auto p_y_p2 = p.slice(1, 4, ny);     // p(j+2)
                    auto p_y_m2 = p.slice(1, 0, ny-4);   // p(j-2)

                    dpdx.slice(2, 2, nx-2).copy_((c1 * (p_x_p1 - p_x_m1) + c2 * (p_x_p2 - p_x_m2)) / rdx);
                    dpdy.slice(1, 2, ny-2).copy_((c1 * (p_y_p1 - p_y_m1) + c2 * (p_y_p2 - p_y_m2)) / rdy);
                } else {
                    // Fallback to 2nd order - cache slices (views, not clones)
                    if (nx > 2) {
                        auto p_x_hi = p.slice(2, 2, nx);
                        auto p_x_lo = p.slice(2, 0, nx-2);
                        dpdx.slice(2, 1, nx-1).copy_((p_x_hi - p_x_lo) / (2.0 * rdx));
                    }
                    if (ny > 2) {
                        auto p_y_hi = p.slice(1, 2, ny);
                        auto p_y_lo = p.slice(1, 0, ny-2);
                        dpdy.slice(1, 1, ny-1).copy_((p_y_hi - p_y_lo) / (2.0 * rdy));
                    }
                }
                
                // Update with optimized coefficients and adaptive timestep
                u = u + adaptive_dt * (ru_tend - dpdx);
                v = v + adaptive_dt * (rv_tend - dpdy);
                
                // Apply smoothing filter for stability
                if (nx > 2 && ny > 2 && nz > 2) {
                    u = torch::nn::functional::avg_pool3d(
                        u.unsqueeze(0).unsqueeze(0), 
                        torch::nn::functional::AvgPool3dFuncOptions(3).stride(1).padding(1)
                    ).squeeze();
                    v = torch::nn::functional::avg_pool3d(
                        v.unsqueeze(0).unsqueeze(0), 
                        torch::nn::functional::AvgPool3dFuncOptions(3).stride(1).padding(1)
                    ).squeeze();
                }
                
                break;
            }
            
            default:
                ACOUSTIC_LOG << "[LibTorch Acoustic C++] Unknown mode: " << mode << std::endl;
                return;
        }
        
        // Ensure numerical stability
        u = torch::clamp(u, -1e6, 1e6);
        v = torch::clamp(v, -1e6, 1e6);
        
        // Copy results back to Fortran arrays with proper layout conversion
        copy_tensor_to_fortran(u, u_data, nx, ny, nz);
        copy_tensor_to_fortran(v, v_data, nx, ny, nz);
        
        ACOUSTIC_LOG << "[LibTorch Acoustic C++] Processing complete" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[LibTorch Acoustic] Error in advance_uv_ad: " << e.what() << std::endl;
    }
}

// Compute acoustic wave gradients
void libtorch_compute_acoustic_gradients(
    float* u_data, float* v_data, float* p_data,
    float* grad_u, float* grad_v, float* grad_p,
    int nx, int ny, int nz) {
    
    try {
        ACOUSTIC_LOG << "[LibTorch Acoustic C++] Computing gradients" << std::endl;
        
        // Validate inputs
        if (!u_data || !v_data || !p_data || !grad_u || !grad_v || !grad_p) {
            std::cerr << "[LibTorch Acoustic] Error: Null pointer(s) provided" << std::endl;
            return;
        }
        
        // Create tensors with gradient computation enabled
        torch::Tensor u = create_tensor_from_fortran(u_data, nx, ny, nz, true);
        torch::Tensor v = create_tensor_from_fortran(v_data, nx, ny, nz, true);
        torch::Tensor p = create_tensor_from_fortran(p_data, nx, ny, nz, true);
        
        // Define acoustic wave energy functional
        auto energy = 0.5 * torch::sum(u.pow(2) + v.pow(2) + p.pow(2));
        
        // Compute gradients using automatic differentiation
        energy.backward();
        
        // Copy gradients back
        // FIX Round194: Use size_t to prevent int overflow on large grids
        const size_t total_size = static_cast<size_t>(nx) * static_cast<size_t>(ny) *
                                  static_cast<size_t>(nz) * sizeof(float);
        if (u.grad().defined()) {
            copy_tensor_to_fortran(u.grad(), grad_u, nx, ny, nz);
        } else {
            std::memset(grad_u, 0, total_size);
        }

        if (v.grad().defined()) {
            copy_tensor_to_fortran(v.grad(), grad_v, nx, ny, nz);
        } else {
            std::memset(grad_v, 0, total_size);
        }

        if (p.grad().defined()) {
            copy_tensor_to_fortran(p.grad(), grad_p, nx, ny, nz);
        } else {
            std::memset(grad_p, 0, total_size);
        }
        
        ACOUSTIC_LOG << "[LibTorch Acoustic C++] Gradient computation complete" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "[LibTorch Acoustic] Error computing gradients: " << e.what() << std::endl;
    }
}

// Optimize acoustic timestep using computational graphs
float libtorch_optimize_acoustic_timestep(
    float* u_data, float* v_data, float* p_data,
    int nx, int ny, int nz, float dt, float rdx, float rdy) {
    
    try {
        ACOUSTIC_LOG << "[LibTorch Acoustic C++] Optimizing timestep" << std::endl;
        
        // Validate inputs
        if (!u_data || !v_data || !p_data) {
            std::cerr << "[LibTorch Acoustic] Error: Null pointer(s) provided" << std::endl;
            return dt;
        }
        
        // Create tensors
        torch::Tensor u = create_tensor_from_fortran(u_data, nx, ny, nz, false);
        torch::Tensor v = create_tensor_from_fortran(v_data, nx, ny, nz, false);
        torch::Tensor p = create_tensor_from_fortran(p_data, nx, ny, nz, false);
        
        // Compute maximum velocities for CFL analysis
        // FIX 2025-12-31 Batch41: NoGradGuard for .item<>() to avoid AD graph issues
        // PERF 2025-12-31 Batch41: Single GPU→CPU transfer instead of 3 separate .item() calls
        // FIX Round194: Added kFloat32 to ensure dtype matches data_ptr<float>()
        float max_u, max_v, max_p;
        {
            torch::NoGradGuard no_grad;
            // Stack max values into single tensor for one GPU→CPU transfer
            // Ensure Float32 dtype for data_ptr<float>() safety
            auto maxvals = torch::stack({
                torch::max(torch::abs(u)),
                torch::max(torch::abs(v)),
                torch::max(torch::abs(p))  // Speed of sound approximation
            }).to(torch::kCPU, torch::kFloat32);
            auto maxvals_ptr = maxvals.data_ptr<float>();
            max_u = maxvals_ptr[0];
            max_v = maxvals_ptr[1];
            max_p = maxvals_ptr[2];
        }
        float sound_speed = std::sqrt(1.4f * std::max(max_p, 1.0f));  // Avoid sqrt of negative
        
        // CFL condition for acoustic waves
        float cfl_acoustic = (max_u + sound_speed) / rdx + (max_v + sound_speed) / rdy;
        
        if (cfl_acoustic > 0.0f) {
            float optimal_dt = 0.5f / cfl_acoustic;  // 50% of maximum stable timestep
            optimal_dt = std::min(optimal_dt, dt);  // Don't exceed original timestep
            
            ACOUSTIC_LOG << "[LibTorch Acoustic C++] Optimal dt: " << optimal_dt 
                      << " (original: " << dt << ")" << std::endl;
            
            return optimal_dt;
        } else {
            return dt;  // Return original if calculation fails
        }
        
    } catch (const std::exception& e) {
        std::cerr << "[LibTorch Acoustic] Error optimizing timestep: " << e.what() << std::endl;
        return dt;  // Return original timestep on error
    }
}

// Cleanup acoustic processing
void libtorch_acoustic_finalize() {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    
    ACOUSTIC_LOG << "[LibTorch Acoustic C++] Finalizing" << std::endl;
    if (g_acoustic_state) {
        g_acoustic_state.reset();
    }
}

} // extern "C"