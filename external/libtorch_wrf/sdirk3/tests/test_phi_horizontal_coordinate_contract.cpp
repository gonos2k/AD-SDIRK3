// Actual RHS contract for canonical native-W PH horizontal advection.
//
// The fixture is ordinary WRFParity geometry: periodic X and symmetric Y.  The
// current C++ advection_order=5 maps to the Fortran PH order-6 branch here.
// This test deliberately compares RhsMode::Full and HEVI ExplicitOnly on the
// same state, then turns on a horizontal-uniform vertical Phi offset.  The
// ExplicitOnly PH channel must be invariant to that offset (horizontal
// gradients only); Full-Explicit must change by the independent scalar
// calc_ww_cp Omega times the offset's vertical gradient.

#include "tile_test_fixture.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace wrf::sdirk3::test;
using wrf::sdirk3::RhsMode;
using wrf::sdirk3::g_sdirk3_config;

struct RhsTag {
    using type = torch::Tensor (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, RhsMode);
    friend type access(RhsTag);
};
struct CoeffTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*,
                                                    const float*, const float*);
    friend type access(CoeffTag);
};
template<class Tag, typename Tag::type Member> struct Accessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct Accessor<RhsTag, &TileSDIRK3UnifiedSolver::computeUnifiedRHS>;
template struct Accessor<CoeffTag,
                         &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;

constexpr double kPi = 3.1415926535897932384626433832795;
constexpr double kMuBase = 80000.0;
constexpr double kRdx = 1.0 / 100000.0;
constexpr double kRdy = 1.0 / 100000.0;
constexpr double kDnw = -0.25;  // WRF signed dnw; rdnw magnitude is 4.

// Deliberately stretched vertical metric. The last two mass-level rdnw values
// drive the top cfn/cfn1 rule in the candidate; the scalar oracle uses the same
// source values independently.
float rdnw_value(int k) {
    constexpr float rdnw[nz] = {4.0f, 3.5f, 5.25f, 2.75f};
    return rdnw[k];
}
float top_cfn() {
    const float dnw_below = 1.0f / rdnw_value(nz-1);
    const float dnw_below2 = 1.0f / rdnw_value(nz-2);
    const float dn_top = 0.5f * (dnw_below + dnw_below2);
    return (0.5f * dnw_below + dn_top) / dn_top;
}
float top_cfn1() {
    const float dnw_below = 1.0f / rdnw_value(nz-1);
    const float dnw_below2 = 1.0f / rdnw_value(nz-2);
    const float dn_top = 0.5f * (dnw_below + dnw_below2);
    return -0.5f * dnw_below / dn_top;
}

// Non-unit geometry/coefficient fixture. The candidate must consume each
// native face map and the c1f/c2f hybrid factor; unit values would hide wiring
// errors through cancellation.
float mass_map_value(int j, int i) {
    return 1.0f + 0.013f * static_cast<float>(j + 1)
                 + 0.007f * static_cast<float>(i + 1);
}
float u_map_value(int j, int i) {
    return 1.0f + 0.009f * static_cast<float>(j + 1)
                 + 0.005f * static_cast<float>(i + 1);
}
float v_map_value(int j, int i) {
    return 1.0f + 0.011f * static_cast<float>(j + 1)
                 + 0.004f * static_cast<float>(i + 1);
}
float mass_perturbation(int j, int i, int m, int n) {
    const double x = 2.0*kPi*(static_cast<double>(i)+0.37)/n;
    const double y = kPi*(static_cast<double>(j)+0.41)/m;
    return static_cast<float>(900.0 + 135.0*std::sin(x)
                              + 57.0*std::cos(2.0*y)
                              + 23.0*std::sin(x-y));
}
float c1f_value(int k) {
    constexpr float c1f[nw] = {0.91f, 1.07f, 0.83f, 1.19f, 0.97f};
    return c1f[k];
}
float c2f_value(int k) {
    constexpr float c2f[nw] = {2.0f, -3.0f, 4.0f, -1.5f, 2.5f};
    return c2f[k];
}

size_t u_index(int j, int k, int i) { return static_cast<size_t>((j*nz+k)*nu+i); }
size_t v_index(int j, int k, int i) { return static_cast<size_t>((j*nz+k)*nx+i); }
size_t ph_index(int j, int k, int i) { return static_cast<size_t>((j*nw+k)*nx+i); }
size_t mu_index(int j, int i) { return static_cast<size_t>(j*nx+i); }

int periodic(int i, int n) {
    int out = i % n;
    return out < 0 ? out + n : out;
}
int reflected(int j, int n) {
    const int period = 2*n;
    int out = j % period;
    if (out < 0) out += period;
    return out < n ? out : period - 1 - out;
}

double phi_value(const std::vector<double>& phi, int j, int k, int i,
                 int m, int n) {
    return phi[ph_index(reflected(j,m), k, periodic(i,n))];
}
double u_value(const std::vector<double>& u, int j, int k, int i) {
    return u[u_index(j,k,i)];
}
double v_value(const std::vector<double>& v, int j, int k, int i) {
    return v[v_index(j,k,i)];
}

torch::Tensor make_state(const TileCase& tile) {
    std::vector<torch::Tensor> blocks;
    for (auto* field : const_cast<TileCase&>(tile).fields()) {
        blocks.push_back(torch::from_blob(field->data(),
            {static_cast<int64_t>(field->size())},
            torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)).clone());
    }
    return torch::cat(blocks);
}

void configure(TileCase& tile, bool packed) {
    tile.solver.setWRFIndices(
        1, packed ? nx : nx+1, 1, packed ? ny : ny+1, 1, nz,
        1, packed ? nx : nx+1, 1, packed ? ny : ny+1, 1, packed ? nz : nz+1,
        1, packed ? nu : nx+4, 1, packed ? nv : ny+4, 1, nz+1);
    tile.solver.setBoundaryConditions(true, false, false, false, true, true,
                                      false, false, false, false);
    for (int j=0; j<ny; ++j) for (int i=0; i<nx; ++i)
        tile.mass_map[j*nx+i] = mass_map_value(j,i);
    for (int j=0; j<ny; ++j) for (int i=0; i<nu; ++i)
        tile.u_map[j*nu+i] = u_map_value(j,i);
    for (int j=0; j<nv; ++j) for (int i=0; i<nx; ++i)
        tile.v_map[j*nx+i] = v_map_value(j,i);
    // Match WRF set_physical_bc2d's packed aliases.  Periodic X aliases are
    // installed before the reflected-Y row aliases, since the latter copy the
    // complete row.  The full layout still carries the periodic east U face.
    const int m = packed ? ny-1 : ny;
    const int n = packed ? nx-1 : nx;
    if (!packed) {
        for (int j=0; j<ny; ++j)
            tile.u_map[j*nu+nx] = tile.u_map[j*nu+0];
    } else {
        for (int j=0; j<ny; ++j) {
            const int source_j = std::min(j, m-1);
            tile.mass_map[j*nx+n] = tile.mass_map[source_j*nx+0];
            tile.u_map[j*nu+n] = tile.u_map[source_j*nu+0];
            tile.u_map[j*nu+n+1] = tile.u_map[source_j*nu+1];
        }
        for (int j=0; j<nv; ++j) {
            const int source_j = std::min(j, m);
            tile.v_map[j*nx+n] = tile.v_map[source_j*nx+0];
        }
        // Row aliases are copied after the X aliases, as in the WRF packed
        // fixture: U/mass are even; the V map is an even ghost map.
        for (int i=0; i<nx; ++i)
            tile.mass_map[m*nx+i] = tile.mass_map[(m-1)*nx+i];
        for (int i=0; i<nu; ++i) {
            tile.u_map[m*nu+i] = tile.u_map[(m-1)*nu+i];
        }
        for (int i=0; i<nx; ++i)
            tile.v_map[(m+1)*nx+i] = tile.v_map[(m-1)*nx+i];
    }
    TORCH_CHECK(m >= 1 && n >= 1, "actual fixture requires nonempty physical domain");
    if (packed) {
        for (int j=0; j<m; ++j) {
            TORCH_CHECK(tile.mass_map[j*nx+n] == tile.mass_map[j*nx+0],
                        "packed mass-map periodic endpoint mismatch");
            TORCH_CHECK(tile.u_map[j*nu+n] == tile.u_map[j*nu+0] &&
                        tile.u_map[j*nu+n+1] == tile.u_map[j*nu+1],
                        "packed U-map periodic endpoints mismatch");
            TORCH_CHECK(tile.v_map[j*nx+n] == tile.v_map[j*nx+0],
                        "packed V-map periodic endpoint mismatch");
        }
        for (int i=0; i<nx; ++i)
            TORCH_CHECK(tile.mass_map[m*nx+i] == tile.mass_map[(m-1)*nx+i],
                        "packed mass-map reflected row mismatch");
        for (int i=0; i<nu; ++i)
            TORCH_CHECK(tile.u_map[m*nu+i] == tile.u_map[(m-1)*nu+i],
                        "packed U-map reflected row mismatch");
        for (int i=0; i<nx; ++i)
            TORCH_CHECK(tile.v_map[(m+1)*nx+i] == tile.v_map[(m-1)*nx+i],
                        "packed V-map reflected row mismatch");
    } else {
        for (int j=0; j<ny; ++j)
            TORCH_CHECK(tile.u_map[j*nu+nx] == tile.u_map[j*nu+0],
                        "full U-map periodic endpoint mismatch");
    }
    for (int k=0; k<nw; ++k)
        tile.metric[k] = -rdnw_value(std::min(k, nz-1));
    std::fill(tile.one.begin(), tile.one.end(), 1.0f);
    std::fill(tile.zero.begin(), tile.zero.end(), 0.0f);
    std::fill(tile.half.begin(), tile.half.end(), 0.5f);
    // Publish the current indices, maps and metric through the normal fixture ABI.
    tile.step(1.0e-4f);
    TORCH_CHECK(tile.solver.getLastStepOutcomeCode() == 0, "fixture setup step failed");
    tile.solver.getGridInfo()->msfty = torch::from_blob(
        tile.mass_map.data(), {ny,nx}, torch::kFloat32).clone();
    const float c1f[nw] = {0.91f, 1.07f, 0.83f, 1.19f, 0.97f};
    const float c2f[nw] = {2.0f, -3.0f, 4.0f, -1.5f, 2.5f};
    const float c1h[nz] = {1,1,1,1};
    const float c2h[nz] = {0,0,0,0};
    (tile.solver.*access(CoeffTag{}))(c1f,c2f,c1h,c2h);
}

void fill_state(TileCase& tile, bool packed, bool vertical_offset) {
    std::fill(tile.u.begin(), tile.u.end(), 0.0f);
    std::fill(tile.v.begin(), tile.v.end(), 0.0f);
    std::fill(tile.w.begin(), tile.w.end(), 0.0f);
    std::fill(tile.ph.begin(), tile.ph.end(), 0.0f);
    std::fill(tile.theta.begin(), tile.theta.end(), 0.0f);
    std::fill(tile.mu.begin(), tile.mu.end(), 0.0f);
    const int m = packed ? ny-1 : ny;
    const int n = packed ? nx-1 : nx;
    const int u_faces = n+1;
    const int v_faces = m+1;
    for (int j=0; j<m; ++j) {
        for (int i=0; i<n; ++i)
            tile.mu[mu_index(j,i)] = mass_perturbation(j,i,m,n);
        for (int k=0; k<nz; ++k) {
            for (int i=0; i<u_faces; ++i) {
                const double x = 2.0*kPi*static_cast<double>(i)/n;
                const double y = kPi*(static_cast<double>(j)+0.5)/m;
                tile.u[u_index(j,k,i)] = static_cast<float>(
                    0.8 + 0.11*std::sin(x) + 0.07*std::cos(y) + 0.03*k);
            }
        }
        for (int k=0; k<nz; ++k) {
            for (int i=0; i<n; ++i) {
                const double x = 2.0*kPi*(static_cast<double>(i)+0.5)/n;
                const double y = kPi*static_cast<double>(j)/m;
                tile.v[v_index(j,k,i)] = static_cast<float>(
                    0.35*std::sin(y) * (0.9*std::cos(x) + 0.04*k));
            }
        }
        for (int k=0; k<nw; ++k) {
            const double z = static_cast<double>(k);
            const double offset = vertical_offset ? 0.27*z + 0.031*z*z : 0.0;
            for (int i=0; i<n; ++i) {
                const double x = 2.0*kPi*static_cast<double>(i)/n;
                const double y = kPi*(static_cast<double>(j)+0.5)/m;
                tile.ph[ph_index(j,k,i)] = static_cast<float>(
                    0.17*std::sin(x) + 0.13*std::cos(2.0*y)
                    + 0.017*static_cast<double>(j)/m + offset);
            }
        }
    }
    // Wall faces and packed aliases follow the WRF ordinary symmetric-Y contract.
    for (int k=0; k<nz; ++k) {
        for (int i=0; i<n; ++i) {
            tile.v[v_index(0,k,i)] = 0.0f;
            tile.v[v_index(m,k,i)] = 0.0f;
        }
    }
    if (!packed) {
        for (int j=0; j<ny; ++j)
            for (int i=0; i<nx; ++i) {
                tile.mu[mu_index(j,i)] = mass_perturbation(j,i,ny,nx);
            }
        for (int j=0; j<ny; ++j)
            for (int k=0; k<nz; ++k)
                tile.u[u_index(j,k,nx)] = tile.u[u_index(j,k,0)];
        return;
    }
    // The state aliases follow the same order as the map aliases above.
    for (int j=0; j<m; ++j) {
        tile.mu[mu_index(j,n)] = tile.mu[mu_index(j,0)];
        for (int k=0; k<nz; ++k) {
            tile.u[u_index(j,k,n)] = tile.u[u_index(j,k,0)];
            tile.u[u_index(j,k,n+1)] = tile.u[u_index(j,k,1)];
            tile.v[v_index(j,k,n)] = tile.v[v_index(j,k,0)];
        }
        for (int k=0; k<nw; ++k) {
            tile.ph[ph_index(j,k,n)] = tile.ph[ph_index(j,k,0)];
        }
    }
    for (int k=0; k<nz; ++k)
        for (int i=0; i<nu; ++i)
            tile.u[u_index(m,k,i)] = tile.u[u_index(m-1,k,i)];
    for (int i=0; i<nx; ++i)
        tile.mu[mu_index(m,i)] = tile.mu[mu_index(m-1,i)];
    for (int k=0; k<nw; ++k)
        for (int i=0; i<nx; ++i)
            tile.ph[ph_index(m,k,i)] = tile.ph[ph_index(m-1,k,i)];
    for (int k=0; k<nz; ++k) {
        for (int i=0; i<nx; ++i) {
            tile.v[v_index(m,k,i)] = 0.0f;
            tile.v[v_index(m+1,k,i)] = -tile.v[v_index(m-1,k,i)];
        }
    }
}

torch::Tensor run_rhs(bool packed, bool vertical_offset, RhsMode mode, int advection_order) {
    g_sdirk3_config.advection_order = advection_order;
    TileCase tile;
    configure(tile, packed);
    fill_state(tile, packed, vertical_offset);
    if (packed) {
        TORCH_CHECK(tile.ph[ph_index(ny-1, 1, 0)] != tile.ph[ph_index(0, 1, 0)],
                    "packed last physical row must remain distinct from first row");
    }
    return (tile.solver.*access(RhsTag{}))(make_state(tile), mode)
        .detach().to(torch::kCPU, torch::kFloat64).contiguous();
}

struct ScalarFields {
    std::vector<double> u, v, phi, mu;
    int m, n;
};
ScalarFields scalar_fields(bool packed, bool vertical_offset) {
    ScalarFields f{std::vector<double>(static_cast<size_t>(ny*nz*nu),0.0),
                    std::vector<double>(static_cast<size_t>(nv*nz*nx),0.0),
                    std::vector<double>(static_cast<size_t>(ny*nw*nx),0.0),
                    std::vector<double>(static_cast<size_t>(ny*nx),0.0),
                    packed ? ny-1 : ny, packed ? nx-1 : nx};
    const int m=f.m,n=f.n;
    for (int j=0;j<m;++j) {
        for (int i=0;i<n;++i)
            f.mu[mu_index(j,i)] = mass_perturbation(j,i,m,n);
        for (int k=0;k<nz;++k) for (int i=0;i<=n;++i) {
            const double x=2.0*kPi*static_cast<double>(i)/n;
            const double y=kPi*(static_cast<double>(j)+0.5)/m;
            f.u[u_index(j,k,i)] = 0.8 + 0.11*std::sin(x)+0.07*std::cos(y)+0.03*k;
        }
        for (int k=0;k<nz;++k) for (int i=0;i<n;++i) {
            const double x=2.0*kPi*(static_cast<double>(i)+0.5)/n;
            const double y=kPi*static_cast<double>(j)/m;
            f.v[v_index(j,k,i)] = 0.35*std::sin(y)*(0.9*std::cos(x)+0.04*k);
        }
        for (int k=0;k<nw;++k) for (int i=0;i<n;++i) {
            const double z=static_cast<double>(k);
            const double offset=vertical_offset ? 0.27*z+0.031*z*z : 0.0;
            const double x=2.0*kPi*static_cast<double>(i)/n;
            const double y=kPi*(static_cast<double>(j)+0.5)/m;
            f.phi[ph_index(j,k,i)] = 0.17*std::sin(x)+0.13*std::cos(2.0*y)
                                    + 0.017*static_cast<double>(j)/m + offset;
        }
    }
    for (int k=0;k<nz;++k) for (int i=0;i<n;++i) {
        f.v[v_index(0,k,i)]=0.0;
        f.v[v_index(m,k,i)]=0.0;
    }
    if (packed) {
        // The packed native-W contract supplies the periodic east U face as
        // the west-face alias.  It participates in the last physical-cell
        // X flux, so the scalar oracle must carry that alias explicitly.
        for (int j=0; j<m; ++j) {
            f.mu[mu_index(j,n)] = f.mu[mu_index(j,0)];
            for (int k=0; k<nz; ++k) {
                f.u[u_index(j,k,n)] = f.u[u_index(j,k,0)];
                f.u[u_index(j,k,n+1)] = f.u[u_index(j,k,1)];
                f.v[v_index(j,k,n)] = f.v[v_index(j,k,0)];
            }
            for (int k=0; k<nw; ++k)
                f.phi[ph_index(j,k,n)] = f.phi[ph_index(j,k,0)];
        }
        for (int i=0; i<nx; ++i)
            f.mu[mu_index(m,i)] = f.mu[mu_index(m-1,i)];
        for (int k=0; k<nz; ++k)
            for (int i=0; i<nu; ++i)
                f.u[u_index(m,k,i)] = f.u[u_index(m-1,k,i)];
        for (int k=0; k<nw; ++k)
            for (int i=0; i<nx; ++i)
                f.phi[ph_index(m,k,i)] = f.phi[ph_index(m-1,k,i)];
    }
    if (!packed) {
        for (int j=0; j<ny; ++j) for (int i=0; i<nx; ++i)
            f.mu[mu_index(j,i)] = mass_perturbation(j,i,ny,nx);
        for (int j=0; j<ny; ++j) for (int k=0; k<nz; ++k)
            f.u[u_index(j,k,nx)] = f.u[u_index(j,k,0)];
    }
    return f;
}

// The scalar oracle reads the same native face endpoints as the candidate.
// Full tiles retain the periodic east-U endpoint; packed tiles additionally
// carry U's second pad column and the reflected Y rows in the input ABI.
float u_map_f(const ScalarFields& f, int j, int face) {
    if (face == f.n) return u_map_value(j, 0);
    if (f.n < nx && face == f.n+1) return u_map_value(j, 1);
    return u_map_value(j, face);
}
float v_map_f(const ScalarFields& f, int face, int i) {
    if (f.n < nx && i == f.n) return v_map_value(face, 0);
    return v_map_value(face, i);
}
float full_mass_at(const ScalarFields& f, int j, int i) {
    const int jj = std::min(std::max(j, 0), f.m-1);
    const int ii = periodic(i, f.n);
    return kMuBase + static_cast<float>(f.mu[mu_index(jj,ii)]);
}

const std::vector<float>& fixture_base_phi() {
    static const std::vector<float> base = [] {
        const auto p_column = torch::linspace(90000.0f, 30000.0f, nz, torch::kFloat32);
        const auto alpha_column = wrf::sdirk3::compute_inverse_density(
            torch::full_like(p_column, 300.0f), p_column, 287.0f, 717.5f, 1004.5f, 100000.0f);
        const std::vector<float> alpha(alpha_column.data_ptr<float>(),
                                       alpha_column.data_ptr<float>() + nz);
        return wrf::sdirk3::integrate_phb_hydrostatic(
            std::vector<float>(nz, -nz), alpha, std::vector<float>(nz, 1.0f),
            std::vector<float>(nz, 0.0f), 80000.0f, 0.0f);
    }();
    return base;
}

double phi_at(const ScalarFields& f, int j, int k, int i) {
    const auto jj = reflected(j,f.m);
    const auto ii = periodic(i,f.n);
    // The candidate forms ph+phb in float32 before the horizontal difference.
    // Preserve that cancellation in this independent scalar oracle.
    const float perturbation = static_cast<float>(f.phi[ph_index(jj,k,ii)]);
    return static_cast<double>(perturbation + fixture_base_phi().at(k));
}
double centered_x(const ScalarFields& f, int j, int k, int i, int order) {
    auto at=[&](int ii){return phi_at(f,j,k,ii);};
    if (order == 2) return at(i)-at(i-1);
    if (order == 4) return (8.0*(at(i+1)-at(i-1))-(at(i+2)-at(i-2)))/12.0;
    TORCH_CHECK(order == 6, "scalar PH oracle order must be 2, 4 or 6");
    return (45.0*(at(i+1)-at(i-1))-9.0*(at(i+2)-at(i-2))
            +(at(i+3)-at(i-3)))/60.0;
}
double centered_y(const ScalarFields& f, int j, int k, int i, int order) {
    auto at=[&](int jj){return phi_at(f,jj,k,i);};
    if (order == 2) return at(j)-at(j-1);
    if (order == 4) return (8.0*(at(j+1)-at(j-1))-(at(j+2)-at(j-2)))/12.0;
    TORCH_CHECK(order == 6, "scalar PH oracle order must be 2, 4 or 6");
    return (45.0*(at(j+1)-at(j-1))-9.0*(at(j+2)-at(j-2))
            +(at(j+3)-at(j-3)))/60.0;
}
double xvel(const ScalarFields& f, int j, int k, int face) {
    if (k==nz) return 1.5*f.u[u_index(j,nz-1,face)]
                    -0.5*f.u[u_index(j,nz-2,face)];
    return f.u[u_index(j,k,face)]+f.u[u_index(j,k-1,face)];
}
double yvel(const ScalarFields& f, int face, int k, int i) {
    if (k==nz) return 1.5*f.v[v_index(face,nz-1,i)]
                    -0.5*f.v[v_index(face,nz-2,i)];
    return f.v[v_index(face,k,i)]+f.v[v_index(face,k-1,i)];
}

float phi_at_f(const ScalarFields& f, int j, int k, int i) {
    const auto jj = reflected(j,f.m);
    const auto ii = periodic(i,f.n);
    const float perturbation = static_cast<float>(f.phi[ph_index(jj,k,ii)]);
    return perturbation + fixture_base_phi().at(k);
}
float centered_x_f(const ScalarFields& f, int j, int k, int i, int order) {
    auto at=[&](int ii){return phi_at_f(f,j,k,ii);};
    if (order == 2) return at(i)-at(i-1);
    if (order == 4) return (8.0f*(at(i+1)-at(i-1))-(at(i+2)-at(i-2)))/12.0f;
    TORCH_CHECK(order == 6, "scalar PH oracle order must be 2, 4 or 6");
    return (45.0f*(at(i+1)-at(i-1))-9.0f*(at(i+2)-at(i-2))
            +(at(i+3)-at(i-3)))/60.0f;
}
float centered_y_f(const ScalarFields& f, int j, int k, int i, int order) {
    auto at=[&](int jj){return phi_at_f(f,jj,k,i);};
    if (order == 2) return at(j)-at(j-1);
    if (order == 4) return (8.0f*(at(j+1)-at(j-1))-(at(j+2)-at(j-2)))/12.0f;
    TORCH_CHECK(order == 6, "scalar PH oracle order must be 2, 4 or 6");
    return (45.0f*(at(j+1)-at(j-1))-9.0f*(at(j+2)-at(j-2))
            +(at(j+3)-at(j-3)))/60.0f;
}
float xvel_f(const ScalarFields& f, int j, int k, int face) {
    if (k==nz) return top_cfn()*static_cast<float>(f.u[u_index(j,nz-1,face)])
                    + top_cfn1()*static_cast<float>(f.u[u_index(j,nz-2,face)]);
    return static_cast<float>(f.u[u_index(j,k,face)])+
           static_cast<float>(f.u[u_index(j,k-1,face)]);
}
float yvel_f(const ScalarFields& f, int face, int k, int i) {
    if (k==nz) return top_cfn()*static_cast<float>(f.v[v_index(face,nz-1,i)])
                    + top_cfn1()*static_cast<float>(f.v[v_index(face,nz-2,i)]);
    return static_cast<float>(f.v[v_index(face,k,i)])+
           static_cast<float>(f.v[v_index(face,k-1,i)]);
}

float mass_u_f(const ScalarFields& f, int j, int face) {
    return 0.5f * (full_mass_at(f,j,face-1) + full_mass_at(f,j,face));
}
float mass_v_f(const ScalarFields& f, int face, int i) {
    if (face <= 0) return full_mass_at(f,0,i);
    if (face >= f.m) return full_mass_at(f,f.m-1,i);
    return 0.5f * (full_mass_at(f,face-1,i) + full_mass_at(f,face,i));
}

// Raw coupled PH horizontal operator, before the final float32 alpha_w
// conversion.  This keeps the independent formula separate from the tensor
// helper while preserving the candidate's declared arithmetic ordering.
std::vector<float> horizontal_raw_oracle(const ScalarFields& f, int phi_order) {
    std::vector<float> out(static_cast<size_t>(f.m*nw*nx),0.0f);
    for (int j=0;j<f.m;++j) for (int k=1;k<=nz;++k) for (int i=0;i<f.n;++i) {
        const float fac=(k==nz)?0.5f:0.25f;
        float raw_x=0.0f, raw_y=0.0f;
        if (phi_order == 2) {
            auto gx=[&](int face){ return phi_at_f(f,j,k,face)-phi_at_f(f,j,k,face-1); };
            auto gy=[&](int face){ return phi_at_f(f,face,k,i)-phi_at_f(f,face-1,k,i); };
            auto fx=[&](int face){
                return 0.25f * (c1f_value(k)*mass_u_f(f,j,face)
                                + c2f_value(k)) * xvel_f(f,j,k,face)
                       * u_map_f(f,j,face) * gx(face);
            };
            auto fy=[&](int face){
                return 0.25f * (c1f_value(k)*mass_v_f(f,face,i)
                                + c2f_value(k)) * yvel_f(f,face,k,i)
                       * v_map_f(f,face,i) * gy(face);
            };
            const float top=(k==nz)?2.0f:1.0f;
            raw_x = -1.0e-5f*top*(fx(i+1)+fx(i)) / mass_map_value(j,i);
            raw_y = -1.0e-5f*top*(fy(j+1)+fy(j)) / mass_map_value(j,i);
        } else {
            const float gx=centered_x_f(f,j,k,i,phi_order);
            const float gy=centered_y_f(f,j,k,i,phi_order);
            const float xface =
                (c1f_value(k)*mass_u_f(f,j,i) + c2f_value(k))
                    * xvel_f(f,j,k,i) * u_map_f(f,j,i)
              + (c1f_value(k)*mass_u_f(f,j,i+1) + c2f_value(k))
                    * xvel_f(f,j,k,i+1) * u_map_f(f,j,i+1);
            const float yface =
                (c1f_value(k)*mass_v_f(f,j,i) + c2f_value(k))
                    * yvel_f(f,j,k,i) * v_map_f(f,j,i)
              + (c1f_value(k)*mass_v_f(f,j+1,i) + c2f_value(k))
                    * yvel_f(f,j+1,k,i) * v_map_f(f,j+1,i);
            raw_x = -fac*1.0e-5f*xface*gx / mass_map_value(j,i);
            raw_y = -fac*1.0e-5f*yface*gy / mass_map_value(j,i);
        }
        out[ph_index(j,k,i)] = raw_x + raw_y;
    }
    return out;
}

// Independent scalar transcription of native-W PH, after the existing
// canonical PH conversion msfty/(c1f*M+c2f).
std::vector<double> horizontal_oracle(const ScalarFields& f, int phi_order) {
    const auto raw = horizontal_raw_oracle(f, phi_order);
    std::vector<double> out(raw.size(),0.0);
    for (int j=0; j<f.m; ++j) for (int k=0; k<nw; ++k) for (int i=0; i<f.n; ++i) {
        const size_t q=ph_index(j,k,i);
        const float alpha_inv = mass_map_value(j,i) /
            (c1f_value(k)*full_mass_at(f,j,i) + c2f_value(k));
        out[q] = static_cast<double>(raw[q] * alpha_inv);
    }
    return out;
}

// Independent scalar calc_ww_cp recurrence. It uses the exact signed WRF dnw,
// c1h=1/c2h=0, and the non-unit native map factors installed in the fixture.
std::vector<float> omega_oracle(const ScalarFields& f) {
    std::vector<float> omega(static_cast<size_t>(f.m*nw*nx),0.0f);
    for (int j=0;j<f.m;++j) for (int i=0;i<f.n;++i) {
        float dmdt=0.0f;
        float divv[nz] = {};
        for (int k=0;k<nz;++k) {
            const float muu_e=mass_u_f(f,j,i+1);
            const float muu_w=mass_u_f(f,j,i);
            const float muv_n=mass_v_f(f,j+1,i);
            const float muv_s=mass_v_f(f,j,i);
            const float cu_e=muu_e*static_cast<float>(f.u[u_index(j,k,i+1)])
                              /u_map_f(f,j,i+1);
            const float cu_w=muu_w*static_cast<float>(f.u[u_index(j,k,i)])
                              /u_map_f(f,j,i);
            const float cv_n=muv_n*static_cast<float>(f.v[v_index(j+1,k,i)])
                              /v_map_f(f,j+1,i);
            const float cv_s=muv_s*static_cast<float>(f.v[v_index(j,k,i)])
                              /v_map_f(f,j,i);
            divv[k]=mass_map_value(j,i)*(-1.0f/rdnw_value(k)) *
                    (1.0e-5f*(cu_e-cu_w)+1.0e-5f*(cv_n-cv_s));
            dmdt+=divv[k];
        }
        for (int k=0;k<nz-1;++k)
            omega[ph_index(j,k+1,i)] = omega[ph_index(j,k,i)]
                                      + (1.0f/rdnw_value(k))*dmdt - divv[k];
    }
    return omega;
}

std::vector<float> vertical_raw_oracle(const ScalarFields& f) {
    const auto omega=omega_oracle(f);
    std::vector<float> out(static_cast<size_t>(f.m*nw*nx),0.0f);
    for (int j=0;j<f.m;++j) for (int k=1;k<nz;++k) for (int i=0;i<f.n;++i) {
        // Match the candidate's float32 (ph+phb) vertical differences.  The
        // offset delta is taken between two complete vertical operators below;
        // this retains the actual base-state cancellation roundoff.
        const float phi_upper = static_cast<float>(phi_at(f,j,k+1,i));
        const float phi_here = static_cast<float>(phi_at(f,j,k,i));
        const float phi_lower = static_cast<float>(phi_at(f,j,k-1,i));
        const float upper=-rdnw_value(k)*(phi_upper-phi_here);
        const float lower=-rdnw_value(k-1)*(phi_here-phi_lower);
        const float weighted = 0.5f*upper + 0.5f*lower;
        const float raw = -omega[ph_index(j,k,i)] * weighted;
        out[ph_index(j,k,i)] = raw;
    }
    return out;
}

std::vector<double> vertical_oracle(const ScalarFields& f) {
    const auto raw = vertical_raw_oracle(f);
    std::vector<double> out(raw.size(),0.0);
    for (int j=0; j<f.m; ++j) for (int k=0; k<nw; ++k) for (int i=0; i<f.n; ++i) {
        const size_t q=ph_index(j,k,i);
        const float alpha_inv = mass_map_value(j,i) /
            (c1f_value(k)*full_mass_at(f,j,i) + c2f_value(k));
        out[q] = static_cast<double>(raw[q] * alpha_inv);
    }
    return out;
}

torch::Tensor ph_block(const torch::Tensor& rhs) {
    return rhs.slice(0, su+sv+sw, su+sv+2*sw).view({ny,nw,nx});
}
torch::Tensor expand_expected(const std::vector<double>& core, bool packed) {
    const int m=packed?ny-1:ny, n=packed?nx-1:nx;
    auto out=torch::zeros({ny,nw,nx},torch::TensorOptions().dtype(torch::kFloat64));
    auto a=out.accessor<double,3>();
    for (int j=0;j<m;++j) for (int k=0;k<nw;++k) for (int i=0;i<n;++i)
        a[j][k][i]=core[ph_index(j,k,i)];
    if (!packed) return out;
    for (int j=0;j<m;++j) for (int k=0;k<nw;++k)
        a[j][k][n]=a[j][k][0];
    for (int k=0;k<nw;++k) for (int i=0;i<nx;++i)
        a[m][k][i]=a[m-1][k][i];
    return out;
}

double max_abs(const torch::Tensor& x) { return x.abs().max().item<double>(); }

torch::Tensor delta(const torch::Tensor& a, const torch::Tensor& b) { return a-b; }

int phi_order_for_advection(int advection_order) {
    switch (advection_order) {
        case 1:
        case 2: return 2;
        case 3: return 4;
        case 5: return 6;
        default:
            TORCH_CHECK(false,
                        "actual PH matrix uses explicit C++ advection_order 1, 2, 3 or 5; got ",
                        advection_order);
    }
}

double nonzero_signal(const torch::Tensor& x, const char* name) {
    const double s=max_abs(x);
    TORCH_CHECK(std::isfinite(s) && s > 1.0e-10, name, " witness is zero/nonfinite: ", s);
    return s;
}

double actual_ad_check(bool packed, int advection_order) {
    g_sdirk3_config.advection_order = advection_order;
    TileCase tile;
    configure(tile, packed);
    fill_state(tile, packed, false);
    const auto state = make_state(tile).set_requires_grad(true);
    // Exact binary perturbations remain representable after PH+PHB in FP32.
    constexpr float direction_amplitude = 0.5f;
    constexpr double fd_step = 0.25;
    const int64_t ph0 = su + sv + sw;
    auto direction = torch::zeros_like(state);
    auto direction_view = direction.slice(0, ph0, ph0 + sw).view({ny,nw,nx});
    // Use a neighboring X point so the centered order-4/6 derivatives have
    // a nonzero diagonal witness as well as the order-2 face stencil.
    direction_view.index_put_({2, 2, 2}, direction_amplitude);
    auto seed = torch::zeros_like(state);
    auto seed_view = seed.slice(0, ph0, ph0 + sw).view({ny,nw,nx});
    seed_view.index_put_({2, 2, 3}, 1.0e5f);
    const auto rhs = (tile.solver.*access(RhsTag{}))(state, RhsMode::ExplicitOnly);
    const auto objective = (rhs * seed).sum();
    const auto grad = torch::autograd::grad({objective}, {state})[0];
    const double ad = (grad * direction).sum().item<double>();

    const auto eval = [&](double step) {
        TileCase t;
        configure(t, packed);
        fill_state(t, packed, false);
        const auto perturbed = (state.detach() + step * direction).to(torch::kFloat32);
        t.set(perturbed);
        const auto out = (t.solver.*access(RhsTag{}))(make_state(t), RhsMode::ExplicitOnly);
        return (out * seed).sum().item<double>();
    };
    const double fd = (eval(fd_step) - eval(-fd_step)) / (2.0 * fd_step);
    const auto scalar = scalar_fields(packed, false);
    const float ph_initial = static_cast<float>(scalar.phi[ph_index(2,2,2)]);
    const float phb0 = fixture_base_phi().at(2);
    const float represented_plus = static_cast<float>(
        static_cast<float>(ph_initial + direction_amplitude * static_cast<float>(fd_step)) + phb0);
    const float represented_minus = static_cast<float>(
        static_cast<float>(ph_initial - direction_amplitude * static_cast<float>(fd_step)) + phb0);
    const double represented_slope =
        static_cast<double>(represented_plus - represented_minus) / (2.0 * fd_step);
    const double scale = std::max(std::abs(ad), std::abs(fd));
    TORCH_CHECK(std::isfinite(ad) && std::isfinite(fd) && std::abs(ad) > 1.0e-8 &&
                std::abs(fd) > 1.0e-8,
                "PH actual RHS AD witness is zero/nonfinite: order=", advection_order,
                " packed=", packed, " AD=", ad, " FD=", fd);
    TORCH_CHECK(std::isfinite(represented_slope) && std::abs(represented_slope) > 1.0e-8,
                "PH actual RHS AD perturbation is not represented in float32 ph+phb: order=",
                advection_order, " packed=", packed, " amp=", direction_amplitude,
                " step=", fd_step, " represented_slope=", represented_slope);
    TORCH_CHECK(std::abs(ad-fd) <= 1.0e-6*scale,
                "PH actual RHS AD directional mismatch: order=", advection_order,
                " packed=", packed, " amp=", direction_amplitude,
                " step=", fd_step, " represented_slope=", represented_slope,
                " AD=", ad, " FD=", fd,
                " rel=", std::abs(ad-fd)/scale);
    std::cout << "PH_AD packed=" << (packed?1:0)
              << " advection_order=" << advection_order
              << " amp=" << direction_amplitude << " step=" << fd_step
              << " represented_slope=" << represented_slope
              << " reverse=" << ad << " central=" << fd
              << " rel=" << std::abs(ad-fd)/scale << " PASS\n";
    return ad;
}

void run_one(bool packed, int advection_order) {
    const int phi_order=phi_order_for_advection(advection_order);
    const auto full_off=ph_block(run_rhs(packed,false,RhsMode::Full,advection_order));
    const auto full_on =ph_block(run_rhs(packed,true ,RhsMode::Full,advection_order));
    const auto exp_off =ph_block(run_rhs(packed,false,RhsMode::ExplicitOnly,advection_order));
    const auto exp_on  =ph_block(run_rhs(packed,true ,RhsMode::ExplicitOnly,advection_order));
    const auto f=scalar_fields(packed,false);
    const auto f_offset=scalar_fields(packed,true);
    const auto raw_h=horizontal_raw_oracle(f,phi_order);
    const auto raw_h_offset=horizontal_raw_oracle(f_offset,phi_order);
    const auto raw_z=vertical_raw_oracle(f);
    const auto raw_z_offset=vertical_raw_oracle(f_offset);
    double mass_signal = 0.0;
    for (int j=0; j<f.m; ++j) for (int i=0; i<f.n; ++i)
        mass_signal = std::max(mass_signal,
                               std::abs(static_cast<double>(full_mass_at(f,j,i)) - kMuBase));
    TORCH_CHECK(std::isfinite(mass_signal) && mass_signal > 1.0,
                "variable-mass actual fixture has no nonzero witness: ", mass_signal);
    std::vector<double> h_core(raw_h.size()), h_offset_core(raw_h.size());
    std::vector<double> vo_core(raw_h.size()), full_core(raw_h.size()), split_core(raw_h.size());
    for (size_t q=0; q<raw_h.size(); ++q) {
        const int k = static_cast<int>((q / static_cast<size_t>(nx)) % nw);
        const int i = static_cast<int>(q % static_cast<size_t>(nx));
        const int j = static_cast<int>(q / static_cast<size_t>(nw*nx));
        const float alpha_inv = mass_map_value(j,i) /
            (c1f_value(k)*full_mass_at(f,j,i) + c2f_value(k));
        // ExplicitOnly exposes h_raw*alpha_inv. Full assembles h_raw+z_raw
        // before the same float32 conversion; this is the independent
        // operator comparison, including only declared float32 rounding.
        const float h0=raw_h[q]*alpha_inv, h1=raw_h_offset[q]*alpha_inv;
        const float z0=raw_z[q]*alpha_inv, z1=raw_z_offset[q]*alpha_inv;
        const float full0=(raw_h[q]+raw_z[q])*alpha_inv;
        const float full1=(raw_h_offset[q]+raw_z_offset[q])*alpha_inv;
        const float slow0=full0-h0, slow1=full1-h1;
        h_core[q]=static_cast<double>(h0);
        h_offset_core[q]=static_cast<double>(h1);
        vo_core[q]=static_cast<double>(z1-z0);
        full_core[q]=static_cast<double>(full1-full0);
        split_core[q]=static_cast<double>(slow1-slow0);
    }
    const auto h=expand_expected(h_core,packed);
    const auto h_offset=expand_expected(h_offset_core,packed);
    const auto vo=expand_expected(vo_core,packed);
    const auto full_expected=expand_expected(full_core,packed);
    const auto split_expected=expand_expected(split_core,packed);
    const auto explicit_offset_delta=exp_on-exp_off;
    const auto full_offset_delta=full_on-full_off;
    const auto full_explicit_offset_delta=(full_on-exp_on)-(full_off-exp_off);
    const auto h_err=(exp_off-h).abs();
    const auto h_offset_err=(exp_on-h_offset).abs();
    const auto inv_err=(explicit_offset_delta-(h_offset-h)).abs();
    const auto full_err=(full_offset_delta-full_expected).abs();
    const auto split_err=(full_explicit_offset_delta-split_expected).abs();
    TORCH_CHECK(torch::isfinite(full_off).all().item<bool>() &&
                torch::isfinite(full_on).all().item<bool>() &&
                torch::isfinite(exp_off).all().item<bool>() &&
                torch::isfinite(exp_on).all().item<bool>(),
                "nonfinite actual PH RHS");
    const double h_signal=nonzero_signal(h,"horizontal");
    const double h_offset_signal=nonzero_signal(h_offset,"horizontal offset");
    const double vertical_signal=nonzero_signal(vo,"vertical Omega");
    const double full_vertical_signal=nonzero_signal(full_expected,"Full vertical");
    const double split_vertical_signal=nonzero_signal(split_expected,"Full/Explicit vertical");
    const double observed_split_signal=nonzero_signal(full_explicit_offset_delta,
                                                       "Full/Explicit vertical");
    const double eps=std::numeric_limits<float>::epsilon();
    // The fixture is float32 at the actual-RHS boundary.  Use a per-cell
    // budget tied to every compared value, including the large base-state
    // vertical term whose float32 conversion is later differenced.  This is
    // several ULPs at the value being compared, rather than a scale-free
    // absolute tolerance that could hide a wrong stencil.
    const auto h_budget = 8.0*eps * (exp_off.abs()+h.abs()+exp_on.abs()+h_offset.abs())
                        + 4.0*eps*1.0e-7;
    const auto v_budget = 8.0*eps * (full_off.abs()+full_expected.abs()+full_on.abs()
                                    +full_explicit_offset_delta.abs()+split_expected.abs())
                        + 4.0*eps*1.0e-7;
    TORCH_CHECK((h_err <= h_budget).all().item<bool>() &&
                (h_offset_err <= h_budget).all().item<bool>(),
                "actual ExplicitOnly scalar PH oracle mismatch: order=", advection_order,
                " packed=", packed, " max=", h_err.max().item<double>(),
                " offset_max=", h_offset_err.max().item<double>(),
                " budget_max=", h_budget.max().item<double>(), " signal=", h_signal);
    TORCH_CHECK((inv_err <= h_budget).all().item<bool>(),
                "horizontal RHS changed under vertical-only Phi offset: order=", advection_order,
                " packed=", packed, " max=", inv_err.max().item<double>(),
                " budget_max=", h_budget.max().item<double>());
    TORCH_CHECK((full_err <= v_budget).all().item<bool>(),
                "Full offset delta disagrees with scalar Omega vertical oracle: order=", advection_order,
                " packed=", packed, " max=", full_err.max().item<double>(),
                " budget_max=", v_budget.max().item<double>(), " signal=", full_vertical_signal);
    TORCH_CHECK((split_err <= v_budget).all().item<bool>(),
                "Full/Explicit offset delta disagrees with scalar Omega vertical oracle: order=", advection_order,
                " packed=", packed, " max=", split_err.max().item<double>(),
                " budget_max=", v_budget.max().item<double>(), " signal=", split_vertical_signal);
    std::cout << "PH_ACTUAL packed=" << (packed?1:0)
              << " advection_order=" << advection_order << " mapped_phi_order=" << phi_order
              << " mass_signal=" << mass_signal
              << " horizontal_signal=" << h_signal
              << " vertical_signal=" << vertical_signal
              << " explicit_oracle_max=" << h_err.max().item<double>()
              << " explicit_offset_max=" << inv_err.max().item<double>()
              << " full_offset_delta_max=" << full_err.max().item<double>()
              << " full_minus_explicit_delta_max=" << split_err.max().item<double>()
              << " budgets_max=" << h_budget.max().item<double>() << ","
              << v_budget.max().item<double>()
              << " PASS\n";
}

} // namespace

int main() {
    torch::set_num_threads(1);
    auto& cfg=g_sdirk3_config;
    cfg=wrf::sdirk3::SDIRK3Config{};
    cfg.debug_level=0;
    cfg.imex_split_mode=1;
    cfg.imex_enabled=false;
    cfg.hevi_split=true;
    cfg.mass_coordinate_mode=1;       // WRFParity: exact calc_ww_cp Omega.
    cfg.advection_order=5;            // C++ 5 -> Fortran PH order 6 in candidate.
    cfg.buoyancy_use_current_w=true;
    cfg.wrf_w_damping=0;
    cfg.implicit_wdamp=false;
    cfg.coriolis_f=0.0f;
    std::cout << "PH_TOP rdnw_km1=" << rdnw_value(nz-1)
              << " rdnw_km2=" << rdnw_value(nz-2)
              << " cfn=" << top_cfn() << " cfn1=" << top_cfn1() << " PASS\n";
    for (const int advection_order : {1,2,3,5}) {
        for (const bool packed : {false,true}) {
            run_one(packed, advection_order);
            actual_ad_check(packed, advection_order);
        }
    }
    return 0;
}
