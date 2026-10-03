#include <torch/torch.h>

#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "wrf_sdirk3_wrms_norm.h"

namespace {

void require_close(float actual, float expected, float tol, const std::string& label) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tol) {
        throw std::runtime_error(label + " expected " + std::to_string(expected) +
                                 " got " + std::to_string(actual));
    }
}

float scalar(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.detach().to(torch::kCPU).item<float>();
}

void require_true(bool value, const std::string& label) {
    if (!value) throw std::runtime_error(label);
}

}  // namespace

int main() {
    torch::manual_seed(42);

    const int64_t n = 1024;
    const wrf::sdirk3::PackedBlockSizes blocks{
        /*u=*/n, /*v=*/0, /*w=*/0, /*ph=*/0, /*t=*/0, /*mu=*/0
    };
    const wrf::sdirk3::WRMSNormConfig cfg{
        /*rtol=*/1.0e-3f,
        /*atol_u=*/1.0e-8f,
        /*atol_v=*/1.0e-8f,
        /*atol_w=*/1.0e-8f,
        /*atol_ph=*/1.0e-8f,
        /*atol_t=*/1.0e-8f,
        /*atol_mu=*/1.0e-8f,
        /*floor=*/1.0e-12f,
    };

    auto y_small = torch::ones({n}, torch::kFloat32);
    auto y_large = 1.0e6f * torch::ones({n}, torch::kFloat32);
    auto r0_small = 1.0e-3f * y_small;
    auto r1_small = 5.0e-4f * y_small;
    auto r0_large = 1.0e-3f * y_large;
    auto r1_large = 5.0e-4f * y_large;

    const float growth_small = scalar(wrf::sdirk3::wrms_growth_packed(r1_small, r0_small, y_small, blocks, cfg));
    const float growth_large = scalar(wrf::sdirk3::wrms_growth_packed(r1_large, r0_large, y_large, blocks, cfg));

    require_close(growth_small, 0.5f, 2.0e-4f, "small-scale WRMS growth");
    require_close(growth_large, 0.5f, 2.0e-4f, "large-scale WRMS growth");
    require_close(growth_large / growth_small, 1.0f, 2.0e-4f, "WRMS scale invariance ratio");

    // A stage-equation roundoff floor only resolves the construction
    // G=Y-B-aF. It does not bound nonlinear RHS evaluation error.
    const auto B=torch::tensor({1.0,-2.0,3.0},torch::kFloat64);
    const auto Y=torch::tensor({1.1,-1.9,3.2},torch::kFloat64);
    const auto K=torch::tensor({0.2,-0.4,0.1},torch::kFloat64);
    const auto F=torch::tensor({0.1,-0.3,0.05},torch::kFloat64);
    constexpr double stage_a=0.25;
    const double eps64=std::numeric_limits<double>::epsilon();
    const double gamma5=5.0*eps64/(1.0-5.0*eps64);
    const auto expected_floor=gamma5*(B.abs()+Y.abs()+(stage_a*K).abs()+
                                      (stage_a*F).abs())/stage_a;
    const auto construction_floor=wrf::sdirk3::stage_equation_residual_roundoff(
        B,Y,K,F,stage_a);
    require_true(torch::allclose(construction_floor,expected_floor,1e-14,1e-30),
                 "stage-equation roundoff floor formula");

    const wrf::sdirk3::PackedBlockSizes floor_blocks{4,0,0,0,0,0};
    const wrf::sdirk3::WRMSNormConfig floor_cfg{
        /*rtol=*/1.0e-3f, /*atol_u=*/0.0f, /*atol_v=*/0.0f, /*atol_w=*/0.0f,
        /*atol_ph=*/0.0f, /*atol_t=*/0.0f, /*atol_mu=*/0.0f, /*floor=*/0.0f};
    const auto y_floor=torch::ones({4},torch::kFloat64);
    const auto B_floor=torch::ones({4},torch::kFloat64);
    const auto Y_floor=torch::ones({4},torch::kFloat64);
    const auto K_floor=0.25*torch::ones({4},torch::kFloat64);
    const auto F_floor=0.125*torch::ones({4},torch::kFloat64);
    const auto residual_floor=wrf::sdirk3::stage_equation_residual_roundoff(
        B_floor,Y_floor,K_floor,F_floor,0.5);
    const auto residual_zero=torch::zeros_like(residual_floor);
    const auto near_zero_residual=0.5*residual_floor;
    const double near_zero_growth=scalar(wrf::sdirk3::wrms_growth_packed(
        near_zero_residual,residual_zero,y_floor,floor_blocks,floor_cfg,residual_floor));
    require_close(static_cast<float>(near_zero_growth),0.5f,2.0e-6f,
                  "near-zero baseline uses the equation-roundoff denominator");

    const auto resolved_initial=150.0*residual_floor;
    const auto resolved_now=30000.0*residual_floor;
    const double resolved_floor_norm=scalar(wrf::sdirk3::wrms_norm_packed(
        residual_floor,y_floor,floor_blocks,floor_cfg));
    const double resolved_initial_norm=scalar(wrf::sdirk3::wrms_norm_packed(
        resolved_initial,y_floor,floor_blocks,floor_cfg));
    const double resolved_growth=scalar(wrf::sdirk3::wrms_growth_packed(
        resolved_now,resolved_initial,y_floor,floor_blocks,floor_cfg,residual_floor));
    require_true(resolved_initial_norm>100.0*resolved_floor_norm,
                 "resolved initial residual exceeds 100x its construction floor");
    // The production growth cap remains 100: a resolved 200x increase must fail.
    require_true(resolved_growth>100.0,"resolved WRMS growth still exceeds the unchanged rejection cap");

    // Common equation scaling and replication must not change the floored ratio.
    constexpr double common_scale=1.0e-6;
    const auto scaled_floor=wrf::sdirk3::stage_equation_residual_roundoff(
        common_scale*B_floor,common_scale*Y_floor,common_scale*K_floor,
        common_scale*F_floor,0.5);
    require_true(torch::allclose(scaled_floor,common_scale*residual_floor,1e-14,1e-35),
                 "equation-roundoff floor scales linearly");
    const double scaled_growth=scalar(wrf::sdirk3::wrms_growth_packed(
        common_scale*resolved_now,common_scale*resolved_initial,common_scale*y_floor,
        floor_blocks,floor_cfg,scaled_floor));
    require_close(static_cast<float>(scaled_growth),static_cast<float>(resolved_growth),
                  2.0e-6f,"common field scale invariance with residual floor");
    const auto replicated_growth=scalar(wrf::sdirk3::wrms_growth_packed(
        torch::cat({resolved_now,resolved_now}),torch::cat({resolved_initial,resolved_initial}),
        torch::cat({y_floor,y_floor}),
        wrf::sdirk3::PackedBlockSizes{8,0,0,0,0,0},floor_cfg,
        torch::cat({residual_floor,residual_floor})));
    require_close(static_cast<float>(replicated_growth),static_cast<float>(resolved_growth),
                  2.0e-6f,"component-count invariance with residual floor");

    const auto field_scales=torch::tensor({100.0,100.0,0.01,0.01},torch::kFloat64);
    const auto multi_B=torch::tensor({1.0,1.0,2.0,2.0},torch::kFloat64);
    const auto multi_Y=torch::tensor({1.0,1.0,3.0,3.0},torch::kFloat64);
    const auto multi_K=torch::full({4},0.25,torch::kFloat64);
    const auto multi_F=torch::full({4},0.125,torch::kFloat64);
    const auto multi_floor=wrf::sdirk3::stage_equation_residual_roundoff(
        multi_B,multi_Y,multi_K,multi_F,0.5);
    const auto multi_initial=150.0*multi_floor;
    const auto multi_now=3000.0*multi_floor;
    const double multi_growth=scalar(wrf::sdirk3::wrms_growth_packed(
        multi_now,multi_initial,multi_Y,
        wrf::sdirk3::PackedBlockSizes{2,0,0,2,0,0},floor_cfg,multi_floor));
    const auto scaled_multi_floor=wrf::sdirk3::stage_equation_residual_roundoff(
        multi_B*field_scales,multi_Y*field_scales,multi_K*field_scales,
        multi_F*field_scales,0.5);
    const double field_scaled_growth=scalar(wrf::sdirk3::wrms_growth_packed(
        multi_now*field_scales,multi_initial*field_scales,multi_Y*field_scales,
        wrf::sdirk3::PackedBlockSizes{2,0,0,2,0,0},floor_cfg,scaled_multi_floor));
    require_close(static_cast<float>(field_scaled_growth),static_cast<float>(multi_growth),
                  2.0e-6f,"per-field scale invariance with residual floor");

    const auto expect_invalid_floor_case=[&](const torch::Tensor& bad_floor,
                                             const torch::Tensor& bad_now,
                                             const std::string& label) {
        bool rejected=false;
        try { (void)wrf::sdirk3::wrms_growth_packed(
            bad_now,resolved_initial,y_floor,floor_blocks,floor_cfg,bad_floor); }
        catch(const std::invalid_argument&) { rejected=true; }
        require_true(rejected,label);
    };
    expect_invalid_floor_case(torch::zeros({3},torch::kFloat64),resolved_now,
                              "mismatched residual-floor shape is rejected");
    expect_invalid_floor_case(residual_floor.to(torch::kFloat32),resolved_now,
                              "non-FP64 residual floor is rejected");
    expect_invalid_floor_case(torch::full({4},NAN,torch::kFloat64),resolved_now,
                              "nonfinite residual floor is rejected");
    expect_invalid_floor_case(-residual_floor,resolved_now,
                              "negative residual floor is rejected");
    expect_invalid_floor_case(residual_floor,torch::full({4},INFINITY,torch::kFloat64),
                              "nonfinite WRMS residual is rejected");
    bool invalid_y_rejected=false;
    try { (void)wrf::sdirk3::wrms_growth_packed(
        resolved_now,resolved_initial,torch::full({4},NAN,torch::kFloat64),
        floor_blocks,floor_cfg,residual_floor); }
    catch(const std::invalid_argument&) { invalid_y_rejected=true; }
    require_true(invalid_y_rejected,"nonfinite WRMS reference is rejected");
    bool invalid_helper_rejected=false;
    try { (void)wrf::sdirk3::stage_equation_residual_roundoff(
        B_floor,Y_floor,K_floor,F_floor,0.0); }
    catch(const std::invalid_argument&) { invalid_helper_rejected=true; }
    require_true(invalid_helper_rejected,"nonpositive stage diagonal is rejected");
    invalid_helper_rejected=false;
    try { (void)wrf::sdirk3::stage_equation_residual_roundoff(
        B_floor.to(torch::kFloat32),Y_floor,K_floor,F_floor,0.5); }
    catch(const std::invalid_argument&) { invalid_helper_rejected=true; }
    require_true(invalid_helper_rejected,"non-FP64 stage equation input is rejected");
    invalid_helper_rejected=false;
    try { (void)wrf::sdirk3::stage_equation_residual_roundoff(
        B_floor,Y_floor,K_floor,torch::full({4},INFINITY,torch::kFloat64),0.5); }
    catch(const std::invalid_argument&) { invalid_helper_rejected=true; }
    require_true(invalid_helper_rejected,"nonfinite stage RHS input is rejected");

    const wrf::sdirk3::PackedBlockSizes multi_blocks{
        /*u=*/4, /*v=*/4, /*w=*/4, /*ph=*/4, /*t=*/4, /*mu=*/4
    };
    const wrf::sdirk3::WRMSNormConfig block_cfg{
        /*rtol=*/1.0e-3f,
        /*atol_u=*/1.0e-7f,
        /*atol_v=*/1.0e-7f,
        /*atol_w=*/1.0e-7f,
        /*atol_ph=*/1.0e-2f,
        /*atol_t=*/1.0e-5f,
        /*atol_mu=*/1.0e-3f,
        /*floor=*/1.0e-12f,
    };
    auto y_blocks = torch::cat({
        torch::ones({4}),
        torch::ones({4}),
        torch::ones({4}),
        1.0e5f * torch::ones({4}),
        300.0f * torch::ones({4}),
        1.0e3f * torch::ones({4}),
    }).to(torch::kFloat32);
    auto r0_blocks = 1.0e-3f * y_blocks;
    auto r1_blocks = 2.5e-4f * y_blocks;
    const float block_growth = scalar(wrf::sdirk3::wrms_growth_packed(
        r1_blocks, r0_blocks, y_blocks, multi_blocks, block_cfg));
    require_close(block_growth, 0.25f, 2.0e-4f, "block-aware WRMS growth");

    // Relative RMS contract: duplicating every component cannot change the
    // ratio, and opposite signs must not cancel in the norm.
    auto k = torch::tensor({1.0f, -2.0f, 3.0f});
    auto r = torch::tensor({0.1f, -0.2f, 0.3f});
    auto k2 = torch::cat({k, k});
    auto r2 = torch::cat({r, r});
    const auto relative = [](const torch::Tensor& residual, const torch::Tensor& reference,
                             double floor = 0.0) {
        return wrf::sdirk3::relative_rms_residual(
            wrf::sdirk3::rms_norm_fp64(residual).item<double>(),
            wrf::sdirk3::rms_norm_fp64(reference).item<double>(), floor);
    };
    const double ratio = relative(r, k);
    const double ratio2 = relative(r2, k2);
    require_close(ratio, 0.1f, 2.0e-6f, "relative RMS ratio");
    require_close(ratio2, ratio, 2.0e-6f, "N replication invariance");
    const float no_cancel = scalar(wrf::sdirk3::rms_norm_fp64(torch::tensor({1.0f, -1.0f})));
    require_close(no_cancel, 1.0f, 2.0e-6f, "opposite-sign RMS");

    auto zero = torch::zeros({3});
    require_close(relative(zero, zero),
                  0.0f, 0.0f, "zero over zero relative residual");
    require_true(std::isinf(relative(r, zero)),
                 "nonzero over zero must fail closed");
    const double floored = relative(r, k, 10.0);
    require_close(floored, scalar(wrf::sdirk3::rms_norm_fp64(r)) / 10.0f,
                  2.0e-6f, "finite K_floor RMS denominator");
    require_close(relative(r, zero, 10.0), scalar(wrf::sdirk3::rms_norm_fp64(r)) / 10.0f,
                  2.0e-6f, "explicit floor defines zero-reference metric");
    require_true(std::isinf(relative(torch::full({3}, NAN), k)),
                 "nonfinite residual must fail closed");
    require_true(std::isinf(relative(r, torch::full({3}, INFINITY))),
                 "nonfinite reference must not produce false zero");
    for (double floor : {-1.0, static_cast<double>(NAN), static_cast<double>(INFINITY)}) {
        bool rejected = false;
        try { (void)relative(r, k, floor); }
        catch (const std::invalid_argument&) { rejected = true; }
        require_true(rejected, "invalid RMS floor must be rejected");
    }
    for (const auto& invalid : {torch::Tensor{}, torch::empty({0}), torch::ones({2, 2}),
                                torch::ones({3}, torch::kInt64)}) {
        bool rejected = false;
        try { (void)wrf::sdirk3::rms_norm_fp64(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require_true(rejected, "invalid RMS vector must be rejected");
    }

    std::cout << "WRMS gate metric scale-invariance tests passed" << std::endl;
    return 0;
}
