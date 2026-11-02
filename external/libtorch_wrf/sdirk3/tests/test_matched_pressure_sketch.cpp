#include "../wrf_sdirk3_matched_pressure_probe.h"
#include <limits>
#include <iostream>

using wrf::sdirk3::matched_pressure::ActionSketch;

int main() {
    const auto o = torch::TensorOptions().dtype(torch::kFloat64);
    const auto A = torch::tensor({{2., 3., -1.}, {1., -4., 2.}, {0., 5., 1.}}, o);
    const auto apply = [&](const torch::Tensor& x) { return torch::matmul(A, x); };
    const auto x0 = torch::tensor({1., 2., 3.}, o);
    const auto x1 = torch::tensor({-2., 1., 4.}, o);
    const auto x2 = torch::tensor({3., -1., 2.}, o);
    ActionSketch good, changed;
    for (const auto& x : {x0, x1, x2}) {
        good.add(x, apply(x));
        changed.add(x, apply(x) + (changed.count() == 1 ? x : torch::zeros_like(x)));
    }
    TORCH_CHECK(good.usable() && good.count() == 3, "missing samples");
    for (int i = 0; i < 2; ++i) {
        TORCH_CHECK(torch::allclose(apply(good.input(i)), good.output(i), 1e-12, 1e-12),
                    "linear sketch mismatch");
        TORCH_CHECK(!torch::allclose(apply(changed.input(i)), changed.output(i), 1e-12, 1e-12),
                    "changed operator not detected");
    }
    ActionSketch invalid;
    invalid.add(x0, apply(x0));
    TORCH_CHECK(!invalid.usable(), "one sample is insufficient");
    bool rejected = false;
    try {
        invalid.add(torch::full_like(x0, std::numeric_limits<double>::infinity()), x0);
    } catch (const c10::Error&) { rejected = true; }
    TORCH_CHECK(rejected, "nonfinite sample accepted");
    std::cout << "matched pressure sketch passed\n";
}
