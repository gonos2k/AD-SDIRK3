// The live C ABI creator must preserve WRF flags installed after env loading.
#include "../wrf_sdirk3_config.h"
#include "../wrf_sdirk3_tile_unified.h"
#include <cstdlib>
#include <iostream>

extern "C" {
void* sdirk3_tile_solver_create_zerocopy(int, int, int, float, float,
                                      float*, int, int, int, int);
void sdirk3_tile_solver_destroy_zerocopy(void*);
}

namespace {
using wrf::sdirk3::g_sdirk3_config;
using wrf::sdirk3::wrf_sdirk3_set_config_bool;
using wrf::sdirk3::wrf_sdirk3_load_env_once;
using wrf::sdirk3::wrf_sdirk3_mark_workers_started;
struct NhTag {
    using type = bool TileSDIRK3UnifiedSolver::*;
    friend type access(NhTag);
};
template<class Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<NhTag, &TileSDIRK3UnifiedSolver::non_hydrostatic_>;

void expect_flags(bool value) {
    TORCH_CHECK(g_sdirk3_config.non_hydrostatic == value &&
                g_sdirk3_config.do_curvature == value &&
                g_sdirk3_config.split_explicit_top_lid == value,
                "WRF dynamics flags lost their configured values");
}
void set_wrf_flags(bool value) {
    for (const auto* key : {"non_hydrostatic", "do_curvature", "split_explicit_top_lid"})
        wrf_sdirk3_set_config_bool(key, value ? 1 : 0);
}
void* make_solver(int id) {
    float rdnw[4] = {-4, -4, -4, -4};
    void* ptr = sdirk3_tile_solver_create_zerocopy(8, 6, 4, 100000, 100000,
                                                rdnw, id, 9, 7, 5);
    TORCH_CHECK(ptr, "C ABI solver creation failed");
    return ptr;
}
}

int main() {
    torch::set_num_threads(1);
    wrf::sdirk3::SDIRK3Config local;
    TORCH_CHECK(!local.non_hydrostatic && !local.do_curvature &&
                !local.split_explicit_top_lid &&
                !local.stage2_rejection_snapshot_diag,
                "standalone defaults changed");
    local.load_from_namelist("non_hydrostatic = .true.\n"
                             "do_curvature = .true.\n"
                             "split_explicit_top_lid = .true.\n"
                             "sdirk3_stage2_rejection_snapshot_diag = .true.\n");
    TORCH_CHECK(local.non_hydrostatic && local.do_curvature &&
                local.split_explicit_top_lid &&
                local.stage2_rejection_snapshot_diag && local.validate(),
                "namelist/validation lost dynamics flags");

    for (const auto* key : {"WRF_SDIRK3_NON_HYDROSTATIC", "WRF_SDIRK3_DO_CURVATURE",
                            "WRF_SDIRK3_SPLIT_EXPLICIT_TOP_LID",
                            "WRF_SDIRK3_STAGE2_REJECTION_SNAPSHOT_DIAG"})
        setenv(key, "1", 1);
    wrf_sdirk3_load_env_once();
    expect_flags(true);
    TORCH_CHECK(g_sdirk3_config.stage2_rejection_snapshot_diag,
                "environment parser lost Stage-2 snapshot flag");

    // The Fortran initializer calls these setters after the shared env hook.
    set_wrf_flags(false);
    wrf_sdirk3_set_config_bool("stage2_rejection_snapshot_diag", 0);
    wrf_sdirk3_load_env_once();
    expect_flags(false);
    TORCH_CHECK(!g_sdirk3_config.stage2_rejection_snapshot_diag,
                "runtime bool setter did not override Stage-2 snapshot flag");
    void* off = make_solver(741);
    expect_flags(false); // Creator's env hook must not reload the still-true env.
    TORCH_CHECK(!(static_cast<TileSDIRK3UnifiedSolver*>(off)->*access(NhTag{})),
                "constructor did not snapshot the WRF NH-off flag");

    set_wrf_flags(true);
    void* on = make_solver(742);
    expect_flags(true);
    TORCH_CHECK(static_cast<TileSDIRK3UnifiedSolver*>(on)->*access(NhTag{}),
                "constructor did not snapshot the WRF NH-on flag");
    TORCH_CHECK(!(static_cast<TileSDIRK3UnifiedSolver*>(off)->*access(NhTag{})),
                "existing NH snapshot changed during later configuration");
    sdirk3_tile_solver_destroy_zerocopy(off);
    sdirk3_tile_solver_destroy_zerocopy(on);

    wrf_sdirk3_mark_workers_started();
    set_wrf_flags(false);
    expect_flags(true);
    std::cout << "WRF_DYNAMICS_CONFIG PASS\n";
}
