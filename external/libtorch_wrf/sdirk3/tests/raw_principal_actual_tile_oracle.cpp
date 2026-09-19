#include "tile_test_fixture_u03.h"

#include "wrf_sdirk3_config.h"
#include "wrf_sdirk3_jvp_fwad_or_fd.h"
#include "wrf_sdirk3_newton_solver.h"
#include "wrf_sdirk3_unified_preconditioner.h"
#include "wrf_sdirk3_unified_rhs.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace wrf::sdirk3;
using namespace wrf::sdirk3::test;
constexpr float kGamma = 0.4358665215f;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(
        const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
template<typename Tag, typename Tag::type Member>
struct Accessor { friend typename Tag::type access(Tag) { return Member; } };
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;

struct Dense {
    int rows;
    int cols;
    std::vector<double> a;
    explicit Dense(int n_) : rows(n_), cols(n_),
        a(static_cast<size_t>(n_) * n_, 0.0) {}
    Dense(int rows_, int cols_) : rows(rows_), cols(cols_),
        a(static_cast<size_t>(rows_) * cols_, 0.0) {}
    double& at(int r, int c) { return a[static_cast<size_t>(r) * cols + c]; }
    double at(int r, int c) const { return a[static_cast<size_t>(r) * cols + c]; }
};

std::vector<double> dense_solve(Dense a, std::vector<double> b) {
    TORCH_CHECK(a.rows == a.cols && static_cast<int>(b.size()) == a.rows,
                "independent dense oracle solve shape mismatch");
    for (int c = 0; c < a.rows; ++c) {
        int pivot = c;
        for (int r = c + 1; r < a.rows; ++r)
            if (std::abs(a.at(r, c)) > std::abs(a.at(pivot, c))) pivot = r;
        TORCH_CHECK(std::abs(a.at(pivot, c)) > 1.0e-14,
                    "independent dense oracle singular pivot ", c);
        if (pivot != c) {
            for (int j = c; j < a.cols; ++j) std::swap(a.at(c, j), a.at(pivot, j));
            std::swap(b[c], b[pivot]);
        }
        for (int r = c + 1; r < a.rows; ++r) {
            const double m = a.at(r, c) / a.at(c, c);
            for (int j = c + 1; j < a.cols; ++j) a.at(r, j) -= m * a.at(c, j);
            b[r] -= m * b[c];
        }
    }
    for (int r = a.rows - 1; r >= 0; --r) {
        for (int j = r + 1; j < a.cols; ++j) b[r] -= a.at(r, j) * b[j];
        b[r] /= a.at(r, r);
    }
    return b;
}

struct Block {
    Dense K;
    Dense B;
    Dense G;
    Dense S;
    explicit Block(int nz)
        : K(nz, nz + 1), B(nz, nz), G(nz + 1, nz), S(nz, nz) {}
};

struct ActualGridOracle {
    std::shared_ptr<WRFGridInfo> grid;
    int nx, ny, nz, nw;
    int64_t su, sv, sw, st, sm, total;
    std::vector<Block> blocks;

    ActualGridOracle(std::shared_ptr<WRFGridInfo> g, const torch::Tensor& state, double h)
        : grid(std::move(g)), nx(grid->nx), ny(grid->ny), nz(grid->nz), nw(grid->nz_w),
          su(static_cast<int64_t>(ny) * nz * grid->nx_u),
          sv(static_cast<int64_t>(grid->ny_v) * nz * nx),
          sw(static_cast<int64_t>(ny) * nw * nx),
          st(static_cast<int64_t>(ny) * nz * nx), sm(static_cast<int64_t>(ny) * nx),
          total(su + sv + 2 * sw + st + sm),
          blocks(static_cast<size_t>(nx) * ny, Block(nz)), block_h(h) {
        TORCH_CHECK(nw == nz + 1, "actual grid nz_w contract failed");
        TORCH_CHECK(grid->p_base.defined() && grid->p_base.dim() == 3 &&
                    grid->p_base.numel() == static_cast<int64_t>(ny) * nz * nx,
                    "actual p_base publication missing");
        TORCH_CHECK(grid->alb.defined() && grid->alb.dim() == 3 &&
                    grid->alb.numel() == static_cast<int64_t>(ny) * nz * nx,
                    "actual alb publication missing");
        TORCH_CHECK(grid->mu_base.defined() && grid->mu_base.dim() == 2 &&
                    grid->mu_base.numel() == static_cast<int64_t>(ny) * nx,
                    "actual mu_base publication missing");
        TORCH_CHECK(grid->c1h.numel() >= nz && grid->c2h.numel() >= nz &&
                    grid->c1f.numel() >= nw && grid->c2f.numel() >= nw,
                    "actual c1/c2 publication lengths failed");
        // Production GridInfo publishes mass-level rdnw/rdn with nz entries.
        TORCH_CHECK(grid->rdnw.numel() >= nz && grid->rdn.numel() >= nz,
                    "actual rdnw/rdn mass-level lengths failed");

        const auto p = grid->p_base.detach().to(torch::kCPU, torch::kFloat64).contiguous();
        const auto alb = grid->alb.detach().to(torch::kCPU, torch::kFloat64).contiguous();
        const auto mass = grid->mu_base.detach().to(torch::kCPU, torch::kFloat64).contiguous();
        const auto rdnw = grid->rdnw.detach().to(torch::kCPU, torch::kFloat64).flatten().contiguous();
        const auto rdn = grid->rdn.detach().to(torch::kCPU, torch::kFloat64).flatten().contiguous();
        TORCH_CHECK(torch::isfinite(rdnw).all().item<bool>() &&
                    torch::isfinite(rdn).all().item<bool>() &&
                    (rdnw > 0.0).all().item<bool>() &&
                    (rdn.numel() <= 1 || (rdn.slice(0, 1) > 0.0).all().item<bool>()),
                    "actual rdnw/rdn owner metrics must be finite positive on physical levels");
        const auto c1h = grid->c1h.detach().to(torch::kCPU, torch::kFloat64).flatten().contiguous();
        const auto c2h = grid->c2h.detach().to(torch::kCPU, torch::kFloat64).flatten().contiguous();
        const auto c1f = grid->c1f.detach().to(torch::kCPU, torch::kFloat64).flatten().contiguous();
        const auto c2f = grid->c2f.detach().to(torch::kCPU, torch::kFloat64).flatten().contiguous();
        const auto state64 = state.detach().to(torch::kCPU, torch::kFloat64).contiguous();
        TORCH_CHECK(state64.numel() == total && torch::isfinite(state64).all().item<bool>(),
                    "actual packed current state invalid");
        const auto* sp = state64.data_ptr<double>();
        const auto* pp = p.data_ptr<double>();
        const auto* ap = alb.data_ptr<double>();
        const auto* mp = mass.data_ptr<double>();
        const auto* rnw = rdnw.data_ptr<double>();
        const auto* rn = rdn.data_ptr<double>();
        const auto* c1hp = c1h.data_ptr<double>();
        const auto* c2hp = c2h.data_ptr<double>();
        const auto* c1fp = c1f.data_ptr<double>();
        const auto* c2fp = c2f.data_ptr<double>();
        const int64_t w0 = su + sv;
        const int64_t phi0 = w0 + sw;
        const int64_t theta0 = phi0 + sw;
        const int64_t mu0 = theta0 + st;
        const double cp_cv = static_cast<double>(grid->cp) / grid->cv;
        const double gval = grid->g;
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                auto& block = blocks[static_cast<size_t>(j) * nx + i];
                const double mu_pert = sp[mu0 + static_cast<int64_t>(j) * nx + i];
                const double M = mp[static_cast<int64_t>(j) * nx + i] + mu_pert;
                TORCH_CHECK(M > 0.0, "actual current mass is nonpositive");
                std::vector<double> q(static_cast<size_t>(nz));
                std::vector<double> stheta(static_cast<size_t>(nz));
                for (int k = 0; k < nz; ++k) {
                    const double H = c1hp[k] * M + c2hp[k];
                    const double theta_abs = grid->t0 + sp[theta0 +
                        (static_cast<int64_t>(j) * nz + k) * nx + i];
                    const double dphi = sp[phi0 +
                        (static_cast<int64_t>(j) * nw + k + 1) * nx + i] -
                        sp[phi0 + (static_cast<int64_t>(j) * nw + k) * nx + i];
                    const double alt = ap[(static_cast<int64_t>(j) * nz + k) * nx + i] -
                        (ap[(static_cast<int64_t>(j) * nz + k) * nx + i] * c1hp[k] * mu_pert -
                         std::abs(rnw[k]) * dphi) / H;
                    TORCH_CHECK(H > 0.0 && pp[(static_cast<int64_t>(j) * nz + k) * nx + i] > 0.0 &&
                                theta_abs > 0.0 && alt > 0.0,
                                "actual EOS oracle input invalid");
                    const double current_p = grid->p0 * std::pow(
                        grid->rd * theta_abs / (grid->p0 * alt), cp_cv);
                    q[k] = cp_cv * current_p / alt * std::abs(rnw[k]) / H;
                    stheta[k] = cp_cv * current_p / theta_abs;
                }
                for (int wk = 1; wk < nz; ++wk) {
                    const int r = wk - 1;
                    const double L = c1fp[wk] * M + c2fp[wk];
                    const double a = gval * std::abs(rn[wk]) / L;
                    block.K.at(r, wk - 1) = a * q[wk - 1];
                    block.K.at(r, wk) = -a * (q[wk] + q[wk - 1]);
                    block.K.at(r, wk + 1) = a * q[wk];
                    block.B.at(r, wk - 1) = a * stheta[wk - 1];
                    block.B.at(r, wk) = -a * stheta[wk];
                }
                const double atop = 2.0 * gval * std::abs(rnw[nz - 1]) /
                    (c1fp[nz] * M + c2fp[nz]);
                block.K.at(nz - 1, nz - 1) = atop * q[nz - 1];
                block.K.at(nz - 1, nz) = -atop * q[nz - 1];
                block.B.at(nz - 1, nz - 1) = atop * stheta[nz - 1];
                for (int pk = 1; pk < nw; ++pk) block.G.at(pk, pk - 1) = gval;
                for (int r = 0; r < nz; ++r) {
                    block.S.at(r, r) = 1.0;
                    for (int pk = 0; pk < nw; ++pk)
                        for (int c = 0; c < nz; ++c)
                            block.S.at(r, c) -= h * h * block.K.at(r, pk) * block.G.at(pk, c);
                }
                for (int r = 0; r < nz; ++r)
                    for (int c = 0; c < nz; ++c)
                        TORCH_CHECK(std::abs(block.S.at(r, c)) < 1.0e-10 ||
                                    std::abs(r - c) <= 1,
                                    "independent actual-grid Schur is not tridiagonal");
            }
        }
    }

    torch::Tensor apply(const torch::Tensor& residual) const {
        const auto input = residual.to(torch::kCPU, torch::kFloat32).contiguous();
        auto output = input.clone();
        const auto* in = input.data_ptr<float>();
        auto* out = output.data_ptr<float>();
        const int64_t w0 = su + sv;
        const int64_t phi0 = w0 + sw;
        const int64_t theta0 = phi0 + sw;
        for (int j = 0; j < ny; ++j) {
            for (int i = 0; i < nx; ++i) {
                const auto& block = blocks[static_cast<size_t>(j) * nx + i];
                std::vector<double> rhs(static_cast<size_t>(nz), 0.0);
                for (int r = 0; r < nz; ++r) {
                    const int wk = r + 1;
                    rhs[r] = in[w0 + (static_cast<int64_t>(j) * nw + wk) * nx + i];
                    for (int pk = 0; pk < nw; ++pk)
                        rhs[r] += block.K.at(r, pk) * in[phi0 +
                            (static_cast<int64_t>(j) * nw + pk) * nx + i] *
                            block_h;
                    for (int tk = 0; tk < nz; ++tk)
                        rhs[r] += block.B.at(r, tk) * in[theta0 +
                            (static_cast<int64_t>(j) * nz + tk) * nx + i] * block_h;
                }
                const auto w = dense_solve(block.S, rhs);
                for (int r = 0; r < nz; ++r)
                    out[w0 + (static_cast<int64_t>(j) * nw + r + 1) * nx + i] =
                        static_cast<float>(w[r]);
                for (int pk = 1; pk < nw; ++pk)
                    out[phi0 + (static_cast<int64_t>(j) * nw + pk) * nx + i] =
                        static_cast<float>(in[phi0 + (static_cast<int64_t>(j) * nw + pk) * nx + i] +
                            block_h * block.G.at(pk, pk - 1) * w[pk - 1]);
            }
        }
        return output;
    }

    double block_h = 0.0;
};

double max_abs(const torch::Tensor& a) {
    return a.detach().to(torch::kCPU, torch::kFloat64).abs().max().item<double>();
}

} // namespace

int main() {
    try {
        torch::set_num_threads(1);
        auto& cfg = g_sdirk3_config;
        cfg.debug_level = 0;
        cfg.imex_enabled = false;
        cfg.imex_split_mode = 3;
        cfg.hevi_split = false;
        cfg.mass_coordinate_mode = static_cast<int>(SDIRK3Config::MassCoordinateMode::WRFParity);
        cfg.buoyancy_use_current_w = true;
        cfg.wrf_w_damping = 0;
        cfg.implicit_wdamp = false;
        cfg.omega_w_blend = 1.0f;
        cfg.precond_acoustic_4x4 = 1;
        cfg.precond_coupled_phi_w = false;
        cfg.precond_phi_w_coupling_scale = 0.0f;
        cfg.do_curvature = false;

        TileCaseU03 tile(100000.0f, 0.0f, 80000.0f, 4.0f, 1.0f);
        for (size_t q = 0; q < tile.setup_map.size(); ++q)
            tile.setup_map[q] = 0.76f + 0.003f * static_cast<float>(q);
        for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i)
            tile.mass_map[j*nx+i] = 0.76f + 0.021f*j + 0.017f*i;
        const std::vector<float> c1f = {1.03f, 0.97f, 1.08f, 0.91f, 1.12f};
        const std::vector<float> c2f = {1400.0f, 1900.0f, 2300.0f, 1700.0f, 2600.0f};
        const std::vector<float> c1h = {0.94f, 1.06f, 0.98f, 1.11f};
        const std::vector<float> c2h = {2100.0f, 1600.0f, 2500.0f, 1800.0f};
        tile.refresh_setup_coefficients(c1f, c2f, c1h, c2h);
        for (int j = 0; j < ny; ++j) for (int i = 0; i < nx; ++i)
            tile.mu[j*nx+i] = 18.0f + 2.0f*j - 1.0f*i;
        for (int j = 0; j < ny; ++j) for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nx; ++i) {
                tile.theta[(j*nz+k)*nx+i] = 1.5f * static_cast<float>(k+1) +
                    0.02f * static_cast<float>(j-i);
                tile.ph[(j*nw+k)*nx+i] = 0.03f * static_cast<float>(k+1) +
                    0.001f * static_cast<float>(j+i);
                tile.ph[(j*nw+k+1)*nx+i] = tile.ph[(j*nw+k)*nx+i] + 0.02f;
            }
        const auto U = tile.state();
        auto grid = tile.solver.getGridInfo();
        const float h = 0.1f * kGamma;
        ActualGridOracle oracle(grid, U, h);
        oracle.block_h = h;
        auto physics = std::make_shared<PhysicsConfig>();
        UnifiedPreconditioner candidate(grid, physics, 1.0f, h);
        candidate.bind_raw_principal_state_or_throw(U, 2, "actual_tile_stage");
        candidate.update_time_coefficients(1.0f, h);

        torch::manual_seed(20260906);
        auto probe = torch::randn({U.numel()}, torch::TensorOptions().dtype(torch::kFloat32));
        const auto want = oracle.apply(probe);
        const auto got = candidate.apply(probe);
        const double apply_err = max_abs(got - want);
        const double apply_ref = std::max(max_abs(want), 1.0e-20);
        TORCH_CHECK(apply_err / apply_ref < 2.0e-5,
                    "actual-grid candidate/oracle apply mismatch: abs=", apply_err,
                    " rel=", apply_err / apply_ref);
        auto transpose_x = torch::randn_like(probe);
        auto transpose_y = torch::randn_like(probe);
        const auto transpose_forward = candidate.apply(transpose_x);
        const auto transpose_result = candidate.apply_inverse_transpose(transpose_y);
        const double transpose_lhs = (transpose_forward * transpose_y).sum().item<double>();
        const double transpose_rhs = (transpose_x * transpose_result).sum().item<double>();
        const double transpose_dot_abs = std::abs(transpose_lhs - transpose_rhs);
        TORCH_CHECK(std::isfinite(transpose_lhs) && std::isfinite(transpose_rhs) &&
                    transpose_dot_abs <= 3.0e-5 *
                        std::max({1.0, std::abs(transpose_lhs), std::abs(transpose_rhs)}),
                    "actual-grid forward/transpose dot identity failed: abs=", transpose_dot_abs);
        const int64_t active_w0 = su + sv;
        const int64_t active_phi0 = active_w0 + sw;
        TORCH_CHECK(torch::equal(transpose_result.index({active_w0}),
                                 transpose_y.index({active_w0})),
                    "transpose changed the restricted bottom W row");
        TORCH_CHECK(max_abs(transpose_result.index({active_phi0}) -
                            transpose_y.index({active_phi0})) > 1.0e-8,
                    "transpose dropped the bottom Phi K^T contribution");

        const auto project = [&](const torch::Tensor& v) {
            auto out = v.clone();
            out.slice(0, su + sv, su + sv + sw).view({ny, nw, nx}).select(1, 0).zero_();
            return out;
        };
        const auto rhs = [&](const torch::Tensor& x) {
            return (tile.solver.*access(RhsTag{}))(x, RhsMode::ImplicitOnly);
        };
        const auto jvp = [&](const torch::Tensor& v) {
            bool used_fd = false;
            std::string reason;
            auto out = compute_jvp_fwad_or_fd(rhs, U, v, 0, 0.0f, &used_fd, &reason);
            TORCH_CHECK(!used_fd, "actual-grid FWAD fell back to FD: ", reason);
            return out;
        };
        const auto A = [&](const torch::Tensor& v) {
            return project(project(v) - h * jvp(project(v)));
        };
        const auto b = project(torch::randn_like(U) * 1.0e-3f);
        const auto x0 = torch::zeros_like(U);
        const auto M_candidate = [&](const torch::Tensor& v) { return project(candidate.apply(project(v))); };
        const auto M_oracle = [&](const torch::Tensor& v) { return project(oracle.apply(project(v))); };
        const auto candidate_result = krylov_methods::solve_fgmres(
            A, b, x0, 0, 0.0f, 8, 1.0e-5f, 1, M_candidate);
        const auto oracle_result = krylov_methods::solve_fgmres(
            A, b, x0, 0, 0.0f, 8, 1.0e-5f, 1, M_oracle);
        const double solution_map_err = max_abs(candidate_result.x - oracle_result.x);
        const double candidate_true_rel = max_abs(b - A(candidate_result.x)) /
            std::max(max_abs(b), 1.0e-20);
        const double oracle_true_rel = max_abs(b - A(oracle_result.x)) /
            std::max(max_abs(b), 1.0e-20);
        std::cout << std::setprecision(17)
                  << "ACTUAL_TILE_RAW_PRINCIPAL_ORACLE columns=" << nx*ny
                  << " nz=" << nz << " rdnw_len=" << grid->rdnw.numel()
                  << " rdn_len=" << grid->rdn.numel()
                  << " apply_abs=" << apply_err
                  << " apply_rel=" << apply_err / apply_ref
                  << " transpose_dot_abs=" << transpose_dot_abs
                  << " fwad_no_fd=1"
                  << " same_A_P_b=1"
                  << " candidate_oracle_solution_abs=" << solution_map_err
                  << " candidate_true_rel=" << candidate_true_rel
                  << " oracle_true_rel=" << oracle_true_rel << '\n';
        TORCH_CHECK(torch::isfinite(candidate_result.x).all().item<bool>() &&
                    torch::isfinite(oracle_result.x).all().item<bool>(),
                    "actual-grid FGMRES solution nonfinite");
        TORCH_CHECK(solution_map_err < 5.0e-5,
                    "actual-grid candidate/oracle FGMRES mismatch: ", solution_map_err);
        std::cout << "ACTUAL_TILE_RAW_PRINCIPAL_RESULT PASS\n";
        return 0;
    } catch (const c10::Error& e) {
        std::cerr << "ACTUAL_TILE_RAW_PRINCIPAL_RESULT FAIL c10=" << e.what() << '\n';
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "ACTUAL_TILE_RAW_PRINCIPAL_RESULT FAIL exception=" << e.what() << '\n';
        return 3;
    }
}
