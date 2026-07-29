/**
 * @file wrf_sdirk3_base_state_zerocopy.cpp
 * @brief Zero-copy implementation for base state initialization
 *
 * This file provides an efficient zero-copy interface for setting base state
 * variables, eliminating the need for temporary array allocation and data copying
 * that exists in the current implementation.
 *
 * DEBUG LEVEL POLICY (FIX Round155):
 *   - debug_level >= 2: Standard diagnostics (shape, basic info)
 *   - debug_level >= 3: Heavy diagnostics (min/max/nonzero full tensor scans)
 * This aligns with tile_unified_impl.cpp policy for performance consistency.
 */

// 9F.D44 (review P0 section 4): include the PUBLIC interface header. This file defines
// extern "C" symbols that wrf_sdirk3_interface.h declares, and it did not include that
// header -- so a declaration and its definition could disagree and still compile. They
// did: sdirk3_tile_set_base_state_zerocopy_v2's 6th parameter is `float* theta_base_ptr`
// in the header and `int its` in the definition. Any caller compiled against the header
// would pass arguments the definition reinterprets; that is undefined behaviour, not a
// style problem. Including the header makes the compiler the gate, permanently.
#include "wrf_sdirk3_interface.h"

#include "wrf_sdirk3_tile_unified.h"
#include "wrf_sdirk3_unified_rhs_extended.h"
#include "wrf_sdirk3_config.h"
#include <torch/torch.h>
#include <mutex>
#include <iostream>
#include <unordered_map>
#include <cmath>
#include <atomic>  // OPT Pass33+: For diagnostic sampling counters

// Declare the global solver registry from wrf_sdirk3_interface_zerocopy.cpp.
// 9F.D41 (review P0-3): the MUTEX is declared here too. This file previously externed
// only the map and then called find() on it unlocked, while the owning TU guards every
// one of its own accesses (wrf_sdirk3_interface_zerocopy.cpp:140). A concurrent create
// or destroy could rehash the map under the find, or free the solver between the find
// and the dereference. Base-state setup is once-per-run, not a hot path, so the lock is
// held across the whole operation -- lifecycle safety costs nothing measurable here.
extern std::unordered_map<void*, std::unique_ptr<TileSDIRK3UnifiedSolver>> g_tile_solvers;
extern std::mutex g_tile_solvers_mutex;

namespace {
    /**
     * Helper function to compute 3D array index for Fortran memory layout
     * Fortran order: (i,k,j) with i varying fastest
     */
    // 9F.D44 (review P0 section 5): widen FIRST. The return type was already int64_t,
    // but every operand was int, so the two products were evaluated in 32-bit and only
    // the result was widened -- signed overflow (UB) for a large enough memory domain,
    // producing a negative or wrapped offset that then feeds from_blob(). The cast has
    // to be on the operands, not the return.
    inline int64_t index_3d(int i, int k, int j,
                           int ims, int kms, int jms,
                           int ime, int kme) {
        const int64_t ni = static_cast<int64_t>(ime) - ims + 1;
        const int64_t nk = static_cast<int64_t>(kme) - kms + 1;
        return (static_cast<int64_t>(j) - jms) * nk * ni +
               (static_cast<int64_t>(k) - kms) * ni +
               (static_cast<int64_t>(i) - ims);
    }

    // 9F.D44 (review P0 section 5): geometry is validated BEFORE any pointer arithmetic.
    // The checked entry point verified only that the pointers were non-null and the
    // solver was in the registry, then computed offsets and handed them to from_blob().
    // A tile outside its memory domain, an inverted bound, or a phb w-level past kme
    // produced an out-of-range view -- reading memory the caller never owned, with no
    // error anywhere. These are cheap integer comparisons on a once-per-run path.
    inline void validate_base_state_geometry(
        int its, int ite, int jts, int jte, int kts, int kte,
        int ims, int ime, int jms, int jme, int kms, int kme) {
        TORCH_CHECK(ims <= its && its <= ite && ite <= ime,
                    "base-state geometry: i tile [", its, ",", ite,
                    "] outside memory [", ims, ",", ime, "]");
        TORCH_CHECK(jms <= jts && jts <= jte && jte <= jme,
                    "base-state geometry: j tile [", jts, ",", jte,
                    "] outside memory [", jms, ",", jme, "]");
        TORCH_CHECK(kms <= kts && kts <= kte && kte <= kme,
                    "base-state geometry: k tile [", kts, ",", kte,
                    "] outside memory [", kms, ",", kme, "]");
        // phb lives on w-levels: one MORE than the mass levels this tile spans.
        TORCH_CHECK(kte + 1 <= kme,
                    "base-state geometry: phb w-level kte+1=", kte + 1,
                    " exceeds memory kme=", kme);
    }

    /**
     * Helper function to compute 2D array index for Fortran memory layout
     * Fortran order: (i,j) with i varying fastest
     */
    inline int64_t index_2d(int i, int j, int ims, int jms, int ime) {
        return (j - jms) * (ime - ims + 1) + (i - ims);
    }

    // =========================================================================
    // OPT Pass33+: DIAGNOSTIC SAMPLING COUNTERS
    // =========================================================================
    // Separate counters for standard (debug_level >= 2) and heavy (debug_level >= 3).
    // Pattern: (period == 0) || ((counter % period) == 0) || (counter == 1)
    // =========================================================================
    std::atomic<uint64_t> g_base_state_diag_call_counter{0};    // Standard diagnostic counter
    std::atomic<uint64_t> g_base_state_heavy_call_counter{0};   // Heavy diagnostic counter
}

extern "C" {

/**
 * Zero-copy version of base state setter
 * 
 * 9F.D41 (review P0-4): THIS IS A SNAPSHOT COPY, NOT ZERO-COPY. The block here used
 * to claim "No data copying". It does copy: each field is built with from_blob() over
 * WRF's strided memory domain and then .contiguous() (lines ~183/241/249/257), and
 * because the memory-domain strides differ from the tile's contiguous strides,
 * .contiguous() materialises the whole tile. The Fortran log line claiming a saved
 * ~5.4 MB of memory traffic was measuring an optimisation that does not happen.
 *
 * That is FINE -- the base state is static and copied once at initialisation -- but the
 * documentation has to say so, because "zero-copy" also implies the caller's buffer
 * stays live and aliased, which would be a lifetime contract this code does not have.
 *
 * What it actually does:
 * 1. Builds strided views over WRF's memory domain via from_blob
 * 2. Materialises OWNED contiguous per-tile snapshots via .contiguous()
 * 3. Hands those snapshots to the solver; WRF's buffers are not retained
 *
 * The exported symbol keeps its ...__zerocopy name for ABI stability -- it is bound by
 * name from Fortran (BIND(C)) -- so renaming it is a separate, verifiable change rather
 * than a side effect of a documentation fix.
 * 
 * @param solver_ptr Pointer to the tile solver
 * @param pb_ptr Base state pressure at memory start (ims,kms,jms)
 * @param t_init_ptr grid%t_init, base-state potential temperature, at memory start
 *        (ims,kms,jms). NOT alb: see the note on setBaseState.
 * @param phb_ptr Base state geopotential at memory start (ims,kms,jms)
 * @param mub_ptr Base state column mass at memory start (ims,jms)
 * @param its,ite,jts,jte,kts,kte Tile bounds
 * @param ims,ime,jms,jme,kms,kme Memory bounds
 */
// 9F.D41 (review P0-2): STATUS-RETURNING. The old signature was void and every failure
// path -- null solver, null data pointer, registry miss, caught exception -- simply
// returned. Fortran then set base_state_initialized = .TRUE. unconditionally, so a total
// failure to initialise the base state published SUCCESS and the run continued on
// uninitialised or stale state. Returns 1 only when every field was validated, copied
// and handed to the solver; 0 otherwise, with nothing published.
int sdirk3_tile_set_base_state_checked(
    void* solver_ptr,
    float* pb_ptr,      // Base state pressure (memory start)
    float* t_init_ptr,     // grid%t_init -- base-state POTENTIAL TEMPERATURE (memory start)
    float* phb_ptr,     // Base state geopotential (memory start)
    float* mub_ptr,     // Base state column mass (memory start)
    int its, int ite, int jts, int jte, int kts, int kte,  // Tile bounds
    int ims, int ime, int jms, int jme, int kms, int kme,  // Memory bounds
    // Extended dynamics parameters
    float* sin_alpha_x, float* sin_alpha_y,
    float* cos_alpha_x, float* cos_alpha_y,
    int div_damp_opt, float div_damp_coef,
    int diff_6th_opt, float diff_6th_factor,
    int diff_6th_slopeopt, float diff_6th_thresh,
    int smagorinsky_opt, float c_s, float c_k)
{
    // Debug output
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

        std::cerr << "\n=== sdirk3_tile_set_base_state_zerocopy ===" << std::endl;

    }
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

        std::cerr << "  Tile bounds: i[" << its << ":" << ite << "] "
              << "j[" << jts << ":" << jte << "] "
              << "k[" << kts << ":" << kte << "]" << std::endl;

    }
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

        std::cerr << "  Memory bounds: i[" << ims << ":" << ime << "] "
              << "j[" << jms << ":" << jme << "] "
              << "k[" << kms << ":" << kme << "]" << std::endl;

    }
    
    // Validate inputs
    if (!solver_ptr) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "ERROR: NULL solver_ptr in set_base_state_zerocopy" << std::endl;

        }
        return 0;   // 9F.D41: failure, nothing published
    }
    
    if (!pb_ptr || !t_init_ptr || !phb_ptr || !mub_ptr) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "ERROR: NULL data pointers in set_base_state_zerocopy" << std::endl;

        }
        return 0;   // 9F.D41: failure, nothing published
    }
    
    // Retrieve solver from registry
    std::lock_guard<std::mutex> registry_lock(g_tile_solvers_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end()) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "ERROR: Solver not found in registry" << std::endl;

        }
        return 0;   // 9F.D41: failure, nothing published
    }
    
    auto& solver = *(it->second);
    
    // Calculate dimensions
    int nx = ite - its + 1;
    int ny = jte - jts + 1;
    int nz = kte - kts + 1;
    
    // Calculate strides for WRF memory layout
    // WRF uses Fortran ordering: (i,k,j) with i varying fastest
    int64_t i_stride = 1;
    int64_t k_stride = (ime - ims + 1);
    int64_t j_stride = (ime - ims + 1) * (kme - kms + 1);
    
    // For 2D arrays: (i,j) with i varying fastest
    int64_t i_stride_2d = 1;
    int64_t j_stride_2d = (ime - ims + 1);
    
    // 9F.D44 (review P0 section 5): validate before ANY offset arithmetic.
    validate_base_state_geometry(its, ite, jts, jte, kts, kte,
                                 ims, ime, jms, jme, kms, kme);

    // Calculate offset to tile start within memory array
    int64_t offset_3d = index_3d(its, kts, jts, ims, kms, jms, ime, kme);
    int64_t offset_2d = index_2d(its, jts, ims, jms, ime);
    
    // Create tensor views without copying data
    // WRF Fortran arrays use (i,k,j) ordering -> C++ tensors use {ny, nz, nx} shape
    // This allows [j][k][i] access pattern which maintains memory contiguity
    
    try {
        // Base state pressure: WRF (i,k,j) -> C++ {ny, nz, nx} for [j][k][i] access
        // This maintains consistency with the rest of SDIRK3 code
        // FIX 2025-12-31 Batch35 Issue 1: Explicit CPU device for host pointer from_blob
        auto cpu_opts = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        auto pb_tile = torch::from_blob(  // LINT_EXCEPTION: CPU opts above
            pb_ptr + offset_3d,
            {ny, nz, nx},
            {j_stride, k_stride, i_stride},
            cpu_opts
        ).contiguous();
        
        // Debug: Check pb_tile for zeros before passing to setBaseState
        // PERF FIX 2025-12-27: Raise threshold to debug_level >= 2 to avoid sync every step
        // FIX Round155: Raise to debug_level >= 3 for policy consistency with
        // tile_unified_impl.cpp - heavy tensor scans (min/max/nonzero) require level 3
        // Also pre-copy to CPU once for all diagnostics
        // OPT Pass33+: Add debug_heavy_sample_period sampling to reduce log density
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 3) {
            const uint64_t heavy_counter = g_base_state_heavy_call_counter.fetch_add(1, std::memory_order_relaxed) + 1;
            const int heavy_period = wrf::sdirk3::g_sdirk3_config.debug_heavy_sample_period;
            const bool do_heavy_sample = (heavy_period == 0) || ((heavy_counter % heavy_period) == 0) || (heavy_counter == 1);

            if (do_heavy_sample) {
                // FIX Round156: Add NoGradGuard to prevent AD graph pollution from .item() calls
                torch::NoGradGuard no_grad;

                // Pre-copy to CPU once to avoid multiple GPU syncs
                auto pb_tile_cpu = pb_tile.to(torch::kCPU);

                std::cerr << "\n=== Zero-copy interface: pb_tile analysis ===" << std::endl;
                std::cerr << "  pb_tile shape after permute: " << pb_tile.sizes() << std::endl;
                std::cerr << "  pb_tile min/max: [" << pb_tile_cpu.min().item<float>()
                          << ", " << pb_tile_cpu.max().item<float>() << "]" << std::endl;

                auto zero_mask = (pb_tile_cpu == 0.0f);
                int zero_count = zero_mask.sum().item<int>();
                std::cerr << "  pb_tile zeros: " << zero_count << " out of " << pb_tile.numel() << std::endl;

                if (zero_count > 0) {
                    // Find location of first zero
                    auto indices = torch::nonzero(zero_mask);
                    if (indices.size(0) > 0) {
                        auto first_zero = indices[0];
                        // Tensor shape is {ny, nz, nx}, so indices are [j, k, i]
                        std::cerr << "  First zero at tensor indices [j][k][i] = ["
                                  << first_zero[0].item<int>() << "]["
                                  << first_zero[1].item<int>() << "]["
                                  << first_zero[2].item<int>() << "]" << std::endl;
                    }
                }

                // Check the raw data pointer that will be passed
                float* pb_data = pb_tile.data_ptr<float>();
                std::cerr << "  First 20 values from pb_tile.data_ptr: ";
                for (int i = 0; i < std::min(20, (int)pb_tile.numel()); i++) {
                    std::cerr << pb_data[i] << " ";
                }
                std::cerr << std::endl;
            }
        }
        
        // Base state inverse density: WRF (i,k,j) -> C++ {ny, nz, nx} for [j][k][i] access
        auto t_init_tile = torch::from_blob(  // LINT_EXCEPTION: CPU opts above
            t_init_ptr + offset_3d,
            {ny, nz, nx},
            {j_stride, k_stride, i_stride},
            cpu_opts
        ).contiguous();

        // Base state geopotential (staggered in z): WRF (i,k,j) -> C++ {ny, nz+1, nx} for [j][k][i] access
        auto phb_tile = torch::from_blob(  // LINT_EXCEPTION: CPU opts above
            phb_ptr + offset_3d,
            {ny, nz + 1, nx},
            {j_stride, k_stride, i_stride},
            cpu_opts
        ).contiguous();

        // Base state column mass (2D): WRF (i,j) -> C++ {ny, nx} for [j][i] access
        auto mub_tile = torch::from_blob(  // LINT_EXCEPTION: CPU opts above
            mub_ptr + offset_2d,
            {ny, nx},
            {j_stride_2d, i_stride_2d},
            cpu_opts
        ).contiguous();
        
        // Debug: Print data ranges
        // FIX Round156: Consolidated debug blocks + NoGradGuard for AD safety
        // OPT Pass32+: Batch 8 min/max + 2 sum D2H into 2 torch::stack() calls
        // OPT Pass33+: Add debug_sample_period sampling to reduce log density
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            const uint64_t diag_counter = g_base_state_diag_call_counter.fetch_add(1, std::memory_order_relaxed) + 1;
            const int diag_period = wrf::sdirk3::g_sdirk3_config.debug_sample_period;
            const bool do_diag_sample = (diag_period == 0) || ((diag_counter % diag_period) == 0) || (diag_counter == 1);

            if (do_diag_sample) {
                torch::NoGradGuard no_grad;
                auto ranges_cpu = torch::stack({
                    pb_tile.min(), pb_tile.max(),
                    t_init_tile.min(), t_init_tile.max(),
                    phb_tile.min(), phb_tile.max(),
                    mub_tile.min(), mub_tile.max()
                }).to(torch::kCPU);
                std::cerr << "  pb range: [" << ranges_cpu[0].item<float>()
                      << ", " << ranges_cpu[1].item<float>() << "]" << std::endl;
                std::cerr << "  alb range: [" << ranges_cpu[2].item<float>()
                      << ", " << ranges_cpu[3].item<float>() << "]" << std::endl;
                std::cerr << "  phb range: [" << ranges_cpu[4].item<float>()
                      << ", " << ranges_cpu[5].item<float>() << "]" << std::endl;
                std::cerr << "  mub range: [" << ranges_cpu[6].item<float>()
                      << ", " << ranges_cpu[7].item<float>() << "]" << std::endl;

                // FIX Round156: Move zero check inside debug block - .item() is expensive
                auto zeros_cpu = torch::stack({
                    (pb_tile == 0).sum(), (t_init_tile == 0).sum()
                }).to(torch::kCPU);
                int pb_zeros = zeros_cpu[0].item<int>();
                int alb_zeros = zeros_cpu[1].item<int>();
                if (pb_zeros > 0 || alb_zeros > 0) {
                    std::cerr << "WARNING: Found zeros - pb: " << pb_zeros
                          << ", alb: " << alb_zeros << std::endl;
                }
            }
        }
        
        // Set base state in solver
        // Note: Current setBaseState expects contiguous tensors, which we ensure with .contiguous()
        // FIX Round194: Tensors are CPU/Float32/contiguous by construction:
        // - Created via from_blob() with cpu_opts (enforces CPU device)
        // - .contiguous() ensures contiguous memory layout
        // - Default dtype is Float32 (no dtype conversion in from_blob)
        solver.setBaseState(
            pb_tile.data_ptr<float>(),
            t_init_tile.data_ptr<float>(),
            phb_tile.data_ptr<float>(),
            mub_tile.data_ptr<float>()
        );
        
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

        
            std::cerr << "  Base state set successfully (zero-copy)" << std::endl;

        
        }
        
        // Set extended dynamics parameters if available
        // Get the solver's grid info and try to cast to extended version
        auto grid_info = solver.getGridInfo();
        auto grid_ext = std::static_pointer_cast<wrf::sdirk3::WRFGridInfoExtended>(grid_info);
        
        if (grid_ext) {
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "  Setting extended dynamics parameters..." << std::endl;

            }
            
            // Set terrain slope arrays if provided (zero-copy)
            if (sin_alpha_x && sin_alpha_y && cos_alpha_x && cos_alpha_y) {
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                    std::cerr << "  Initializing terrain slope arrays (zero-copy)..." << std::endl;

                }
                
                // Calculate 2D offset for terrain slope arrays (at mass points)
                int64_t offset_2d = index_2d(its, jts, ims, jms, ime);
                
                // Create tensor views for terrain slope arrays
                // WRF (i,j) -> C++ {ny, nx} for [j][i] access
                // FIX 2025-12-31 Batch35 Issue 1: Explicit CPU device for host pointer from_blob
                auto cpu_opts_2d = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
                auto sin_alpha_x_tile = torch::from_blob(  // LINT_EXCEPTION: CPU opts above
                    sin_alpha_x + offset_2d,
                    {ny, nx},
                    {j_stride_2d, i_stride_2d},
                    cpu_opts_2d
                ).contiguous();

                auto sin_alpha_y_tile = torch::from_blob(  // LINT_EXCEPTION: CPU opts above
                    sin_alpha_y + offset_2d,
                    {ny, nx},
                    {j_stride_2d, i_stride_2d},
                    cpu_opts_2d
                ).contiguous();

                auto cos_alpha_x_tile = torch::from_blob(  // LINT_EXCEPTION: CPU opts above
                    cos_alpha_x + offset_2d,
                    {ny, nx},
                    {j_stride_2d, i_stride_2d},
                    cpu_opts_2d
                ).contiguous();

                auto cos_alpha_y_tile = torch::from_blob(  // LINT_EXCEPTION: CPU opts above
                    cos_alpha_y + offset_2d,
                    {ny, nx},
                    {j_stride_2d, i_stride_2d},
                    cpu_opts_2d
                ).contiguous();
                
                // Initialize terrain slope in grid_ext using contiguous data
                // FIX Round194: Tensors are CPU/Float32/contiguous by construction:
                // - Created via from_blob() with cpu_opts_2d (enforces CPU device)
                // - .contiguous() ensures contiguous memory layout
                grid_ext->initializeTerrainSlope(
                    sin_alpha_x_tile.data_ptr<float>(),
                    sin_alpha_y_tile.data_ptr<float>(),
                    cos_alpha_x_tile.data_ptr<float>(),
                    cos_alpha_y_tile.data_ptr<float>(),
                    nx, ny
                );
            }
            
            // Set divergence damping parameters
            grid_ext->div_damp_opt = div_damp_opt;
            grid_ext->div_damp_coef = div_damp_coef;
            
            // CRITICAL: Also set kdamp in base grid_info for UnifiedRHS
            grid_info->kdamp = div_damp_coef;
            
            // Set 6th order diffusion parameters
            grid_ext->diff_6th_opt = diff_6th_opt;
            grid_ext->diff_6th_factor = diff_6th_factor;
            grid_ext->diff_6th_slopeopt = diff_6th_slopeopt;
            grid_ext->diff_6th_thresh = diff_6th_thresh;
            
            // Set Smagorinsky turbulence parameters
            grid_ext->smagorinsky_opt = smagorinsky_opt;
            grid_ext->c_s = c_s;
            // c_k parameter not used in current implementation
            
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            
                std::cerr << "  Extended dynamics configured:" << std::endl;

            
            }
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "    Divergence damping: opt=" << div_damp_opt 
                      << ", coef=" << div_damp_coef << std::endl;

            }
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "    6th order diffusion: opt=" << diff_6th_opt 
                      << ", factor=" << diff_6th_factor << std::endl;

            }
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "    Slope damping: opt=" << diff_6th_slopeopt
                      << ", thresh=" << diff_6th_thresh << std::endl;

            }
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "    Smagorinsky: opt=" << smagorinsky_opt
                      << ", c_s=" << c_s << ", c_k=" << c_k << std::endl;

            }
            
            if (grid_ext->hasTerrainSlope()) {
                // FIX 2025-12-27: Add .to(kCPU) before .item<float>() to avoid GPU sync
                // FIX 2025-12-28: Changed from pointer dereference to value member access
                // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
                // OPT Pass32+: Batch 2 D2H into single torch::stack().to(kCPU)
                torch::NoGradGuard no_grad;
                auto sin_maxes_cpu = torch::stack({
                    grid_ext->sin_alpha_x.abs().max(),
                    grid_ext->sin_alpha_y.abs().max()
                }).to(torch::kCPU);
                float sin_x_max = sin_maxes_cpu[0].item<float>();
                float sin_y_max = sin_maxes_cpu[1].item<float>();
                float max_slope_x = std::asin(sin_x_max) * 180.0f / M_PI;
                float max_slope_y = std::asin(sin_y_max) * 180.0f / M_PI;
                if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                    std::cerr << "    Terrain slope: max_x=" << max_slope_x
                          << " deg, max_y=" << max_slope_y << " deg" << std::endl;

                }
            }
        } else {
            if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

                std::cerr << "  WARNING: Extended grid info not available, advanced features disabled" << std::endl;

            }
        }
        
    } catch (const std::exception& e) {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {

            std::cerr << "ERROR in set_base_state_zerocopy: " << e.what() << std::endl;

        }
        return 0;   // 9F.D41: an exception is a FAILURE, not a quiet completion
    }
    return 1;
}

// ABI-compatible wrapper. The original void symbol is what Fortran BIND(C) resolves, so
// it is kept; the Fortran interface is being moved to the checked form in the same
// change. Retained (rather than deleted) so any out-of-tree caller keeps linking --
// but note it necessarily discards the status, which is the defect above.
extern "C" void sdirk3_tile_set_base_state_zerocopy(
    void* solver_ptr,
    float* pb_ptr, float* t_init_ptr, float* phb_ptr, float* mub_ptr,
    int its, int ite, int jts, int jte, int kts, int kte,
    int ims, int ime, int jms, int jme, int kms, int kme,
    float* sin_alpha_x, float* sin_alpha_y,
    float* cos_alpha_x, float* cos_alpha_y,
    int div_damp_opt, float div_damp_coef,
    int diff_6th_opt, float diff_6th_factor,
    int diff_6th_slopeopt, float diff_6th_thresh,
    int smagorinsky_opt, float c_s, float c_k)
{
    // 9F.D44 (review P0 section 8): do NOT discard the status. D41 made the in-tree
    // Fortran path safe by switching it to the checked entry point, but this wrapper
    // kept swallowing the result -- so for any out-of-tree caller still bound to the
    // old symbol, the original defect was fully intact: failure -> status dropped ->
    // caller proceeds as if the base state were set. Aborting here is the only honest
    // option, because a void ABI has no way to say "no".
    const int ok = sdirk3_tile_set_base_state_checked(
        solver_ptr, pb_ptr, t_init_ptr, phb_ptr, mub_ptr,
        its, ite, jts, jte, kts, kte, ims, ime, jms, jme, kms, kme,
        sin_alpha_x, sin_alpha_y, cos_alpha_x, cos_alpha_y,
        div_damp_opt, div_damp_coef, diff_6th_opt, diff_6th_factor,
        diff_6th_slopeopt, diff_6th_thresh, smagorinsky_opt, c_s, c_k);
    if (ok != 1) {
        wrf::sdirk3::mpi_safety::abort_c_abi_exception(
            "sdirk3_tile_set_base_state_zerocopy",
            "base-state initialization FAILED and this deprecated void entry point "
            "cannot report it. Use sdirk3_tile_set_base_state_checked, which returns "
            "1 on success and 0 on failure.");
    }
}

/**
 * Alternative implementation that passes indices instead of creating tensors
 * This version lets the solver create tensors internally with proper layout
 */
// 9F.D44 (review P0 section 4): EXACT public signature, and it FAILS CLOSED.
//
// What was here: a definition whose 6th parameter was `int its` while the public header
// declared `float* theta_base_ptr`, a body that assembled a local [[maybe_unused]]
// BaseStateConfig, and the one line that would have used it commented out --
//     // solver.setBaseStateZeroCopy(config);
// So the symbol was simultaneously (a) ABI-incompatible with its own declaration and
// (b) a silent no-op. A caller compiled against the header would pass a pointer where
// an int was read, and then continue believing the base state had been set. Nothing in
// this tree calls it, which is the only reason it never detonated.
//
// It is NOT quietly deleted: the symbol may be in an export list or a downstream build.
// Keeping it with the correct signature and an abort means a caller gets a loud,
// immediate failure naming the supported entry point, instead of silent wrong physics.
void sdirk3_tile_set_base_state_zerocopy_v2(
    void* /*solver_ptr*/,
    float* /*pb_ptr*/, float* /*t_init_ptr*/, float* /*phb_ptr*/, float* /*mub_ptr*/,
    float* /*theta_base_ptr*/,
    int /*its*/, int /*ite*/, int /*jts*/, int /*jte*/, int /*kts*/, int /*kte*/,
    int /*ims*/, int /*ime*/, int /*jms*/, int /*jme*/, int /*kms*/, int /*kme*/,
    float* /*sin_alpha_x*/, float* /*sin_alpha_y*/,
    float* /*cos_alpha_x*/, float* /*cos_alpha_y*/,
    int /*div_damp_opt*/, float /*div_damp_coef*/,
    int /*diff_6th_opt*/, float /*diff_6th_factor*/,
    int /*diff_6th_slopeopt*/, float /*diff_6th_thresh*/,
    int /*smagorinsky_opt*/, float /*c_s*/, float /*c_k*/)
{
    wrf::sdirk3::mpi_safety::abort_c_abi_exception(
        "sdirk3_tile_set_base_state_zerocopy_v2",
        "unsupported: this entry point never set any solver state (its only mutating "
        "call was commented out) and its declaration disagreed with its definition. "
        "Use sdirk3_tile_set_base_state_checked, which validates its inputs and "
        "reports success or failure.");
}

} // extern "C"