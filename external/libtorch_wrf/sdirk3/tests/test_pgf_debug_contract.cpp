// Finite PGF values must not depend on the RHS debug level.
// This is a production-linked witness for the float32 norm-overflow guard.
#include "tile_test_fixture.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
struct RuTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(RuTag);
};
struct BoundaryTag {
    using type = wrf::sdirk3::WdampRuntimeContract TileSDIRK3UnifiedSolver::*;
    friend type access(BoundaryTag);
};
struct CoeffTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*,
                                                   const float*, const float*);
    friend type access(CoeffTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<RuTag, &TileSDIRK3UnifiedSolver::ru_tend_>;
template struct Accessor<BoundaryTag, &TileSDIRK3UnifiedSolver::wdamp_contract_>;
template struct Accessor<CoeffTag, &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;

struct Snapshot {
    torch::Tensor rhs;
    torch::Tensor ru;
};

void configure_geometry(TileCase& tile) {
    tile.solver.setWRFIndices(1,nx,1,ny,1,nz, 1,nx,1,ny,1,nz,
                              1,nu,1,nv,1,nw);
    // Resolve the same one-rank contract as the stage initializer without
    // integrating this synthetic, extremely fine-grid RHS fixture.
    tile.solver.*access(BoundaryTag{}) = wrf::sdirk3::resolve_wdamp_runtime_contract(
        true, 1, 1, true, true, false, false, false, false,
        false, true, true, false, false, false, false, false);
    auto grid = tile.solver.getGridInfo();
    grid->rdnw = torch::full({nz}, 4.0f, torch::kFloat32);
    grid->rdn = torch::full({nz}, 4.0f, torch::kFloat32);
    grid->dnw = torch::full({nz}, -0.25f, torch::kFloat32);

    // The fixture's rk_step=2 zero-copy setup is retained; these are the same
    // WRF coefficients, made explicit for a direct RHS-only call.
    std::vector<float> c1f(nw, 1.0f), c2f(nw, 0.0f);
    std::vector<float> c1h(nz, 1.0f), c2h(nz, 0.0f);
    (tile.solver.*access(CoeffTag{}))(c1f.data(), c2f.data(), c1h.data(), c2h.data());
}

void configure_state(TileCase& tile) {
    std::fill(tile.u.begin(), tile.u.end(), 0.0f);
    std::fill(tile.v.begin(), tile.v.end(), 0.0f);
    std::fill(tile.w.begin(), tile.w.end(), 0.0f);
    std::fill(tile.theta.begin(), tile.theta.end(), 0.0f);
    std::fill(tile.mu.begin(), tile.mu.end(), 0.0f);
    for (int j = 0; j < ny; ++j)
        for (int k = 0; k < nw; ++k)
            for (int i = 0; i < nx; ++i)
                tile.ph[(j*nw + k)*nx + i] = static_cast<float>(i);
}

Snapshot evaluate(TileCase& tile, int debug_level, RhsMode mode) {
    wrf::sdirk3::g_sdirk3_config.debug_level = debug_level;
    const auto rhs = (tile.solver.*access(RhsTag{}))(tile.state(), mode).detach().clone();
    const auto ru = (tile.solver.*access(RuTag{})).detach().clone();
    return {rhs, ru};
}

torch::Tensor u_block(const Snapshot& x) {
    return x.rhs.slice(0, 0, su);
}

bool finite(const Snapshot& x) {
    return torch::isfinite(x.rhs).all().item<bool>() &&
           torch::isfinite(x.ru).all().item<bool>();
}

bool equal(const Snapshot& a, const Snapshot& b) {
    return torch::equal(a.rhs, b.rhs);
}
} // namespace

void check_pgf_debug_contract() {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.imex_split_mode = 3;
    cfg.mass_coordinate_mode = 1; // WRFParity: complete mass-coordinate path
    cfg.wrf_omega_ww_cp = false;
    cfg.mu_horizontal_div_only = false;
    cfg.omega_w_blend = 1.0f;
    cfg.sign_smooth_delta = 0.0f;

    TileCase tile(1.0e-14f);
    configure_geometry(tile);
    configure_state(tile);

    const auto implicit0 = evaluate(tile, 0, RhsMode::ImplicitOnly);
    const auto implicit2 = evaluate(tile, 2, RhsMode::ImplicitOnly);
    const auto full0 = evaluate(tile, 0, RhsMode::Full);
    const auto full2 = evaluate(tile, 2, RhsMode::Full);

    TORCH_CHECK(finite(implicit0) && finite(implicit2) && finite(full0) && finite(full2),
                "finite PGF probe returned a nonfinite RHS or raw U force");

    // The raw debug-0 U PGF entries are finite, but the production float32
    // reduction over their squares overflows. This is the trigger that used
    // to make debug_level=2 erase the whole U PGF tensor.
    const float raw_norm = implicit0.ru.norm().item<float>();
    TORCH_CHECK(std::isinf(raw_norm),
                "fixture did not exercise the finite float32 norm overflow: norm=", raw_norm);
    TORCH_CHECK(u_block(implicit0).abs().max().item<double>() > 0.0,
                "debug_level=0 produced no physical U PGF signal");
    TORCH_CHECK(u_block(implicit2).abs().max().item<double>() > 0.0,
                "debug_level=2 erased a finite physical U PGF signal");

    // With u=v=w=mu=theta'=0 and PH linear only in x, Full and ImplicitOnly
    // share the same pressure-force RHS. Debug diagnostics must not change it.
    TORCH_CHECK(equal(implicit0, implicit2),
                "ImplicitOnly RHS changed when debug_level changed");
    TORCH_CHECK(equal(full0, full2),
                "Full RHS changed when debug_level changed");
    TORCH_CHECK(equal(implicit0, full0) && equal(implicit2, full2),
                "Full and ImplicitOnly pressure-force RHS disagree");

    std::cout << "PGF_DEBUG_CONTRACT spacing=" << tile.spacing
              << " raw_u_norm_f32=inf u_max="
              << u_block(implicit0).abs().max().item<double>() << '\n';
}
