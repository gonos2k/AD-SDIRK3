#include <torch/torch.h>

#include <cmath>
#include <iostream>
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

    std::cout << "WRMS gate metric scale-invariance tests passed" << std::endl;
    return 0;
}
