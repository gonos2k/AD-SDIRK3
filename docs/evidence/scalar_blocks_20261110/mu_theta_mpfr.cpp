// Directed interval, NPY, packed grid and mass-flux helpers adapted from
// PR348 docs/evidence/w_error_20261110/w_full_mpfr.cpp; original is unchanged.
// Interval, NPY, and accumulation helpers adapted from
// eos_descent_20261110/eos_mpfr.cpp (SHA256
// c0cb5b2e3bbe358f64aab99eaa40371d63f3e9698f040daaea3b75e0ceb492bb).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mpfr.h>
#include <stdexcept>
#include <string>
#include <vector>
using V = std::vector<double>;
void req(bool p, const char *s) {
  if (!p)
    throw std::runtime_error(s);
}
// Strict little-endian, C-order, float64 NumPy v1/v2 reader. The older A5
// fd_mpfr.cpp reads SDIRKA53 records, not NPY; no parser is shared with it.
V npy(const char *path, size_t expected) {
  const uint16_t endian = 1;
  req(*reinterpret_cast<const unsigned char *>(&endian) == 1,
      "little-endian host required");
  std::ifstream f(path, std::ios::binary);
  req(bool(f), "npy open");
  char m[6];
  f.read(m, 6);
  req(bool(f) && std::memcmp(m, "\x93NUMPY", 6) == 0, "npy magic");
  unsigned char v[2];
  f.read((char *)v, 2);
  req(bool(f) && (v[0] == 1 || v[0] == 2) && v[1] == 0, "npy version");
  uint32_t len = 0;
  for (int i = 0; i < (v[0] == 1 ? 2 : 4); ++i) {
    unsigned char c = 0;
    f.read((char *)&c, 1);
    len |= uint32_t(c) << (8 * i);
  }
  req(bool(f) && len < 65536, "npy header length");
  std::string h(len, '\0');
  f.read(h.data(), len);
  req(bool(f) && h.find("'<f8'") != h.npos &&
          h.find("'fortran_order': False") != h.npos,
      "npy dtype/order");
  auto pos = h.find("'shape'");
  auto a = h.find('(', pos), b = h.find(')', a);
  req(pos != h.npos && a != h.npos && b != h.npos, "npy shape");
  auto shape = h.substr(a + 1, b - a - 1);
  size_t count = 0;
  try {
    size_t used = 0;
    count = std::stoull(shape, &used);
    req(shape.find_first_not_of(" ,\t\r\n", used) == shape.npos,
        "npy requires flat 1D");
  } catch (...) {
    throw std::runtime_error("npy bad flat shape");
  }
  req(count == expected, "npy size mismatch");
  V x(count);
  f.read((char *)x.data(), count * sizeof(double));
  req(bool(f) && f.peek() == std::char_traits<char>::eof(),
      "npy truncated/trailing");
  for (double q : x)
    req(std::isfinite(q), "npy nonfinite");
  return x;
}
struct I {
  mpfr_t l, u;
  I() {
    mpfr_init2(l, 256);
    mpfr_init2(u, 256);
  }
  I(double x) : I() {
    mpfr_set_d(l, x, MPFR_RNDD);
    mpfr_set_d(u, x, MPFR_RNDU);
  }
  I(const I &o) : I() {
    mpfr_set(l, o.l, MPFR_RNDD);
    mpfr_set(u, o.u, MPFR_RNDU);
  }
  I &operator=(const I &o) {
    if (this != &o) {
      mpfr_set(l, o.l, MPFR_RNDD);
      mpfr_set(u, o.u, MPFR_RNDU);
    }
    return *this;
  }
  ~I() {
    mpfr_clear(l);
    mpfr_clear(u);
  }
};
I add(const I &a, const I &b) {
  I z;
  mpfr_add(z.l, a.l, b.l, MPFR_RNDD);
  mpfr_add(z.u, a.u, b.u, MPFR_RNDU);
  return z;
}
I sub(const I &a, const I &b) {
  I z;
  mpfr_sub(z.l, a.l, b.u, MPFR_RNDD);
  mpfr_sub(z.u, a.u, b.l, MPFR_RNDU);
  return z;
}
I mul(const I &a, const I &b) {
  I z;
  mpfr_t lo, hi;
  mpfr_inits2(256, lo, hi, (mpfr_ptr)0);
  bool first = true;
  for (auto x : {a.l, a.u})
    for (auto y : {b.l, b.u}) {
      mpfr_mul(lo, x, y, MPFR_RNDD);
      mpfr_mul(hi, x, y, MPFR_RNDU);
      if (first || mpfr_cmp(lo, z.l) < 0)
        mpfr_set(z.l, lo, MPFR_RNDD);
      if (first || mpfr_cmp(hi, z.u) > 0)
        mpfr_set(z.u, hi, MPFR_RNDU);
      first = false;
    }
  mpfr_clears(lo, hi, (mpfr_ptr)0);
  return z;
}
I divi(const I &a, const I &b) {
  req(mpfr_sgn(b.l) > 0, "nonpositive denominator");
  I inv;
  mpfr_ui_div(inv.l, 1, b.u, MPFR_RNDD);
  mpfr_ui_div(inv.u, 1, b.l, MPFR_RNDU);
  return mul(a, inv);
}
struct Acc {
  mpfr_t mx, sum;
  std::vector<double> level;
  Acc(size_t n) : level(n, 0) {
    mpfr_inits2(256, mx, sum, (mpfr_ptr)0);
    mpfr_set_zero(mx, 0);
    mpfr_set_zero(sum, 0);
  }
  ~Acc() { mpfr_clears(mx, sum, (mpfr_ptr)0); }
  void put(const I &x, double y, size_t k) {
    mpfr_t l, u, e, t;
    mpfr_inits2(256, l, u, e, t, (mpfr_ptr)0);
    mpfr_set_d(t, y, MPFR_RNDN);
    mpfr_sub(l, t, x.l, MPFR_RNDA);
    mpfr_abs(l, l, MPFR_RNDU);
    mpfr_sub(u, t, x.u, MPFR_RNDA);
    mpfr_abs(u, u, MPFR_RNDU);
    mpfr_max(e, l, u, MPFR_RNDU);
    mpfr_max(mx, mx, e, MPFR_RNDU);
    mpfr_mul(t, e, e, MPFR_RNDU);
    mpfr_add(sum, sum, t, MPFR_RNDU);
    level[k] = std::max(level[k], mpfr_get_d(e, MPFR_RNDU));
    mpfr_clears(l, u, e, t, (mpfr_ptr)0);
  }
  void emit() {
    mpfr_t n;
    mpfr_init2(n, 256);
    mpfr_sqrt(n, sum, MPFR_RNDU);
    std::cout << "{\"linf_upper\":" << mpfr_get_d(mx, MPFR_RNDU)
              << ",\"l2_upper\":" << mpfr_get_d(n, MPFR_RNDU)
              << ",\"per_level_linf_upper\":[";
    for (size_t i = 0; i < level.size(); ++i) {
      if (i)
        std::cout << ',';
      std::cout << level[i];
    }
    std::cout << "]}";
    mpfr_clear(n);
  }
};
// Exact-real packed state and W residual for periodic X / symmetric Y.
// All non-unit maps, alternative branches, and unknown inputs are unsupported.
struct Grid {
  int m, n, z;
  size_t ny, nx, nu, nv, nw, nt, nmu, total, ou, ov, ow, op, ot, om;
  Grid(int m_, int n_, int z_)
      : m(m_), n(n_), z(z_), ny(m + 1), nx(n + 1), nu(ny * z * (nx + 1)),
        nv((ny + 1) * z * nx), nw(ny * (z + 1) * nx), nt(ny * z * nx),
        nmu(ny * nx), total(nu + nv + 2 * nw + nt + nmu), ou(0), ov(nu),
        ow(nu + nv), op(ow + nw), ot(op + nw), om(ot + nt) {}
  size_t mass(int j, int k, int i) const {
    return (size_t(j) * z + k) * nx + i;
  }
  size_t w(int j, int k, int i) const {
    return (size_t(j) * (z + 1) + k) * nx + i;
  }
  size_t u(int j, int k, int i) const {
    return (size_t(j) * z + k) * (nx + 1) + i;
  }
  size_t v(int j, int k, int i) const { return (size_t(j) * z + k) * nx + i; }
  size_t mu(int j, int i) const { return size_t(j) * nx + i; }
};
struct Inputs {
  Grid grid;
  V B, K0, K1, R0, R1, mub, c1h, c2h, mass_rdnw;
  Inputs(Grid g, std::array<V, 9> a)
      : grid(g), B(std::move(a[0])), K0(std::move(a[1])), K1(std::move(a[2])),
        R0(std::move(a[3])), R1(std::move(a[4])), mub(std::move(a[5])),
        c1h(std::move(a[6])), c2h(std::move(a[7])), mass_rdnw(std::move(a[8])) {
  }
};
struct Physics {
  I a, rdx, rdy;
  Physics(double a_, double dx, double dy) : a(a_), rdx(dx), rdy(dy) {}
};
I half(const I &a) { return mul(a, I(0.5)); }
I exact_state(const V &B, const V &K, size_t i, const I &a) {
  return add(I(B[i]), mul(a, I(K[i])));
}
I mass_at(const Inputs &d, const V &K, const Physics &p, int j, int i) {
  const auto &g = d.grid;
  return exact_state(d.B, K, g.om + g.mu(j, i), p.a);
}
I u_at(const Inputs &d, const V &K, const Physics &p, int j, int k, int face) {
  const auto &g = d.grid;
  return exact_state(d.B, K, g.ou + g.u(j, k, face == g.n ? 0 : face), p.a);
}
I v_at(const Inputs &d, const V &K, const Physics &p, int face, int k, int i) {
  const auto &g = d.grid;
  if (face == 0 || face == g.m)
    return I(0);
  return exact_state(d.B, K, g.ov + g.v(face, k, i), p.a);
}
I total_mass(const Inputs &d, const V &K, const Physics &p, int j, int i) {
  const auto &g = d.grid;
  return add(mass_at(d, K, p, j, i), I(d.mub[g.mu(j, i)]));
}
I face_mass_u(const Inputs &d, const V &K, const Physics &p, int j, int face) {
  const auto &g = d.grid;
  int right = face == g.n ? 0 : face, left = right == 0 ? g.n - 1 : right - 1;
  return half(add(total_mass(d, K, p, j, left), total_mass(d, K, p, j, right)));
}
I face_mass_v(const Inputs &d, const V &K, const Physics &p, int face, int i) {
  const auto &g = d.grid;
  int low = face == 0 ? 0 : face - 1, high = face == g.m ? g.m - 1 : face;
  return half(add(total_mass(d, K, p, low, i), total_mass(d, K, p, high, i)));
}
I mass_rate(const Inputs &d, const V &K, const Physics &p, int j, int i) {
  const auto &g = d.grid;
  I sum(0);
  for (int k = 0; k < g.z; ++k) {
    I c1(d.c1h[k]), c2(d.c2h[k]);
    I l = mul(add(mul(c1, face_mass_u(d, K, p, j, i)), c2),
              u_at(d, K, p, j, k, i));
    I r = mul(add(mul(c1, face_mass_u(d, K, p, j, i + 1)), c2),
              u_at(d, K, p, j, k, i + 1));
    I b = mul(add(mul(c1, face_mass_v(d, K, p, j, i)), c2),
              v_at(d, K, p, j, k, i));
    I t = mul(add(mul(c1, face_mass_v(d, K, p, j + 1, i)), c2),
              v_at(d, K, p, j + 1, k, i));
    I div = add(mul(p.rdx, sub(r, l)), mul(p.rdy, sub(t, b)));
    I eta = sub(I(0), divi(I(1), I(d.mass_rdnw[k])));
    sum = add(sum, mul(eta, div));
  }
  return sum;
}
// Source eps_theta is float32 1e-10 normally, or float32 1e-4 when
// CPU FP16 autocast is active. Capture did not record that TLS state.
I theta_eps_interval() {
  I v(static_cast<double>(1e-10f));
  mpfr_set_d(v.u, static_cast<double>(1e-4f), MPFR_RNDU);
  return v;
}
double weighted_upper(const Acc &err, double weight, size_t count,
                      size_t total) {
  mpfr_t value;
  mpfr_init2(value, 256);
  mpfr_set_ui(value, count, MPFR_RNDU);
  mpfr_div_ui(value, value, total, MPFR_RNDU);
  mpfr_sqrt(value, value, MPFR_RNDU);
  mpfr_mul_d(value, value, weight, MPFR_RNDU);
  mpfr_mul(value, value, err.mx, MPFR_RNDU);
  double out = mpfr_get_d(value, MPFR_RNDU);
  mpfr_clear(value);
  req(std::isfinite(out), "weighted bound overflow");
  return out;
}
int main(int argc, char **argv) {
  try {
    req(argc == 18, "usage: mu_theta_mpfr m n nz a rdx rdy t_weight_max "
                    "mu_weight_max B K0 K1 R0 R1 mub c1h c2h mass_rdnw");
    int m = std::stoi(argv[1]), n = std::stoi(argv[2]), z = std::stoi(argv[3]);
    req(m >= 2 && n >= 2 && z >= 2 && m < 1000 && n < 1000 && z < 1000,
        "unsupported grid");
    Grid g(m, n, z);
    double av = std::stod(argv[4]), dx = std::stod(argv[5]),
           dy = std::stod(argv[6]), wt = std::stod(argv[7]),
           wm = std::stod(argv[8]);
    for (double v : {av, dx, dy, wt, wm})
      req(std::isfinite(v) && v > 0, "invalid scalar");
    Physics p(av, dx, dy);
    std::array<V, 9> x;
    for (int i = 0; i < 5; ++i)
      x[i] = npy(argv[9 + i], g.total);
    x[5] = npy(argv[14], g.nmu);
    x[6] = npy(argv[15], z);
    x[7] = npy(argv[16], z);
    x[8] = npy(argv[17], z);
    Inputs d(g, std::move(x));
    for (double r : d.mass_rdnw)
      req(r > 0, "invalid mass rdnw");
    I eps = theta_eps_interval();
    std::cout << std::setprecision(17)
              << "{\"status\":\"ok\",\"precision_bits\":256,\"packed_N\":"
              << g.total << ",\"t_count\":" << g.nt << ",\"mu_count\":" << g.nmu
              << ",\"eps_theta_low\":" << static_cast<double>(1e-10f)
              << ",\"eps_theta_high\":" << static_cast<double>(1e-4f)
              << ",\"states\":[";
    for (int state = 0; state < 2; ++state) {
      const V &K = state ? d.K1 : d.K0, &R = state ? d.R1 : d.R0;
      Acc muerr(1), terr(z);
      for (int jj = 0; jj <= m; ++jj)
        for (int ii = 0; ii <= n; ++ii) {
          int j = jj == m ? m - 1 : jj, i = ii == n ? 0 : ii;
          I mass = total_mass(d, K, p, j, i);
          req(mpfr_sgn(mass.l) > 0, "mu floor branch unsupported");
          I mdot = mass_rate(d, K, p, j, i);
          const size_t mi = g.mu(jj, ii);
          I mu_res = sub(I(K[g.om + mi]), mdot);
          muerr.put(mu_res, R[g.om + mi], 0);
          for (int k = 0; k < z; ++k) {
            I t = exact_state(d.B, K, g.ot + g.mass(j, k, i), p.a);
            I cm = I(d.c1h[k]);
            I denom = add(add(mul(cm, mass), I(d.c2h[k])), eps);
            req(mpfr_sgn(denom.l) > 0, "theta denominator unsupported");
            I source = sub(I(0), divi(mul(mul(t, cm), mdot), denom));
            const size_t ti = g.mass(jj, k, ii);
            I target = sub(I(K[g.ot + ti]), source);
            terr.put(target, R[g.ot + ti], k);
          }
        }
      if (state)
        std::cout << ',';
      std::cout << "{\"mu_error\":";
      muerr.emit();
      std::cout << ",\"mu_weighted_math_rms_upper\":"
                << weighted_upper(muerr, wm, g.nmu, g.total)
                << ",\"theta_error\":";
      terr.emit();
      std::cout << ",\"theta_weighted_math_rms_upper\":"
                << weighted_upper(terr, wt, g.nt, g.total) << '}';
    }
    std::cout << "],\"scope\":\"mu and theta residual against exact real "
                 "Q(B+aK), fixed unit maps; eps encloses both source branches; "
                 "excludes prior B/K formation and native norm\"}\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
