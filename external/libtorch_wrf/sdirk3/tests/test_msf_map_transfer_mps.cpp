// Actual MPS map-cache transfer contract.
// The test returns 77 only when the MPS C++ backend is unavailable. Any map
// publication, refresh, reset, or numeric mismatch is a test failure (1).
#include "tile_test_fixture.h"

#if defined(__has_include)
#  if __has_include(<torch/mps.h>)
#    include <torch/mps.h>
#    define SDIRK3_HAS_MPS_HEADER 1
#  endif
#endif

#ifndef SDIRK3_HAS_MPS_HEADER
#  define SDIRK3_HAS_MPS_HEADER 0
#endif

#if SDIRK3_HAS_MPS_HEADER

#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};

#define DEFINE_ACCESSOR(TAG, TYPE, MEMBER) \
    struct TAG { using type = TYPE TileSDIRK3UnifiedSolver::*; friend type access(TAG); }; \
    template struct Accessor<TAG, &TileSDIRK3UnifiedSolver::MEMBER>

DEFINE_ACCESSOR(MuBaseTag, torch::Tensor, mu_base_);
DEFINE_ACCESSOR(MsftxCpuTag, torch::Tensor, msftx_cpu_);
DEFINE_ACCESSOR(MsftyCpuTag, torch::Tensor, msfty_cpu_);
DEFINE_ACCESSOR(MsfuxCpuTag, torch::Tensor, msfux_cpu_);
DEFINE_ACCESSOR(MsfuyCpuTag, torch::Tensor, msfuy_cpu_);
DEFINE_ACCESSOR(MsfvxCpuTag, torch::Tensor, msfvx_cpu_);
DEFINE_ACCESSOR(MsfvyCpuTag, torch::Tensor, msfvy_cpu_);
DEFINE_ACCESSOR(MsftxTag, torch::Tensor, msftx_);
DEFINE_ACCESSOR(MsftyTag, torch::Tensor, msfty_);
DEFINE_ACCESSOR(MsfuxTag, torch::Tensor, msfux_);
DEFINE_ACCESSOR(MsfuyTag, torch::Tensor, msfuy_);
DEFINE_ACCESSOR(MsfvxTag, torch::Tensor, msfvx_);
DEFINE_ACCESSOR(MsfvyTag, torch::Tensor, msfvy_);
DEFINE_ACCESSOR(MsfEpochTag, uint64_t, msf_epoch_);
DEFINE_ACCESSOR(MsfEpochCachedTag, uint64_t, msf_epoch_cached_);

#undef DEFINE_ACCESSOR

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)
        (const torch::Tensor&, wrf::sdirk3::RhsMode);
    friend type access(RhsTag);
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;

namespace {

using namespace wrf::sdirk3::test;
using TensorMember = torch::Tensor TileSDIRK3UnifiedSolver::*;

struct MapSources {
    // Keep the WRF fixture's common backing extent for every map view. The
    // publication supplies the actual staggered shapes and strides.
    std::vector<float> tx = std::vector<float>(nu * nv, 1.0f);
    std::vector<float> ty = std::vector<float>(nu * nv, 1.0f);
    std::vector<float> ux = std::vector<float>(nu * nv, 1.0f);
    std::vector<float> uy = std::vector<float>(nu * nv, 1.0f);
    std::vector<float> vx = std::vector<float>(nu * nv, 1.0f);
    std::vector<float> vy = std::vector<float>(nu * nv, 1.0f);
};

std::array<TensorMember, 6> cpu_members() {
    return {access(MsftxCpuTag{}), access(MsftyCpuTag{}), access(MsfuxCpuTag{}),
            access(MsfuyCpuTag{}), access(MsfvxCpuTag{}), access(MsfvyCpuTag{})};
}

std::array<TensorMember, 6> device_members() {
    return {access(MsftxTag{}), access(MsftyTag{}), access(MsfuxTag{}),
            access(MsfuyTag{}), access(MsfvxTag{}), access(MsfvyTag{})};
}

std::array<std::vector<float>*, 6> source_members(MapSources& source) {
    return {&source.tx, &source.ty, &source.ux, &source.uy, &source.vx, &source.vy};
}

void publish_cpu(TileCase& tile, const std::array<std::vector<float>*, 6>& maps) {
    wrf::sdirk3::ZeroCopyConfig config{};
    config.nx = nx; config.ny = ny; config.nz = nz;
    config.nx_u = nu; config.ny_v = nv; config.nz_w = nw;
    config.ids = config.its = config.ims = 1;
    config.ide = config.ite = config.ime = nu;
    config.jds = config.jts = config.jms = 1;
    config.jde = config.jte = config.jme = nv;
    config.kds = config.kts = config.kms = 1;
    config.kde = config.kte = config.kme = nw;
    config.u_ptr = config.v_ptr = config.w_ptr = config.ph_ptr = config.t_ptr =
        config.p_ptr = tile.setup_state.data();
    config.mu_ptr = tile.setup_state.data();
    config.al_ptr = tile.setup_density.data();
    config.rdx = config.rdy = 1.0f / tile.spacing;
    config.rdnw_ptr = config.rdn_ptr = tile.metric.data();
    config.msftx_ptr = maps[0]->data(); config.msfty_ptr = maps[1]->data();
    config.msfux_ptr = maps[2]->data(); config.msfuy_ptr = maps[3]->data();
    config.msfvx_ptr = maps[4]->data(); config.msfvy_ptr = maps[5]->data();
    config.c1f_ptr = config.c1h_ptr = tile.one.data();
    config.c2f_ptr = config.c2h_ptr = tile.zero.data();
    config.fnm_ptr = config.fnp_ptr = tile.half.data();
    config.f_ptr = tile.setup_f.data();
    config.e_ptr = config.sina_ptr = tile.setup_zero.data();
    config.cosa_ptr = tile.setup_map.data();
    tile.solver.advanceZeroCopy(config, 2, 0.1f);
}

std::array<const void*, 6> source_pointers(const TileSDIRK3UnifiedSolver& solver) {
    const auto members = cpu_members();
    std::array<const void*, 6> result{};
    for (std::size_t i = 0; i < members.size(); ++i)
        result[i] = (solver.*members[i]).data_ptr();
    return result;
}

std::array<const void*, 6> incoming_pointers(
    const std::array<std::vector<float>*, 6>& maps) {
    std::array<const void*, 6> result{};
    for (std::size_t i = 0; i < maps.size(); ++i)
        result[i] = maps[i]->data();
    return result;
}

void require_cpu_publication(const TileSDIRK3UnifiedSolver& solver,
                             const std::array<std::vector<float>*, 6>& maps,
                             const char* phase) {
    const auto got = source_pointers(solver);
    const auto expected = incoming_pointers(maps);
    for (std::size_t i = 0; i < got.size(); ++i)
        TORCH_CHECK(got[i] == expected[i], phase, ": CPU map slot ", i,
                    " did not follow advanceZeroCopy publication");
}

void require_cpu_values(const TileSDIRK3UnifiedSolver& solver,
                        float expected,
                        const char* phase) {
    const auto cpus = cpu_members();
    for (std::size_t i = 0; i < cpus.size(); ++i) {
        const auto host = (solver.*cpus[i]).detach().to(torch::kCPU).contiguous();
        TORCH_CHECK(host.min().item<float>() == expected &&
                        host.max().item<float>() == expected,
                    phase, ": CPU map slot ", i, " does not contain ", expected);
    }
}

void require_all_undefined(const TileSDIRK3UnifiedSolver& solver, const char* phase) {
    const auto cpus = cpu_members();
    const auto devices = device_members();
    for (std::size_t i = 0; i < cpus.size(); ++i) {
        TORCH_CHECK(!(solver.*cpus[i]).defined(), phase, ": CPU map slot ", i,
                    " remains defined");
        TORCH_CHECK(!(solver.*devices[i]).defined(), phase, ": device map slot ", i,
                    " remains defined");
    }
}

void require_mps_maps(const TileSDIRK3UnifiedSolver& solver, const char* phase) {
    const auto devices = device_members();
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const auto& map = solver.*devices[i];
        TORCH_CHECK(map.defined(), phase, ": device map slot ", i, " is undefined");
        TORCH_CHECK(map.device().type() == torch::kMPS,
                    phase, ": device map slot ", i, " is ", map.device());
    }
}

void require_mps_values(const TileSDIRK3UnifiedSolver& solver,
                        float expected,
                        const char* phase) {
    torch::mps::synchronize();
    const auto devices = device_members();
    for (std::size_t i = 0; i < devices.size(); ++i) {
        const auto& map = solver.*devices[i];
        const auto host = map.detach().to(torch::kCPU).contiguous();
        torch::mps::synchronize();
        const float min = host.min().item<float>();
        const float max = host.max().item<float>();
        if (min != expected || max != expected)
            std::cerr << phase << ": slot=" << i << " expected=" << expected
                      << " min=" << min << " max=" << max << "\n";
        TORCH_CHECK(min == expected && max == expected,
                    phase, ": MPS map slot ", i, " does not contain ", expected);
    }
}

void require_cpu_device_members(TileSDIRK3UnifiedSolver& solver, const char* phase) {
    const auto cpus = cpu_members();
    const auto devices = device_members();
    for (std::size_t i = 0; i < devices.size(); ++i) {
        solver.*devices[i] = solver.*cpus[i];
        TORCH_CHECK((solver.*devices[i]).defined() &&
                        (solver.*devices[i]).device().type() == torch::kCPU,
                    phase, ": map slot ", i, " was not restored to CPU");
    }
}

void clear_cpu_originals(TileSDIRK3UnifiedSolver& solver, const char* phase) {
    for (const auto member : cpu_members())
        solver.*member = torch::Tensor();
    for (const auto member : cpu_members())
        TORCH_CHECK(!(solver.*member).defined(), phase, ": CPU original remained defined");
}

void restore_rhs_base_state(TileSDIRK3UnifiedSolver& solver, const torch::Device& mps) {
    static const std::vector<float> p_base(st, 100000.0f);
    static const std::vector<float> t_base(st, 0.0f);
    static const std::vector<float> ph_base(sw, 0.0f);
    static const std::vector<float> mu_base(sm, 80000.0f);
    solver.setBaseState(p_base.data(), t_base.data(), ph_base.data(), mu_base.data());
    solver.*access(MuBaseTag{}) = (solver.*access(MuBaseTag{})).to(mps);
}

void invoke_actual_rhs(TileCase& tile, const torch::Device& mps) {
    torch::NoGradGuard no_grad;
    try {
        const auto rhs = (tile.solver.*access(RhsTag{}))(tile.state().to(mps),
                                                          wrf::sdirk3::RhsMode::ExplicitOnly);
        TORCH_CHECK(rhs.defined() && rhs.device().type() == torch::kMPS,
                    "RHS returned an unexpected tensor");
        std::cout << "RHS_COMPLETE: PASS\n";
    } catch (const std::exception& error) {
        const std::string message(error.what());
        TORCH_CHECK(message.find("Expected all tensors to be on the same device") !=
                        std::string::npos &&
                        message.find("mps") != std::string::npos &&
                        message.find("cpu") != std::string::npos,
                    "unexpected RHS exception before map validation: ", message);
        const auto newline = message.find('\n');
        std::cout << "RHS_BOUNDARY: " << message.substr(0, newline) << "\n";
    }
}

} // namespace

int main() {
    if (!torch::mps::is_available()) {
        std::cout << "MPS_MAP_TRANSFER: SKIP (MPS unavailable)\n";
        return 77;
    }

    try {
        const torch::Device mps(torch::kMPS);
        wrf::sdirk3::g_sdirk3_config = wrf::sdirk3::SDIRK3Config{};
        wrf::sdirk3::g_sdirk3_config.mass_coordinate_mode = 0;

        TileCase tile;
        MapSources initial;
        auto active = source_members(initial);
        publish_cpu(tile, active);
        require_cpu_publication(tile.solver, active, "baseline");
        require_cpu_values(tile.solver, 1.0f, "baseline");
        restore_rhs_base_state(tile.solver, mps);
        invoke_actual_rhs(tile, mps);
        require_mps_maps(tile.solver, "baseline");
        require_mps_values(tile.solver, 1.0f, "baseline");
        const auto device_members_now = device_members();
        std::array<torch::Tensor, 6> previous_maps{};
        for (std::size_t i = 0; i < previous_maps.size(); ++i)
            previous_maps[i] = tile.solver.*device_members_now[i];

        const std::array<int, 6> sizes = {nu * nv, nu * nv, nu * nv,
                                          nu * nv, nu * nv, nu * nv};
        const char* names[6] = {"msftx", "msfty", "msfux", "msfuy", "msfvx", "msfvy"};
        std::vector<std::unique_ptr<std::vector<float>>> replacements;
        for (std::size_t slot = 0; slot < active.size(); ++slot) {
            auto* old_source = active[slot];
            replacements.emplace_back(std::make_unique<std::vector<float>>(sizes[slot], 1.0f));
            active[slot] = replacements.back().get();
            publish_cpu(tile, active);
            require_cpu_publication(tile.solver, active, names[slot]);
            require_cpu_values(tile.solver, 1.0f, names[slot]);
            restore_rhs_base_state(tile.solver, mps);
            invoke_actual_rhs(tile, mps);
            require_mps_maps(tile.solver, names[slot]);
            require_mps_values(tile.solver, 1.0f, names[slot]);
            TORCH_CHECK((tile.solver.*device_members_now[slot]).unsafeGetTensorImpl() !=
                            previous_maps[slot].unsafeGetTensorImpl(),
                        names[slot], ": equal-value source replacement reused device storage");
            (*old_source)[0] = 17.0f;
            TORCH_CHECK((*active[slot])[0] == 1.0f,
                        names[slot], ": old source mutation reached new publication");
            for (std::size_t i = 0; i < previous_maps.size(); ++i)
                previous_maps[i] = tile.solver.*device_members_now[i];
        }

        for (auto* source : active)
            std::fill(source->begin(), source->end(), 2.0f);
        publish_cpu(tile, active);
        require_cpu_values(tile.solver, 2.0f, "value-change");
        restore_rhs_base_state(tile.solver, mps);
        invoke_actual_rhs(tile, mps);
        require_mps_maps(tile.solver, "value-change");
        require_mps_values(tile.solver, 2.0f, "value-change");

        tile.solver.invalidateCaches();
        require_all_undefined(tile.solver, "full-reset");
        for (auto* source : active)
            std::fill(source->begin(), source->end(), 3.0f);
        publish_cpu(tile, active);
        require_cpu_publication(tile.solver, active, "reset-republish");
        require_cpu_values(tile.solver, 3.0f, "reset-republish");
        restore_rhs_base_state(tile.solver, mps);
        invoke_actual_rhs(tile, mps);
        require_mps_maps(tile.solver, "reset-republish");
        require_mps_values(tile.solver, 3.0f, "reset-republish");

        // Exercise all six fallback branches: current map members are CPU,
        // CPU originals are undefined, and the MPS RHS forces a refresh.
        publish_cpu(tile, active);
        require_cpu_values(tile.solver, 3.0f, "fallback-setup");
        require_cpu_device_members(tile.solver, "fallback-setup");
        clear_cpu_originals(tile.solver, "fallback-setup");
        restore_rhs_base_state(tile.solver, mps);
        invoke_actual_rhs(tile, mps);
        require_mps_maps(tile.solver, "fallback");
        require_mps_values(tile.solver, 3.0f, "fallback");

        // Exercise the legacy align_msf path outside need_refresh. Retain the
        // automatically refreshed msftx tensor, restore the other five members
        // to CPU, and preserve epoch/cache equality before the RHS call.
        const auto retained_msftx = tile.solver.*access(MsftxTag{});
        publish_cpu(tile, active);
        require_cpu_publication(tile.solver, active, "legacy-align-setup");
        require_cpu_values(tile.solver, 3.0f, "legacy-align-setup");
        const auto cpus = cpu_members();
        const auto devices = device_members();
        tile.solver.*devices[0] = retained_msftx;
        for (std::size_t i = 1; i < devices.size(); ++i)
            tile.solver.*devices[i] = tile.solver.*cpus[i];
        TORCH_CHECK((tile.solver.*access(MsfEpochCachedTag{})) ==
                        (tile.solver.*access(MsfEpochTag{})),
                    "legacy-align setup changed cache generation");
        restore_rhs_base_state(tile.solver, mps);
        invoke_actual_rhs(tile, mps);
        require_mps_maps(tile.solver, "legacy-align");
        require_mps_values(tile.solver, 3.0f, "legacy-align");

        std::cout << "MPS_MAP_TRANSFER: PASS source-republish/value-change/reset/fallback/legacy-align\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MPS_MAP_TRANSFER: FAIL: " << error.what() << "\n";
        return 1;
    }
}

#else

int main() {
    std::cout << "MPS_MAP_TRANSFER: SKIP (torch/mps.h unavailable)\n";
    return 77;
}

#endif
