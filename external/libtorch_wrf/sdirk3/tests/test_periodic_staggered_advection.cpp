#include "../wrf_sdirk3_tile_unified.h"
#include "../wrf_sdirk3_config.h"

#include <torch/torch.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

// Test-only access to the production packed helper.  The helper remains
// private and the test does not add a production-facing API.
struct PackedMomentumXTag {
    using type = std::pair<torch::Tensor, torch::Tensor>
        (TileSDIRK3UnifiedSolver::*)(const torch::Tensor&, const torch::Tensor&,
                                     const torch::Tensor&, float, bool);
    friend type access(PackedMomentumXTag);
};
template<typename Tag, typename Tag::type Member> struct MethodAccessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct MethodAccessor<PackedMomentumXTag,
                               &TileSDIRK3UnifiedSolver::advectPackedPeriodicMomentumX>;

struct CoordinateTag {
    using type = void (TileSDIRK3UnifiedSolver::*)(const float*, const float*,
                                                    const float*, const float*);
    friend type access(CoordinateTag);
};
template struct MethodAccessor<CoordinateTag,
                               &TileSDIRK3UnifiedSolver::setVerticalCoordinateCoefficients>;

struct MsfUxTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(MsfUxTag);
};
template<typename Tag, typename Tag::type Member> struct DataAccessor {
    friend typename Tag::type access(Tag) { return Member; }
};
template struct DataAccessor<MsfUxTag, &TileSDIRK3UnifiedSolver::msfux_>;

struct MsfUyTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(MsfUyTag);
};
template struct DataAccessor<MsfUyTag, &TileSDIRK3UnifiedSolver::msfuy_>;

struct MsfVxTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(MsfVxTag);
};
template struct DataAccessor<MsfVxTag, &TileSDIRK3UnifiedSolver::msfvx_>;

struct MsfVyTag {
    using type = torch::Tensor TileSDIRK3UnifiedSolver::*;
    friend type access(MsfVyTag);
};
template struct DataAccessor<MsfVyTag, &TileSDIRK3UnifiedSolver::msfvy_>;

namespace {

// Production packed contract: N physical mass columns, one mass endpoint,
// U has the seam plus the second pad column, and V has one x endpoint.
constexpr int kN = 40;
constexpr int kM = 2;          // true mass rows; packed mass rows = 3
constexpr int kMassX = kN + 1;
constexpr int kPackedU = kN + 2;
constexpr int kPackedV = kN + 1;
constexpr int kPackedMassY = kM + 1;
constexpr int kPackedVY = kM + 2;
constexpr int kNz = 2;
constexpr int kNzW = kNz + 1;
constexpr float kRdx = 0.37f;

int wrap_x(int i) {
    i %= kN;
    return i < 0 ? i + kN : i;
}

float sign_wrf(float velocity) {
    return velocity > 0.0f ? 1.0f : velocity < 0.0f ? -1.0f : 0.0f;
}

float flux5(float qm3, float qm2, float qm1, float qi,
            float qp1, float qp2, float velocity) {
    const float flux6 = (37.0f * (qi + qm1) - 8.0f * (qp1 + qm2) +
                         (qp2 + qm3)) / 60.0f;
    const float upwind = sign_wrf(velocity) *
        ((qp2 - qm3) - 5.0f * (qp1 - qm2) + 10.0f * (qi - qm1)) / 60.0f;
    return velocity * (flux6 - upwind);
}

float flux3(float qm2, float qm1, float qi, float qp1, float velocity) {
    const float flux4 = (7.0f * (qi + qm1) - (qp1 + qm2)) / 12.0f;
    const float upwind = sign_wrf(velocity) *
        ((qp1 - qm2) - 3.0f * (qi - qm1)) / 12.0f;
    return velocity * (flux4 + upwind);
}

float flux2(float qm1, float qi, float velocity) {
    return 0.5f * velocity * (qi + qm1);
}

float flux(int order, const std::vector<float>& q, int face, float velocity) {
    const auto at = [&](int i) { return q[wrap_x(i)]; };
    if (order >= 5) {
        return flux5(at(face - 3), at(face - 2), at(face - 1), at(face),
                     at(face + 1), at(face + 2), velocity);
    }
    if (order >= 3) {
        return flux3(at(face - 2), at(face - 1), at(face), at(face + 1),
                     velocity);
    }
    return flux2(at(face - 1), at(face), velocity);
}

size_t u_index(int j, int k, int i) {
    return (static_cast<size_t>(j) * kNz + k) * kPackedU + i;
}

size_t v_index(int j, int k, int i) {
    return (static_cast<size_t>(j) * kNz + k) * kPackedV + i;
}

size_t mass_index(int j, int i) {
    return static_cast<size_t>(j) * kMassX + i;
}

size_t true_mass_index(int j, int i) {
    return static_cast<size_t>(j) * kN + i;
}

size_t true_u_index(int j, int k, int i) {
    return (static_cast<size_t>(j) * kNz + k) * kN + i;
}

size_t true_v_index(int j, int k, int i) {
    return (static_cast<size_t>(j) * kNz + k) * kN + i;
}

struct Coefficients {
    std::vector<float> c1h;
    std::vector<float> c2h;
};

struct Fields {
    std::vector<float> u;       // [packed_mass_y, nz, N+2]
    std::vector<float> v;       // [packed_v_y, nz, N+1]
    std::vector<float> mu_full; // [packed_mass_y, N+1]
    std::vector<float> mu_true; // [true_mass_y, N]
    std::vector<float> msfux;   // [packed_mass_y, N+2]
    std::vector<float> msfuy;   // [packed_mass_y, N+2]
    std::vector<float> msfvx;   // [packed_v_y, N+1]
    std::vector<float> msfvy;   // [packed_v_y, N+1]
};

Fields make_fields(float transport_sign, bool varying_mu, bool constant_field,
                   bool varying_maps) {
    Fields f;
    f.u.resize(static_cast<size_t>(kPackedMassY) * kNz * kPackedU);
    f.v.resize(static_cast<size_t>(kPackedVY) * kNz * kPackedV);
    f.mu_full.resize(static_cast<size_t>(kPackedMassY) * kMassX);
    f.mu_true.resize(static_cast<size_t>(kM) * kN);
    f.msfux.resize(static_cast<size_t>(kPackedMassY) * kPackedU);
    f.msfuy.resize(static_cast<size_t>(kPackedMassY) * kPackedU);
    f.msfvx.resize(static_cast<size_t>(kPackedVY) * kPackedV);
    f.msfvy.resize(static_cast<size_t>(kPackedVY) * kPackedV);

    constexpr float pi = 3.14159265358979323846f;
    for (int j = 0; j < kM; ++j) {
        for (int i = 0; i < kN; ++i) {
            const float phase = 2.0f * pi * i / kN;
            const float mu = varying_mu
                ? 1.10f + 0.16f * std::sin(phase + 0.23f * j) +
                    0.05f * std::cos(2.0f * phase - 0.17f * j)
                : 1.0f;
            f.mu_true[true_mass_index(j, i)] = mu;
        }
    }
    for (int j = 0; j < kPackedMassY; ++j) {
        const int source_j = std::min(j, kM - 1);
        for (int i = 0; i < kMassX; ++i) {
            const int source_i = wrap_x(i);
            f.mu_full[mass_index(j, i)] = f.mu_true[true_mass_index(source_j, source_i)];
        }
    }

    for (int j = 0; j < kPackedMassY; ++j) {
        for (int i = 0; i < kPackedU; ++i) {
            const float x = 2.0f * pi * wrap_x(i) / kN;
            const float y = static_cast<float>(std::min(j, kM - 1));
            const float u_value = constant_field
                ? transport_sign * 0.72f
                : transport_sign * (0.72f + 0.14f * std::sin(x + 0.11f * y) +
                                     0.08f * std::cos(2.0f * x - 0.13f * y));
            const float msf_u_x = varying_maps
                ? 0.83f + 0.013f * i + 0.021f * j
                : 0.73f;
            const float msf_u_y = varying_maps
                ? 1.19f + 0.009f * i + 0.017f * j
                : 1.67f;
            f.u[u_index(j, 0, i)] = u_value;
            f.msfux[static_cast<size_t>(j) * kPackedU + i] = msf_u_x;
            f.msfuy[static_cast<size_t>(j) * kPackedU + i] = msf_u_y;
            for (int k = 1; k < kNz; ++k) {
                const float uk = u_value + (constant_field ? 0.0f : 0.025f * k);
                f.u[u_index(j, k, i)] = uk;
            }
        }
    }
    for (int j = 0; j < kPackedVY; ++j) {
        for (int i = 0; i < kPackedV; ++i) {
            const float x = 2.0f * pi * wrap_x(i) / kN;
            const float y = static_cast<float>(std::min(j, kM));
            const float v_value = constant_field
                ? transport_sign * 0.51f
                : transport_sign * (0.51f + 0.12f * std::cos(x - 0.19f) +
                                     0.06f * std::sin(3.0f * x + 0.08f * y));
            const float msf_v_x = varying_maps
                ? 1.31f + 0.011f * i + 0.019f * j
                : 1.43f;
            const float msf_v_y = varying_maps
                ? 0.91f + 0.015f * i + 0.014f * j
                : 0.86f;
            for (int k = 0; k < kNz; ++k) {
                f.v[v_index(j, k, i)] = v_value +
                    (constant_field ? 0.0f : 0.02f * k);
            }
            f.msfvx[static_cast<size_t>(j) * kPackedV + i] = msf_v_x;
            f.msfvy[static_cast<size_t>(j) * kPackedV + i] = msf_v_y;
        }
    }

    // Packed identities are the true-domain aliases used by pad3u/pad3v and
    // the new helper.  The mass last row repeats the last true row.
    for (int j = 0; j < kPackedMassY; ++j) {
        const int source_j = std::min(j, kM - 1);
        for (int k = 0; k < kNz; ++k) {
            for (int i = kN; i < kPackedU; ++i) {
                f.u[u_index(j, k, i)] = f.u[u_index(source_j, k, i - kN)];
            }
        }
        for (int i = kN; i < kPackedU; ++i) {
            f.msfux[static_cast<size_t>(j) * kPackedU + i] =
                f.msfux[static_cast<size_t>(source_j) * kPackedU + i - kN];
            f.msfuy[static_cast<size_t>(j) * kPackedU + i] =
                f.msfuy[static_cast<size_t>(source_j) * kPackedU + i - kN];
        }
    }
    for (int j = 0; j < kPackedVY; ++j) {
        const int source_j = std::min(j, kM);
        for (int k = 0; k < kNz; ++k) {
            f.v[v_index(j, k, kN)] = f.v[v_index(source_j, k, 0)];
        }
        f.msfvx[static_cast<size_t>(j) * kPackedV + kN] =
            f.msfvx[static_cast<size_t>(source_j) * kPackedV];
        f.msfvy[static_cast<size_t>(j) * kPackedV + kN] =
            f.msfvy[static_cast<size_t>(source_j) * kPackedV];
    }
    // The row aliases must be copied after x aliases are set.
    for (int k = 0; k < kNz; ++k) {
        for (int i = 0; i < kPackedU; ++i) {
            f.u[u_index(kM, k, i)] = f.u[u_index(kM - 1, k, i)];
        }
    }
    for (int i = 0; i < kPackedU; ++i) {
        f.msfux[static_cast<size_t>(kM) * kPackedU + i] =
            f.msfux[static_cast<size_t>(kM - 1) * kPackedU + i];
        f.msfuy[static_cast<size_t>(kM) * kPackedU + i] =
            f.msfuy[static_cast<size_t>(kM - 1) * kPackedU + i];
    }
    for (int k = 0; k < kNz; ++k) {
        for (int i = 0; i < kPackedV; ++i) {
            f.v[v_index(kM + 1, k, i)] = f.v[v_index(kM, k, i)];
        }
    }
    for (int i = 0; i < kPackedV; ++i) {
        f.msfvx[static_cast<size_t>(kM + 1) * kPackedV + i] =
            f.msfvx[static_cast<size_t>(kM) * kPackedV + i];
        f.msfvy[static_cast<size_t>(kM + 1) * kPackedV + i] =
            f.msfvy[static_cast<size_t>(kM) * kPackedV + i];
    }
    return f;
}

torch::Tensor tensor_u(const std::vector<float>& x) {
    return torch::from_blob(const_cast<float*>(x.data()),
                            {kPackedMassY, kNz, kPackedU}, torch::kFloat32).clone();
}

torch::Tensor tensor_v(const std::vector<float>& x) {
    return torch::from_blob(const_cast<float*>(x.data()),
                            {kPackedVY, kNz, kPackedV}, torch::kFloat32).clone();
}

torch::Tensor tensor_mu(const std::vector<float>& x) {
    return torch::from_blob(const_cast<float*>(x.data()),
                            {kPackedMassY, kMassX}, torch::kFloat32).clone();
}

float true_mu_u(const Fields& f, int j, int i) {
    return 0.5f * (f.mu_true[true_mass_index(j, wrap_x(i))] +
                   f.mu_true[true_mass_index(j, wrap_x(i - 1))]);
}

float true_mu_v(const Fields& f, int j, int i) {
    const int jj = std::min(std::max(j, 0), kM);
    if (jj == 0) return f.mu_true[true_mass_index(0, wrap_x(i))];
    if (jj == kM) return f.mu_true[true_mass_index(kM - 1, wrap_x(i))];
    return 0.5f * (f.mu_true[true_mass_index(jj - 1, wrap_x(i))] +
                    f.mu_true[true_mass_index(jj, wrap_x(i))]);
}

float map_u(const Fields& f, bool x_map, int j, int i) {
    const auto& map = x_map ? f.msfux : f.msfuy;
    return map[static_cast<size_t>(j) * kPackedU + i];
}

float map_v(const Fields& f, bool y_map, int j, int i) {
    const auto& map = y_map ? f.msfvy : f.msfvx;
    return map[static_cast<size_t>(j) * kPackedV + i];
}

struct Oracle {
    std::vector<float> canonical_u;
    std::vector<float> canonical_v;
    std::vector<float> alpha_u;
    std::vector<float> alpha_v;
};

size_t packed_u_index(int j, int k, int i) {
    return u_index(j, k, i);
}

size_t packed_v_index(int j, int k, int i) {
    return v_index(j, k, i);
}

Oracle make_oracle(const Fields& f, const Coefficients& c, int order,
                   float rdx) {
    Oracle out;
    out.canonical_u.resize(f.u.size());
    out.canonical_v.resize(f.v.size());
    out.alpha_u.resize(f.u.size(), 0.0f);
    out.alpha_v.resize(f.v.size(), 0.0f);
    // First compute alpha and coupled Ru on the true domains.  These formulas
    // are independent of the implementation and mirror WRF's staggered mass
    // interpolation, including periodic x and symmetric-y wall repetition.
    std::vector<float> ru(static_cast<size_t>(kNz) * kM * kN);
    std::vector<float> transport_v(static_cast<size_t>(kNz) * (kM + 1) * kN, 0.0f);
    for (int j = 0; j < kM; ++j) {
        for (int k = 0; k < kNz; ++k) {
            for (int i = 0; i < kN; ++i) {
                const float alpha = (c.c1h[k] * true_mu_u(f, j, i) + c.c2h[k]) /
                    map_u(f, false, j, i);
                out.alpha_u[packed_u_index(j, k, i)] = alpha;
                ru[true_u_index(j, k, i)] = alpha *
                    f.u[u_index(j, k, i)];
            }
        }
    }
    for (int j = 0; j <= kM; ++j) {
        for (int k = 0; k < kNz; ++k) {
            for (int i = 0; i < kN; ++i) {
                const float alpha = (c.c1h[k] * true_mu_v(f, j, i) + c.c2h[k]) /
                    map_v(f, false, j, i);
                out.alpha_v[packed_v_index(j, k, i)] = alpha;
                const int ru_j = std::min(j, kM - 1);
                const int ru_jm1 = std::min(std::max(j - 1, 0), kM - 1);
                transport_v[true_v_index(j, k, i)] =
                    0.5f * (ru[true_u_index(ru_j, k, i)] +
                             ru[true_u_index(ru_jm1, k, i)]);
            }
        }
    }

    for (int j = 0; j < kM; ++j) {
        for (int k = 0; k < kNz; ++k) {
            std::vector<float> q(kN), t(kN);
            for (int i = 0; i < kN; ++i) {
                q[i] = f.u[u_index(j, k, i)];
                t[i] = 0.5f * (ru[true_u_index(j, k, i)] +
                                ru[true_u_index(j, k, wrap_x(i - 1))]);
            }
            for (int i = 0; i < kN; ++i) {
                const float west = flux(order, q, i, t[i]);
                const float east = flux(order, q, i + 1, t[wrap_x(i + 1)]);
                out.canonical_u[packed_u_index(j, k, i)] =
                    -(east - west) * map_u(f, true, j, i) * rdx;
            }
        }
    }
    for (int j = 0; j <= kM; ++j) {
        for (int k = 0; k < kNz; ++k) {
            std::vector<float> q(kN), t(kN);
            for (int i = 0; i < kN; ++i) {
                q[i] = f.v[v_index(j, k, i)];
                t[i] = transport_v[true_v_index(j, k, i)];
            }
            for (int i = 0; i < kN; ++i) {
                const float west = flux(order, q, i, t[i]);
                const float east = flux(order, q, i + 1, t[wrap_x(i + 1)]);
                out.canonical_v[packed_v_index(j, k, i)] =
                    -(east - west) * map_v(f, true, j, i) * rdx;
            }
        }
    }

    // Copy every packed alias, including the repeated y row.  The core
    // columns/rows were computed above on the true domain; aliases must copy
    // those values rather than remain at the vector's zero initialization.
    for (int j = 0; j < kPackedMassY; ++j) {
        const int source_j = std::min(j, kM - 1);
        for (int k = 0; k < kNz; ++k) {
            for (int i = 0; i < kPackedU; ++i) {
                if (j >= kM || i >= kN) {
                    const int source_i = i < kN ? i : (i == kN ? 0 : 1);
                    out.canonical_u[packed_u_index(j, k, i)] =
                        out.canonical_u[packed_u_index(source_j, k, source_i)];
                }
            }
        }
    }
    for (int j = 0; j < kPackedVY; ++j) {
        const int source_j = std::min(j, kM);
        for (int k = 0; k < kNz; ++k) {
            for (int i = 0; i < kPackedV; ++i) {
                if (j > kM || i >= kN) {
                    const int source_i = i < kN ? i : 0;
                    out.canonical_v[packed_v_index(j, k, i)] =
                        out.canonical_v[packed_v_index(source_j, k, source_i)];
                }
            }
        }
    }
    for (int j = 0; j < kPackedMassY; ++j) {
        const int source_j = std::min(j, kM - 1);
        for (int k = 0; k < kNz; ++k) {
            for (int i = 0; i < kPackedU; ++i) {
                if (j >= kM || i >= kN) {
                    const int source_i = i < kN ? i : (i == kN ? 0 : 1);
                    out.alpha_u[packed_u_index(j, k, i)] =
                        out.alpha_u[packed_u_index(source_j, k, source_i)];
                }
            }
        }
    }
    for (int j = 0; j < kPackedVY; ++j) {
        const int source_j = std::min(j, kM);
        for (int k = 0; k < kNz; ++k) {
            for (int i = 0; i < kPackedV; ++i) {
                if (j > kM || i >= kN) {
                    const int source_i = i < kN ? i : 0;
                    out.alpha_v[packed_v_index(j, k, i)] =
                        out.alpha_v[packed_v_index(source_j, k, source_i)];
                }
            }
        }
    }
    return out;
}

class PackedPeriodicTile {
public:
    TileSDIRK3UnifiedSolver solver;

    explicit PackedPeriodicTile(const Coefficients& c)
        : solver(kMassX, kPackedMassY, kNz, 1.0f, 1.0f,
                 {1.0f}, {1.0f}, std::vector<float>(kNz, 1.0f), 0) {
        solver.setStaggeredDimensions(kPackedU, kPackedVY, kNzW);
        solver.setWRFIndices(
            1, kMassX, 1, kPackedMassY, 1, kNz,
            1, kMassX, 1, kPackedMassY, 1, kNzW,
            -4, kMassX + 5, -4, kPackedMassY + 5, 1, kNzW);
        const std::vector<float> c1f(kNzW, 1.0f);
        const std::vector<float> c2f(kNzW, 0.0f);
        (solver.*access(CoordinateTag{}))(
            c1f.data(), c2f.data(), c.c1h.data(), c.c2h.data());
        solver.setBoundaryConditions(true, false, false, false, true, true,
                                     false, false, false, false);
    }

    void set_maps(const Fields& f) {
        const auto options = torch::TensorOptions().dtype(torch::kFloat32);
        (solver.*access(MsfUxTag{})) = torch::from_blob(
            const_cast<float*>(f.msfux.data()),
            {kPackedMassY, kPackedU}, options).clone();
        (solver.*access(MsfUyTag{})) = torch::from_blob(
            const_cast<float*>(f.msfuy.data()),
            {kPackedMassY, kPackedU}, options).clone();
        (solver.*access(MsfVxTag{})) = torch::from_blob(
            const_cast<float*>(f.msfvx.data()),
            {kPackedVY, kPackedV}, options).clone();
        (solver.*access(MsfVyTag{})) = torch::from_blob(
            const_cast<float*>(f.msfvy.data()),
            {kPackedVY, kPackedV}, options).clone();
    }
};

float max_abs_difference(const torch::Tensor& actual,
                        const std::vector<float>& expected) {
    TORCH_CHECK(actual.defined(), "oracle comparison received undefined output");
    TORCH_CHECK(actual.numel() == static_cast<int64_t>(expected.size()),
                "oracle comparison size mismatch: actual=", actual.numel(),
                " expected=", expected.size());
    const auto x = actual.to(torch::kCPU).contiguous();
    TORCH_CHECK(torch::isfinite(x).all().item<bool>(),
                "oracle comparison received nonfinite production output");
    for (const float value : expected) {
        TORCH_CHECK(std::isfinite(value), "oracle comparison received nonfinite expected value");
    }
    const auto* p = x.data_ptr<float>();
    float error = 0.0f;
    for (int64_t i = 0; i < x.numel(); ++i) {
        const float difference = std::abs(p[i] - expected[static_cast<size_t>(i)]);
        TORCH_CHECK(std::isfinite(difference),
                    "oracle comparison produced nonfinite difference at ", i);
        error = std::max(error, difference);
    }
    return error;
}

std::vector<float> physical_expected(const std::vector<float>& canonical,
                                     const std::vector<float>& alpha) {
    std::vector<float> result(canonical.size());
    for (size_t i = 0; i < result.size(); ++i) {
        result[i] = canonical[i] / alpha[i];
    }
    return result;
}

void check_aliases(const std::pair<torch::Tensor, torch::Tensor>& out,
                   const Oracle& oracle, float tolerance) {
    TORCH_CHECK(std::abs(out.first[0][0][kN].item<float>() -
                         out.first[0][0][0].item<float>()) < tolerance,
                "U seam alias U[N] != U[0]");
    TORCH_CHECK(std::abs(out.first[0][0][kN + 1].item<float>() -
                         out.first[0][0][1].item<float>()) < tolerance,
                "U pad alias U[N+1] != U[1]");
    TORCH_CHECK(std::abs(out.second[0][0][kN].item<float>() -
                         out.second[0][0][0].item<float>()) < tolerance,
                "V seam alias V[N] != V[0]");
    TORCH_CHECK(std::abs(out.second[kM + 1][0][0].item<float>() -
                         out.second[kM][0][0].item<float>()) < tolerance,
                "V y pad alias != last true V row");
    (void)oracle;
}

void check_canonical_periodic_sum(const torch::Tensor& out_u,
                                  const torch::Tensor& out_v,
                                  const Fields& f, float tolerance) {
    const auto u_cpu = out_u.to(torch::kCPU).contiguous();
    const auto v_cpu = out_v.to(torch::kCPU).contiguous();
    const auto u = u_cpu.accessor<float, 3>();
    const auto v = v_cpu.accessor<float, 3>();
    for (int j = 0; j < kM; ++j) {
        for (int k = 0; k < kNz; ++k) {
            float sum = 0.0f;
            for (int i = 0; i < kN; ++i) sum += u[j][k][i] / map_u(f, true, j, i);
            TORCH_CHECK(std::abs(sum) < tolerance,
                        "canonical U periodic flux sum is nonzero");
        }
    }
    for (int j = 0; j <= kM; ++j) {
        for (int k = 0; k < kNz; ++k) {
            float sum = 0.0f;
            for (int i = 0; i < kN; ++i) sum += v[j][k][i] / map_v(f, true, j, i);
            TORCH_CHECK(std::abs(sum) < tolerance,
                        "canonical V periodic flux sum is nonzero");
        }
    }
}

void check_physical_basis(const std::pair<torch::Tensor, torch::Tensor>& physical,
                          const Oracle& oracle, float tolerance) {
    const auto ru = physical.first.to(torch::kCPU).contiguous();
    const auto rv = physical.second.to(torch::kCPU).contiguous();
    const auto* pu = ru.data_ptr<float>();
    const auto* pv = rv.data_ptr<float>();
    for (size_t i = 0; i < oracle.canonical_u.size(); ++i) {
        const float lhs = pu[i];
        const float rhs = oracle.canonical_u[i] / oracle.alpha_u[i];
        TORCH_CHECK(std::abs(lhs - rhs) < tolerance,
                    "physical U output does not match canonical contribution / alpha");
    }
    for (size_t i = 0; i < oracle.canonical_v.size(); ++i) {
        const float lhs = pv[i];
        const float rhs = oracle.canonical_v[i] / oracle.alpha_v[i];
        TORCH_CHECK(std::abs(lhs - rhs) < tolerance,
                    "physical V output does not match canonical contribution / alpha");
    }
}

void check_input_aliases(const Fields& f) {
    for (int j = 0; j < kPackedMassY; ++j) {
        for (int k = 0; k < kNz; ++k) {
            TORCH_CHECK(f.u[u_index(j, k, kN)] == f.u[u_index(j, k, 0)] &&
                            f.u[u_index(j, k, kN + 1)] == f.u[u_index(j, k, 1)],
                        "U packed x aliases are inconsistent");
        }
    }
    for (int j = 0; j < kPackedVY; ++j) {
        for (int k = 0; k < kNz; ++k) {
            TORCH_CHECK(f.v[v_index(j, k, kN)] == f.v[v_index(j, k, 0)],
                        "V packed x alias is inconsistent");
        }
    }
    for (int i = 0; i < kMassX; ++i) {
        TORCH_CHECK(f.mu_full[mass_index(kM, i)] ==
                        f.mu_full[mass_index(kM - 1, i)],
                    "mass y pad is inconsistent");
    }
}

void check_one_case(float sign, bool varying_mu, bool constant_field,
                    bool varying_maps, int order, const Coefficients& c,
                    bool run_autograd) {
    Fields f = make_fields(sign, varying_mu, constant_field, varying_maps);
    check_input_aliases(f);
    PackedPeriodicTile tile(c);
    tile.set_maps(f);
    wrf::sdirk3::g_sdirk3_config.advection_order = order;
    const auto oracle = make_oracle(f, c, order, kRdx);
    const auto u = tensor_u(f.u);
    const auto v = tensor_v(f.v);
    const auto mu = tensor_mu(f.mu_full);
    const auto out_true = (tile.solver.*access(PackedMomentumXTag{}))(
        u, v, mu, kRdx, true);
    const auto out_false = (tile.solver.*access(PackedMomentumXTag{}))(
        u, v, mu, kRdx, false);
    const auto expected_physical_u = physical_expected(
        oracle.canonical_u, oracle.alpha_u);
    const auto expected_physical_v = physical_expected(
        oracle.canonical_v, oracle.alpha_v);
    const float true_u_error = max_abs_difference(out_true.first, oracle.canonical_u);
    const float true_v_error = max_abs_difference(out_true.second, oracle.canonical_v);
    const float physical_u_error = max_abs_difference(out_false.first, expected_physical_u);
    const float physical_v_error = max_abs_difference(out_false.second, expected_physical_v);
    std::cout << "packed periodic order=" << order << " sign=" << sign
              << " mu=" << (varying_mu ? "varying" : "one")
              << " field=" << (constant_field ? "constant" : "varying")
              << " maps=" << (varying_maps ? "varying" : "constant")
              << " true_err=" << true_u_error << "," << true_v_error
              << " physical_err=" << physical_u_error << "," << physical_v_error << '\n';
    constexpr float tolerance = 3.0e-5f;
    TORCH_CHECK(true_u_error < tolerance && true_v_error < tolerance &&
                    physical_u_error < tolerance && physical_v_error < tolerance,
                "packed periodic momentum oracle mismatch");
    check_aliases(out_true, oracle, tolerance);
    check_aliases(out_false, oracle, tolerance);
    if (constant_field && !varying_mu && !varying_maps) {
        TORCH_CHECK(out_true.first.abs().max().item<float>() < tolerance &&
                        out_true.second.abs().max().item<float>() < tolerance &&
                        out_false.first.abs().max().item<float>() < tolerance &&
                        out_false.second.abs().max().item<float>() < tolerance,
                    "constant-field conservative invariant failed");
    }
    if (!constant_field && varying_mu && varying_maps) {
        check_canonical_periodic_sum(out_true.first, out_true.second, f, 5.0e-5f);
        check_physical_basis(out_false, oracle, 5.0e-5f);
    }

    if (run_autograd) {
        auto ua = tensor_u(f.u).to(torch::kFloat64).requires_grad_(true);
        auto va = tensor_v(f.v).to(torch::kFloat64).requires_grad_(true);
        auto ma = tensor_mu(f.mu_full).to(torch::kFloat64).requires_grad_(true);
        const auto autod = (tile.solver.*access(PackedMomentumXTag{}))(
            ua, va, ma, kRdx, true);
        const auto wu = torch::sin(torch::arange(autod.first.numel(),
                                                   torch::TensorOptions().dtype(torch::kFloat64)))
                            .reshape_as(autod.first);
        const auto wv = torch::cos(torch::arange(autod.second.numel(),
                                                   torch::TensorOptions().dtype(torch::kFloat64)))
                            .reshape_as(autod.second);
        const auto loss = (autod.first * wu).sum() + (autod.second * wv).sum();
        loss.backward();
        TORCH_CHECK(ua.grad().defined() && va.grad().defined() && ma.grad().defined(),
                    "canonical packed helper did not produce autograd gradients");
        TORCH_CHECK(torch::isfinite(ua.grad()).all().item<bool>() &&
                        torch::isfinite(va.grad()).all().item<bool>() &&
                        torch::isfinite(ma.grad()).all().item<bool>(),
                    "canonical packed helper produced nonfinite autograd gradients");
        const auto du = torch::sin(torch::arange(ua.numel(),
                                                  torch::TensorOptions().dtype(torch::kFloat64)))
                            .reshape_as(ua) * 0.07f;
        const auto dv = torch::cos(torch::arange(va.numel(),
                                                  torch::TensorOptions().dtype(torch::kFloat64)))
                            .reshape_as(va) * 0.05f;
        const auto dm = torch::sin(torch::arange(ma.numel(),
                                                  torch::TensorOptions().dtype(torch::kFloat64)))
                            .reshape_as(ma) * 0.03f;
        const double ad_dot = (ua.grad() * du).sum().item<double>() +
            (va.grad() * dv).sum().item<double>() +
            (ma.grad() * dm).sum().item<double>();
        constexpr double eps = 1.0e-5;
        torch::NoGradGuard no_grad;
        const auto plus = (tile.solver.*access(PackedMomentumXTag{}))(
            ua.detach() + eps * du, va.detach() + eps * dv,
            ma.detach() + eps * dm, kRdx, true);
        const auto minus = (tile.solver.*access(PackedMomentumXTag{}))(
            ua.detach() - eps * du, va.detach() - eps * dv,
            ma.detach() - eps * dm, kRdx, true);
        const double fd_dot = (((plus.first - minus.first) * wu).sum().item<double>() +
                              ((plus.second - minus.second) * wv).sum().item<double>()) /
            (2.0f * eps);
        std::cout << "periodic canonical AD/FD dots=" << ad_dot << "," << fd_dot << '\n';
        TORCH_CHECK(std::isfinite(ad_dot) && std::isfinite(fd_dot) &&
                        std::abs(fd_dot) > 1.0e-6,
                    "uninformative or nonfinite periodic AD reference");
        TORCH_CHECK(std::abs(fd_dot - ad_dot) <
                        1.0e-7 * std::max(std::abs(ad_dot), std::abs(fd_dot)),
                    "canonical packed helper autograd dot check failed");
    }
}

}  // namespace

int main() {
    torch::set_num_threads(1);
    wrf::sdirk3::g_sdirk3_config = wrf::sdirk3::SDIRK3Config{};
    wrf::sdirk3::g_sdirk3_config.n_threads = 1;
    wrf::sdirk3::g_sdirk3_config.sign_smooth_delta = 0.0f;
    wrf::sdirk3::g_sdirk3_config.debug_level = 0;

    const std::vector<Coefficients> coefficients = {
        {{1.20f, 0.80f}, {0.15f, -0.07f}},
        {{0.65f, 1.10f}, {-0.12f, 0.23f}},
    };
    for (const auto& c : coefficients) {
        for (int order : {2, 3, 5}) {
            for (float sign : {-1.0f, 1.0f}) {
                check_one_case(sign, false, true, false, order, c, false);
                check_one_case(sign, true, false, true, order, c,
                               order == 5 && sign > 0.0f);
            }
        }
    }
    std::cout << "packed periodic momentum X contract: PASS\n";
}
