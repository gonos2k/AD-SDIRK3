// Focused production-linked regression for the standalone fnm/fnp contract.
// This source intentionally uses the implementation's private state only to
// verify storage and orientation; test_fnm_fnp_rhs_contract exercises the
// actual computeUnifiedRHS consumer.
#include "../wrf_sdirk3_tile_unified.h"

#include <cmath>
#include <iostream>
#include <vector>


struct FnmDataTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(FnmDataTag);
};
template<typename Tag, typename Tag::type Member> struct DataAccessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct DataAccessor<FnmDataTag, &TileSDIRK3UnifiedSolver::fnm_>;

struct FnpDataTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(FnpDataTag);
};
template struct DataAccessor<FnpDataTag, &TileSDIRK3UnifiedSolver::fnp_>;

void check_fnm_fnp_rhs();

int main() {
    constexpr int nx = 3, ny = 2, nz = 3, nw = nz + 1;
    TileSDIRK3UnifiedSolver solver(nx, ny, nz, 1.0f, 1.0f,
                                   {1.0f}, {1.0f},
                                   /*rdnw: nonuniform eta reciprocal spacing*/
                                   {1.0f, 2.0f, 4.0f}, 0);
    solver.setStaggeredDimensions(nx + 1, ny + 1, nw);
    solver.setVerticalInterpolationCoefficients(nullptr, nullptr, 0.5f, 0.0f, 0.0f);

    TORCH_CHECK((solver.*access(FnmDataTag{})).numel() == nw && (solver.*access(FnpDataTag{})).numel() == nw,
                "derived fnm/fnp must be w-staggered");
    // dnw = [1, 1/2, 1/4]. WRF fnm(k)=dnw(k-1)/sum and
    // fnp(k)=dnw(k)/sum. Reconstruction uses (fnm[0],fnp[0])=(0,1)
    // and a zero w-top endpoint because the boundary gradient is zero.
    TORCH_CHECK(std::abs((solver.*access(FnmDataTag{}))[0].item<float>()) < 1e-7f &&
                std::abs((solver.*access(FnpDataTag{}))[0].item<float>() - 1.0f) < 1e-7f &&
                std::abs((solver.*access(FnmDataTag{}))[nw - 1].item<float>()) < 1e-7f &&
                std::abs((solver.*access(FnpDataTag{}))[nw - 1].item<float>()) < 1e-7f,
                "the reconstructed endpoint policy changed");
    TORCH_CHECK(std::abs((solver.*access(FnmDataTag{}))[1].item<float>() - 2.0f / 3.0f) < 1e-6f &&
                std::abs((solver.*access(FnpDataTag{}))[1].item<float>() - 1.0f / 3.0f) < 1e-6f &&
                std::abs((solver.*access(FnmDataTag{}))[2].item<float>() - 2.0f / 3.0f) < 1e-6f &&
                std::abs((solver.*access(FnpDataTag{}))[2].item<float>() - 1.0f / 3.0f) < 1e-6f,
                "nonuniform WRF fnm/fnp orientation is wrong");

    // The lengthless pointer contract remains safe: only nz_ entries are read,
    // while the w-top endpoint is supplied by the explicit policy above.
    const float supplied_fnm[nz] = {0.0f, 0.25f, 0.75f};
    const float supplied_fnp[nz] = {0.0f, 0.75f, 0.25f};
    solver.setVerticalInterpolationCoefficients(supplied_fnm, supplied_fnp,
                                                 0.5f, 0.0f, 0.0f);
    TORCH_CHECK((solver.*access(FnmDataTag{})).numel() == nw && (solver.*access(FnpDataTag{})).numel() == nw,
                "provided fnm/fnp must retain w-staggered storage");
    TORCH_CHECK(std::abs((solver.*access(FnmDataTag{}))[2].item<float>() - 0.75f) < 1e-7f &&
                std::abs((solver.*access(FnpDataTag{}))[2].item<float>() - 0.25f) < 1e-7f &&
                std::abs((solver.*access(FnmDataTag{}))[3].item<float>()) < 1e-7f &&
                std::abs((solver.*access(FnpDataTag{}))[3].item<float>()) < 1e-7f,
                "provided endpoint policy or safe read extent changed");

    check_fnm_fnp_rhs();
    std::cout << "FNM_FNP_GEOMETRY: PASS\n";
    return 0;
}
