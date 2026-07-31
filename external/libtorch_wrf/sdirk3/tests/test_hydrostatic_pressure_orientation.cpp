// 9F.D50: the eta orientation of the PRODUCTION hydrostatic pressure integrator
// (review section 3).
//
// WHY THIS TEST AND NOT THE ONE WE ALREADY HAD. D48 shipped Hydrostatic_Balance_Contract,
// which pins exactly this class of defect -- it even measures the signature, an exact
// factor 2.000 in the balance residual when |rdnw| is fed to signed algebra. It did not
// catch this one, because it fed WRF-SIGNED metrics to a SYNTHETIC helper. It proved
// "signed metric into signed formula is correct", which nobody doubted. Production does
// the other thing: every path into getRdnwTensor()/getRdnTensor() returns a MAGNITUDE
// (require_metric_magnitude at tile_unified_impl.cpp:3233 and :4209, .abs() at :2576),
// and compute_pressure_hydrostatic transcribed WRF's signed algebra unchanged.
//
// A contract test that does not call production code tests the test.
//
// So this file calls compute_pressure_hydrostatic() itself, both overloads, with the
// magnitudes production actually supplies.
//
// THE PHYSICS BEING PINNED. mu' is the column-mass perturbation. More mass in the column
// must mean HIGHER pressure. WRF gets that from rdnw < 0 turning its two minus signs into
// pluses. With |rdnw| and no orientation, p' came out negated -- a mass excess read as a
// pressure deficit, feeding V- and W-momentum PGF and the full pressure p = p' + pb.

#include "../wrf_hydrostatic_pressure.h"

#include <torch/torch.h>

#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

std::string sci(double v) {
    std::ostringstream o; o << std::scientific << std::setprecision(3) << v; return o.str();
}

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

constexpr float RD = 287.0f, CP = 1004.5f, CV = 717.5f, P0 = 1.0e5f, P1000 = 1.0e5f;
constexpr int NZ = 8;
constexpr float MU_PERT = 1.0e3f;   // Pa of excess column mass

// A single column with a uniform eta grid, in the MAGNITUDE convention production uses.
// znw runs 1 -> 0 over NZ layers, so WRF's dnw = -1/NZ and WRF's rdnw = -NZ. We store +NZ.
struct Column {
    std::vector<float> c1h, c2h, rdnw_abs, rdn_abs;
};

Column make_column() {
    Column c;
    c.c1h.assign(NZ, 1.0f);          // dry mass coordinate
    c.c2h.assign(NZ, 0.0f);
    c.rdnw_abs.assign(NZ, float(NZ));
    // rdn (mass-level spacing) is the same magnitude here; a uniform grid keeps the
    // arithmetic checkable by hand, which is the point of a contract fixture.
    c.rdn_abs.assign(NZ, float(NZ));
    return c;
}

torch::Tensor call_vector_overload(const Column& c, float mu_pert) {
    auto opts   = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto theta  = torch::full({1, NZ, 1}, 300.0f, opts);
    auto pbase  = torch::full({1, NZ, 1}, 5.0e4f, opts);
    auto mu_b   = torch::full({1, 1}, 1.0e5f, opts);
    auto mu_f   = mu_b + mu_pert;
    return wrf::sdirk3::compute_pressure_hydrostatic(
        theta, mu_f, mu_b, pbase, mu_b, c.c1h, c.c2h, c.rdnw_abs, c.rdn_abs,
        RD, CV, CP, P0, P1000);
}

torch::Tensor call_tensor_overload(const std::vector<float>& c1h,
                                   const std::vector<float>& c2h,
                                   const std::vector<float>& rdnw,
                                   const std::vector<float>& rdn,
                                   float mu_pert) {
    auto opts  = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
    auto theta = torch::full({1, NZ, 1}, 300.0f, opts);
    auto pbase = torch::full({1, NZ, 1}, 5.0e4f, opts);
    auto mu_b  = torch::full({1, 1}, 1.0e5f, opts);
    auto mu_f  = mu_b + mu_pert;
    auto t = [&](const std::vector<float>& v) {
        return torch::tensor(std::vector<float>(v.begin(), v.end()), opts);
    };
    return wrf::sdirk3::compute_pressure_hydrostatic(
        theta, mu_f, mu_b, pbase, mu_b, t(c1h), t(c2h), t(rdnw), t(rdn),
        RD, CV, CP, P0, P1000);
}

// NOT named `at`: that collides with torch's ::at namespace.
float p_at(const torch::Tensor& p, int k) { return p.index({0, k, 0}).item<float>(); }

}  // namespace

int main() {
    const auto col = make_column();
    const auto p = call_vector_overload(col, MU_PERT);

    // --- the top level, against the closed form ---
    // WRF:  p_top = -0.5*c1*mu'/rdnw   with rdnw < 0   =>   +0.5*c1*mu'/|rdnw|
    {
        const double want = 0.5 * col.c1h[NZ - 1] * MU_PERT / col.rdnw_abs[NZ - 1];
        const double got  = p_at(p, NZ - 1);
        const double rel  = std::abs(got - want) / std::abs(want);
        check(rel < 1e-6, "top level = +0.5*c1*mu'/|rdnw| (want " + sci(want) +
                          ", got " + sci(got) + ", rel=" + sci(rel) + ")");
    }

    // --- THE PHYSICS: a mass excess is a pressure EXCESS, at every level ---
    // This is the check the defect failed. It needs no reference implementation and no
    // tolerance: it is a sign.
    {
        bool all_pos = true;
        for (int k = 0; k < NZ; ++k) if (!(p_at(p, k) > 0.0f)) all_pos = false;
        check(all_pos, "mu' > 0 gives p' > 0 at EVERY level -- more column mass, more "
                       "pressure (k=0: " + sci(p_at(p, 0)) + ", k=top: " + sci(p_at(p, NZ - 1)) + ")");
    }

    // --- and it GROWS downward, because the integration accumulates ---
    {
        bool monotone = true;
        for (int k = 0; k + 1 < NZ; ++k) if (!(p_at(p, k) > p_at(p, k + 1))) monotone = false;
        check(monotone, "p' increases monotonically downward, as the downward integral of "
                        "a positive mass excess must");
    }

    // --- the recurrence itself, level by level ---
    {
        double worst = 0.0;
        for (int k = NZ - 2; k >= 0; --k) {
            const double want = p_at(p, k + 1) + col.c1h[k] * MU_PERT / col.rdn_abs[k + 1];
            const double rel  = std::abs(p_at(p, k) - want) / std::abs(want);
            if (rel > worst) worst = rel;
        }
        check(worst < 1e-6, "every level satisfies p'[k] = p'[k+1] + c1*mu'/|rdn[k+1]| "
                            "(worst rel=" + sci(worst) + ")");
    }

    // --- NEGATIVE CONTROL: feeding WRF's OWN SIGNED metric must now be WRONG ---
    // This is the direction that makes the contract two-sided. The helper is documented to
    // take magnitudes; hand it WRF's negative values and it must return exactly -1x, not
    // something merely different. An exact -1 is the signature of a convention mismatch,
    // the same way Hydrostatic_Balance_Contract's exact 2.000 is.
    //
    // It also means a future "simplification" that reverts to signed input is caught here
    // rather than in a trajectory six months later.
    {
        std::vector<float> rdnw_signed(NZ), rdn_signed(NZ);
        for (int k = 0; k < NZ; ++k) {
            rdnw_signed[k] = -col.rdnw_abs[k];
            rdn_signed[k]  = -col.rdn_abs[k];
        }
        const auto p_signed = call_tensor_overload(col.c1h, col.c2h, rdnw_signed,
                                                   rdn_signed, MU_PERT);
        double worst = 0.0;
        for (int k = 0; k < NZ; ++k) {
            const double rel = std::abs(p_at(p_signed, k) + p_at(p, k)) / std::abs(p_at(p, k));
            if (rel > worst) worst = rel;
        }
        check(worst < 1e-6, "WRF-signed metrics give EXACTLY -1x -- a convention mismatch, "
                            "not a discretisation error (worst rel=" + sci(worst) + ")");
    }

    // --- linearity in mu': the sign follows the mass anomaly, not the code ---
    {
        const auto p_neg = call_vector_overload(col, -MU_PERT);
        double worst = 0.0;
        bool all_neg = true;
        for (int k = 0; k < NZ; ++k) {
            if (!(p_at(p_neg, k) < 0.0f)) all_neg = false;
            const double rel = std::abs(p_at(p_neg, k) + p_at(p, k)) / std::abs(p_at(p, k));
            if (rel > worst) worst = rel;
        }
        check(all_neg, "mu' < 0 gives p' < 0 everywhere: a mass DEFICIT is a pressure deficit");
        check(worst < 1e-6, "and p'(-mu') = -p'(mu') exactly, so the operator is linear in "
                            "the mass anomaly (worst rel=" + sci(worst) + ")");
    }

    // --- the two overloads must agree ---
    // They are two hand-written copies of the same integration loop (review section 13.2).
    // Duplicated loops are how the EOS defect happened; this is the cheapest guard until
    // they are merged.
    {
        const auto p_t = call_tensor_overload(col.c1h, col.c2h, col.rdnw_abs,
                                              col.rdn_abs, MU_PERT);
        double worst = 0.0;
        for (int k = 0; k < NZ; ++k) {
            const double rel = std::abs(p_at(p_t, k) - p_at(p, k)) / std::abs(p_at(p, k));
            if (rel > worst) worst = rel;
        }
        check(worst == 0.0, "the vector and tensor overloads agree BITWISE (worst rel=" +
                            sci(worst) + "); they are duplicated loops");
    }

    constexpr int expected_checks = 8;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "HYDROSTATIC_PRESSURE_ORIENTATION: PASS" << std::endl; return 0; }
    std::cout << "HYDROSTATIC_PRESSURE_ORIENTATION: FAIL (" << failures << ")" << std::endl;
    return 1;
}
