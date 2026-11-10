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
#include <map>
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
I power_four(const I &a, const I &b) {
  req(mpfr_sgn(a.l) > 0, "nonpositive pow base");
  I z;
  mpfr_t lo, hi;
  mpfr_inits2(256, lo, hi, (mpfr_ptr)0);
  bool first = true;
  for (auto x : {a.l, a.u})
    for (auto y : {b.l, b.u}) {
      mpfr_pow(lo, x, y, MPFR_RNDD);
      mpfr_pow(hi, x, y, MPFR_RNDU);
      if (first || mpfr_cmp(lo, z.l) < 0)
        mpfr_set(z.l, lo, MPFR_RNDD);
      if (first || mpfr_cmp(hi, z.u) > 0)
        mpfr_set(z.u, hi, MPFR_RNDU);
      first = false;
    }
  mpfr_clears(lo, hi, (mpfr_ptr)0);
  req(mpfr_number_p(z.l) && mpfr_number_p(z.u), "nonfinite pow");
  return z;
}
// For sign-definite exponent the function is monotone in the positive base.
// At each selected base endpoint, its relation to 1 chooses the exponent
// endpoint. An interval crossing exponent zero retains the four-corner path.
I power(const I &base, const I &exponent) {
  req(mpfr_sgn(base.l) > 0, "nonpositive pow base");
  const bool positive = mpfr_sgn(exponent.l) >= 0;
  const bool negative = mpfr_sgn(exponent.u) <= 0;
  if (!positive && !negative)
    return power_four(base, exponent);
  mpfr_srcptr lower_base = positive ? base.l : base.u;
  mpfr_srcptr upper_base = positive ? base.u : base.l;
  mpfr_srcptr lower_exp =
      mpfr_cmp_ui(lower_base, 1) < 0 ? exponent.u : exponent.l;
  mpfr_srcptr upper_exp =
      mpfr_cmp_ui(upper_base, 1) < 0 ? exponent.l : exponent.u;
  I out;
  mpfr_pow(out.l, lower_base, lower_exp, MPFR_RNDD);
  mpfr_pow(out.u, upper_base, upper_exp, MPFR_RNDU);
  req(mpfr_number_p(out.l) && mpfr_number_p(out.u), "nonfinite pow");
  return out;
}
void power_selftest() {
  for (auto b : {std::pair<double, double>{0.2, 0.8},
                 {0.2, 1.7},
                 {1.0, 1.0},
                 {1.2, 3.0}})
    for (auto e : {std::pair<double, double>{0.1, 1.4},
                   {-2.0, -0.2},
                   {-1.0, 2.0},
                   {0.0, 2.0},
                   {-1.0, 0.0}}) {
      I base(b.first), exponent(e.first);
      mpfr_set_d(base.u, b.second, MPFR_RNDU);
      mpfr_set_d(exponent.u, e.second, MPFR_RNDU);
      I old = power_four(base, exponent), current = power(base, exponent);
      req(mpfr_equal_p(old.l, current.l) && mpfr_equal_p(old.u, current.u),
          "power endpoint mismatch");
    }
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
  V B, K0, K1, R0, R1, theta, pb, mub, c1h, c2h, c1f, c2f, mass_rdnw,
      pressure_rdn, top_rdnw, sinv;
  Inputs(Grid g, std::array<V, 16> a)
      : grid(g), B(std::move(a[0])), K0(std::move(a[1])), K1(std::move(a[2])),
        R0(std::move(a[3])), R1(std::move(a[4])), theta(std::move(a[5])),
        pb(std::move(a[6])), mub(std::move(a[7])), c1h(std::move(a[8])),
        c2h(std::move(a[9])), c1f(std::move(a[10])), c2f(std::move(a[11])),
        mass_rdnw(std::move(a[12])), pressure_rdn(std::move(a[13])),
        top_rdnw(std::move(a[14])), sinv(std::move(a[15])) {}
};
struct Physics {
  I a, rdx, rdy, g, rd, cv, cp, p0, t0, p1000;
  Physics(const std::array<double, 9> &c)
      : a(c[0]), rdx(c[1]), rdy(c[2]), g(c[3]), rd(c[4]), cv(c[5]), cp(c[6]),
        p0(c[7]), t0(c[8]), p1000(100000.0) {
    for (int k : {0, 1, 2, 3, 4, 5, 6, 7})
      req(c[k] > 0, "nonpositive physical constant");
  }
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
I eos_pressure(const Inputs &d, const V &K, const Physics &p, int j, int k,
               int i, std::map<std::pair<double, double>, I> &alb_cache) {
  const auto &g = d.grid;
  size_t q = g.mass(j, k, i), w0 = g.op + g.w(j, k, i),
         w1 = g.op + g.w(j, k + 1, i);
  I theta0(d.theta[q]), pb(d.pb[q]), mub(d.mub[g.mu(j, i)]),
      mu = mass_at(d, K, p, j, i);
  req(mpfr_sgn(theta0.l) > 0 && mpfr_sgn(pb.l) > 0, "base EOS domain");
  auto key = std::make_pair(d.theta[q], d.pb[q]);
  auto found = alb_cache.find(key);
  if (found == alb_cache.end()) {
    I initial = mul(mul(divi(p.rd, p.p1000), theta0),
                    power(divi(pb, p.p1000), sub(I(0), divi(p.cv, p.cp))));
    found = alb_cache.emplace(key, initial).first;
  }
  I alb = found->second;
  I den = add(mul(I(d.c1h[k]), add(mub, mu)), I(d.c2h[k]));
  req(mpfr_sgn(den.l) > 0, "EOS hybrid mass");
  I dphi = sub(exact_state(d.B, K, w1, p.a), exact_state(d.B, K, w0, p.a));
  I term = sub(mul(mul(alb, I(d.c1h[k])), mu), mul(I(d.mass_rdnw[k]), dphi));
  I alt = sub(alb, divi(term, den));
  req(mpfr_sgn(alt.l) > 0, "EOS inverse density");
  I theta = add(exact_state(d.B, K, g.ot + q, p.a), p.t0);
  req(mpfr_sgn(theta.l) > 0, "EOS theta");
  I arg = divi(mul(p.rd, theta), mul(p.p0, alt));
  return sub(mul(p.p0, power(arg, divi(p.cp, p.cv))), pb);
}
I physical_w(const Inputs &d, const V &K, const Physics &p, int j, int k, int i,
             const std::vector<I> &pressure, const I &mdot) {
  const auto &g = d.grid;
  I mu = mass_at(d, K, p, j, i);
  I den = add(mul(I(d.c1f[k]), total_mass(d, K, p, j, i)), I(d.c2f[k]));
  req(mpfr_sgn(den.l) > 0, "W hybrid mass");
  I source(0);
  if (k > 0 && k < g.z)
    source = mul(p.g, sub(mul(sub(I(0), I(d.pressure_rdn[k])),
                              sub(pressure[k], pressure[k - 1])),
                          mul(I(d.c1f[k]), mu)));
  else if (k == g.z)
    source = mul(p.g, sub(mul(mul(I(2), I(d.top_rdnw[0])), pressure[g.z - 1]),
                          mul(I(d.c1f[k]), mu)));
  I w = exact_state(d.B, K, g.ow + g.w(j, k, i), p.a);
  return divi(sub(source, mul(mul(w, I(d.c1f[k])), mdot)), den);
}
int main(int argc, char **argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--selftest-pow") {
      power_selftest();
      std::cout << "PASS optimized MPFR power endpoints match four corners\n";
      return 0;
    }
    req(argc == 29,
        "usage: w_full_mpfr m n nz a rdx rdy g rd cv cp p0 t0 [16 flat NPY]");
    int m = std::stoi(argv[1]), n = std::stoi(argv[2]), z = std::stoi(argv[3]);
    req(m >= 2 && n >= 2 && z >= 2 && m < 1000 && n < 1000 && z < 1000, "grid");
    Grid g(m, n, z);
    std::array<double, 9> c;
    for (int i = 0; i < 9; ++i) {
      c[i] = std::stod(argv[4 + i]);
      req(std::isfinite(c[i]), "nonfinite constant");
    }
    Physics p(c);
    std::array<V, 16> x;
    for (int i = 0; i < 16; ++i) {
      size_t count =
          i < 5
              ? g.total
              : (i == 5 || i == 6
                     ? g.nt
                     : (i == 7 ? g.nmu
                               : (i == 8 || i == 9 || i == 12 || i == 13
                                      ? size_t(z)
                                      : (i == 10 || i == 11
                                             ? size_t(z + 1)
                                             : (i == 14 ? size_t(1) : g.nw)))));
      x[i] = npy(argv[13 + i], count);
    }
    Inputs d(g, std::move(x));
    for (auto v : d.mass_rdnw)
      req(v > 0, "mass rdnw must be positive");
    for (int k = 1; k < z; ++k)
      req(d.pressure_rdn[k] > 0, "pressure rdn must be positive");
    for (auto v : d.top_rdnw)
      req(v > 0, "top rdnw must be positive");
    for (auto v : d.sinv)
      req(v >= 0, "W inverse scale must be nonnegative");
    req(g.nw > 0 && g.total > 0, "invalid weighted norm counts");
    double max_weight = *std::max_element(d.sinv.begin(), d.sinv.end());
    std::cout
        << std::setprecision(17)
        << "{\"status\":\"ok\",\"precision_bits\":256,\"W_count_per_state\":"
        << g.nw << ",\"packed_N\":" << g.total
        << ",\"max_W_inverse_weight\":" << max_weight << ",\"states\":[";
    std::map<std::pair<double, double>, I> alb_cache;
    for (int state = 0; state < 2; ++state) {
      const auto &K = state ? d.K1 : d.K0;
      const auto &R = state ? d.R1 : d.R0;
      Acc err(z + 1);
      for (int jj = 0; jj <= m; ++jj)
        for (int ii = 0; ii <= n; ++ii) {
          int j = jj == m ? m - 1 : jj, i = ii == n ? 0 : ii;
          I active_mass = total_mass(d, K, p, j, i);
          req(mpfr_sgn(active_mass.l) > 0, "mu floor branch unsupported");
          I mdot = mass_rate(d, K, p, j, i);
          std::vector<I> pressure;
          pressure.reserve(z);
          for (int k = 0; k < z; ++k)
            pressure.push_back(eos_pressure(d, K, p, j, k, i, alb_cache));
          for (int k = 0; k <= z; ++k) {
            I f = physical_w(d, K, p, j, k, i, pressure, mdot);
            size_t wi = g.w(jj, k, ii);
            I true_r = sub(I(K[g.ow + wi]), f);
            err.put(true_r, R[g.ow + wi], k);
          }
        }
      mpfr_t bound;
      mpfr_init2(bound, 256);
      mpfr_set_ui(bound, g.nw, MPFR_RNDU);
      mpfr_div_ui(bound, bound, g.total, MPFR_RNDU);
      mpfr_sqrt(bound, bound, MPFR_RNDU);
      mpfr_mul_d(bound, bound, max_weight, MPFR_RNDU);
      mpfr_mul(bound, bound, err.mx, MPFR_RNDU);
      double weighted = mpfr_get_d(bound, MPFR_RNDU);
      mpfr_clear(bound);
      req(std::isfinite(weighted), "weighted result overflow");
      if (state)
        std::cout << ',';
      std::cout << "{\"physical_error\":";
      err.emit();
      std::cout << ",\"weighted_math_rms_upper\":" << weighted << '}';
    }
    std::cout << "],\"scope\":\"exact real W residual at exact Q(B+aK), unit "
                 "maps and specified profile; excludes B/K input formation and "
                 "native norm\"}\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
