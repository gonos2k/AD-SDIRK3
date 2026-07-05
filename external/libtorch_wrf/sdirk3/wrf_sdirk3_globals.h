#ifndef WRF_SDIRK3_GLOBALS_H
#define WRF_SDIRK3_GLOBALS_H

#include <memory>
#include <mutex>
#include "wrf_sdirk3_torch_wrapper.h"
#include "wrf_sdirk3_unified_rhs.h"
#include "wrf_sdirk3_unified_state_vector.h"

namespace wrf {
namespace sdirk3 {

// Global solver state structure
struct SolverState {
    std::unique_ptr<UnifiedStateVector> state_vector;
    std::unique_ptr<UnifiedRHS> rhs_calculator;
    std::shared_ptr<WRFGridInfo> grid_info;
    
    bool initialized = false;
    int nx = 0, ny = 0, nz = 0;
    float dx = 0.0f, dy = 0.0f, dt = 0.0f;
    
    // Solver configuration
    float newton_tol = 1e-6f;
    float newton_rtol = 1e-10f;
    int max_newton_iter = 4;
    float krylov_tol = 1e-3f;
    int max_krylov_iter = 100;
    int gmres_restart = 30;
    int precond_type = 0;
    
    // Statistics
    int total_newton_iters = 0;
    int total_gmres_iters = 0;
    float last_residual = 0.0f;
};

// Global solver instance
extern SolverState g_solver_state;
extern std::mutex g_solver_mutex;

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_GLOBALS_H