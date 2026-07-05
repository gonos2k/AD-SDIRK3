// wrf_sdirk3_tile_unified_interface.h
// C interface for unified SDIRK3 solver

#ifndef WRF_SDIRK3_TILE_UNIFIED_INTERFACE_H
#define WRF_SDIRK3_TILE_UNIFIED_INTERFACE_H

#ifdef __cplusplus
extern "C" {
#endif

// Create unified solver instance
void* sdirk3_unified_solver_create(
    int nx, int ny, int nz,
    float dx, float dy,
    float rdx, float rdy, float* rdnw,
    int tile_id);

// Destroy unified solver instance
void sdirk3_unified_solver_destroy(void* solver_handle);

// 4DVAR checkpoint metadata helpers
int sdirk3_unified_solver_get_saved_trajectory_count(void* solver_handle);
int sdirk3_unified_solver_get_saved_global_timestep(void* solver_handle);
void sdirk3_unified_solver_clear_saved_trajectory(void* solver_handle);

// Unified SDIRK-3 step (all 3 stages internally)
void sdirk3_tile_unified_step(
    void* solver_handle,
    float* u, float* v, float* w,
    float* ph, float* al, float* mu,
    float* ru_tend, float* rv_tend, float* rw_tend,
    float* ph_tend, float* al_tend, float* mu_tend,
    float rdx, float rdy, float* rdnw, float* rdn,  // rdx, rdy are scalars, rdnw/rdn are arrays
    float* msftx, float* msfty,         // Map factors at mass points
    float* msfux, float* msfuy,         // Map factors at u-points
    float* msfvx, float* msfvy,         // Map factors at v-points
    float* c1f, float* c2f,             // Vertical coordinate coefficients at w-levels
    float* c1h, float* c2h,             // Vertical coordinate coefficients at mass levels
    int rk_step, float dt,
    int nx, int ny, int nz,
    int nx_u, int ny_v, int nz_w);  // Staggered dimensions

#ifdef __cplusplus
}
#endif

#endif // WRF_SDIRK3_TILE_UNIFIED_INTERFACE_H
