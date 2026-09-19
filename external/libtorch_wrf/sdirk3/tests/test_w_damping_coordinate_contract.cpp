// Private actual-RHS witness for the W-damping coordinate conversion.
//
// The public TileCase step is used first to publish the zero-copy geometry,
// map factors, vertical coefficients, timestep, and runtime boundary contract.
// The state is then restored and the private actual computeUnifiedRHS is called
// on identical input with wrf_w_damping off and on.  The independent scalar
// reference is a direct transcription of WRF's W_DAMP + small_step_finish:
//
//   raw = SIGN(w) * w_alpha * (|ww/Mf|*rdnw*dt - w_crit_cfl) * Mf
//   physical = raw * msfty / Mf
//
// where Mf = c1f*M+c2f and M=mu+mu_base.  The current frozen RHS path returns
// the coupled raw contribution divided by velocity_mass_w.  The actual
// canonical-horizontal caller correction is expected to store
// -raw*velocity_mass_w/(Mf/msfty), which becomes -raw*msfty/Mf after the
// existing velocity conversion. The raw capture remains unscaled.

#include "tile_test_fixture.h"
#include "wrf_sdirk3_rw_term_capture.h"
#include "wrf_sdirk3_ww_cp.h"
#include "wrf_sdirk3_w_damping.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
struct CoeffTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*,
                                                   const float*, const float*);
    friend type access(CoeffTag);
};
template<typename Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<CoeffTag,
                         &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;

constexpr double pi = 3.1415926535897932384626433832795;
constexpr double mu_base = 80000.0;
constexpr double w_alpha = 0.3;
constexpr double w_crit_cfl = 0.1;
constexpr double rdnw = 4.0;
constexpr double dnw = -0.25;
constexpr float c1h_ref[nz] = {0.61f, 0.93f, 1.17f, 0.78f};
constexpr float c2h_ref[nz] = {900.0f, -450.0f, 1600.0f, -800.0f};

size_t u_index(int j, int k, int i) { return (j*nz + k)*nu + i; }
size_t v_index(int j, int k, int i) { return (j*nz + k)*nx + i; }
size_t w_index(int j, int k, int i) { return (j*nw + k)*nx + i; }
size_t mu_index(int j, int i) { return static_cast<size_t>(j*nx+i); }

std::vector<double> values(const torch::Tensor& x) {
    const auto y = x.detach().to(torch::kCPU, torch::kFloat64).contiguous();
    const auto* p = y.data_ptr<double>();
    return std::vector<double>(p, p + y.numel());
}

const torch::Tensor& captured_term(
    const std::vector<std::pair<std::string, torch::Tensor>>& terms,
    const char* name) {
    for (const auto& kv : terms) {
        if (kv.first == name) return kv.second;
    }
    TORCH_CHECK(false, "missing production rw capture term: ", name);
}

struct CapturedRhs {
    std::vector<double> rhs;
    std::vector<double> rw_final;
    std::vector<double> state_mu;
    std::vector<double> mu_full;
    std::vector<double> w_damp;
    std::vector<double> wd_vert_cfl;
    std::vector<double> wd_mass_factor;
    std::vector<double> w_input;
    std::string inventory;
    int term_count = 0;
};

CapturedRhs capture_rhs(TileCase& tile, bool damping_on, RhsMode mode) {
    wrf::sdirk3::RwTermCaptureScope scope;
    TORCH_CHECK(scope.armed_ok(), "failed to arm production rw capture");
    const auto U = tile.state();
    const auto rhs_tensor = (tile.solver.*access(RhsTag{}))(U, mode);
    const auto terms = scope.take();
    CapturedRhs out;
    out.term_count = static_cast<int>(terms.size());
    out.inventory = wrf::sdirk3::validate_rw_term_inventory(terms, damping_on);
    TORCH_CHECK(out.inventory.empty(), "production rw capture inventory: ",
                out.inventory);
    out.rhs = values(rhs_tensor);
    out.rw_final = values(captured_term(terms, "rw_tend_final"));
    out.state_mu = values(U.slice(0, su + sv + 2*sw + st, total));
    if (damping_on) {
        out.w_damp = values(captured_term(terms, "w_damp_padded"));
        out.wd_vert_cfl = values(captured_term(terms, "wd_vert_cfl"));
        out.wd_mass_factor = values(captured_term(terms, "wd_mass_factor"));
        out.w_input = values(captured_term(terms, "w_input"));
        const auto state_w = values(U.slice(0, su + sv, su + sv + sw));
        for (int j = 0; j < ny; ++j) {
            for (int k = 1; k < nz; ++k) {
                for (int i = 0; i < nx; ++i) {
                    const size_t input_q = static_cast<size_t>(
                        (j*(nz-1) + (k-1))*nx + i);
                    TORCH_CHECK(out.w_input[input_q] ==
                                    state_w[w_index(j,k,i)],
                                "production w_input differs from RHS state w at j=",
                                j, " k=", k, " i=", i);
                }
            }
        }
        const auto captured_mu = captured_term(terms, "mu_input");
        out.mu_full = values(captured_mu);
        TORCH_CHECK(out.mu_full.size() == out.state_mu.size(),
                    "production mu_input shape differs from RHS state mu");
        for (size_t q = 0; q < out.mu_full.size(); ++q) {
            const float expected = static_cast<float>(out.state_mu[q]) +
                                   static_cast<float>(mu_base);
            TORCH_CHECK(std::isfinite(out.mu_full[q]) && out.mu_full[q] > 0.0 &&
                        static_cast<float>(out.mu_full[q]) == expected,
                        "production mu_input disagrees with state mass at q=", q,
                        " captured=", out.mu_full[q], " expected=", expected);
        }
    }
    TORCH_CHECK(rhs_tensor.defined(), "production RHS is undefined");
    return out;
}

double sign_wrf(double x) { return std::signbit(x) ? -1.0 : 1.0; }

double float_ulp(float x) {
    if (!std::isfinite(x)) return std::numeric_limits<double>::infinity();
    const float next = std::nextafterf(x, x >= 0.0f
                                             ? std::numeric_limits<float>::infinity()
                                             : -std::numeric_limits<float>::infinity());
    return std::abs(static_cast<double>(next) - static_cast<double>(x));
}

void configure_fixture(TileCase& tile, double msfty_value) {
    // Packed whole-domain tile: periodic X and two-sided symmetric Y.
    tile.solver.setWRFIndices(1, nx, 1, ny, 1, nz,
                              1, nx, 1, ny, 1, nz,
                              1, nu, 1, nv, 1, nw);
    tile.solver.setBoundaryConditions(true, false, false, false, true, true,
                                      false, false, false, false);

    std::fill(tile.mass_map.begin(), tile.mass_map.end(),
              static_cast<float>(msfty_value));
    std::fill(tile.u_map.begin(), tile.u_map.end(), 1.0f);
    std::fill(tile.v_map.begin(), tile.v_map.end(), 1.0f);
    std::fill(tile.metric.begin(), tile.metric.end(), 4.0f);

    // c1f/c2f are deliberately nontrivial at W levels.  The runner installs
    // distinct c1h/c2h at mass levels after the required public step, so the
    // Omega oracle exercises a genuinely hybrid coordinate.
    const float c1[nw] = {0.72f, 1.08f, 0.86f, 1.24f, 0.95f};
    const float c2[nw] = {1400.0f, -700.0f, 2200.0f, -1100.0f, 900.0f};
    for (int k = 0; k < nw; ++k) {
        tile.one[k] = c1[k];
        tile.zero[k] = c2[k];
    }
    std::fill(tile.half.begin(), tile.half.end(), 0.5f);
}

void fill_counterexample_state(TileCase& tile) {
    std::fill(tile.u.begin(), tile.u.end(), 0.0f);
    std::fill(tile.v.begin(), tile.v.end(), 0.0f);
    std::fill(tile.w.begin(), tile.w.end(), 0.0f);
    std::fill(tile.ph.begin(), tile.ph.end(), 0.0f);
    std::fill(tile.theta.begin(), tile.theta.end(), 0.0f);
    std::fill(tile.mu.begin(), tile.mu.end(), 0.0f);

    constexpr double u_amplitude = 500000.0;
    constexpr double v_amplitude = 180000.0;
    constexpr double w_amplitude = 6.0;
    const int m = ny - 1;
    const int n = nx - 1;
    for (int j = 0; j < m; ++j) {
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i <= n; ++i) {
                const int ip = i % n;
                const double x = 2.0*pi*static_cast<double>(ip)/n;
                const double y = pi*(static_cast<double>(j)+0.5)/m;
                tile.u[u_index(j,k,i)] = static_cast<float>(
                    u_amplitude * (std::sin(x) + 0.12*std::cos(y) + 0.03*k));
            }
        }
        for (int k = 0; k < nz; ++k) {
            // The packed adapter column is the second physical U face.
            tile.u[u_index(j,k,n+1)] = tile.u[u_index(j,k,1)];
        }
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i < nx; ++i) {
                const int ip = i % n;
                const double x = 2.0*pi*static_cast<double>(ip)/n;
                const double y = pi*static_cast<double>(j)/m;
                tile.v[v_index(j,k,i)] = static_cast<float>(
                    v_amplitude * std::sin(y) * (0.8*std::cos(x) - 0.05*k));
            }
            tile.v[v_index(j,k,n)] = tile.v[v_index(j,k,0)];
        }
        for (int k = 0; k < nw; ++k) {
            for (int i = 0; i < nx; ++i) {
                const int ip = i % n;
                const double x = 2.0*pi*static_cast<double>(ip)/n;
                const double y = pi*(static_cast<double>(j)+0.5)/m;
                const double vertical = (k == 0 || k == nw-1)
                    ? 0.0 : (1.0 + 0.2*k);
                tile.w[w_index(j,k,i)] = static_cast<float>(
                    w_amplitude * vertical * std::sin(x) * std::cos(y));
            }
        }
        // Nonzero perturbation mass makes both M and Mf spatially variable.
        for (int i = 0; i < nx; ++i) {
            const int ip = i % n;
            tile.mu[mu_index(j,i)] = static_cast<float>(
                1200.0*std::sin(2.0*pi*static_cast<double>(ip)/n) +
                250.0*std::cos(pi*(static_cast<double>(j)+0.5)/m));
        }
    }
    // Symmetric-Y adapter row for U/W and odd wall row for V.
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nu; ++i)
            tile.u[u_index(m,k,i)] = tile.u[u_index(m-1,k,i)];
    for (int k = 0; k < nw; ++k)
        for (int i = 0; i < nx; ++i)
            tile.w[w_index(m,k,i)] = tile.w[w_index(m-1,k,i)];
    for (int i = 0; i < nx; ++i)
        tile.mu[mu_index(m,i)] = tile.mu[mu_index(m-1,i)];
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            tile.v[v_index(m,k,i)] = 0.0f;
            tile.v[v_index(m+1,k,i)] = -tile.v[v_index(m-1,k,i)];
        }
    }
}

void check_packed_aliases(const TileCase& tile) {
    const int m = ny - 1;
    const int n = nx - 1;
    for (int j = 0; j < m; ++j) {
        for (int k = 0; k < nz; ++k) {
            TORCH_CHECK(tile.u[u_index(j,k,n)] == tile.u[u_index(j,k,0)] &&
                        tile.u[u_index(j,k,n+1)] == tile.u[u_index(j,k,1)],
                        "packed U periodic aliases differ");
        }
        for (int k = 0; k < nw; ++k) {
            TORCH_CHECK(tile.w[w_index(j,k,n)] == tile.w[w_index(j,k,0)],
                        "packed W periodic alias differs");
        }
        for (int k = 0; k < nz; ++k) {
            TORCH_CHECK(tile.v[v_index(j,k,n)] == tile.v[v_index(j,k,0)],
                        "packed V periodic alias differs");
        }
    }
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nu; ++i)
            TORCH_CHECK(tile.u[u_index(m,k,i)] == tile.u[u_index(m-1,k,i)],
                        "packed U symmetric row differs");
        for (int i = 0; i < nx; ++i) {
            TORCH_CHECK(tile.v[v_index(m,k,i)] == 0.0f,
                        "packed V wall row is not zero");
            TORCH_CHECK(tile.v[v_index(m+1,k,i)] == -tile.v[v_index(m-1,k,i)],
                        "packed V odd symmetric adapter row differs");
        }
    }
    for (int k = 0; k < nw; ++k)
        for (int i = 0; i < nx; ++i)
            TORCH_CHECK(tile.w[w_index(m,k,i)] == tile.w[w_index(m-1,k,i)],
                        "packed W symmetric row differs");
    for (int i = 0; i < nx; ++i)
        TORCH_CHECK(tile.mu[mu_index(m,i)] == tile.mu[mu_index(m-1,i)],
                    "packed mass symmetric row differs");
}

struct OmegaOracle {
    std::vector<double> omega;
    double max_abs_omega = 0.0;
};

OmegaOracle scalar_wrf_omega(const TileCase& tile,
                             const std::vector<double>* full_mass = nullptr) {
    // The W-damping production block calls compute_wrf_ww_cp directly on the
    // packed tensors.  Its helper therefore sees the full [ny,nx] arrays;
    // this oracle mirrors that call exactly, including the packed endpoint
    // aliases in the seam averages.  The final witness still reports only
    // independent cells j<ny-1, i<nx-1.
    const int m = ny;
    const int n = nx;
    const double rdx = 1.0/static_cast<double>(tile.spacing);
    const double rdy = 1.0/static_cast<double>(tile.spacing);
    const float* c1 = c1h_ref;
    const float* c2 = c2h_ref;
    OmegaOracle out{std::vector<double>(static_cast<size_t>(m*nw*n), 0.0), 0.0};
    auto mass = [&](int j, int i) {
        if (full_mass != nullptr)
            return (*full_mass)[mu_index(j,i)];
        return mu_base + static_cast<double>(tile.mu[mu_index(j,i)]);
    };
    auto mass_u = [&](int j, int face) {
        const int left = face == 0 ? n-1 : face-1;
        const int right = face == n ? 0 : face;
        return 0.5*(mass(j,left)+mass(j,right));
    };
    auto mass_v = [&](int face, int i) {
        const int lower = std::max(0, face-1);
        const int upper = std::min(m-1, face);
        return 0.5*(mass(lower,i)+mass(upper,i));
    };
    auto omega_index = [=](int j, int k, int i) {
        return static_cast<size_t>((j*nw+k)*n+i);
    };
    for (int j = 0; j < m; ++j) {
        for (int i = 0; i < n; ++i) {
            double divv[nz] = {};
            double dmdt = 0.0;
            const double msftx = tile.mass_map[mu_index(j,i)];
            for (int k = 0; k < nz; ++k) {
                auto cu = [&](int face) {
                    return (static_cast<double>(c1[k])*mass_u(j,face) + c2[k]) *
                        static_cast<double>(tile.u[u_index(j,k,face)]) /
                        static_cast<double>(tile.u_map[j*nu+face]);
                };
                auto cv = [&](int face) {
                    return (static_cast<double>(c1[k])*mass_v(face,i) + c2[k]) *
                        static_cast<double>(tile.v[v_index(face,k,i)]) /
                        static_cast<double>(tile.v_map[face*nx+i]);
                };
                divv[k] = msftx*dnw*(rdx*(cu(i+1)-cu(i)) +
                                      rdy*(cv(j+1)-cv(j)));
                dmdt += divv[k];
            }
            for (int k = 0; k < nz-1; ++k) {
                out.omega[omega_index(j,k+1,i)] =
                    out.omega[omega_index(j,k,i)] - dnw*c1[k]*dmdt - divv[k];
            }
        }
    }
    for (const double x : out.omega) {
        TORCH_CHECK(std::isfinite(x), "non-finite scalar Omega");
        out.max_abs_omega = std::max(out.max_abs_omega, std::abs(x));
    }
    return out;
}

struct Result {
    std::vector<double> rhs;
    std::vector<double> state;
    CapturedRhs capture;
    OmegaOracle omega;
    int active = 0;
};

Result run_case(bool damping_on, double msfty_value, RhsMode mode) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg.wrf_w_damping = damping_on ? 1 : 0;
    TileCase tile(10000.0f);
    configure_fixture(tile, msfty_value);

    // Public initialization first.  The state is zero here, so this call is a
    // harmless publisher for map/coefficient tensors, dt_stage_, U_ref_stage_,
    // and the resolved periodic-X/symmetric-Y runtime contract.
    tile.step(0.03f);
    TORCH_CHECK(tile.solver.getLastStepOutcomeCode() == 0,
                "public initialization step did not complete");
    // Keep c1f/c2f at the deliberately distinct W levels from configure_fixture,
    // then replace only the mass-level c1h/c2h used by calc_ww_cp.  This is a
    // private fixture mutation after the required public priming call.
    (tile.solver.*access(CoeffTag{}))(
        tile.one.data(), tile.zero.data(), c1h_ref, c2h_ref);

    fill_counterexample_state(tile);
    check_packed_aliases(tile);
    const auto U = tile.state();
    const auto captured = capture_rhs(tile, damping_on, mode);
    const auto& rhs = captured.rhs;
    TORCH_CHECK(rhs.size() == static_cast<size_t>(total),
                "unexpected Full RHS size: ", rhs.size());
    Result out;
    out.rhs = rhs;
    out.state = values(U);
    out.capture = captured;
    out.omega = scalar_wrf_omega(
        tile, damping_on ? &out.capture.mu_full : nullptr);

    const int m = ny - 1;
    const int n = nx - 1;
    const int w_offset = su + sv;
    const double dt = 0.03;
    const double gate = w_crit_cfl;
    for (int j = 0; j < m; ++j) {
        for (int k = 1; k < nz; ++k) {
            for (int i = 0; i < n; ++i) {
                const double M = damping_on
                    ? out.capture.mu_full[mu_index(j,i)]
                    : mu_base + tile.mu[mu_index(j,i)];
                const double Mf = static_cast<double>(tile.one[k])*M + tile.zero[k];
                const double ww = out.omega.omega[(j*nw+k)*nx+i];
                const double cfl = std::abs(ww/Mf*rdnw*dt);
                const double w = tile.w[w_index(j,k,i)];
                const double raw = (cfl > gate)
                    ? sign_wrf(w)*w_alpha*(cfl-w_crit_cfl)*Mf : 0.0;
                const double physical = raw*msfty_value/Mf;
                const size_t q = static_cast<size_t>(w_offset + w_index(j,k,i));
                const double actual = damping_on ? rhs[q] : 0.0;
                // The off path is recorded separately by the caller; on/off
                // delta checks are done after both cases have been evaluated.
                if (raw != 0.0) ++out.active;
                TORCH_CHECK(std::isfinite(raw) && std::isfinite(physical) &&
                            std::isfinite(actual), "non-finite W damping sample");
            }
        }
    }
    return out;
}

void check_map_case(double msfty_value, RhsMode mode) {
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    const Result off = run_case(false, msfty_value, mode);
    const Result on = run_case(true, msfty_value, mode);
    const Result off_repeat = run_case(false, msfty_value, mode);
    TORCH_CHECK(off.rhs == off_repeat.rhs,
                "damping-off RHS changed across frozen-baseline repeats: msfty=",
                msfty_value, " mode=", mode == RhsMode::Full ? "Full" : "ImplicitOnly");
    TORCH_CHECK(off.state == on.state,
                "on/off RHS inputs differ after public priming: msfty=",
                msfty_value);
    TORCH_CHECK(off.capture.state_mu == on.capture.state_mu,
                "on/off captured mu inputs differ: msfty=", msfty_value);
    std::cerr << "WDAMP_DEBUG msfty=" << msfty_value
              << " omega_max=" << off.omega.max_abs_omega
              << " active_from_scalar_reference=" << on.active
              << " capture_terms_off=" << off.capture.term_count
              << " capture_terms_on=" << on.capture.term_count << '\n';
    TORCH_CHECK(off.omega.max_abs_omega > 1.0e-3,
                "Omega must be nonzero: msfty=", msfty_value,
                " max=", off.omega.max_abs_omega);
    TORCH_CHECK(on.omega.max_abs_omega > 1.0e-3,
                "Omega must be nonzero in on case: msfty=", msfty_value);
    TORCH_CHECK(on.active > 0, "W-damping gate never activated: msfty=", msfty_value);

    const int w_offset = su + sv;
    const int m = ny - 1;
    const int n = nx - 1;
    double actual_l2 = 0.0;
    double physical_l2 = 0.0;
    double current_vs_physical_max = 0.0;
    double raw_vs_actual_max = 0.0;
    double physical_delta_error = 0.0;
    double physical_delta_bound_max = 0.0;
    double coupled_raw_max = 0.0;
    double coupled_raw_bound_max = 0.0;
    double scalar_cfl_max = 0.0;
    double scalar_raw_max = 0.0;
    double actual_raw_l2 = 0.0;
    double raw_sq = 0.0;
    double delta_max = 0.0;
    // Re-run the scalar bookkeeping on the same deterministic state held by
    // the Result-independent helper, using the formulas and output vectors.
    // The state values are reconstructed here so no production private member
    // is used to manufacture the reference.
    TileCase reference_tile(10000.0f);
    configure_fixture(reference_tile, msfty_value);
    fill_counterexample_state(reference_tile);
    const auto reference_state = values(reference_tile.state());
    TORCH_CHECK(reference_state == off.state && reference_state == on.state,
                "reference/on/off state bytes differ after priming: msfty=",
                msfty_value);
    double mass_min = std::numeric_limits<double>::infinity();
    double mass_max = -std::numeric_limits<double>::infinity();
    for (int j = 0; j < m; ++j) {
        for (int i = 0; i < n; ++i) {
            const double M = on.capture.mu_full[mu_index(j,i)];
            mass_min = std::min(mass_min, M);
            mass_max = std::max(mass_max, M);
        }
    }
    TORCH_CHECK(mass_max > mass_min + 1.0,
                "fixture must exercise variable full mass M");
    TORCH_CHECK(msfty_value == 1.0 || msfty_value == 1.3,
                "unexpected map-factor case");
    if (msfty_value > 1.0)
        TORCH_CHECK(std::abs(msfty_value - 1.0) > 1.0e-6,
                    "nonunit msfty case collapsed");
    TORCH_CHECK(std::abs(reference_tile.one[1] - reference_tile.one[2]) > 1.0e-6f &&
                std::abs(reference_tile.zero[1] - reference_tile.zero[2]) > 1.0e-3f,
                "fixture must exercise distinct hybrid coefficients");
    const OmegaOracle omega = scalar_wrf_omega(reference_tile,
                                               &on.capture.mu_full);
    int actual_active = 0;
    for (int j = 0; j < m; ++j) {
        for (int k = 1; k < nz; ++k) {
            for (int i = 0; i < n; ++i) {
                const double M = on.capture.mu_full[mu_index(j,i)];
                const double Mf = static_cast<double>(reference_tile.one[k])*M +
                                  reference_tile.zero[k];
                const double ww = omega.omega[(j*nw+k)*nx+i];
                const double cfl = std::abs(ww/Mf*rdnw*0.03);
                const double w = reference_tile.w[w_index(j,k,i)];
                const double raw = (cfl > w_crit_cfl)
                    ? sign_wrf(w)*w_alpha*(cfl-w_crit_cfl)*Mf : 0.0;
                const size_t q = static_cast<size_t>(w_offset + w_index(j,k,i));
                const double delta = on.rhs[q] - off.rhs[q];
                const size_t cap_q = static_cast<size_t>(w_index(j,k,i));
                const size_t cap_int_q = static_cast<size_t>(
                    (j*(nz-1) + (k-1))*nx + i);
                const double actual_raw = on.capture.w_damp[cap_q];
                const double actual_cfl = on.capture.wd_vert_cfl[cap_int_q];
                const double actual_mf = on.capture.wd_mass_factor[cap_int_q];
                const double coupled_delta = on.capture.rw_final[cap_q] -
                                             off.capture.rw_final[cap_q];
                const float msfty_f = static_cast<float>(
                    reference_tile.mass_map[mu_index(j,i)]);
                const float raw_f = static_cast<float>(actual_raw);
                float velocity_mass_f = static_cast<float>(
                    on.capture.mu_full[mu_index(j,i)]);
                const float level_mass_f = static_cast<float>(actual_mf);
                const float alpha_f = level_mass_f / msfty_f;
                const double physical_delta =
                    -actual_raw/static_cast<double>(alpha_f);
                const double legacy_delta =
                    static_cast<double>(-raw_f / velocity_mass_f);
                const float expected_coupled_f =
                    (-raw_f * velocity_mass_f) / alpha_f;
                const double expected_coupled =
                    static_cast<double>(expected_coupled_f);
                const double rhs_bound = 8.0 * std::max({
                    float_ulp(static_cast<float>(on.rhs[q])),
                    float_ulp(static_cast<float>(off.rhs[q])),
                    float_ulp(static_cast<float>(physical_delta))}) + 1.0e-12;
                TORCH_CHECK(std::isfinite(delta) &&
                            std::isfinite(alpha_f) &&
                            std::isfinite(legacy_delta) &&
                            std::isfinite(expected_coupled) &&
                            std::isfinite(physical_delta) &&
                            std::isfinite(actual_raw) && std::isfinite(actual_cfl) &&
                            std::isfinite(actual_mf) && std::isfinite(coupled_delta),
                            "non-finite delta/capture");
                actual_l2 += delta*delta;
                physical_l2 += physical_delta*physical_delta;
                actual_raw_l2 += actual_raw*actual_raw;
                raw_sq += raw*raw;
                delta_max = std::max(delta_max, std::abs(delta));
                physical_delta_error = std::max(
                    physical_delta_error, std::abs(delta-physical_delta));
                physical_delta_bound_max = std::max(
                    physical_delta_bound_max, rhs_bound);
                coupled_raw_max = std::max(coupled_raw_max,
                                           std::abs(coupled_delta-expected_coupled));
                const double coupled_bound = 8.0 * std::max({
                    float_ulp(static_cast<float>(on.capture.rw_final[cap_q])),
                    float_ulp(static_cast<float>(off.capture.rw_final[cap_q])),
                    float_ulp(expected_coupled_f)}) + 1.0e-9;
                coupled_raw_bound_max = std::max(coupled_raw_bound_max,
                                                 coupled_bound);
                scalar_cfl_max = std::max(scalar_cfl_max,
                                          std::abs(actual_cfl-cfl));
                scalar_raw_max = std::max(scalar_raw_max,
                                          std::abs(actual_raw-raw));
                const double actual_current_delta = legacy_delta;
                current_vs_physical_max = std::max(
                    current_vs_physical_max,
                    std::abs(actual_current_delta-physical_delta));
                raw_vs_actual_max = std::max(
                    raw_vs_actual_max, std::abs(delta-legacy_delta));
                if (actual_raw != 0.0) ++actual_active;
                const double mf_round_bound = 8.0 * std::max(
                    float_ulp(static_cast<float>(actual_mf)),
                    float_ulp(static_cast<float>(Mf))) + 1.0e-12;
                TORCH_CHECK(std::abs(actual_mf-Mf) <= mf_round_bound,
                            "production c1f*M+c2f disagrees with fixture: msfty=",
                            msfty_value, " j=", j, " k=", k, " i=", i,
                            " actual=", actual_mf, " reference=", Mf);
                TORCH_CHECK(std::abs(delta-physical_delta) <= rhs_bound,
                            "actual on/off W RHS misses physical reference at "
                            "msfty=", msfty_value, " j=", j, " k=", k,
                            " i=", i, " error=", std::abs(delta-physical_delta),
                            " bound=", rhs_bound);
                TORCH_CHECK(std::abs(coupled_delta-expected_coupled) <= coupled_bound,
                            "actual coupled W tendency misses raw/alpha at "
                            "msfty=", msfty_value, " j=", j, " k=", k,
                            " i=", i, " error=",
                            std::abs(coupled_delta-expected_coupled),
                            " bound=", coupled_bound);
            }
        }
    }
    actual_l2 = std::sqrt(actual_l2);
    physical_l2 = std::sqrt(physical_l2);
    const double raw_l2 = std::sqrt(raw_sq);
    actual_raw_l2 = std::sqrt(actual_raw_l2);
    std::cerr << "WDAMP_CAPTURE msfty=" << msfty_value
              << " scalar_active=" << on.active
              << " production_active=" << actual_active
              << " cfl_max_error=" << scalar_cfl_max
              << " raw_max_error=" << scalar_raw_max
              << " coupled_raw_max_error=" << coupled_raw_max
              << " physical_delta_max_error=" << physical_delta_error
              << " physical_delta_bound=" << physical_delta_bound_max << '\n';
    TORCH_CHECK(raw_l2 > 1.0e-6 && actual_raw_l2 > 1.0e-10 &&
                actual_l2 > 1.0e-10 && physical_l2 > 1.0e-10,
                "W-damping on/off witness is zero or inactive: msfty=", msfty_value,
                " raw_l2=", raw_l2, " actual_l2=", actual_l2,
                " physical_l2=", physical_l2);
    TORCH_CHECK(actual_active == on.active,
                "scalar and production W-damping active sets differ: msfty=",
                msfty_value, " scalar=", on.active,
                " production=", actual_active);
    TORCH_CHECK(scalar_cfl_max <= 8.0e-3,
                "scalar Omega oracle disagrees with production vert_cfl: msfty=",
                msfty_value, " max_error=", scalar_cfl_max);
    TORCH_CHECK(scalar_raw_max <= 8.0e-2,
                "scalar Omega/raw oracle disagrees with production raw term: msfty=",
                msfty_value, " max_error=", scalar_raw_max);
    TORCH_CHECK(coupled_raw_max <= coupled_raw_bound_max,
                "on/off coupled rw tendency misses actual corrected raw/alpha: msfty=",
                msfty_value, " max_error=", coupled_raw_max,
                " bound=", coupled_raw_bound_max);
    TORCH_CHECK(physical_delta_error <= physical_delta_bound_max,
                "actual corrected on/off RHS delta differs from independent "
                "physical -raw*msfty/level_mass beyond the FP32 bound: msfty=",
                msfty_value,
                " max_error=", physical_delta_error,
                " bound=", physical_delta_bound_max);
    // Preserve the original frozen-path mismatch as a regression witness:
    // before the private caller correction, raw/M is materially different
    // from WRF's raw*msfty/(c1f*M+c2f) physical reconstruction.
    TORCH_CHECK(current_vs_physical_max > 1.0e-3,
                "legacy raw/M coordinate mismatch collapsed for msfty=", msfty_value);
    std::cout << "WDAMP_COORD mode="
              << (mode == RhsMode::Full ? "Full" : "ImplicitOnly")
              << " msfty=" << msfty_value
              << " damping_off_repeat_exact=1"
              << " omega_max=" << off.omega.max_abs_omega
              << " active_scalar=" << on.active
              << " active_production=" << actual_active
              << " raw_l2=" << raw_l2
              << " actual_raw_l2=" << actual_raw_l2
              << " actual_delta_l2=" << actual_l2
              << " physical_ref_l2=" << physical_l2
              << " physical_delta_max_error=" << physical_delta_error
              << " physical_delta_bound=" << physical_delta_bound_max
              << " coupled_raw_max_error=" << coupled_raw_max
              << " coupled_raw_bound=" << coupled_raw_bound_max
              << " scalar_cfl_max_error=" << scalar_cfl_max
              << " scalar_raw_max_error=" << scalar_raw_max
              << " runtime_vs_legacy_raw_over_M_max_error=" << raw_vs_actual_max
              << " legacy_raw_over_M_vs_physical_max_error=" << current_vs_physical_max
              << " delta_max=" << delta_max << '\n';
    (void)cfg;
}

}  // namespace

int main() {
    torch::NoGradGuard no_grad;
    torch::set_num_threads(1);
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    cfg = wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level = 0;
    cfg.imex_split_mode = 3;
    cfg.mass_coordinate_mode = 1;
    cfg.wrf_omega_ww_cp = false;       // mode 1 is the authority
    cfg.mu_horizontal_div_only = false;
    cfg.hevi_split = false;
    cfg.implicit_wdamp = true;
    cfg.wrf_w_crit_cfl = static_cast<float>(w_crit_cfl);
    cfg.wrf_zadvect_implicit = 1;
    cfg.omega_w_blend = 1.0f;
    cfg.sign_smooth_delta = 0.0f;
    cfg.retain_graph_for_adjoint = false;
    cfg.use_autograd = false;
    cfg.max_newton_iter = 20;
    cfg.newton_tol = 1.0e-7f;
    cfg.krylov_tol = 1.0e-6f;
    cfg.gmres_restart = 15;
    cfg.max_krylov_iter = 3;
    cfg.stage_fail_action = 1;
    cfg.gmres_warmstart = false;
    cfg.inn_warmstart_enable = false;

    check_map_case(1.0, RhsMode::Full);
    check_map_case(1.3, RhsMode::Full);
    check_map_case(1.0, RhsMode::ImplicitOnly);
    check_map_case(1.3, RhsMode::ImplicitOnly);
    std::cout << "WDAMP_COORD normal exit: PASS actual runtime W-damping correction\n";
    return 0;
}
