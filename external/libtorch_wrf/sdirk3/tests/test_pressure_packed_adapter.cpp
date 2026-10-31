#include "../wrf_sdirk3_pressure_packed_adapter.h"
#include "tile_test_fixture.h"
#include <cmath>
#include <iostream>

using namespace wrf::sdirk3;
namespace {
using namespace wrf::sdirk3::test;
constexpr int n = nx - 1, m = ny - 1, levels = nz;
const auto opts = torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU);

torch::Tensor independent(const ReferencePressureFields& f) {
    return torch::cat({f.u.flatten(), f.v.flatten(), f.w.flatten(), f.phi.flatten(),
                       f.theta.flatten(), f.mu.flatten()});
}
ReferencePressureProfiles profiles() {
    return {{0.7,1.1,0.8,1.2}, {2.0,2.5,2.2,2.7}, {0.3,0.4,0.5,0.6},
            {0.5,0.8,0.6,0.7}, {0.9,0.6,0.8,0.7}, {1.2,1.4,1.1,1.5},
            {2.0,3.0,2.5,3.5}, {0.1,-0.05,0.04,0.08}, {-0.4,-0.6,-0.5,-0.7}};
}
void check_close(const torch::Tensor& a, const torch::Tensor& b, double tol, const char* what) {
    TORCH_CHECK(torch::allclose(a, b, tol, tol), what,
                "; max error=", (a - b).abs().max().item<double>());
}
}
int main() {
    try {
        torch::NoGradGuard no_grad;
        torch::set_num_threads(1);
        torch::manual_seed(189);
        g_sdirk3_config = SDIRK3Config{};
        TileCase tile;
        tile.solver.setWRFIndices(1, nx, 1, ny, 1, nz,
                                  1, nx, 1, ny, 1, nz,
                                  1, nu, 1, nv, 1, nw);
        TORCH_CHECK(tile.packedPeriodicLayout(), "fixture is not native packed periodic");
        PressurePackedAdapter adapter(n, m, levels);
        TORCH_CHECK(adapter.size() == total, "packed layout mismatch");
        const auto layout = StateLayout::from_grid_dims(nx, ny, nz, nu, nv, nw);
        auto bottom = [&](const torch::Tensor& x) {
            auto out = torch::zeros_like(x);
            for (int block : {2, 3}) {
                const auto& b = layout.blocks[block];
                auto src = x.slice(0, b.start, b.start + b.size).view({ny, nw, nx});
                auto dst = out.slice(0, b.start, b.start + b.size).view({ny, nw, nx});
                dst.select(1, 0).copy_(src.select(1, 0));
            }
            return out;
        };
        auto x = torch::randn({total}, opts);
        auto q = adapter.project(x);
        check_close(adapter.project(q), q, 0, "Qhat is not idempotent");
        const auto native_q = tile.projectControlState(x);
        check_close(native_q - q, tile.projectControlState(bottom(x)), 0,
                    "Qhat differs from native Q beyond bottom projection");
        TORCH_CHECK((native_q - q).norm().item<double>() > 0,
                    "bottom projection negative control missing");
        check_close(adapter.project(x - bottom(x)), tile.projectControlState(x - bottom(x)), 0,
                    "native Q and Qhat differ on bottom-zero inputs");

        // Every packed unit tests seams, wall ghosts, physical top and the
        // retained complement. Native Q is the independent implementation.
        auto e = torch::zeros({total}, opts);
        for (int64_t i = 0; i < total; ++i) {
            e[i] = 1;
            auto qe = adapter.project(e);
            check_close(adapter.project(qe), qe, 0, "unit Qhat idempotence");
            check_close(tile.projectControlState(e) - qe, tile.projectControlState(bottom(e)), 0,
                        "unit native Q mismatch");
            auto complement = e - qe;
            check_close(adapter.project(complement), torch::zeros_like(e), 0,
                        "unit complement not annihilated");
            e[i] = 0;
        }
        // R E = I over every independent physical degree, including the top.
        auto fields = adapter.extract(x);
        auto shape = independent(fields);
        auto z = torch::zeros_like(shape);
        const int64_t independent_size = z.numel();
        // To make each basis, first use an admissible extension and set its
        // representative via a packed basis. The distinct representatives are
        // covered explicitly by the R E comparison below and packed unit loop.
        check_close(independent(adapter.extract(adapter.extend(fields))), shape, 0,
                    "R E differs from identity on independent random fields");
        for (int64_t i = 0; i < total; ++i) {
            e[i] = 1;
            auto f = adapter.extract(e);
            check_close(independent(adapter.extract(adapter.extend(f))), independent(f), 0,
                        "R E differs from identity on a physical unit");
            e[i] = 0;
        }
        TORCH_CHECK(independent_size == 4*m*levels*n + (m-1)*levels*n + m*n,
                    "wrong independent physical dimension");

        // u[i] is the west face: mass divergence is u[i+1]-u[i].
        // Compare wrapped core stencil with native packed face differences at
        // both seam impulses; native nx includes the duplicate endpoint.
        for (int i : {0, n - 1}) {
            auto impulse = torch::zeros({total}, opts);
            const auto& ub = layout.blocks[0];
            auto u = impulse.slice(0, ub.start, ub.start + ub.size).view({ny, nz, nu});
            u[0][0][i] = 1;
            auto ext = adapter.project(impulse);
            auto uf = adapter.extract(ext).u;
            auto native_u = ext.slice(0, ub.start, ub.start + ub.size).view({ny, nz, nu});
            auto native_div = native_u.slice(0, 0, m).slice(2, 1, n+1) -
                              native_u.slice(0, 0, m).slice(2, 0, n);
            auto wrapped = torch::roll(uf, {-1}, {2}) - uf;
            check_close(native_div, wrapped, 0, "west-face periodic seam stencil mismatch");
        }
        // Build the entire full-packed matrix independently by applying each
        // unit vector.  Include nonuniform physical Phi0 and arbitrary aliases:
        // Phi0 is identity in the complement but forces the P0 core.
        {
            constexpr int sn = 3, sm = 3, sk = 2;
            PressurePackedAdapter small(sn, sm, sk);
            ReferencePressureProfiles sp{{0.7,1.1},{2.0,2.5},{0.3,0.4},{0.5,0.8},
                {0.9,0.6},{1.2,1.4},{2.0,3.0},{0.1,-0.05},{-0.4,-0.6}};
            ReferencePressureModel small_model(sn, sm, sp, 0.7, 2.0, 3.0, 4.0);
            const auto sl = StateLayout::from_grid_dims(sn+1, sm+1, sk, sn+2, sm+2, sk+1);
            auto rhs = torch::randn({small.size()}, opts);
            const auto& ph = sl.blocks[3];
            auto phi = rhs.slice(0, ph.start, ph.start + ph.size).view({sm+1, sk+1, sn+1});
            phi.select(1, 0).slice(0, 0, sm).slice(1, 0, sn).copy_(
                torch::arange(sm*sn, opts).view({sm,sn}) * 0.17 - 0.5);
            auto phi0_only = torch::zeros_like(rhs);
            auto p0 = phi0_only.slice(0, ph.start, ph.start + ph.size).view({sm+1, sk+1, sn+1});
            p0.select(1, 0).copy_(phi.select(1, 0));
            auto p0_response = small.solve(small_model, phi0_only);
            TORCH_CHECK(small.project(p0_response).norm().item<double>() > 1e-6,
                        "nonconstant Phi0 did not couple into the core");
            check_close(p0_response - small.project(p0_response),
                        phi0_only - small.project(phi0_only), 2e-11,
                        "Phi0 changed the identity complement");
            std::vector<torch::Tensor> columns;
            auto unit = torch::zeros_like(rhs);
            for (int64_t i = 0; i < small.size(); ++i) {
                unit[i] = 1;
                columns.push_back(small.apply(small_model, unit));
                unit[i] = 0;
            }
            auto dense = torch::stack(columns, 1);
            auto expected = torch::linalg_solve(dense, rhs.unsqueeze(1)).squeeze(1);
            auto solved = small.solve(small_model, rhs);
            check_close(solved, expected, 2e-11, "Phi0 shifted packed inverse differs from dense");
            check_close(small.apply(small_model, solved), rhs, 2e-11,
                        "Phi0 shifted full packed residual");
            check_close(solved - small.project(solved), rhs - small.project(rhs), 2e-11,
                        "non-Q complement changed under Phi0 shift");
        }
        ReferencePressureModel model(n, m, profiles(), 0.7, 2.0, 3.0, 4.0);
        auto b = torch::randn({total}, opts);
        auto solution = adapter.solve(model, b);
        check_close(adapter.apply(model, solution), b, 2e-11, "packed P0 inverse mismatch");
        check_close(solution - adapter.project(solution), b - adapter.project(b), 2e-11,
                    "identity complement was not preserved");
        // General, spatially varying S does not commute with Qhat.
        auto S = 0.5 + torch::arange(total, opts) / total;
        auto bs = torch::randn({total}, opts);
        TORCH_CHECK((adapter.project(S * bs) - S * adapter.project(bs)).norm().item<double>() > 1e-6,
                    "noncommuting scale negative control failed");
        auto xs = adapter.solve_scaled(model, bs, S);
        check_close(adapter.apply(model, S * xs) / S, bs, 2e-11,
                    "S inverse M inverse S wrapper mismatch");
        std::cout << "PRESSURE_PACKED_ADAPTER native_Q_units=" << total
                  << " independent=" << independent_size << " physical_nx=" << n << '\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
