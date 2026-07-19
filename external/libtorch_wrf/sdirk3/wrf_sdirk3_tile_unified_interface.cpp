// wrf_sdirk3_tile_unified_interface.cpp
// Unified SDIRK3 C interface implementation

#include "wrf_sdirk3_stage_history_diag.h"
#include "wrf_sdirk3_torch_wrapper.h"
#include "wrf_sdirk3_tile_unified.h"
#include "wrf_sdirk3_config.h"  // FIX Round157: For g_sdirk3_config debug_level
#include "wrf_sdirk3_common_macros.h"  // OPT Pass34: For SDIRK3_*_LOG macros
#include <cstring>
#include <iostream>
#include <mutex>
#include <vector>
#include <unordered_map>

namespace wrf {
namespace sdirk3 {
    extern std::unordered_map<void*, TileSDIRK3Solver*> g_tile_solvers;
    extern std::mutex g_solver_mutex;
}
}

extern "C" {



// Create unified solver instance
// Note: This matches the Fortran call from module_implicit_sdirk3.F which passes rdx, rdy as scalars, rdnw as array
void* sdirk3_unified_solver_create(
    int nx, int ny, int nz,
    float dx, float dy,
    float rdx, float rdy, float* rdnw,
    int tile_id) {
    
    try {
        // OPT Pass34: Use SDIRK3_INFO_LOG for debug output
        SDIRK3_INFO_LOG(2, "sdirk3_unified_solver_create: nx=" << nx
                      << ", ny=" << ny << ", nz=" << nz
                      << ", dx=" << dx << ", dy=" << dy
                      << ", tile_id=" << tile_id);

        if (rdnw == nullptr) {
            SDIRK3_ERROR_LOG("Grid metric rdnw is NULL!");
            return nullptr;
        }

        // Convert grid metrics to vectors
        // rdx and rdy are scalars (reciprocals of dx and dy)
        std::vector<float> rdx_vec(nx, rdx);  // Fill vector with scalar value
        std::vector<float> rdy_vec(ny, rdy);  // Fill vector with scalar value
        std::vector<float> rdnw_vec(rdnw, rdnw + nz);

        // OPT Pass34: Use SDIRK3_INFO_LOG for debug output
        SDIRK3_INFO_LOG(2, "Creating TileSDIRK3UnifiedSolver with rdnw_vec size=" << rdnw_vec.size());

        // OPT Pass34: Use unique_ptr for exception safety during creation/registration.
        // If constructor or map insertion throws, unique_ptr destructor cleans up.
        // release() transfers ownership only after successful registration.
        auto solver = std::make_unique<TileSDIRK3UnifiedSolver>(
            nx, ny, nz, dx, dy,
            rdx_vec, rdy_vec, rdnw_vec,
            tile_id);

        // Register solver in global map
        void* ptr = static_cast<void*>(solver.get());
        {
            std::lock_guard<std::mutex> lock(wrf::sdirk3::g_solver_mutex);
            // Use static_cast since TileSDIRK3UnifiedSolver inherits from TileSDIRK3Solver
            wrf::sdirk3::g_tile_solvers[ptr] = static_cast<wrf::sdirk3::TileSDIRK3Solver*>(solver.get());

            // OPT Pass34: Use SDIRK3_INFO_LOG with log_solver_pointer policy
            if (wrf::sdirk3::g_sdirk3_config.log_solver_pointer) {
                SDIRK3_INFO_LOG(2, "Solver created at address: " << ptr);
            } else {
                SDIRK3_INFO_LOG(2, "Solver created at address: <masked>");
            }
            SDIRK3_INFO_LOG(2, "Registered in g_tile_solvers, map size: "
                          << wrf::sdirk3::g_tile_solvers.size());
        }

        // Registration succeeded - transfer ownership to global map (caller deletes via destroy)
        solver.release();
        return ptr;
    }
    catch (const std::exception& e) {
        SDIRK3_ERROR_LOG("Error creating unified SDIRK3 solver: " << e.what());
        return nullptr;
    }
}

// Destroy unified solver instance
void sdirk3_unified_solver_destroy(void* solver_handle) {
    if (solver_handle) {
        // Remove from global map
        {
            std::lock_guard<std::mutex> lock(wrf::sdirk3::g_solver_mutex);
            wrf::sdirk3::g_tile_solvers.erase(solver_handle);
        }
        
        auto* solver = static_cast<TileSDIRK3UnifiedSolver*>(solver_handle);
        delete solver;
    }
}

int sdirk3_unified_solver_get_saved_trajectory_count(void* solver_handle) {
    if (!solver_handle) {
        return 0;
    }
    auto* solver = static_cast<TileSDIRK3UnifiedSolver*>(solver_handle);
    return static_cast<int>(solver->getSavedTrajectoryCount());
}

int sdirk3_unified_solver_get_saved_global_timestep(void* solver_handle) {
    if (!solver_handle) {
        return 0;
    }
    auto* solver = static_cast<TileSDIRK3UnifiedSolver*>(solver_handle);
    return solver->getSavedGlobalTimestep();
}

void sdirk3_unified_solver_clear_saved_trajectory(void* solver_handle) {
    if (!solver_handle) {
        return;
    }
    auto* solver = static_cast<TileSDIRK3UnifiedSolver*>(solver_handle);
    solver->clearSavedTrajectory();
}

// NOTE: sdirk3_tile_unified_step is implemented in wrf_sdirk3_tile_unified.cpp

} // extern "C"
