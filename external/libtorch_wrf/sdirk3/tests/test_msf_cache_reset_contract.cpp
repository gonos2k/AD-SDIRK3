// Focused CPU structural regression for map-factor cache reset ownership.
// The CUDA copy path is intentionally not exercised on this host; the test
// verifies that reset releases every source/device view and that the normal
// zero-copy publication path reconstructs them before a later solve.
#include "tile_test_fixture.h"

#include <array>
#include <cstdint>
#include <iostream>

template<typename Tag, typename Tag::type Member> struct DataAccessor {
    friend typename Tag::type access(Tag) { return Member; }
};

#define DEFINE_ACCESSOR(TAG, TYPE, MEMBER) \
    struct TAG { \
        using type = TYPE TileSDIRK3UnifiedSolver::*; \
        friend type access(TAG); \
    }; \
    template struct DataAccessor<TAG, &TileSDIRK3UnifiedSolver::MEMBER>

DEFINE_ACCESSOR(MsftxCpuTag, torch::Tensor, msftx_cpu_);
DEFINE_ACCESSOR(MsftyCpuTag, torch::Tensor, msfty_cpu_);
DEFINE_ACCESSOR(MsfuxCpuTag, torch::Tensor, msfux_cpu_);
DEFINE_ACCESSOR(MsfuyCpuTag, torch::Tensor, msfuy_cpu_);
DEFINE_ACCESSOR(MsfvxCpuTag, torch::Tensor, msfvx_cpu_);
DEFINE_ACCESSOR(MsfvyCpuTag, torch::Tensor, msfvy_cpu_);
DEFINE_ACCESSOR(MsftxDeviceTag, torch::Tensor, msftx_);
DEFINE_ACCESSOR(MsftyDeviceTag, torch::Tensor, msfty_);
DEFINE_ACCESSOR(MsfuxDeviceTag, torch::Tensor, msfux_);
DEFINE_ACCESSOR(MsfuyDeviceTag, torch::Tensor, msfuy_);
DEFINE_ACCESSOR(MsfvxDeviceTag, torch::Tensor, msfvx_);
DEFINE_ACCESSOR(MsfvyDeviceTag, torch::Tensor, msfvy_);
DEFINE_ACCESSOR(MsfEpochTag, uint64_t, msf_epoch_);
DEFINE_ACCESSOR(MsfEpochCachedTag, uint64_t, msf_epoch_cached_);
DEFINE_ACCESSOR(MsfSignatureTag, float, msf_signature_);
DEFINE_ACCESSOR(LastStepInputGraphTag, torch::Tensor, last_step_input_graph_);
DEFINE_ACCESSOR(LastStepOutputGraphTag, torch::Tensor, last_step_output_graph_);

#undef DEFINE_ACCESSOR

namespace {

using wrf::sdirk3::test::TileCase;
using wrf::sdirk3::test::nx;
using wrf::sdirk3::test::ny;
using wrf::sdirk3::test::nz;
using wrf::sdirk3::test::nu;
using wrf::sdirk3::test::nv;
using wrf::sdirk3::test::nw;

template<typename Tag>
bool member_defined(const TileSDIRK3UnifiedSolver& solver) {
    return (solver.*access(Tag{})).defined();
}

template<typename Tag>
const void* member_data(const TileSDIRK3UnifiedSolver& solver) {
    const auto& tensor = solver.*access(Tag{});
    return tensor.defined() ? tensor.data_ptr() : nullptr;
}

struct MapSnapshot {
    std::array<const void*, 12> data{};
    uint64_t epoch = 0;
    uint64_t epoch_cached = 0;
    float signature = 0.0f;
};

MapSnapshot snapshot(const TileSDIRK3UnifiedSolver& solver) {
    return {
        {
            member_data<MsftxCpuTag>(solver), member_data<MsftyCpuTag>(solver),
            member_data<MsfuxCpuTag>(solver), member_data<MsfuyCpuTag>(solver),
            member_data<MsfvxCpuTag>(solver), member_data<MsfvyCpuTag>(solver),
            member_data<MsftxDeviceTag>(solver), member_data<MsftyDeviceTag>(solver),
            member_data<MsfuxDeviceTag>(solver), member_data<MsfuyDeviceTag>(solver),
            member_data<MsfvxDeviceTag>(solver), member_data<MsfvyDeviceTag>(solver),
        },
        solver.*access(MsfEpochTag{}),
        solver.*access(MsfEpochCachedTag{}),
        solver.*access(MsfSignatureTag{}),
    };
}

void require_maps_defined(const TileSDIRK3UnifiedSolver& solver, bool expected,
                          const char* phase) {
#define CHECK_DEFINED(TAG) \
    TORCH_CHECK(member_defined<TAG>(solver) == expected, phase, ": ", #TAG, \
                " expected ", expected ? "defined" : "undefined")
    CHECK_DEFINED(MsftxCpuTag);
    CHECK_DEFINED(MsftyCpuTag);
    CHECK_DEFINED(MsfuxCpuTag);
    CHECK_DEFINED(MsfuyCpuTag);
    CHECK_DEFINED(MsfvxCpuTag);
    CHECK_DEFINED(MsfvyCpuTag);
    CHECK_DEFINED(MsftxDeviceTag);
    CHECK_DEFINED(MsftyDeviceTag);
    CHECK_DEFINED(MsfuxDeviceTag);
    CHECK_DEFINED(MsfuyDeviceTag);
    CHECK_DEFINED(MsfvxDeviceTag);
    CHECK_DEFINED(MsfvyDeviceTag);
#undef CHECK_DEFINED
}

void republish_maps(TileCase& tile) {
    wrf::sdirk3::ZeroCopyConfig setup{};
    setup.nx = nx;
    setup.ny = ny;
    setup.nz = nz;
    setup.nx_u = nu;
    setup.ny_v = nv;
    setup.nz_w = nw;
    setup.ids = setup.its = setup.ims = 1;
    setup.ide = setup.ite = setup.ime = nu;
    setup.jds = setup.jts = setup.jms = 1;
    setup.jde = setup.jte = setup.jme = nv;
    setup.kds = setup.kts = setup.kms = 1;
    setup.kde = setup.kme = nw;
    setup.kte = nz;
    setup.u_ptr = setup.v_ptr = setup.w_ptr = setup.ph_ptr = setup.t_ptr = setup.p_ptr =
        tile.setup_state.data();
    setup.mu_ptr = tile.setup_state.data();
    setup.al_ptr = tile.setup_density.data();
    setup.rdx = setup.rdy = 1.0f / tile.spacing;
    setup.rdnw_ptr = setup.rdn_ptr = tile.metric.data();
    setup.msftx_ptr = setup.msfty_ptr = setup.msfux_ptr = setup.msfuy_ptr =
        setup.msfvx_ptr = setup.msfvy_ptr = tile.setup_map.data();
    setup.c1f_ptr = setup.c1h_ptr = tile.one.data();
    setup.c2f_ptr = setup.c2h_ptr = tile.zero.data();
    setup.fnm_ptr = setup.fnp_ptr = tile.half.data();
    setup.f_ptr = tile.setup_f.data();
    setup.e_ptr = setup.sina_ptr = tile.setup_zero.data();
    setup.cosa_ptr = tile.setup_map.data();
    tile.solver.advanceZeroCopy(setup, 2, 0.1f);
}

void install_pullback_graph(TileSDIRK3UnifiedSolver& solver) {
    const auto options = torch::TensorOptions().dtype(torch::kFloat32).requires_grad(true);
    const auto input = torch::ones({2}, options);
    solver.*access(LastStepInputGraphTag{}) = input;
    solver.*access(LastStepOutputGraphTag{}) = input * input;
}

void require_pullback_rejected(TileSDIRK3UnifiedSolver& solver,
                               const char* phase) {
    TORCH_CHECK(!(solver.*access(LastStepInputGraphTag{})).defined() &&
                    !(solver.*access(LastStepOutputGraphTag{})).defined(),
                phase, ": full reset retained a pullback graph");
    bool rejected = false;
    try {
        (void)solver.pullbackLastStep(torch::ones({2}, torch::kFloat32));
    } catch (const std::exception&) {
        rejected = true;
    }
    TORCH_CHECK(rejected, phase, ": pullbackLastStep accepted a reset-invalidated graph");
}

} // namespace

int main() {
    wrf::sdirk3::g_sdirk3_config = wrf::sdirk3::SDIRK3Config{};
    TileCase tile;

    require_maps_defined(tile.solver, true, "initial publication");
    const MapSnapshot before_full_reset = snapshot(tile.solver);
    install_pullback_graph(tile.solver);

    tile.solver.invalidateCaches();
    require_maps_defined(tile.solver, false, "invalidateCaches");
    require_pullback_rejected(tile.solver, "invalidateCaches");
    const MapSnapshot after_full_reset = snapshot(tile.solver);
    TORCH_CHECK(after_full_reset.epoch == before_full_reset.epoch + 1 &&
                    after_full_reset.epoch_cached == 0 &&
                    after_full_reset.signature == 0.0f,
                "invalidateCaches did not clear map metadata");

    republish_maps(tile);
    require_maps_defined(tile.solver, true, "republish after invalidateCaches");
    const MapSnapshot before_global_reset = snapshot(tile.solver);
    install_pullback_graph(tile.solver);

    tile.solver.invalidateGlobalCachesOnly();
    require_maps_defined(tile.solver, false, "invalidateGlobalCachesOnly");
    require_pullback_rejected(tile.solver, "invalidateGlobalCachesOnly");
    const MapSnapshot after_global_reset = snapshot(tile.solver);
    TORCH_CHECK(after_global_reset.epoch == before_global_reset.epoch + 1 &&
                    after_global_reset.epoch_cached == 0 &&
                    after_global_reset.signature == 0.0f,
                "invalidateGlobalCachesOnly did not clear map metadata");

    republish_maps(tile);
    require_maps_defined(tile.solver, true, "republish after invalidateGlobalCachesOnly");
    const MapSnapshot before_light_reset = snapshot(tile.solver);

    // reset_state's implementation is grid_info->resetPerSolverState(). It is
    // the supported lightweight path and must preserve published pointers.
    tile.solver.getGridInfo()->resetPerSolverState();
    const MapSnapshot after_light_reset = snapshot(tile.solver);
    require_maps_defined(tile.solver, true, "lightweight reset");
    TORCH_CHECK(before_light_reset.data == after_light_reset.data &&
                    before_light_reset.epoch == after_light_reset.epoch &&
                    before_light_reset.epoch_cached == after_light_reset.epoch_cached &&
                    before_light_reset.signature == after_light_reset.signature,
                "lightweight reset changed the published map-factor cache");

    std::cout << "MSF_CACHE_RESET: PASS (CPU structural; CUDA transfer unverified)\n";
    return 0;
}
