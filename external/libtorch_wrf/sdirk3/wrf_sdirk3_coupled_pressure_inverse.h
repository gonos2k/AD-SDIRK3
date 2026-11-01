#pragma once

#include <torch/torch.h>
#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <new>

namespace wrf::sdirk3 {

// P0solve and C are fixed, linear physical-coordinate operators. C returns a
// projected V-only packed tensor. The caller owns their context and lifetime.
struct CoupledPressureResult {
    torch::Tensor x;
    torch::Tensor base;
    bool converged = false;
    int arnoldi = 0;
    int inverse_calls = 0;
    int action_calls = 0;
    double true_relative_residual = std::numeric_limits<double>::infinity();
};

inline CoupledPressureResult solve_coupled_pressure(
    const torch::Tensor& r, double a,
    const std::function<torch::Tensor(const torch::Tensor&)>& P0solve,
    const std::function<torch::Tensor(const torch::Tensor&)>& C) {
    constexpr int budget = 4;
    constexpr double tolerance = 1e-2;
    CoupledPressureResult result;
    torch::NoGradGuard no_grad;
    const auto valid = [&](const torch::Tensor& t) {
        return t.defined() && t.device().is_cpu() && t.scalar_type() == torch::kFloat64 &&
               t.sizes() == r.sizes() && torch::isfinite(t).all().item<bool>();
    };
    if (!r.defined() || r.dim() != 1 || !r.device().is_cpu() ||
        r.scalar_type() != torch::kFloat64 || !std::isfinite(a) ||
        !P0solve || !C || !torch::isfinite(r).all().item<bool>()) return result;
    try {
        const auto inverse = [&](const torch::Tensor& v) {
            ++result.inverse_calls;
            return P0solve(v);
        };
        const auto action = [&](const torch::Tensor& v) {
            ++result.action_calls;
            return C(v);
        };
        const auto z0 = inverse(r);
        if (!valid(z0)) return result;
        result.base = z0;
        const auto g = action(z0);
        if (!valid(g)) return result;
        const double beta = g.norm().item<double>();
        if (!std::isfinite(beta)) return result;
        if (beta == 0.0) {
            result.x = z0;
            result.converged = true;
            result.true_relative_residual = 0.0;
            return result;
        }
        std::array<torch::Tensor, budget+1> v;
        std::array<torch::Tensor, budget> xbasis;
        std::array<std::array<double, budget>, budget+1> h{};
        std::array<double, budget> cs{}, sn{};
        std::array<double, budget+1> rhs{};
        v[0] = g / beta;
        rhs[0] = beta;
        for (int j=0; j<budget; ++j) {
            xbasis[j] = inverse(v[j]);
            if (!valid(xbasis[j])) return result;
            auto cv = action(xbasis[j]);
            if (!valid(cv)) return result;
            auto w = v[j] - a*cv;
            if (!valid(w)) return result;
            const double initial_norm = w.norm().item<double>();
            if (!std::isfinite(initial_norm)) return result;
            for (int pass=0; pass<2; ++pass) {
                for (int i=0; i<=j; ++i) {
                    const double dot = torch::dot(v[i], w).item<double>();
                    if (!std::isfinite(dot)) return result;
                    h[i][j] += dot;
                    w = w - dot*v[i];
                }
            }
            h[j+1][j] = w.norm().item<double>();
            if (!std::isfinite(h[j+1][j])) return result;
            const bool breakdown = h[j+1][j] <=
                32.0*std::numeric_limits<double>::epsilon()*initial_norm;
            if (!breakdown && j+1<budget) v[j+1] = w/h[j+1][j];
            for (int i=0; i<j; ++i) {
                const double top = cs[i]*h[i][j] + sn[i]*h[i+1][j];
                h[i+1][j] = -sn[i]*h[i][j] + cs[i]*h[i+1][j];
                h[i][j] = top;
            }
            const double diag = std::hypot(h[j][j], h[j+1][j]);
            if (!std::isfinite(diag) || diag == 0.0) return result;
            cs[j] = h[j][j]/diag;
            sn[j] = h[j+1][j]/diag;
            h[j][j] = diag;
            rhs[j+1] = -sn[j]*rhs[j];
            rhs[j] *= cs[j];
            result.arnoldi = j+1;
            if (std::abs(rhs[j+1])/beta > tolerance && !breakdown && j+1<budget) continue;
            std::array<double, budget> coeff{};
            for (int i=j; i>=0; --i) {
                double value = rhs[i];
                for (int k=i+1; k<=j; ++k) value -= h[i][k]*coeff[k];
                if (!std::isfinite(value) || h[i][i] == 0.0) return result;
                coeff[i] = value/h[i][i];
                if (!std::isfinite(coeff[i])) return result;
            }
            auto y = torch::zeros_like(r);
            auto correction = torch::zeros_like(r);
            for (int i=0; i<=j; ++i) {
                y = y + coeff[i]*v[i];
                correction = correction + coeff[i]*xbasis[i];
            }
            const auto cc = action(correction);
            if (!valid(cc)) return result;
            const auto residual = g - (y-a*cc);
            if (!valid(residual)) return result;
            result.true_relative_residual = residual.norm().item<double>()/beta;
            if (!std::isfinite(result.true_relative_residual)) return result;
            if (result.true_relative_residual <= tolerance) {
                auto x = z0+a*correction;
                if (!valid(x)) return result;
                result.x = x;
                result.converged = true;
                return result;
            }
            if (breakdown) return result;
        }
    } catch (const c10::OutOfMemoryError&) {
        throw;
    } catch (const std::bad_alloc&) {
        throw;
    } catch (const std::exception&) {
        // A failed inner solve is never returned as a usable preconditioner.
    }
    return result;
}

} // namespace wrf::sdirk3
