//------------------------------------------------------------------------
// wrf_sdirk3_tile_unified.cpp
//
// Unified SDIRK-3 tile interface implementation
// NO MODE SEPARATION - All dynamics solved together
//------------------------------------------------------------------------

#include "wrf_sdirk3_interface.h"
#include "wrf_sdirk3_coefficients.h"
#include "wrf_sdirk3_tile_unified.h"
#include "wrf_sdirk3_unified_rhs.h"
#include "wrf_sdirk3_unified_state_vector.h"
#include "wrf_sdirk3_newton_solver.h"
#include "wrf_sdirk3_unified_preconditioner.h"
#include "wrf_sdirk3_config.h"  // FIX Round157: For g_sdirk3_config debug_level
#include <memory>
#include <iostream>
#include <unordered_map>
#include <mutex>

namespace wrf {
namespace sdirk3 {

// Forward declaration
class TileSDIRK3Solver;

// External global tile solver map from wrf_sdirk3_tile_interface.cpp
extern std::unordered_map<void*, TileSDIRK3Solver*> g_tile_solvers;
extern std::mutex g_solver_mutex;

} // namespace sdirk3
} // namespace wrf

extern "C" {

// Unified SDIRK3 step - solves ALL dynamics together
void sdirk3_tile_unified_step(
    void* solver_ptr,
    float* u_2, float* v_2, float* w_2,
    float* ph_2, float* al, float* mu_2,
    float* ru_tend, float* rv_tend, float* rw_tend,
    float* ph_tend, float* al_tend, float* mu_tend,
    float rdx, float rdy, float* rdnw, float* rdn,  // rdx, rdy are scalars, rdnw/rdn are arrays
    float* msftx, float* msfty,         // Map factors at mass points
    float* msfux, float* msfuy,         // Map factors at u-points
    float* msfvx, float* msfvy,         // Map factors at v-points
    float* c1f, float* c2f,             // Vertical coordinate coefficients at w-levels
    float* c1h, float* c2h,             // Vertical coordinate coefficients at mass levels
    int stage, float dt,  // Full timestep, not acoustic substep
    int nx, int ny, int nz,
    int nx_u, int ny_v, int nz_w  // Staggered dimensions
) {
    // FIX Round157: Gate entry debug output with debug_level >= 2
    // FIX Round159: Apply log_solver_pointer policy for pointer masking
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cout << "\n=== ENTERED sdirk3_tile_unified_step (NON-ZEROCOPY) from Fortran ===" << std::endl;
        if (wrf::sdirk3::g_sdirk3_config.log_solver_pointer) {
            std::cout << "  solver_ptr=" << solver_ptr << ", stage=" << stage << ", dt=" << dt << std::endl;
        } else {
            std::cout << "  solver_ptr=<masked>, stage=" << stage << ", dt=" << dt << std::endl;
        }
        std::cout << "  nx=" << nx << " ny=" << ny << " nz=" << nz << std::endl;
    }
    
    using namespace wrf::sdirk3;
    
    if (!solver_ptr) {
        // FIX Round157: Error output always needed for diagnostics
        std::cerr << "ERROR: sdirk3_tile_unified_step called with null solver" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(g_solver_mutex);
    auto it = g_tile_solvers.find(solver_ptr);
    if (it == g_tile_solvers.end()) {
        // FIX Round159: Apply log_solver_pointer policy for pointer masking
        if (wrf::sdirk3::g_sdirk3_config.log_solver_pointer) {
            std::cerr << "ERROR: Solver " << solver_ptr << " not found in tile solver map" << std::endl;
        } else {
            std::cerr << "ERROR: Solver <masked> not found in tile solver map" << std::endl;
        }
        // FIX Round157: Gate verbose debug output with debug_level >= 2
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "Map size: " << g_tile_solvers.size() << std::endl;
            std::cerr << "Current keys in map:" << std::endl;
            for (const auto& pair : g_tile_solvers) {
                if (wrf::sdirk3::g_sdirk3_config.log_solver_pointer) {
                    std::cerr << "  Key: " << pair.first << std::endl;
                } else {
                    std::cerr << "  Key: <masked>" << std::endl;
                }
            }
        }
        return;
    }
    
    auto& solver = it->second;

    // FIX Round157: Gate log output with debug_level >= 2
    if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
        std::cout << "SDIRK3 Unified Step: stage=" << stage << ", dt=" << dt
                  << " (NO acoustic substeps!)" << std::endl;
        std::cout << "Tile: nx=" << nx << ", ny=" << ny << ", nz=" << nz
                  << ", nx_u=" << nx_u << ", ny_v=" << ny_v << ", nz_w=" << nz_w << std::endl;
    }
    
    try {
        // Call the solver's unifiedStep method with staggered dimensions
        auto* unified_solver = dynamic_cast<TileSDIRK3UnifiedSolver*>(solver);
        if (!unified_solver) {
            std::cerr << "ERROR: Failed to cast to TileSDIRK3UnifiedSolver" << std::endl;
            return;
        }

        // FIX Round157: Gate debug output with debug_level >= 2
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cout << "DEBUG: About to call unified_solver->unifiedStep from NON-ZEROCOPY path" << std::endl;
        }
        
        unified_solver->unifiedStep(
            u_2, v_2, w_2, ph_2, al, mu_2,
            ru_tend, rv_tend, rw_tend, ph_tend, al_tend, mu_tend,
            rdx, rdy, rdnw, rdn,
            msftx, msfty, msfux, msfuy, msfvx, msfvy,  // Map factors
            c1f, c2f, c1h, c2h,  // Vertical coordinate coefficients
            nullptr, nullptr,  // fnm, fnp - vertical interpolation weights (not used in non-zerocopy path)
            stage, dt,
            nx, ny, nz, nx_u, ny_v, nz_w  // Pass staggered dimensions
        );

        // FIX Round157: Gate debug output with debug_level >= 2
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cout << "DEBUG: Returned from unified_solver->unifiedStep" << std::endl;
        }
        
    } catch (const std::exception& e) {
        std::cerr << "ERROR in sdirk3_tile_unified_step: " << e.what() << std::endl;
        // Zero out tendencies on error (with correct staggered sizes)
        // FIX Round160: Cast to size_t before multiplication to prevent int overflow
        std::fill_n(ru_tend, static_cast<size_t>(nx_u)*ny*nz, 0.0);      // U-staggered
        std::fill_n(rv_tend, static_cast<size_t>(nx)*ny_v*nz, 0.0);      // V-staggered
        std::fill_n(rw_tend, static_cast<size_t>(nx)*ny*nz_w, 0.0);      // W-staggered
        std::fill_n(ph_tend, static_cast<size_t>(nx)*ny*nz_w, 0.0);      // W-staggered (same as W)
        std::fill_n(al_tend, static_cast<size_t>(nx)*ny*nz, 0.0);        // Mass points
        std::fill_n(mu_tend, static_cast<size_t>(nx)*ny, 0.0);           // 2D mass points
    }
}

} // extern "C"