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
int main(int argc, char **argv) {
  try {
    req(argc == 25,
        "usage: eos_mpfr ny nz nx rd cv cp p0 t0 [16 flat npy files]");
    long ny = std::stol(argv[1]), nz = std::stol(argv[2]),
         nx = std::stol(argv[3]);
    req(ny > 0 && nz > 0 && nx > 0 && ny < 10000 && nz < 10000 && nx < 10000,
        "dimensions");
    std::array<double, 5> c;
    for (int i = 0; i < 5; ++i) {
      c[i] = std::stod(argv[4 + i]);
      req(std::isfinite(c[i]), "constant nonfinite");
    }
    req(c[0] > 0 && c[1] > 0 && c[2] > 0 && c[3] > 0, "bad constants");
    size_t nm = size_t(ny) * nz * nx, nw = size_t(ny) * (nz + 1) * nx,
           n2 = size_t(ny) * nx;
    std::array<V, 16> x;
    for (int i = 0; i < 16; ++i) {
      size_t n = i < 10
                     ? ((i % 5 == 0) ? nw : (i % 5 == 2 ? n2 : nm))
                     : (i == 10 ? n2 : (i == 11 || i == 12 ? nm : size_t(nz)));
      x[i] = npy(argv[9 + i], n);
    }
    for (double r : x[15])
      req(r > 0, "rdnw must be strictly positive");
    std::cout
        << std::setprecision(17)
        << "{\"status\":\"ok\",\"precision_bits\":256,\"cells_per_state\":"
        << nm << ",\"states\":[";
    for (int state = 0; state < 2; ++state) {
      Acc alt(nz), press(nz);
      const int o = state * 5;
      I rd(c[0]), cv(c[1]), cp(c[2]), p0(c[3]), t0(c[4]), ex = divi(cp, cv);
      for (int j = 0; j < ny; ++j)
        for (int k = 0; k < nz; ++k)
          for (int i = 0; i < nx; ++i) {
            size_t q = (size_t(j) * nz + k) * nx + i,
                   w = (size_t(j) * (nz + 1) + k) * nx + i,
                   m = size_t(j) * nx + i;
            I ph0(x[o][w]), ph1(x[o][w + nx]), th(x[o + 1][q]), mu(x[o + 2][m]),
                mb(x[10][m]), alb(x[11][q]), pb(x[12][q]), c1(x[13][k]),
                c2(x[14][k]), r(x[15][k]);
            I den = add(mul(c1, add(mb, mu)), c2);
            req(mpfr_cmp_d(den.l, 1e-100) > 0,
                "nonpositive or near-zero hybrid mass");
            I term = sub(mul(mul(alb, c1), mu), mul(r, sub(ph1, ph0)));
            I al = sub(alb, divi(term, den));
            req(mpfr_sgn(al.l) > 0, "nonpositive alt");
            I theta = add(th, t0);
            req(mpfr_sgn(theta.l) > 0, "nonpositive theta");
            I arg = divi(mul(rd, theta), mul(p0, al));
            I p = sub(mul(p0, power(arg, ex)), pb);
            alt.put(al, x[o + 3][q], k);
            press.put(p, x[o + 4][q], k);
          }
      if (state)
        std::cout << ',';
      std::cout << "{\"alt\":";
      alt.emit();
      std::cout << ",\"p_pert\":";
      press.emit();
      std::cout << '}';
    }
    std::cout << "],\"scope\":\"EOS arithmetic at captured rounded inputs; "
                 "excludes upstream and full RHS error\"}\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
