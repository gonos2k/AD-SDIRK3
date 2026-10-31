#include "../wrf_sdirk3_jvp_autograd.h"
#include <torch/torch.h>
#include <c10/core/InferenceMode.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>

using wrf::sdirk3::PreparedJvp;

static void check_close(const torch::Tensor& a, const torch::Tensor& b, double tol) {
    torch::NoGradGuard guard;
    TORCH_CHECK(torch::allclose(a, b, tol, tol), "tensor mismatch: ", a, " vs ", b);
}

static void run(torch::ScalarType dtype) {
    auto options = torch::TensorOptions().dtype(dtype);
    const double tol = dtype == torch::kFloat64 ? 1e-12 : 1e-5;
    auto A = torch::tensor({{1., 2., 3.}, {4., 5., 7.}}, options);
    auto x = torch::tensor({1., 2., 3.}, options);
    auto v = torch::tensor({3., 1., 2.}, options);
    auto u = torch::tensor({-1., 4., 2.}, options);
    int calls = 0;
    auto F = [&](const torch::Tensor& z) { ++calls; return A.matmul(z); };
    {
        torch::NoGradGuard no_grad;
        PreparedJvp prepared(F, x);
        TORCH_CHECK(!torch::GradMode::is_enabled());
        check_close(prepared.primal(), A.matmul(x), tol);
        check_close(prepared.apply(v), A.matmul(v), tol);
        check_close(prepared.apply(u), A.matmul(u), tol);
        TORCH_CHECK(!torch::GradMode::is_enabled() && calls == 1);
    }
    auto nonlinear = [](const torch::Tensor& z) { return z.square(); };
    PreparedJvp first(nonlinear, x), refreshed(nonlinear, x + 1);
    check_close(first.apply(v), 2*x*v, tol);
    check_close(refreshed.apply(v), 2*(x+1)*v, tol);
    auto constant = [&](const torch::Tensor&) { return torch::ones({2}, options); };
    PreparedJvp zero(constant, x);
    check_close(zero.apply(v), torch::zeros({2}, options), tol);
    auto unrelated = torch::ones({2}, options).set_requires_grad(true);
    PreparedJvp disconnected([&](const torch::Tensor&) { return unrelated.square(); }, x);
    check_close(disconnected.apply(v), torch::zeros({2}, options), tol);

    auto z = torch::tensor({2., 3., 4., 5.}, options);
    auto dz = torch::tensor({0.2, -0.1, 0.3, -0.2}, options);
    auto flux = [](const torch::Tensor& t) {
        auto m=t.slice(0,0,2), q=t.slice(0,2,4);
        auto mdot=torch::stack({q[1]-q[0],2*q[0]-q[1]});
        auto fdot=torch::stack({m[1]*q[1]-m[0]*q[0],m[0]*q[1]+m[1]*q[0]});
        return (fdot-q*mdot)/m;
    };
    auto m=z.slice(0,0,2), q=z.slice(0,2,4);
    auto dm=dz.slice(0,0,2), dq=dz.slice(0,2,4);
    auto mdot=torch::stack({q[1]-q[0],2*q[0]-q[1]});
    auto dmdot=torch::stack({dq[1]-dq[0],2*dq[0]-dq[1]});
    auto fdot=torch::stack({m[1]*q[1]-m[0]*q[0],m[0]*q[1]+m[1]*q[0]});
    auto dfdot=torch::stack({dm[1]*q[1]+m[1]*dq[1]-dm[0]*q[0]-m[0]*dq[0],
                             dm[0]*q[1]+m[0]*dq[1]+dm[1]*q[0]+m[1]*dq[0]});
    auto analytic=(dfdot-dq*mdot-q*dmdot)/m-(fdot-q*mdot)*dm/(m*m);
    PreparedJvp mass(flux,z);
    check_close(mass.apply(dz), analytic, tol);

    bool bad_shape = false;
    try { first.apply(torch::zeros({4}, options)); } catch (const c10::Error&) { bad_shape = true; }
    TORCH_CHECK(bad_shape);
    bool bad_dtype = false;
    try { first.apply(v.to(dtype == torch::kFloat64 ? torch::kFloat32 : torch::kFloat64)); }
    catch (const c10::Error&) { bad_dtype = true; }
    TORCH_CHECK(bad_dtype);
    {
        c10::InferenceMode inference;
        bool rejected = false;
        try { PreparedJvp fail(F, x); } catch (const c10::Error&) { rejected = true; }
        TORCH_CHECK(rejected);
        rejected = false;
        try { first.apply(v); } catch (const c10::Error&) { rejected = true; }
        TORCH_CHECK(rejected);
    }
    std::weak_ptr<torch::autograd::Node> node;
    {
        auto tracked = [&](const torch::Tensor& a) { auto y=a.square(); node=y.grad_fn(); return y; };
        PreparedJvp temporary(tracked, x);
    }
    TORCH_CHECK(node.expired());
    {
        torch::NoGradGuard no_grad;
        bool thrown = false;
        try { PreparedJvp fail([&](const torch::Tensor& a) -> torch::Tensor { auto y=a.square(); node=y.grad_fn(); throw std::runtime_error("expected"); }, x); }
        catch (const std::runtime_error&) { thrown = true; }
        TORCH_CHECK(thrown && !torch::GradMode::is_enabled() && node.expired());
    }
}

int main() {
    static_assert(!std::is_copy_constructible<PreparedJvp>::value, "must be move-only");
    static_assert(std::is_move_constructible<PreparedJvp>::value, "must be movable");
    try { run(torch::kFloat64); run(torch::kFloat32); std::cout << "prepared JVP tests passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
