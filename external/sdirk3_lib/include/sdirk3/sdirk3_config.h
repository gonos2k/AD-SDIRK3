#ifndef WRF_SDIRK3_CONFIG_H
#define WRF_SDIRK3_CONFIG_H

// Force CPU-only build
#define SDIRK3_CPU_ONLY 1

// Disable CUDA even if available
#ifdef USE_CUDA
#undef USE_CUDA
#endif

// CPU optimization flags
#define USE_OPENMP 1
#define USE_SIMD 1
#define USE_MKL 0  // Set to 1 if Intel MKL is available

// Cache optimization parameters
#define CACHE_LINE_SIZE 64
#define L1_CACHE_SIZE (32 * 1024)
#define L2_CACHE_SIZE (256 * 1024)
#define L3_CACHE_SIZE (8 * 1024 * 1024)

// Blocking factors for cache efficiency
#define BLOCK_SIZE_I 32
#define BLOCK_SIZE_J 32
#define BLOCK_SIZE_K 8

// Thread configuration
#define DEFAULT_NUM_THREADS 0  // 0 means use all available
#define BIND_THREADS 1
#define THREAD_AFFINITY "scatter"

// Memory pool settings
#define USE_MEMORY_POOL 1
#define MEMORY_POOL_INITIAL_SIZE (100 * 1024 * 1024)  // 100 MB

// Numerical settings
#define DEFAULT_NEWTON_TOL 1e-6
#define DEFAULT_GMRES_TOL 1e-7
#define DEFAULT_GMRES_RESTART 30
#define DEFAULT_MAX_NEWTON_ITER 10
#define DEFAULT_MAX_GMRES_ITER 100

// Debug settings
#define ENABLE_TIMING 1
#define ENABLE_CONVERGENCE_MONITORING 1
#define ENABLE_MEMORY_TRACKING 0

// Ensure PyTorch uses CPU
#define TORCH_USE_CUDA 0

#endif // WRF_SDIRK3_CONFIG_H