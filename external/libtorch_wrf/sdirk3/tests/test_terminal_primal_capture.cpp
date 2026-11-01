#include "../wrf_sdirk3_jvp_fwad_or_fd.h"
#include <iostream>
#include <string>

int main() {
    using wrf::sdirk3::compute_jvp_fwad_or_fd;
    auto u = torch::tensor({1.0, 2.0, -3.0}, torch::kFloat64);
    auto v = torch::tensor({2.0, -1.0, 0.5}, torch::kFloat64);
    torch::Tensor primal = torch::ones_like(u);
    bool fallback = true;
    std::string reason;
    int calls = 0;
    auto rhs = [&](const torch::Tensor& x) { ++calls; return x.square(); };
    auto tangent = compute_jvp_fwad_or_fd(rhs, u, v, 0, 0.0f, &fallback, &reason, &primal);
    if (fallback || calls != 1 || !primal.defined() ||
        !torch::allclose(primal, u.square()) || !torch::allclose(tangent, 2 * u * v)) {
        std::cerr << "native primal/tangent capture failed; calls=" << calls << "\n";
        return 1;
    }
    calls = 0;
    auto detached = [&](const torch::Tensor& x) { ++calls; return x.square().detach(); };
    primal = torch::ones_like(u); // Verify stale caller output is cleared.
    fallback = false;
    tangent = compute_jvp_fwad_or_fd(detached, u, v, 0, 0.0f, &fallback, &reason, &primal);
    if (!fallback || primal.defined() || calls <= 1 || !tangent.defined() || reason.empty()) {
        std::cerr << "fallback did not clear primal; calls=" << calls << "\n";
        return 1;
    }
    std::cout << "native primal capture and fallback clearing passed\n";
}
