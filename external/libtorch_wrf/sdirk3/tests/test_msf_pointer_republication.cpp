// Host publication must rebind all six map arrays even when their values are equal.
#include "tile_test_fixture.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <vector>

template<typename Tag, typename Tag::type Member> struct DataAccessor {
    friend typename Tag::type access(Tag) { return Member; }
};

#define DEFINE_ACCESSOR(TAG, TYPE, MEMBER) \
    struct TAG { using type = TYPE TileSDIRK3UnifiedSolver::*; friend type access(TAG); }; \
    template struct DataAccessor<TAG, &TileSDIRK3UnifiedSolver::MEMBER>

DEFINE_ACCESSOR(MsfEpochTag, uint64_t, msf_epoch_);
DEFINE_ACCESSOR(MsfEpochCachedTag, uint64_t, msf_epoch_cached_);
DEFINE_ACCESSOR(MsfSignatureTag, float, msf_signature_);
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

#undef DEFINE_ACCESSOR

namespace {

using namespace wrf::sdirk3::test;

struct MapSources {
    std::vector<float> tx = std::vector<float>(nu * nv, 1.0f);
    std::vector<float> ty = std::vector<float>(nu * nv, 1.0f);
    std::vector<float> ux = std::vector<float>(nu * nv, 1.0f);
    std::vector<float> uy = std::vector<float>(nu * nv, 1.0f);
    std::vector<float> vx = std::vector<float>(nu * nv, 1.0f);
    std::vector<float> vy = std::vector<float>(nu * nv, 1.0f);
};

std::array<const void*, 6> cpu_sources(const TileSDIRK3UnifiedSolver& solver) {
    return {
        (solver.*access(MsftxCpuTag{})).data_ptr(),
        (solver.*access(MsftyCpuTag{})).data_ptr(),
        (solver.*access(MsfuxCpuTag{})).data_ptr(),
        (solver.*access(MsfuyCpuTag{})).data_ptr(),
        (solver.*access(MsfvxCpuTag{})).data_ptr(),
        (solver.*access(MsfvyCpuTag{})).data_ptr(),
    };
}

std::array<const void*, 6> device_sources(const TileSDIRK3UnifiedSolver& solver) {
    return {
        (solver.*access(MsftxTag{})).data_ptr(),
        (solver.*access(MsftyTag{})).data_ptr(),
        (solver.*access(MsfuxTag{})).data_ptr(),
        (solver.*access(MsfuyTag{})).data_ptr(),
        (solver.*access(MsfvxTag{})).data_ptr(),
        (solver.*access(MsfvyTag{})).data_ptr(),
    };
}

void publish(TileCase& tile, const std::array<std::vector<float>*, 6>& maps) {
    wrf::sdirk3::ZeroCopyConfig config{};
    config.nx = nx;
    config.ny = ny;
    config.nz = nz;
    config.nx_u = nu;
    config.ny_v = nv;
    config.nz_w = nw;
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
    config.msftx_ptr = maps[0]->data();
    config.msfty_ptr = maps[1]->data();
    config.msfux_ptr = maps[2]->data();
    config.msfuy_ptr = maps[3]->data();
    config.msfvx_ptr = maps[4]->data();
    config.msfvy_ptr = maps[5]->data();
    config.c1f_ptr = config.c1h_ptr = tile.one.data();
    config.c2f_ptr = config.c2h_ptr = tile.zero.data();
    config.fnm_ptr = config.fnp_ptr = tile.half.data();
    config.f_ptr = tile.setup_f.data();
    config.e_ptr = config.sina_ptr = tile.setup_zero.data();
    config.cosa_ptr = tile.setup_map.data();
    tile.solver.advanceZeroCopy(config, 2, 0.1f);
}

void require_all_equal(const char* phase,
                       const std::array<const void*, 6>& got,
                       const std::array<const void*, 6>& expected) {
    for (std::size_t slot = 0; slot < got.size(); ++slot) {
        TORCH_CHECK(got[slot] == expected[slot], phase, " slot ", slot,
                    " got ", got[slot], " expected ", expected[slot]);
    }
}

std::array<const void*, 6> incoming(const std::array<std::vector<float>*, 6>& active) {
    std::array<const void*, 6> result{};
    for (std::size_t slot = 0; slot < active.size(); ++slot) {
        result[slot] = active[slot]->data();
    }
    return result;
}

} // namespace

int main() {
    wrf::sdirk3::g_sdirk3_config = wrf::sdirk3::SDIRK3Config{};
    TileCase tile;
    MapSources initial;
    std::array<std::vector<float>*, 6> active = {
        &initial.tx, &initial.ty, &initial.ux, &initial.uy, &initial.vx, &initial.vy};

    publish(tile, active);
    const auto expected_baseline = incoming(active);
    require_all_equal("baseline CPU publication", cpu_sources(tile.solver), expected_baseline);
    require_all_equal("baseline device publication", device_sources(tile.solver), expected_baseline);
    TORCH_CHECK(tile.solver.*access(MsfSignatureTag{}) == 54.0f,
                "baseline nine-point signature changed unexpectedly");
    std::cout << "BASELINE_12_POINTERS: PASS epoch="
              << (tile.solver.*access(MsfEpochTag{})) << "\n";

    const char* names[6] = {"msftx", "msfty", "msfux", "msfuy", "msfvx", "msfvy"};
    std::vector<std::unique_ptr<std::vector<float>>> replacements;
    for (std::size_t slot = 0; slot < active.size(); ++slot) {
        const void* old_pointer = active[slot]->data();
        std::vector<float>* old_source = active[slot];
        replacements.emplace_back(std::make_unique<std::vector<float>>(nu * nv, 1.0f));
        active[slot] = replacements.back().get();
        publish(tile, active);
        const auto expected = incoming(active);
        require_all_equal(names[slot], cpu_sources(tile.solver), expected);
        const auto device = device_sources(tile.solver);
        if (device != expected) {
            std::cout << "STALE_DEVICE_AFTER_" << names[slot] << "\n";
            for (std::size_t i = 0; i < device.size(); ++i) {
                std::cout << "  slot=" << i << " current=" << device[i]
                          << " incoming=" << expected[i] << "\n";
            }
            return 2;
        }
        TORCH_CHECK(tile.solver.*access(MsfSignatureTag{}) == 54.0f,
                    names[slot], " changed the equal-value signature");
        TORCH_CHECK(cpu_sources(tile.solver)[slot] != old_pointer &&
                        device_sources(tile.solver)[slot] != old_pointer,
                    names[slot], " still aliases its old source");

        // Keep the old allocation alive and make it observably different. A
        // current publication must remain on the fresh allocation.
        (*old_source)[0] = 17.0f;
        TORCH_CHECK((*active[slot])[0] == 1.0f,
                    names[slot], " incoming source was unexpectedly modified");
        std::cout << "REPLACE_" << names[slot] << ": PASS all12 old_alias="
                  << old_pointer << " new=" << expected[slot]
                  << " epoch=" << (tile.solver.*access(MsfEpochTag{})) << "\n";
    }

    std::cout << "MSF_POINTER_REPUBLISH: PASS (six equal-value array replacements; all 12 views follow incoming buffers)\n";
    return 0;
}
