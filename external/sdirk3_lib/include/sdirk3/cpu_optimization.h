#ifndef WRF_SDIRK3_CPU_OPTIMIZATION_H
#define WRF_SDIRK3_CPU_OPTIMIZATION_H

#include "torch_standalone.h"
#include <omp.h>
#include <immintrin.h>  // For SIMD intrinsics
#include <memory>

namespace wrf {
namespace sdirk3 {
namespace cpu_opt {

// OPT Pass33+: Verbose output control (default: disabled for production)
// Set to true for debugging CPU optimization initialization
inline bool& get_verbose_flag() {
    static bool verbose = false;
    return verbose;
}

// CPU-specific optimization settings
struct CPUConfig {
    int num_threads = 0;  // 0 means use all available
    bool use_mkl = true;
    bool use_openmp = true;
    bool use_simd = true;
    bool force_contiguous = true;
    int cache_line_size = 64;
    
    // Thread affinity settings
    bool bind_threads = true;
    std::string affinity_type = "scatter";  // scatter, compact, explicit
    
    // Memory settings
    size_t l1_cache_size = 32 * 1024;      // 32 KB
    size_t l2_cache_size = 256 * 1024;     // 256 KB
    size_t l3_cache_size = 8 * 1024 * 1024; // 8 MB
    
    // Blocking parameters for cache optimization
    int block_size_i = 32;
    int block_size_j = 32;
    int block_size_k = 8;
    
    static CPUConfig& get_instance() {
        static CPUConfig instance;
        return instance;
    }
    
    void initialize() {
        if (num_threads == 0) {
            num_threads = omp_get_max_threads();
        }
        
        // Set PyTorch to use CPU only
        torch::set_num_threads(num_threads);
        torch::set_num_interop_threads(1);
        
        // Disable GPU
        // OPT Pass33+: Gate warning output with verbose flag
        if (torch::cuda::is_available() && get_verbose_flag()) {
            std::cerr << "Warning: CUDA available but CPU-only mode enforced" << std::endl;
        }
        
        // Set MKL threads if available
        #ifdef USE_MKL
        mkl_set_num_threads(num_threads);
        #endif
        
        // Configure OpenMP
        if (use_openmp) {
            omp_set_num_threads(num_threads);
            if (bind_threads) {
                omp_set_proc_bind(omp_proc_bind_spread);
            }
        }
    }
};

// Force tensor to be CPU and contiguous
inline torch::Tensor ensure_cpu_contiguous(const torch::Tensor& t) {
    if (!t.is_cpu()) {
        return t.to(torch::kCPU).contiguous();
    } else if (!t.is_contiguous()) {
        return t.contiguous();
    }
    return t;
}

// FIX Round194: Force tensor to be CPU, contiguous, AND Float32
// Use this before data_ptr<float>() to guarantee type safety
inline torch::Tensor ensure_cpu_contiguous_float32(const torch::Tensor& t) {
    torch::Tensor result = t;
    // Ensure CPU first (move to CPU if on GPU)
    if (!result.is_cpu()) {
        result = result.to(torch::kCPU);
    }
    // Ensure Float32 dtype (convert if needed)
    if (result.scalar_type() != torch::kFloat32) {
        result = result.to(torch::kFloat32);
    }
    // Ensure contiguous layout
    if (!result.is_contiguous()) {
        result = result.contiguous();
    }
    return result;
}

// FIX Round194: Macro for asserting CPU + contiguous + Float32 before data_ptr<float>()
#define CHECK_CPU_CONTIGUOUS_FLOAT32(x) \
    TORCH_CHECK((x).is_cpu(), #x " must be a CPU tensor for data_ptr<float>()"); \
    TORCH_CHECK((x).is_contiguous(), #x " must be contiguous for data_ptr<float>()"); \
    TORCH_CHECK((x).scalar_type() == torch::kFloat32, #x " must be Float32 for data_ptr<float>(), got ", (x).scalar_type())

// Legacy macro for backward compatibility (CPU + contiguous only)
#ifndef CHECK_CPU_CONTIGUOUS
#define CHECK_CPU_CONTIGUOUS(x) \
    TORCH_CHECK((x).is_cpu(), #x " must be a CPU tensor"); \
    TORCH_CHECK((x).is_contiguous(), #x " must be contiguous")
#endif

// Optimized matrix-vector multiplication for CPU
torch::Tensor cpu_optimized_matvec(const torch::Tensor& matrix, 
                                  const torch::Tensor& vector);

// Optimized element-wise operations
torch::Tensor cpu_optimized_add(const torch::Tensor& a, 
                                const torch::Tensor& b, 
                                double alpha = 1.0);

// Cache-aware blocking for 3D operations
template<typename Func>
void blocked_3d_operation(int nx, int ny, int nz,
                         int block_i, int block_j, int block_k,
                         Func&& operation) {
    #pragma omp parallel for collapse(3) schedule(static)
    for (int jb = 0; jb < ny; jb += block_j) {
        for (int kb = 0; kb < nz; kb += block_k) {
            for (int ib = 0; ib < nx; ib += block_i) {
                int je = std::min(jb + block_j, ny);
                int ke = std::min(kb + block_k, nz);
                int ie = std::min(ib + block_i, nx);
                
                // Process block
                for (int j = jb; j < je; ++j) {
                    for (int k = kb; k < ke; ++k) {
                        #pragma omp simd
                        for (int i = ib; i < ie; ++i) {
                            operation(i, j, k);
                        }
                    }
                }
            }
        }
    }
}

// SIMD-optimized operations
class SIMDOps {
public:
    // Vectorized add: c = a + alpha * b
    static void vec_add(float* c, const float* a, const float* b, 
                       float alpha, size_t n) {
        const size_t simd_width = 8;  // AVX: 8 floats
        const size_t vec_size = n - (n % simd_width);
        
        __m256 alpha_vec = _mm256_set1_ps(alpha);
        
        #pragma omp parallel for
        for (size_t i = 0; i < vec_size; i += simd_width) {
            __m256 a_vec = _mm256_loadu_ps(&a[i]);
            __m256 b_vec = _mm256_loadu_ps(&b[i]);
            __m256 result = _mm256_fmadd_ps(alpha_vec, b_vec, a_vec);
            _mm256_storeu_ps(&c[i], result);
        }
        
        // Handle remainder
        for (size_t i = vec_size; i < n; ++i) {
            c[i] = a[i] + alpha * b[i];
        }
    }
    
    // Vectorized dot product
    static float vec_dot(const float* a, const float* b, size_t n) {
        const size_t simd_width = 8;
        const size_t vec_size = n - (n % simd_width);
        
        __m256 sum_vec = _mm256_setzero_ps();
        
        for (size_t i = 0; i < vec_size; i += simd_width) {
            __m256 a_vec = _mm256_loadu_ps(&a[i]);
            __m256 b_vec = _mm256_loadu_ps(&b[i]);
            sum_vec = _mm256_fmadd_ps(a_vec, b_vec, sum_vec);
        }
        
        // Horizontal sum
        float sum_array[8];
        _mm256_storeu_ps(sum_array, sum_vec);
        float sum = 0.0f;
        for (int i = 0; i < 8; ++i) {
            sum += sum_array[i];
        }
        
        // Handle remainder
        for (size_t i = vec_size; i < n; ++i) {
            sum += a[i] * b[i];
        }
        
        return sum;
    }
};

// Memory pool for efficient allocation
class MemoryPool {
private:
    struct Block {
        std::unique_ptr<float[]> data;
        size_t size;
        bool in_use;
    };
    
    std::vector<Block> blocks_;
    std::mutex mutex_;
    size_t total_allocated_ = 0;
    size_t high_water_mark_ = 0;
    
public:
    float* allocate(size_t size) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Find free block of sufficient size
        for (auto& block : blocks_) {
            if (!block.in_use && block.size >= size) {
                block.in_use = true;
                return block.data.get();
            }
        }
        
        // Allocate new block
        Block new_block;
        new_block.size = size;
        new_block.in_use = true;
        new_block.data = std::make_unique<float[]>(size);
        
        total_allocated_ += size * sizeof(float);
        high_water_mark_ = std::max(high_water_mark_, total_allocated_);
        
        blocks_.push_back(std::move(new_block));
        return blocks_.back().data.get();
    }
    
    void deallocate(float* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (auto& block : blocks_) {
            if (block.data.get() == ptr) {
                block.in_use = false;
                return;
            }
        }
    }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        blocks_.clear();
        total_allocated_ = 0;
    }
    
    size_t get_high_water_mark() const {
        return high_water_mark_;
    }
};

// Thread-local workspace for temporary arrays
class ThreadLocalWorkspace {
private:
    static thread_local std::vector<torch::Tensor> workspace_;
    static thread_local size_t workspace_size_;
    
public:
    static torch::Tensor get_workspace(const torch::IntArrayRef& shape, 
                                      const torch::TensorOptions& options) {
        size_t required_size = 1;
        for (auto s : shape) {
            required_size *= s;
        }
        
        // Reuse existing workspace if large enough
        if (!workspace_.empty() && workspace_[0].numel() >= required_size) {
            return workspace_[0].view(shape);
        }
        
        // Allocate new workspace
        workspace_.clear();
        workspace_.push_back(torch::empty(shape, options.device(torch::kCPU)));
        workspace_size_ = required_size;
        
        return workspace_[0];
    }
    
    static void clear() {
        workspace_.clear();
        workspace_size_ = 0;
    }
};

// CPU-optimized tensor operations
class CPUTensorOps {
public:
    // Fused multiply-add: c = a + alpha * b
    // FIX Round194: Use ensure_cpu_contiguous_float32 to guarantee dtype for data_ptr<float>()
    static torch::Tensor fma(const torch::Tensor& a,
                            const torch::Tensor& b,
                            double alpha) {
        auto a_cpu = ensure_cpu_contiguous_float32(a);
        auto b_cpu = ensure_cpu_contiguous_float32(b);
        auto c = torch::empty_like(a_cpu);

        float* a_data = a_cpu.data_ptr<float>();
        float* b_data = b_cpu.data_ptr<float>();
        float* c_data = c.data_ptr<float>();

        SIMDOps::vec_add(c_data, a_data, b_data, alpha, a_cpu.numel());

        return c;
    }

    // Optimized batched matrix multiplication
    // FIX Round194: Use ensure_cpu_contiguous_float32 for dtype safety
    static torch::Tensor batched_matmul(const torch::Tensor& a,
                                       const torch::Tensor& b) {
        // Ensure CPU + Float32 tensors for BLAS compatibility
        auto a_cpu = ensure_cpu_contiguous_float32(a);
        auto b_cpu = ensure_cpu_contiguous_float32(b);
        
        // Use optimized BLAS if available
        #ifdef USE_MKL
        return torch::bmm(a_cpu, b_cpu);
        #else
        // Fall back to PyTorch implementation
        return torch::matmul(a_cpu, b_cpu);
        #endif
    }
};

// Initialize CPU optimizations
// OPT Pass33+: Verbose output gated by get_verbose_flag() (default: false)
inline void initialize_cpu_optimizations() {
    auto& config = CPUConfig::get_instance();
    config.initialize();

    if (get_verbose_flag()) {
        std::cout << "CPU Optimizations initialized:" << std::endl;
        std::cout << "  Threads: " << config.num_threads << std::endl;
        std::cout << "  OpenMP: " << (config.use_openmp ? "enabled" : "disabled") << std::endl;
        std::cout << "  SIMD: " << (config.use_simd ? "enabled" : "disabled") << std::endl;
        std::cout << "  Thread binding: " << (config.bind_threads ? "enabled" : "disabled") << std::endl;
    }
}

} // namespace cpu_opt
} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_CPU_OPTIMIZATION_H