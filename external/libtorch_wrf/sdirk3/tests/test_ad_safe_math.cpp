// Focused contract test for ADSafeMath's zero and epsilon policy.

#include "../wrf_sdirk3_ad_safe_helpers.h"

#include <torch/torch.h>

#include <cmath>
#include <iostream>
#include <limits>

namespace {

int failures = 0;

void check(bool ok, const char* message) {
    if (!ok) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool throws_bad_epsilon(const torch::Tensor& x, float epsilon) {
    try {
        (void)wrf::sdirk3::ADSafeMath::safe_reciprocal(x, epsilon);
    } catch (const c10::Error&) {
        return true;
    }
    return false;
}

}  // namespace

int main() {
    constexpr float epsilon = 1.0e-3f;
    auto x = torch::tensor({0.0f, 0.5e-3f, -0.5e-3f, 2.0e-3f, -2.0e-3f});
    auto reciprocal = wrf::sdirk3::ADSafeMath::safe_reciprocal(x, epsilon);
    auto expected = torch::tensor({1000.0f, 1000.0f, -1000.0f, 500.0f, -500.0f});
    check(torch::allclose(reciprocal, expected, 1.0e-5, 1.0e-5),
          "reciprocal guards zero and preserves the sign of negative near-zero values");

    auto numerator = torch::ones_like(x);
    auto quotient = wrf::sdirk3::ADSafeMath::safe_divide(numerator, x, epsilon);
    check(torch::allclose(quotient, expected, 1.0e-5, 1.0e-5),
          "divide uses the same signed near-zero denominator guard");

    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    check(throws_bad_epsilon(x, 0.0f), "zero epsilon is rejected");
    check(throws_bad_epsilon(x, -epsilon), "negative epsilon is rejected");
    check(throws_bad_epsilon(x, nan), "NaN epsilon is rejected");
    check(throws_bad_epsilon(x, inf), "infinite epsilon is rejected");

    std::cout << (failures == 0 ? "AD_SAFE_MATH: PASS\n" : "AD_SAFE_MATH: FAIL\n");
    return failures == 0 ? 0 : 1;
}
