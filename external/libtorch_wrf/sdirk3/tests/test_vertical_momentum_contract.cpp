// Production-linked regression for ordinary WRF Omega vertical momentum advection.
#include "tile_test_fixture.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
struct RuTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(RuTag);
};
struct RvTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(RvTag);
};
struct RwTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(RwTag);
};
struct CoefficientsTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*,
                                                   const float*, const float*);
    friend type access(CoefficientsTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<RuTag, &TileSDIRK3UnifiedSolver::ru_adv_z_work_>;
template struct Accessor<RvTag, &TileSDIRK3UnifiedSolver::rv_adv_z_work_>;
template struct Accessor<RwTag, &TileSDIRK3UnifiedSolver::rw_adv_z_work_>;
template struct Accessor<CoefficientsTag,
    &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;

constexpr double pi = 3.14159265358979323846;
constexpr double mu0 = 80000.0;
constexpr double dnw = -0.25;
constexpr double rdnw = 4.0;

struct CaseSpec {
    bool packed;
    int m;
    int n;
    std::vector<float> c2;
};

size_t u_index(int j, int k, int i) { return (j*nz + k)*nu + i; }
size_t v_index(int j, int k, int i) { return (j*nz + k)*nx + i; }
size_t w_index(int j, int k, int i) { return (j*nw + k)*nx + i; }
size_t level_index(int j, int k, int i, int levels, int cols) {
    return (j*levels + k)*cols + i;
}
size_t u_core_index(int j, int k, int i, int cols) {
    return level_index(j,k,i,nz,cols);
}
size_t w_core_index(int j, int k, int i, int cols) {
    return level_index(j,k,i,nw,cols);
}

double sign_wrf(double x) { return x > 0.0 ? 1.0 : x < 0.0 ? -1.0 : 0.0; }

template<typename Q, typename O>
std::vector<double> vertical_flux3(int rows, int cols, Q q, O om) {
    std::vector<double> flux(static_cast<size_t>(rows*nw*cols), 0.0);
    for (int j = 0; j < rows; ++j) {
        for (int kf = 1; kf < nz; ++kf) {
            for (int i = 0; i < cols; ++i) {
                const double omega = om(j, kf, i);
                double qface;
                if (kf == 1 || kf == nz-1) {
                    qface = 0.5 * (q(j, kf, i) + q(j, kf-1, i));
                } else {
                    const double qim2 = q(j, kf-2, i);
                    const double qim1 = q(j, kf-1, i);
                    const double qi = q(j, kf, i);
                    const double qip1 = q(j, kf+1, i);
                    const double centered =
                        (7.0*(qi + qim1) - (qip1 + qim2))/12.0;
                    const double upwind = sign_wrf(-omega) *
                        ((qip1-qim2) - 3.0*(qi-qim1))/12.0;
                    qface = centered + upwind;
                }
                flux[level_index(j,kf,i,nw,cols)] = omega*qface;
            }
        }
    }
    std::vector<double> tendency(static_cast<size_t>(rows*nz*cols), 0.0);
    for (int j = 0; j < rows; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < cols; ++i)
                tendency[u_core_index(j,k,i,cols)] = rdnw *
                    (flux[level_index(j,k+1,i,nw,cols)] -
                     flux[level_index(j,k,i,nw,cols)]);
    return tendency;
}

template<typename Q, typename O>
std::vector<double> vertical_w_flux3(int rows, int cols, Q q, O om) {
    std::vector<double> flux(static_cast<size_t>(rows*nz*cols), 0.0);
    for (int j = 0; j < rows; ++j) {
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i < cols; ++i) {
                const double vel = 0.5 * (om(j,k,i) + om(j,k+1,i));
                double qface;
                if (k == 0 || k == nz-1) {
                    qface = 0.5 * (q(j,k,i) + q(j,k+1,i));
                } else {
                    const double qim2 = q(j,k-1,i);
                    const double qim1 = q(j,k,i);
                    const double qi = q(j,k+1,i);
                    const double qip1 = q(j,k+2,i);
                    const double centered =
                        (7.0*(qi + qim1) - (qip1 + qim2))/12.0;
                    const double upwind = sign_wrf(-vel) *
                        ((qip1-qim2) - 3.0*(qi-qim1))/12.0;
                    qface = centered + upwind;
                }
                flux[level_index(j,k,i,nz,cols)] = vel*qface;
            }
        }
    }
    std::vector<double> tendency(static_cast<size_t>(rows*nw*cols), 0.0);
    for (int j = 0; j < rows; ++j) {
        for (int i = 0; i < cols; ++i) {
            for (int k = 1; k < nz; ++k)
                tendency[w_core_index(j,k,i,cols)] = rdnw *
                    (flux[level_index(j,k,i,nz,cols)] -
                     flux[level_index(j,k-1,i,nz,cols)]);
            tendency[w_core_index(j,nz,i,cols)] = -2.0 * rdnw *
                flux[level_index(j,nz-1,i,nz,cols)];
        }
    }
    return tendency;
}

// Independent scalar form of calc_ww_cp. The face masses are averaged before
// forming cu/cv, and the packed aliases are excluded from this recurrence.
std::vector<double> omega_oracle(const TileCase& tile, const CaseSpec& spec) {
    std::vector<double> omega(static_cast<size_t>(spec.m*nw*spec.n), 0.0);
    const double rdx = 1.0 / tile.spacing;
    auto mass = [&](int j, int i) {
        return mu0 + static_cast<double>(tile.mu[j*nx+i]);
    };
    auto mass_u = [&](int j, int face) {
        const int left = face == 0 ? spec.n - 1 : face - 1;
        const int right = face == spec.n ? 0 : face;
        return 0.5 * (mass(j,left) + mass(j,right));
    };
    auto mass_v = [&](int face, int i) {
        const int lower = std::max(0, face - 1);
        const int upper = std::min(spec.m - 1, face);
        return 0.5 * (mass(lower,i) + mass(upper,i));
    };
    for (int j = 0; j < spec.m; ++j) {
        for (int i = 0; i < spec.n; ++i) {
            std::array<double, nz> divv{};
            double dmdt = 0.0;
            const double msftx = tile.mass_map[j*nx+i];
            for (int k = 0; k < nz; ++k) {
                const double c2 = spec.c2[k];
                auto cu = [&](int face) {
                    return (mass_u(j,face) + c2) *
                        tile.u[u_index(j,k,face)] /
                        tile.u_map[j*nu+face];
                };
                auto cv = [&](int face) {
                    return (mass_v(face,i) + c2) *
                        tile.v[v_index(face,k,i)] /
                        tile.v_map[face*nx+i];
                };
                divv[k] = msftx * dnw * rdx *
                    ((cu(i+1)-cu(i)) + (cv(j+1)-cv(j)));
                dmdt += divv[k];
            }
            for (int k = 0; k < nz-1; ++k)
                omega[level_index(j,k+1,i,nw,spec.n)] =
                    omega[level_index(j,k,i,nw,spec.n)] - dnw*dmdt - divv[k];
        }
    }
    return omega;
}

std::vector<double> extend_u(const std::vector<double>& core, const CaseSpec& spec) {
    if (!spec.packed) return core;
    std::vector<double> full(static_cast<size_t>(ny*nz*nu), 0.0);
    for (int j = 0; j < spec.m; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i <= spec.n; ++i)
                full[u_index(j,k,i)] = core[u_core_index(j,k,i,spec.n+1)];
    for (int j = 0; j < spec.m; ++j)
        for (int k = 0; k < nz; ++k)
            full[u_index(j,k,spec.n+1)] = full[u_index(j,k,1)];
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nu; ++i)
            full[u_index(spec.m,k,i)] = full[u_index(spec.m-1,k,i)];
    return full;
}

std::vector<double> extend_v(const std::vector<double>& core, const CaseSpec& spec) {
    if (!spec.packed) return core;
    std::vector<double> full(static_cast<size_t>(nv*nz*nx), 0.0);
    for (int j = 0; j <= spec.m; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < spec.n; ++i)
                full[v_index(j,k,i)] = core[u_core_index(j,k,i,spec.n)];
    for (int j = 0; j <= spec.m; ++j)
        for (int k = 0; k < nz; ++k)
            full[v_index(j,k,spec.n)] = full[v_index(j,k,0)];
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i)
            full[v_index(spec.m+1,k,i)] = -full[v_index(spec.m-1,k,i)];
    return full;
}

std::vector<double> extend_w(const std::vector<double>& core, const CaseSpec& spec) {
    if (!spec.packed) return core;
    std::vector<double> full(static_cast<size_t>(ny*nw*nx), 0.0);
    for (int j = 0; j < spec.m; ++j)
        for (int k = 0; k < nw; ++k)
            for (int i = 0; i < spec.n; ++i)
                full[w_index(j,k,i)] = core[w_core_index(j,k,i,spec.n)];
    for (int j = 0; j < spec.m; ++j)
        for (int k = 0; k < nw; ++k)
            full[w_index(j,k,spec.n)] = full[w_index(j,k,0)];
    for (int k = 0; k < nw; ++k)
        for (int i = 0; i <= spec.n; ++i)
            full[w_index(spec.m,k,i)] = full[w_index(spec.m-1,k,i)];
    return full;
}

std::vector<double> tensor_values(const torch::Tensor& x) {
    const auto cpu = x.detach().to(torch::kCPU, torch::kFloat64).contiguous();
    const auto* p = cpu.data_ptr<double>();
    return std::vector<double>(p, p + cpu.numel());
}

bool all_finite(const std::vector<double>& x) {
    for (const double value : x)
        if (!std::isfinite(value)) return false;
    return true;
}

double max_abs(const std::vector<double>& x) {
    double result = 0.0;
    for (const double value : x) {
        TORCH_CHECK(std::isfinite(value), "non-finite vertical momentum diagnostic");
        result = std::max(result, std::abs(value));
    }
    return result;
}

double max_difference(const std::vector<double>& a, const std::vector<double>& b) {
    TORCH_CHECK(a.size() == b.size(), "vertical momentum shape mismatch");
    TORCH_CHECK(all_finite(a) && all_finite(b),
                "non-finite vertical momentum work or oracle");
    double result = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        result = std::max(result, std::abs(a[i]-b[i]));
    return result;
}

void install_step(TileCase& tile, std::vector<float>& v_y_map) {
    tile.solver.unifiedStep(
        tile.u.data(), tile.v.data(), tile.w.data(), tile.ph.data(), tile.theta.data(),
        tile.mu.data(), tile.ru.data(), tile.rv.data(), tile.rw.data(), tile.rph.data(),
        tile.rt.data(), tile.rm.data(), 1.0f/tile.spacing, 1.0f/tile.spacing,
        tile.metric.data(), tile.metric.data(), tile.mass_map.data(), tile.mass_map.data(),
        tile.u_map.data(), tile.u_map.data(), tile.v_map.data(), v_y_map.data(),
        tile.one.data(), tile.zero.data(), tile.one.data(), tile.zero.data(),
        tile.half.data(), tile.half.data(), 1, 1.0e-4f,
        nx, ny, nz, nu, nv, nw);
    TORCH_CHECK(tile.solver.getLastStepOutcomeCode() == 0,
                "tile setup step did not complete");
}

void fill_probe_fields(TileCase& tile, const CaseSpec& spec) {
    std::fill(tile.w.begin(), tile.w.end(), 0.0f);
    std::fill(tile.ph.begin(), tile.ph.end(), 0.0f);
    std::fill(tile.theta.begin(), tile.theta.end(), 0.0f);
    std::fill(tile.mu.begin(), tile.mu.end(), 0.0f);
    for (int j = 0; j < spec.m; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i <= spec.n; ++i)
                tile.u[u_index(j,k,i)] = static_cast<float>(
                    std::sin(2.0*pi*(i % spec.n)/spec.n) *
                    (1.0 + 0.2*k + 0.15*k*k*k));
    if (spec.packed) {
        for (int j = 0; j < spec.m; ++j)
            for (int k = 0; k < nz; ++k)
                tile.u[u_index(j,k,spec.n+1)] = tile.u[u_index(j,k,1)];
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nu; ++i)
                tile.u[u_index(spec.m,k,i)] = tile.u[u_index(spec.m-1,k,i)];
    }
    for (int j = 0; j <= spec.m; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < spec.n; ++i) {
                const double y = (j == 0 || j == spec.m) ? 0.0 :
                    std::sin(pi*j/spec.m);
                tile.v[v_index(j,k,i)] = static_cast<float>(y *
                    (1.0 + 0.3*k + 0.1*k*k*k));
            }
    if (spec.packed) {
        for (int j = 0; j <= spec.m; ++j)
            for (int k = 0; k < nz; ++k)
                tile.v[v_index(j,k,spec.n)] = tile.v[v_index(j,k,0)];
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nx; ++i)
                tile.v[v_index(spec.m+1,k,i)] = -tile.v[v_index(spec.m-1,k,i)];
    }
    for (int j = 0; j < spec.m; ++j)
        for (int k = 0; k < nw; ++k)
            for (int i = 0; i < spec.n; ++i)
                tile.w[w_index(j,k,i)] = static_cast<float>(
                    k * (0.2*k + 0.03*i + 0.01*j));
    if (spec.packed) {
        for (int j = 0; j < spec.m; ++j)
            for (int k = 0; k < nw; ++k)
                tile.w[w_index(j,k,spec.n)] = tile.w[w_index(j,k,0)];
        for (int k = 0; k < nw; ++k)
            for (int i = 0; i <= spec.n; ++i)
                tile.w[w_index(spec.m,k,i)] = tile.w[w_index(spec.m-1,k,i)];
    }
}

void check_case(bool packed) {
    CaseSpec spec{packed, packed ? ny-1 : ny, packed ? nx-1 : nx,
                  std::vector<float>(nw)};
    for (int k = 0; k < nw; ++k) spec.c2[k] = 1200.0f + 250.0f*k;

    TileCase tile;
    if (packed)
        tile.solver.setWRFIndices(1, nx, 1, ny, 1, nz,
                                  1, nx, 1, ny, 1, nz,
                                  1, nu, 1, nv, 1, nw);
    std::fill(tile.mass_map.begin(), tile.mass_map.end(), 1.25f);
    std::fill(tile.u_map.begin(), tile.u_map.end(), 1.50f);
    std::fill(tile.v_map.begin(), tile.v_map.end(), 0.75f);
    std::vector<float> v_y_map(static_cast<size_t>(nv*nx), 0.90f);
    install_step(tile, v_y_map);
    tile.solver.getGridInfo()->rdnw = torch::full({nz}, static_cast<float>(rdnw));
    tile.solver.getGridInfo()->dnw = torch::full({nz}, static_cast<float>(dnw));
    tile.solver.getGridInfo()->rdn = torch::full({nz}, static_cast<float>(rdnw));
    const std::vector<float> c1h(nw, 1.0f);
    std::vector<float> c2f(nw);
    for (int k = 0; k < nw; ++k) c2f[k] = 1075.0f + 250.0f*k;
    (tile.solver.*access(CoefficientsTag{}))(
        tile.one.data(), c2f.data(), c1h.data(), spec.c2.data());
    fill_probe_fields(tile, spec);

    const auto omega = omega_oracle(tile, spec);
    const auto ru_raw = vertical_flux3(
        spec.m, spec.n + 1,
        [&](int j,int k,int i) { return static_cast<double>(tile.u[u_index(j,k,i)]); },
        [&](int j,int k,int i) {
            const int left = i == 0 ? spec.n-1 : i-1;
            const int right = i == spec.n ? 0 : i;
            return 0.5 * (omega[level_index(j,k,left,nw,spec.n)] +
                          omega[level_index(j,k,right,nw,spec.n)]);
        });
    const auto rv_raw = vertical_flux3(
        spec.m + 1, spec.n,
        [&](int j,int k,int i) { return static_cast<double>(tile.v[v_index(j,k,i)]); },
        [&](int j,int k,int i) {
            const int lower = std::max(0, j-1);
            const int upper = std::min(spec.m-1, j);
            return 0.5 * (omega[level_index(lower,k,i,nw,spec.n)] +
                          omega[level_index(upper,k,i,nw,spec.n)]);
        });
    const auto rw_raw = vertical_w_flux3(
        spec.m, spec.n,
        [&](int j,int k,int i) { return static_cast<double>(tile.w[w_index(j,k,i)]); },
        [&](int j,int k,int i) {
            return omega[level_index(j,k,i,nw,spec.n)];
        });

    std::vector<double> ru_expected(ru_raw.size()), rv_expected(rv_raw.size()),
                        rw_expected(rw_raw.size());
    for (int j = 0; j < spec.m; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i <= spec.n; ++i) {
                const double alpha = (mu0 + spec.c2[k]) / tile.u_map[j*nu+i];
                ru_expected[u_core_index(j,k,i,spec.n+1)] = ru_raw[
                    u_core_index(j,k,i,spec.n+1)] / alpha;
            }
    for (int j = 0; j <= spec.m; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < spec.n; ++i) {
                const double alpha = (mu0 + spec.c2[k]) / tile.v_map[j*nx+i];
                const double map_ratio = 0.90 / tile.v_map[j*nx+i];
                rv_expected[u_core_index(j,k,i,spec.n)] =
                    rv_raw[u_core_index(j,k,i,spec.n)] * map_ratio / alpha;
            }
    for (int j = 0; j < spec.m; ++j)
        for (int k = 0; k < nw; ++k)
            for (int i = 0; i < spec.n; ++i) {
                const double alpha = (mu0 + c2f[k]) / tile.mass_map[j*nx+i];
                rw_expected[w_core_index(j,k,i,spec.n)] = rw_raw[
                    w_core_index(j,k,i,spec.n)] / alpha;
            }
    ru_expected = extend_u(ru_expected, spec);
    rv_expected = extend_v(rv_expected, spec);
    rw_expected = extend_w(rw_expected, spec);

    const auto rhs = (tile.solver.*access(RhsTag{}))(tile.state(), RhsMode::Full);
    const auto ru_actual = tensor_values(tile.solver.*access(RuTag{}));
    const auto rv_actual = tensor_values(tile.solver.*access(RvTag{}));
    const auto rw_actual = tensor_values(tile.solver.*access(RwTag{}));
    TORCH_CHECK(torch::isfinite(rhs).all().item<bool>(), "Full RHS is non-finite");
    TORCH_CHECK(all_finite(ru_actual) && all_finite(rv_actual) && all_finite(rw_actual),
                "vertical momentum work contains NaN/Inf");
    TORCH_CHECK(all_finite(ru_expected) && all_finite(rv_expected) && all_finite(rw_expected),
                "vertical momentum oracle contains NaN/Inf");
    std::vector<double> ru_scaled(ru_actual.size()), rv_scaled(rv_actual.size()),
                        rw_scaled(rw_actual.size());
    for (size_t i = 0; i < ru_actual.size(); ++i) ru_scaled[i] = ru_actual[i] / mu0;
    for (size_t i = 0; i < rv_actual.size(); ++i) rv_scaled[i] = rv_actual[i] / mu0;
    for (size_t i = 0; i < rw_actual.size(); ++i) rw_scaled[i] = rw_actual[i] / mu0;
    const double ru_error = max_difference(ru_scaled, ru_expected);
    const double rv_error = max_difference(rv_scaled, rv_expected);
    const double rw_error = max_difference(rw_scaled, rw_expected);
    const double omega_max = max_abs(omega);
    double top_u = 0.0, top_v = 0.0, top_w = 0.0;
    for (int j = 0; j < spec.m; ++j) {
        for (int i = 0; i <= spec.n; ++i)
            top_u = std::max(top_u, std::abs(ru_expected[u_index(j,nz-1,i)]));
        for (int i = 0; i < spec.n; ++i)
            top_w = std::max(top_w, std::abs(rw_expected[w_index(j,nz,i)]));
    }
    for (int j = 0; j <= spec.m; ++j)
        for (int i = 0; i < spec.n; ++i)
            top_v = std::max(top_v, std::abs(rv_expected[v_index(j,nz-1,i)]));
    std::cout << "VERTICAL_MOMENTUM packed=" << packed
              << " omega_max=" << omega_max
              << " ru_error=" << ru_error << " rv_error=" << rv_error
              << " rw_error=" << rw_error << " top_u=" << top_u
              << " top_v=" << top_v << " top_w=" << top_w << '\n';
    TORCH_CHECK(omega_max > 1.0e-5, "Omega probe is zero");
    TORCH_CHECK(std::min({top_u, top_v, top_w}) > 1.0e-8,
                "a top-level transport oracle is uninformative");
    const double tolerance = 128.0 * std::numeric_limits<float>::epsilon() *
        std::max({max_abs(ru_expected), max_abs(rv_expected), max_abs(rw_expected)});
    TORCH_CHECK(ru_error < tolerance && rv_error < tolerance && rw_error < tolerance,
                "WRF order-3 vertical momentum mismatch: ru=", ru_error,
                " rv=", rv_error, " rw=", rw_error, " tolerance=", tolerance, " packed=", packed);
}
} // namespace

int main() {
    torch::set_num_threads(1);
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.imex_split_mode = 0;
    cfg.mass_coordinate_mode = 1;
    cfg.wrf_omega_ww_cp = false;
    cfg.mu_horizontal_div_only = false;
    cfg.hevi_split = false;
    cfg.omega_w_blend = 1.0f;
    cfg.sign_smooth_delta = 0.0f;
    check_case(false);
    check_case(true);
    return 0;
}
