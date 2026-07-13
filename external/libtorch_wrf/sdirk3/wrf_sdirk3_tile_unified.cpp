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
#include <cstdlib>  // std::abort (fail-close stub)
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
    // FAIL-CLOSE STUB (external review round 3j): this legacy NON-ZEROCOPY
    // entry bypassed every geometry/msf/split guard added on the zerocopy-v2
    // path — it passed caller dimensions UNVALIDATED into unifiedStep, and on
    // exception computed std::fill_n lengths from those same unvalidated
    // values (a negative dimension converts to a huge size_t: OOB write). No
    // in-tree caller exists (module_implicit_sdirk3.F: "OLD interface removed
    // - Use sdirk3_tile_unified_step_zerocopy_v2 instead"), so the honest
    // posture is an explicit refusal, not a partially-guarded compute path.
    // A future caller must port to the v2 zerocopy ABI, which carries the
    // validated-geometry contract.
    (void)solver_ptr; (void)u_2; (void)v_2; (void)w_2; (void)ph_2; (void)al;
    (void)mu_2; (void)ru_tend; (void)rv_tend; (void)rw_tend; (void)ph_tend;
    (void)al_tend; (void)mu_tend; (void)rdx; (void)rdy; (void)rdnw; (void)rdn;
    (void)msftx; (void)msfty; (void)msfux; (void)msfuy; (void)msfvx; (void)msfvy;
    (void)c1f; (void)c2f; (void)c1h; (void)c2h; (void)stage; (void)dt;
    (void)nx; (void)ny; (void)nz; (void)nx_u; (void)ny_v; (void)nz_w;
    std::cerr << "[SDIRK3] FATAL: sdirk3_tile_unified_step (non-zerocopy) is "
                 "fail-closed — it bypasses the validated-geometry/msf/split "
                 "guards of the v2 zerocopy path. Port the caller to "
                 "sdirk3_tile_unified_step_zerocopy_v2." << std::endl;
    std::abort();
}

} // extern "C"