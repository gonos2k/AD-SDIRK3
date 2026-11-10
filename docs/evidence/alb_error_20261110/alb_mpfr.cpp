// Offline alb analysis: interval types, NPY parser and Acc adapted from
// eos-descent-20261110/eos_mpfr.cpp (SHA256
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
I power(const I &a, const I &b) {
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
// Two EOS values are compared at the same exact stored state and coefficients.
// Reusing alb as an interval here is intentional; no native al_pert was
// retained.
struct EOS {
  I alpha, pressure;
};
EOS eos(const I &alb, const I &mu, const I &mub, const I &c1, const I &c2,
        const I &r, const I &dph, const I &theta, const I &rd, const I &p0,
        const I &ex, const I &pb) {
  I den = add(mul(c1, add(mub, mu)), c2);
  req(mpfr_cmp_d(den.l, 1e-100) > 0, "nonpositive or near-zero hybrid mass");
  I term = sub(mul(mul(alb, c1), mu), mul(r, dph));
  I alpha = sub(alb, divi(term, den));
  req(mpfr_sgn(alpha.l) > 0, "nonpositive alpha");
  I arg = divi(mul(rd, theta), mul(p0, alpha));
  return {alpha, sub(mul(p0, power(arg, ex)), pb)};
}
int main(int argc, char **argv) {
  try {
    req(argc == 27, "usage: alb_mpfr ny nz nx rd cv cp eos_p0 t0 p1000 [theta "
                    "+ 16 EOS flat npy files]");
    long ny = std::stol(argv[1]), nz = std::stol(argv[2]),
         nx = std::stol(argv[3]);
    req(ny > 0 && nz > 0 && nx > 0 && ny < 10000 && nz < 10000 && nx < 10000,
        "dimensions");
    std::array<double, 6> c;
    for (int i = 0; i < 6; ++i) {
      c[i] = std::stod(argv[4 + i]);
      req(std::isfinite(c[i]), "nonfinite constant");
    }
    req(c[0] > 0 && c[1] > 0 && c[2] > 0 && c[3] > 0 && c[5] > 0,
        "invalid physical constant");
    size_t nm = size_t(ny) * nz * nx, nw = size_t(ny) * (nz + 1) * nx,
           n2 = size_t(ny) * nx;
    V base_theta = npy(argv[10], nm);
    std::array<V, 16> x;
    for (int i = 0; i < 16; ++i) {
      size_t n = i < 10
                     ? ((i % 5 == 0) ? nw : (i % 5 == 2 ? n2 : nm))
                     : (i == 10 ? n2 : (i == 11 || i == 12 ? nm : size_t(nz)));
      x[i] = npy(argv[11 + i], n);
    }
    for (double r : x[15])
      req(r > 0, "rdnw must be positive");
    I rd(c[0]), cv(c[1]), cp(c[2]), p0(c[3]), t0(c[4]), p1000(c[5]);
    I upstream_ex = sub(I(0), divi(cv, cp)), eos_ex = divi(cp, cv);
    Acc upstream(nz);
    struct StateStats {
      Acc induced_alt, induced_alpert, induced_p, composed_alt, composed_p;
      explicit StateStats(size_t n)
          : induced_alt(n), induced_alpert(n), induced_p(n), composed_alt(n),
            composed_p(n) {}
    };
    StateStats base(nz), accepted(nz);
    std::array<StateStats *, 2> states{&base, &accepted};
    for (int j = 0; j < ny; ++j)
      for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx; ++i) {
          size_t q = (size_t(j) * nz + k) * nx + i,
                 w = (size_t(j) * (nz + 1) + k) * nx + i,
                 m = size_t(j) * nx + i;
          I tb(base_theta[q]), pb(x[12][q]), saved_alb(x[11][q]), mb(x[10][m]),
              c1(x[13][k]), c2(x[14][k]), r(x[15][k]);
          req(mpfr_sgn(tb.l) > 0 && mpfr_sgn(pb.l) > 0,
              "invalid base theta or pressure");
          I ideal_alb = mul(mul(divi(rd, p1000), tb),
                            power(divi(pb, p1000), upstream_ex));
          req(mpfr_sgn(ideal_alb.l) > 0 && mpfr_sgn(saved_alb.l) > 0,
              "nonpositive base inverse density");
          upstream.put(ideal_alb, x[11][q], k);
          I da = sub(ideal_alb, saved_alb);
          for (int state = 0; state < 2; ++state) {
            int o = state * 5;
            auto &z = *states[state];
            I ph0(x[o][w]), ph1(x[o][w + nx]), theta = add(I(x[o + 1][q]), t0),
                                               mu(x[o + 2][m]);
            req(mpfr_sgn(theta.l) > 0, "nonpositive EOS theta");
            I den = add(mul(c1, add(mb, mu)), c2);
            req(mpfr_cmp_d(den.l, 1e-100) > 0,
                "nonpositive or near-zero hybrid mass");
            I dph = sub(ph1, ph0);
            EOS raw = eos(saved_alb, mu, mb, c1, c2, r, dph, theta, rd, p0,
                          eos_ex, pb);
            EOS ideal = eos(ideal_alb, mu, mb, c1, c2, r, dph, theta, rd, p0,
                            eos_ex, pb);
            I beta = divi(add(mul(c1, mb), c2), den);
            I dalpert = mul(sub(I(0), divi(mul(c1, mu), den)), da);
            z.induced_alt.put(mul(beta, da), 0.0, k);
            z.induced_alpert.put(dalpert, 0.0, k);
            z.induced_p.put(sub(ideal.pressure, raw.pressure), 0.0, k);
            z.composed_alt.put(ideal.alpha, x[o + 3][q], k);
            z.composed_p.put(ideal.pressure, x[o + 4][q], k);
          }
        }
    std::cout
        << std::setprecision(17)
        << "{\"status\":\"ok\",\"precision_bits\":256,\"cells_per_state\":"
        << nm << ",\"upstream_alb\":";
    upstream.emit();
    std::cout << ",\"states\":[";
    for (int i = 0; i < 2; ++i) {
      if (i)
        std::cout << ',';
      auto &s = *states[i];
      std::cout << "{\"induced_alpha\":";
      s.induced_alt.emit();
      std::cout << ",\"induced_al_pert\":";
      s.induced_alpert.emit();
      std::cout << ",\"induced_p_pert\":";
      s.induced_p.emit();
      std::cout << ",\"composed_native_alpha\":";
      s.composed_alt.emit();
      std::cout << ",\"composed_native_p_pert\":";
      s.composed_p.emit();
      std::cout << '}';
    }
    std::cout << "],\"scope\":\"captured rounded arguments; excludes state "
                 "formation, other RHS and norm\"}\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
