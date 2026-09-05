#include "tile_test_fixture.h"
#include "wrf_sdirk3_unified_preconditioner.h"
#include "wrf_sdirk3_config.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {

using wrf::sdirk3::UnifiedPreconditioner;
using namespace wrf::sdirk3::test;

struct WThetaTag {
    using type = void (UnifiedPreconditioner::*) (
        torch::Tensor&, torch::Tensor&, int, int, torch::Tensor*, const float*,
        const float*, int, UnifiedPreconditioner::GradPolicy);
    friend type access(WThetaTag);
};
struct VdWTag {
    using type = torch::Tensor UnifiedPreconditioner::*;
    friend type access(VdWTag);
};
struct VlWTag {
    using type = torch::Tensor UnifiedPreconditioner::*;
    friend type access(VlWTag);
};
struct VuWTag {
    using type = torch::Tensor UnifiedPreconditioner::*;
    friend type access(VuWTag);
};
struct WThetaUpperTag {
    using type = torch::Tensor UnifiedPreconditioner::*;
    friend type access(WThetaUpperTag);
};
struct WThetaLowerTag {
    using type = torch::Tensor UnifiedPreconditioner::*;
    friend type access(WThetaLowerTag);
};
struct ThetaWUpperTag {
    using type = torch::Tensor UnifiedPreconditioner::*;
    friend type access(ThetaWUpperTag);
};
struct ThetaWLowerTag {
    using type = torch::Tensor UnifiedPreconditioner::*;
    friend type access(ThetaWLowerTag);
};
struct PhiWTag {
    using type = std::vector<float> UnifiedPreconditioner::*;
    friend type access(PhiWTag);
};
struct PhiWNoBoostTag {
    using type = std::vector<float> UnifiedPreconditioner::*;
    friend type access(PhiWNoBoostTag);
};
struct PhiDiagTag {
    using type = torch::Tensor UnifiedPreconditioner::*;
    friend type access(PhiDiagTag);
};
struct DzEffectiveTag {
    using type = std::vector<float> UnifiedPreconditioner::*;
    friend type access(DzEffectiveTag);
};
struct PhiWCacheGenTag {
    using type = uint64_t UnifiedPreconditioner::*;
    friend type access(PhiWCacheGenTag);
};
struct ComputePhiWTag {
    using type = bool (UnifiedPreconditioner::*)(int, int);
    friend type access(ComputePhiWTag);
};

template<typename Tag, typename Tag::type Member>
struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};

template struct Accessor<WThetaTag, &UnifiedPreconditioner::solve_coupled_w_theta_batched>;
template struct Accessor<VdWTag, &UnifiedPreconditioner::vertical_diag_w_>;
template struct Accessor<VlWTag, &UnifiedPreconditioner::vertical_lower_w_>;
template struct Accessor<VuWTag, &UnifiedPreconditioner::vertical_upper_w_>;
template struct Accessor<WThetaUpperTag, &UnifiedPreconditioner::w_theta_coupling_upper_>;
template struct Accessor<WThetaLowerTag, &UnifiedPreconditioner::w_theta_coupling_lower_>;
template struct Accessor<ThetaWUpperTag, &UnifiedPreconditioner::theta_w_coupling_upper_>;
template struct Accessor<ThetaWLowerTag, &UnifiedPreconditioner::theta_w_coupling_lower_>;
template struct Accessor<PhiWTag, &UnifiedPreconditioner::phi_w_coupling_wph_>;
template struct Accessor<PhiWNoBoostTag, &UnifiedPreconditioner::phi_w_D_w_nosboost_>;
template struct Accessor<PhiDiagTag, &UnifiedPreconditioner::vertical_diag_phi_>;
template struct Accessor<DzEffectiveTag, &UnifiedPreconditioner::dz_effective_cached_>;
template struct Accessor<PhiWCacheGenTag, &UnifiedPreconditioner::phi_w_cached_gen_>;
template struct Accessor<ComputePhiWTag, &UnifiedPreconditioner::compute_phi_w_coupling_coefficients>;

struct Options {
    bool phi_unity = false;
    bool uncapped = false;
    bool floor_hit = false;
};

Options parse_options(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--phi-unity") o.phi_unity = true;
        else if (arg == "--uncapped") o.uncapped = true;
        else if (arg == "--floor-hit") o.floor_hit = true;
        else TORCH_CHECK(false, "unknown option: ", arg);
    }
    return o;
}

float helper_phi(float h, float c_s, float dz, bool unity) {
    return unity ? 1.0f : 1.0f + h*c_s*c_s/(dz*dz);
}

std::vector<float> local_a_eff(const std::vector<float>& raw,
                               const std::vector<float>& dphi,
                               const std::vector<float>& d_full,
                               float cap) {
    std::vector<float> result(raw.size(), 0.0f);
    int cap_hits = 0;
    for (size_t k = 0; k < raw.size(); ++k) {
        result[k] = raw[k];
        if (cap > 0.0f && dphi[k] > 1.0e-6f) {
            const float a_max = std::sqrt(cap*dphi[k]*std::abs(d_full[k]));
            if (raw[k] > a_max) ++cap_hits;
            result[k] = std::min(raw[k], a_max);
        }
    }
    std::cout << "PHASE2_CAP cap=" << cap << " cap_hits=" << cap_hits << '\n';
    return result;
}

} // namespace

int main(int argc, char** argv) {
    const Options opt = parse_options(argc, argv);
    if (opt.phi_unity)
        ::setenv("WRF_SDIRK3_PHI_SCHUR_DENOM_UNITY", "1", /*overwrite=*/1);
    else
        ::unsetenv("WRF_SDIRK3_PHI_SCHUR_DENOM_UNITY");

    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.debug_level = 0;
    cfg.precond_acoustic_4x4 = 1;
    cfg.imex_split_mode = 3;
    cfg.precond_coupled_phi_w = true;
    cfg.precond_phi_w_coupling_scale = 2;
    cfg.precond_phi_w_schur_boost_on = true;
    cfg.precond_phi_w_schur_rhs_inject_on = true;
    cfg.precond_phi_w_schur_backsub_on = true;
    cfg.precond_phi_w_schur_cap_decay = 1.0f;
    cfg.precond_phi_w_schur_ru_scale = 0.0f;
    cfg.precond_phi_w_schur_alpha_gs = 0.0f;
    cfg.precond_phi_feedback_fallback_gs = false;
    cfg.precond_gs_min_iterations = 1;
    cfg.precond_gs_max_iterations = 1;
    cfg.precond_dw_nosboost_floor = opt.floor_hit ? 1.2f : 0.1f;
    // 1e-6 intentionally exercises the existing D_W_full-relative cap.  Zero is
    // the source's explicit uncapped setting.
    cfg.precond_phi_w_schur_boost_cap = opt.uncapped ? 0.0f : 1.0e-6f;

    TileCase tile;
    tile.step(1.0e-4f);
    const auto U = tile.state();
    TORCH_CHECK(U.scalar_type() == torch::kFloat32, "fixture must exercise Float32");

    auto grid = tile.solver.getGridInfo();
    auto physics = std::make_shared<wrf::sdirk3::PhysicsConfig>();
    constexpr float gamma = 0.4358665215f;
    constexpr float dt0 = 0.1f;
    // Use a deliberately larger but finite stage step so D_phi is visibly
    // different from one.  This catches confusing the Phase-2 phi_block
    // handoff (already r_phi/D_phi) with the raw r_phi input.
    constexpr float dt1 = 10.0f;
    UnifiedPreconditioner P(grid, physics, dt0, gamma);
    const auto mu_pert = U.slice(0, total-sm, total).reshape({ny, nx});
    P.bind_stage_state_or_throw(mu_pert, 1);
    P.update(U, dt0, gamma);

    const int nz_runtime = nz;
    const int nz_w_runtime = nw;
    TORCH_CHECK((P.*access(ComputePhiWTag{}))(nz_runtime, nz_w_runtime),
                "initial Phase 2 cache construction failed");
    const uint64_t generation0 = P.coefficient_generation();
    const uint64_t cache_generation0 = P.*access(PhiWCacheGenTag{});
    TORCH_CHECK(cache_generation0 == generation0,
                "initial cache generation mismatch: cache=", cache_generation0,
                " coefficients=", generation0);

    // update() rebuilds coefficients and must leave the old cache stale until the
    // explicit cache consumer rebuilds it. This checks cache invalidation across coefficient updates.
    P.update(U, dt1, gamma);
    const uint64_t generation1 = P.coefficient_generation();
    const uint64_t stale_cache_generation = P.*access(PhiWCacheGenTag{});
    TORCH_CHECK(generation1 > generation0, "coefficient generation did not advance");
    TORCH_CHECK(stale_cache_generation != generation1,
                "cache unexpectedly remained current across coefficient invalidation");
    TORCH_CHECK((P.*access(ComputePhiWTag{}))(nz_runtime, nz_w_runtime),
                "rebuild Phase 2 cache failed");
    const uint64_t cache_generation1 = P.*access(PhiWCacheGenTag{});
    TORCH_CHECK(cache_generation1 == generation1,
                "rebuilt cache generation mismatch: cache=", cache_generation1,
                " coefficients=", generation1);

    const auto vd_cpu = (P.*access(VdWTag{})).to(torch::kCPU).contiguous();
    const auto phi_cpu = (P.*access(PhiDiagTag{})).to(torch::kCPU).contiguous();
    const auto& raw_a = P.*access(PhiWTag{});
    const auto& cached_d0 = P.*access(PhiWNoBoostTag{});
    const auto& dz = P.*access(DzEffectiveTag{});
    TORCH_CHECK(vd_cpu.scalar_type() == torch::kFloat32 &&
                phi_cpu.scalar_type() == torch::kFloat32,
                "preconditioner contract must remain Float32");
    TORCH_CHECK(static_cast<int>(raw_a.size()) == nz_w_runtime &&
                cached_d0.size() == raw_a.size() &&
                phi_cpu.numel() == static_cast<int64_t>(raw_a.size()) &&
                dz.size() >= 2,
                "Phase 2 cache has inconsistent dimensions");

    const float h = dt1*gamma;
    const float c_s = grid->cs;
    const float beta = cfg.precond_w_acoustic_boost;
    const float floor = cfg.precond_dw_nosboost_floor;
    const float cap = cfg.precond_phi_w_schur_boost_cap;
    const auto* vd = vd_cpu.data_ptr<float>();
    const auto* phi = phi_cpu.data_ptr<float>();
    std::vector<float> expected_d0(raw_a.size(), 0.0f);
    std::vector<float> expected_dphi(raw_a.size(), 0.0f);
    std::vector<float> d0_raw(raw_a.size(), 0.0f);
    int floor_hits = 0;
    double max_cache_d0_err = 0.0;
    double max_phi_helper_err = 0.0;
    double max_builder_schur = 0.0;
    for (int k = 0; k < nz_w_runtime; ++k) {
        expected_d0[k] = vd[k];
        expected_dphi[k] = phi[k];
        if (k > 0 && k < nz_runtime) {
            const int klo = std::min(k - 1, static_cast<int>(dz.size()) - 1);
            const int khi = std::min(k, static_cast<int>(dz.size()) - 1);
            const float dz_builder = 0.5f*(dz[klo] + dz[khi]);
            TORCH_CHECK(std::isfinite(dz_builder) && dz_builder > 0.0f,
                        "invalid test builder dz at k=", k);
            const float helper = helper_phi(h, c_s, dz_builder, opt.phi_unity);
            const float acfl = h*c_s/dz_builder;
            const float builder_schur = beta*acfl*acfl/helper;
            d0_raw[k] = vd[k] - builder_schur;
            expected_d0[k] = std::max(d0_raw[k], floor);
            expected_dphi[k] = helper;
            if (d0_raw[k] < floor) ++floor_hits;
            max_builder_schur = std::max(max_builder_schur,
                                          static_cast<double>(builder_schur));
            max_phi_helper_err = std::max(
                max_phi_helper_err,
                std::abs(static_cast<double>(phi[k] - helper)));
        }
        max_cache_d0_err = std::max(
            max_cache_d0_err,
            std::abs(static_cast<double>(cached_d0[k] - expected_d0[k])));
    }
    TORCH_CHECK((floor_hits > 0) == opt.floor_hit,
                "floor-hit mode disagrees with measured D0 raw values: hits=", floor_hits);
    TORCH_CHECK(max_cache_d0_err < 3.0e-5,
                "cache D0 differs from builder Schur/floor contract: ", max_cache_d0_err);
    TORCH_CHECK(max_phi_helper_err < 3.0e-5,
                "stored Phi diagonal differs from shared helper: ", max_phi_helper_err);

    const auto& lower = P.*access(VlWTag{});
    const auto& upper = P.*access(VuWTag{});
    TORCH_CHECK(lower.scalar_type() == torch::kFloat32 && upper.scalar_type() == torch::kFloat32,
                "W stencil coefficients must remain Float32");

    std::vector<float> d_full(vd, vd + nz_w_runtime);
    const auto a_eff = local_a_eff(raw_a, expected_dphi, d_full, cap);
    int interior_cap_hits = 0;
    for (int k = 1; k < nz_runtime; ++k) {
        if (cap > 0.0f && expected_dphi[k] > 1.0e-6f) {
            const float a_max = std::sqrt(cap*expected_dphi[k]*std::abs(d_full[k]));
            if (raw_a[k] > a_max) ++interior_cap_hits;
        }
    }
    if (!opt.uncapped) {
        TORCH_CHECK(interior_cap_hits > 0, "capped run did not exercise Dfull-relative cap");
    } else {
        TORCH_CHECK(interior_cap_hits == 0, "uncapped run unexpectedly reported cap hits");
    }

    // Remove vertical and W-theta off-diagonals in this test only. The
    // production batched Phase 2 routine then becomes a set of independent dense
    // 2x2 W/Phi solves, so its output can be compared directly with the block.
    (P.*access(VlWTag{})).zero_();
    (P.*access(VuWTag{})).zero_();
    (P.*access(WThetaUpperTag{})).zero_();
    (P.*access(WThetaLowerTag{})).zero_();
    (P.*access(ThetaWUpperTag{})).zero_();
    (P.*access(ThetaWLowerTag{})).zero_();

    auto w_rhs = torch::zeros({1, nz_w_runtime, 1}, U.options());
    auto theta_rhs = torch::zeros({1, nz_runtime, 1}, U.options());
    auto phi_rhs = torch::zeros({1, nz_w_runtime, 1}, U.options());
    std::vector<float> raw_phi_rhs(nz_w_runtime, 0.0f);
    for (int k = 1; k < nz_runtime; ++k) {
        w_rhs[0][k][0] = 1.0f + 0.25f*k;
        raw_phi_rhs[k] = 0.2f + 0.1f*k;
    }
    const auto w_rhs_before = w_rhs.clone();
    std::vector<float> phi_diag_vec(phi, phi + nz_w_runtime);

    double max_dense_err = 0.0;
    double max_double_schur_err = 0.0;
    double max_handoff_err = 0.0;
    double max_wrong_predivision_oracle_err = 0.0;
    float min_dphi_distance_from_one = std::numeric_limits<float>::max();
    float max_dphi_distance_from_one = 0.0f;
    for (int k = 1; k < nz_runtime; ++k) {
        min_dphi_distance_from_one = std::min(
            min_dphi_distance_from_one, std::abs(expected_dphi[k] - 1.0f));
        max_dphi_distance_from_one = std::max(
            max_dphi_distance_from_one, std::abs(expected_dphi[k] - 1.0f));
        phi_rhs[0][k][0] = raw_phi_rhs[k] / expected_dphi[k];
    }
    if (!opt.phi_unity) {
        TORCH_CHECK(max_dphi_distance_from_one > 5.0e-2f,
                    "D_phi is too close to one for an independent handoff test: min_delta=",
                    min_dphi_distance_from_one, " max_delta=", max_dphi_distance_from_one);
    }
    const auto phi_block_before = phi_rhs.clone();
    (P.*access(WThetaTag{}))(w_rhs, theta_rhs, nz_runtime, nz_w_runtime,
                              &phi_rhs, phi_diag_vec.data(), a_eff.data(),
                              nz_w_runtime, UnifiedPreconditioner::GradPolicy::Disabled);

    for (int k = 1; k < nz_runtime; ++k) {
        const float D0 = expected_d0[k];
        const float Dfull = d_full[k];
        const float Dphi = expected_dphi[k];
        const float A = a_eff[k];
        const float rw = w_rhs_before[0][k][0].item<float>();
        const float raw_rp = raw_phi_rhs[k];
        const float phi_block_in = phi_block_before[0][k][0].item<float>();
        const float handoff_expected = raw_rp / Dphi;
        max_handoff_err = std::max(
            max_handoff_err,
            std::abs(static_cast<double>(phi_block_in - handoff_expected)));
        const float expected_base = D0;
        const float denominator = expected_base + A*A/Dphi;
        const float expected_w = (rw - A*raw_rp/Dphi)/denominator;
        const float expected_phi = (raw_rp + A*expected_w)/Dphi;
        // Reconstruct the rejected oracle explicitly: it treats the already
        // divided phi_block as raw r_phi and divides by D_phi once more.
        const float wrong_w = (rw - A*phi_block_in/Dphi)/denominator;
        const float wrong_phi = phi_block_in + A/Dphi*wrong_w;
        const float old_base = Dfull;
        const float old_denominator = old_base + A*A/Dphi;
        const float old_w = (rw - A*raw_rp/Dphi)/old_denominator;
        const float old_phi = (raw_rp + A*old_w)/Dphi;
        const float actual_w = w_rhs[0][k][0].item<float>();
        const float actual_phi = phi_rhs[0][k][0].item<float>();
        max_dense_err = std::max({max_dense_err,
            std::abs(static_cast<double>(actual_w - expected_w)),
            std::abs(static_cast<double>(actual_phi - expected_phi))});
        max_double_schur_err = std::max({max_double_schur_err,
            std::abs(static_cast<double>(actual_w - old_w)),
            std::abs(static_cast<double>(actual_phi - old_phi))});
        max_wrong_predivision_oracle_err = std::max({max_wrong_predivision_oracle_err,
            std::abs(static_cast<double>(actual_w - wrong_w)),
            std::abs(static_cast<double>(actual_phi - wrong_phi))});
    }
    TORCH_CHECK(std::abs(w_rhs[0][0][0].item<float>()) < 1.0e-7f &&
                std::abs(phi_rhs[0][0][0].item<float>()) < 1.0e-7f,
                "Phase 2 touched uncoupled bottom boundary");
    TORCH_CHECK(max_dense_err < 4.0e-5,
                "real Phase 2 solver disagrees with extracted dense block: ", max_dense_err);
    TORCH_CHECK(max_handoff_err < 3.0e-7,
                "Phase 2 phi handoff is not raw r_phi/D_phi: ", max_handoff_err);
    if (!opt.phi_unity) {
        TORCH_CHECK(max_wrong_predivision_oracle_err > 1.0e-6,
                    "wrong pre-divided oracle was not separated by non-unity D_phi: ",
                    max_wrong_predivision_oracle_err);
    }
    TORCH_CHECK(max_double_schur_err > std::max(1.0e-7, max_dense_err + 1.0e-7),
                "opposite Schur ownership did not diverge: candidate_err=",
                max_dense_err, " opposite_err=", max_double_schur_err,
                " max_builder_schur=", max_builder_schur);
    std::cout << "PHASE2_REAL_DENSE implementation="
              << "consistent_d0"
              << " mode=" << (opt.phi_unity ? "phi_unity" : "default")
              << " cap_mode=" << (opt.uncapped ? "uncapped" : "capped")
              << " floor_mode=" << (opt.floor_hit ? "floor_hit" : "floor_inactive")
              << " generation0=" << generation0
              << " generation1=" << generation1
              << " cache_generation=" << cache_generation1
              << " floor_hits=" << floor_hits
              << " cap_hits=" << interior_cap_hits
              << " max_phi_helper_err=" << max_phi_helper_err
              << " max_cached_D0_err=" << max_cache_d0_err
              << " max_builder_schur=" << max_builder_schur
              << " max_dense_err=" << max_dense_err
              << " max_double_schur_err=" << max_double_schur_err
              << " max_handoff_err=" << max_handoff_err
              << " max_wrong_predivision_oracle_err=" << max_wrong_predivision_oracle_err
              << " dphi_delta_min=" << min_dphi_distance_from_one
              << " dphi_delta_max=" << max_dphi_distance_from_one
              << " dtype=float32\n";
    std::cout << "PHASE2_REAL_DENSE NOTE: raw Phi/W dimensional U03 model remains unresolved; "
                 "this test validates candidate algebra and implementation ownership only.\n";
    std::cout.flush();
    std::cerr.flush();
    return 0;
}
