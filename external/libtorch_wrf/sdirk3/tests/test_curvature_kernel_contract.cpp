#include "curvature_scalar_oracle.h"
#include "wrf_sdirk3_curvature.h"

#include <torch/torch.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>

namespace {

using torch::indexing::Slice;

constexpr int kM = 3;
constexpr int kN = 4;
constexpr int kZ = 3;
constexpr double kRdx = 1.0;
constexpr double kRdy = 0.75;
constexpr double kReRadius = 0.125;
constexpr double kEpsilon = std::numeric_limits<double>::epsilon();
// The vector implementation and the independent scalar oracle use different
// reduction/orderings.  This is a count of a few dozen FP64 operations, rather
// than an absolute physical-tendency tolerance.
constexpr double kRoundoffFactor = 256.0;

struct Fields {
  torch::Tensor u, v, w;
  torch::Tensor alpha_u, alpha_v, alpha_w;
  torch::Tensor msfux, msfuy, msfvx, msfvy, msftx, msfty;
  torch::Tensor xlat;
  torch::Tensor fzm, fzp;
};

torch::Tensor make3(int y, int z, int x, double b, double cy, double cz,
                    double cx, bool periodic_x_alias) {
  auto q = torch::empty({y, z, x}, torch::TensorOptions().dtype(torch::kFloat64));
  auto a = q.accessor<double, 3>();
  for (int j = 0; j < y; ++j) {
    for (int k = 0; k < z; ++k) {
      for (int i = 0; i < x; ++i) {
        a[j][k][i] = b + cy * j + cz * k + cx * i;
      }
      if (periodic_x_alias) a[j][k][x - 1] = a[j][k][0];
    }
  }
  return q;
}

torch::Tensor make2(int y, int x, double b, double cy, double cx,
                    bool periodic_x_alias) {
  auto q = torch::empty({y, x}, torch::TensorOptions().dtype(torch::kFloat64));
  auto a = q.accessor<double, 2>();
  for (int j = 0; j < y; ++j) {
    for (int i = 0; i < x; ++i) a[j][i] = b + cy * j + cx * i;
    if (periodic_x_alias) a[j][x - 1] = a[j][0];
  }
  return q;
}

torch::Tensor make1(int n, double b, double c) {
  auto q = torch::empty({n}, torch::TensorOptions().dtype(torch::kFloat64));
  auto a = q.accessor<double, 1>();
  for (int i = 0; i < n; ++i) a[i] = b + c * i;
  return q;
}

Fields make_fields() {
  Fields f;
  f.u = make3(kM, kZ, kN + 1, 0.19, 0.043, 0.029, 0.013, true);
  f.v = make3(kM + 1, kZ, kN, -0.11, 0.037, 0.025, 0.017, false);
  f.w = make3(kM, kZ + 1, kN, 0.071, 0.031, 0.014, 0.009, false);

  // The alpha fields are deliberately independent nonunit maps.  R=alpha*u
  // is formed here for the independent oracle; the candidate receives alpha.
  f.alpha_u = make3(kM, kZ, kN + 1, 1.31, 0.071, 0.047, 0.019, true);
  f.alpha_v = make3(kM + 1, kZ, kN, 0.83, 0.063, 0.039, 0.023, false);
  f.alpha_w = make3(kM, kZ + 1, kN, 1.17, 0.052, 0.081, 0.011, false);

  // Six distinct map tensors, with the u maps carrying their periodic alias.
  f.msfux = make2(kM, kN + 1, 1.07, 0.017, 0.009, true);
  f.msfuy = make2(kM, kN + 1, 1.23, 0.011, 0.014, true);
  f.msfvx = make2(kM + 1, kN, 0.91, 0.021, 0.016, false);
  f.msfvy = make2(kM + 1, kN, 1.36, 0.013, 0.019, false);
  f.msftx = make2(kM, kN, 1.57, 0.023, 0.012, false);
  f.msfty = make2(kM, kN, 1.84, 0.016, 0.021, false);
  f.xlat = make2(kM, kN, 0.18, 0.013, 0.008, false);

  // The two vertical weights are unequal at every used interior level.
  f.fzm = make1(kZ + 1, 0.29, 0.071);
  f.fzp = make1(kZ + 1, 0.83, -0.113);
  return f;
}

torch::Tensor packed_u(const torch::Tensor& q, int m) {
  auto x = torch::cat({q, q.slice(2, 1, 2)}, 2);
  return torch::cat({x, x.slice(0, m - 1, m)}, 0);
}

torch::Tensor packed_v(const torch::Tensor& q, int m) {
  auto x = torch::cat({q, q.slice(2, 0, 1)}, 2);
  return torch::cat({x, -x.slice(0, m - 1, m)}, 0);
}

torch::Tensor packed_v_alpha(const torch::Tensor& q, int m) {
  auto x = torch::cat({q, q.slice(2, 0, 1)}, 2);
  return torch::cat({x, x.slice(0, m - 1, m)}, 0);
}

torch::Tensor packed_w(const torch::Tensor& q, int m) {
  auto x = torch::cat({q, q.slice(2, 0, 1)}, 2);
  return torch::cat({x, x.slice(0, m - 1, m)}, 0);
}

torch::Tensor packed_u_map(const torch::Tensor& q, int m) {
  auto x = torch::cat({q, q.slice(1, 1, 2)}, 1);
  return torch::cat({x, x.slice(0, m - 1, m)}, 0);
}

torch::Tensor packed_v_map(const torch::Tensor& q, int m) {
  auto x = torch::cat({q, q.slice(1, 0, 1)}, 1);
  return torch::cat({x, x.slice(0, m - 1, m)}, 0);
}

torch::Tensor packed_w_map(const torch::Tensor& q, int m) {
  auto x = torch::cat({q, q.slice(1, 0, 1)}, 1);
  return torch::cat({x, x.slice(0, m - 1, m)}, 0);
}

struct ErrorStats {
  double max_abs = 0.0;
  double max_scaled = 0.0;
  int64_t witness_count = 0;
};

void update_stats(const torch::Tensor& got, const torch::Tensor& want,
                  const char* component, const std::string& label,
                  ErrorStats* stats) {
  TORCH_CHECK(got.sizes() == want.sizes(), label, " ", component,
              " shape mismatch: got ", got.sizes(), " want ", want.sizes());
  const auto abs_err = (got - want).abs();
  const auto scale = 1.0 + got.abs() + want.abs();
  const auto scaled = abs_err / (kRoundoffFactor * kEpsilon * scale);
  const double max_abs = abs_err.max().item<double>();
  const double max_scaled = scaled.max().item<double>();
  stats->max_abs = std::max(stats->max_abs, max_abs);
  stats->max_scaled = std::max(stats->max_scaled, max_scaled);
  stats->witness_count += (want.abs() > 1.0e-12).sum().item<int64_t>();
  TORCH_CHECK(std::isfinite(max_scaled), label, " ", component,
              " nonfinite scaled error");
  TORCH_CHECK(max_scaled <= 1.0, label, " ", component,
              " exceeds FP64 rounding budget: max_scaled=", max_scaled,
              " max_abs=", max_abs);
}

void check_nonzero(const torch::Tensor& q, const char* component,
                   const std::string& label) {
  const double witness = q.abs().max().item<double>();
  TORCH_CHECK(std::isfinite(witness) && witness > 1.0e-12, label, " ",
              component, " has no nonzero witness; max_abs=", witness);
}

void compare_case(const Fields& f, int map_proj, bool packed) {
  const auto u = f.u;
  const auto v = f.v;
  const auto w = f.w;
  const auto ru = f.alpha_u * u;
  const auto rv = f.alpha_v * v;
  const auto rw = f.alpha_w * w;
  const auto xlat = map_proj == 6 ? f.xlat : torch::Tensor();
  const std::string label = std::string(packed ? "packed" : "full") +
                            "/proj" + std::to_string(map_proj);

  wrf::sdirk3::CurvatureTendencies got;
  curvature_oracle::Tendencies want;
  if (!packed) {
    got = wrf::sdirk3::wrf_curvature_tendencies(
        u, v, w, f.alpha_u, f.alpha_v, f.alpha_w, f.msfux, f.msfuy,
        f.msfvx, f.msfvy, f.msftx, f.msfty, xlat, f.fzm, f.fzp, kRdx,
        kRdy, kReRadius, map_proj, false);
    want = curvature_oracle::curvature_canonical(
        u, v, w, ru, rv, rw, f.msfux, f.msfuy, f.msfvx, f.msfvy, f.msftx,
        f.msfty, xlat, f.fzm, f.fzp, kM, kN, false, true, true, map_proj,
        false, static_cast<float>(kRdx), static_cast<float>(kRdy),
        static_cast<float>(kReRadius));
  } else {
    const auto pu = packed_u(u, kM);
    const auto pv = packed_v(v, kM);
    const auto pw = packed_w(w, kM);
    const auto pau = packed_u(f.alpha_u, kM);
    const auto pav = packed_v_alpha(f.alpha_v, kM);
    const auto paw = packed_w(f.alpha_w, kM);
    const auto pmsfux = packed_u_map(f.msfux, kM);
    const auto pmsfuy = packed_u_map(f.msfuy, kM);
    const auto pmsfvx = packed_v_map(f.msfvx, kM);
    const auto pmsfvy = packed_v_map(f.msfvy, kM);
    const auto pmsftx = packed_w_map(f.msftx, kM);
    const auto pmsfty = packed_w_map(f.msfty, kM);
    const auto pxlat = map_proj == 6 ? packed_w_map(f.xlat, kM)
                                     : torch::Tensor();
    got = wrf::sdirk3::wrf_curvature_tendencies_packed(
        pu, pv, pw, pau, pav, paw, pmsfux, pmsfuy, pmsfvx, pmsfvy, pmsftx,
        pmsfty, pxlat, f.fzm, f.fzp, kM, kN, kRdx, kRdy, kReRadius,
        map_proj, false);
    want = curvature_oracle::curvature_canonical(
        pu, pv, pw, packed_u(ru, kM), packed_v(rv, kM), packed_w(rw, kM),
        pmsfux, pmsfuy, pmsfvx, pmsfvy, pmsftx, pmsfty, pxlat, f.fzm, f.fzp,
        kM, kN, true, true, true, map_proj, false,
        static_cast<float>(kRdx), static_cast<float>(kRdy),
        static_cast<float>(kReRadius));
  }

  ErrorStats stats;
  update_stats(got.u, want.u, "u", label, &stats);
  update_stats(got.v, want.v, "v", label, &stats);
  update_stats(got.w, want.w, "w", label, &stats);
  check_nonzero(want.u, "u", label);
  check_nonzero(want.v, "v", label);
  check_nonzero(want.w, "w", label);
  TORCH_CHECK(stats.witness_count > 10, label,
              " has too few nonzero oracle cells: ", stats.witness_count);
  std::cout << "kernel-contract " << std::left << std::setw(12) << label
            << " max_abs=" << std::scientific << std::setprecision(17)
            << stats.max_abs << " max_scaled=" << stats.max_scaled
            << " witnesses=" << stats.witness_count << " PASS\n";
}

torch::Tensor direction_like(const torch::Tensor& q, double b, double c) {
  auto d = torch::empty_like(q);
  auto flat = d.reshape({-1});
  auto a = flat.accessor<double, 1>();
  for (int64_t i = 0; i < d.numel(); ++i) a[i] = b + c * (i % 13);
  return d;
}

torch::Tensor cotangent_like(const torch::Tensor& q, double b, double c) {
  auto d = torch::empty_like(q);
  auto flat = d.reshape({-1});
  auto a = flat.accessor<double, 1>();
  for (int64_t i = 0; i < d.numel(); ++i) a[i] = b + c * (i % 11);
  return d;
}

void check_ad(const Fields& f, int map_proj) {
  const auto xlat = map_proj == 6 ? f.xlat : torch::Tensor();
  const auto u0 = f.u;
  const auto v0 = f.v;
  const auto w0 = f.w;
  auto u = u0.clone().set_requires_grad(true);
  auto v = v0.clone().set_requires_grad(true);
  auto w = w0.clone().set_requires_grad(true);
  const auto eval = [&](const torch::Tensor& uu, const torch::Tensor& vv,
                        const torch::Tensor& ww) {
    return wrf::sdirk3::wrf_curvature_tendencies(
        uu, vv, ww, f.alpha_u, f.alpha_v, f.alpha_w, f.msfux, f.msfuy,
        f.msfvx, f.msfvy, f.msftx, f.msfty, xlat, f.fzm, f.fzp, kRdx,
        kRdy, kReRadius, map_proj, false);
  };
  const auto cot_u = cotangent_like(u0, 0.71, 0.023);
  const auto cot_v = cotangent_like(v0, 0.93, -0.017);
  const auto cot_w = cotangent_like(w0, 0.64, 0.031);
  const auto out = eval(u, v, w);
  const auto objective = (out.u * cot_u).sum() + (out.v * cot_v).sum() +
                         (out.w * cot_w).sum();
  objective.backward();
  const auto du = direction_like(u0, 0.013, 0.0021);
  const auto dv = direction_like(v0, -0.021, 0.0017);
  const auto dw = direction_like(w0, 0.017, -0.0013);
  const double reverse = (u.grad() * du).sum().item<double>() +
                         (v.grad() * dv).sum().item<double>() +
                         (w.grad() * dw).sum().item<double>();
  constexpr double h = 1.0e-6;
  const auto plus = eval(u0 + h * du, v0 + h * dv, w0 + h * dw);
  const auto minus = eval(u0 - h * du, v0 - h * dv, w0 - h * dw);
  const double central =
      (((plus.u - minus.u) * cot_u).sum() +
       ((plus.v - minus.v) * cot_v).sum() +
       ((plus.w - minus.w) * cot_w).sum())
          .item<double>() /
      (2.0 * h);
  const double abs_error = std::abs(reverse - central);
  const double relative_error =
      abs_error / std::max({std::abs(reverse), std::abs(central), 1.0e-14});
  TORCH_CHECK(std::isfinite(reverse) && std::isfinite(central),
              "AD/projection", map_proj, " nonfinite directional derivative");
  TORCH_CHECK(std::abs(reverse) > 1.0e-12 && std::abs(central) > 1.0e-12,
              "AD/projection", map_proj, " zero directional witness");
  TORCH_CHECK(relative_error <= 2.0e-8, "AD/projection", map_proj,
              " relative error exceeds FP64 finite-difference budget: ",
              relative_error, " reverse=", reverse, " central=", central);
  std::cout << "kernel-contract AD/proj" << map_proj
            << " reverse=" << std::scientific << std::setprecision(17)
            << reverse << " central=" << central << " abs=" << abs_error
            << " relative=" << relative_error << " PASS\n";
}

}  // namespace

int main() {
  torch::set_num_threads(1);
  const auto fields = make_fields();
  compare_case(fields, 1, false);
  compare_case(fields, 6, false);
  compare_case(fields, 1, true);
  compare_case(fields, 6, true);
  check_ad(fields, 1);
  check_ad(fields, 6);
  std::cout << "kernel-contract all cases PASS\n";
  return 0;
}
