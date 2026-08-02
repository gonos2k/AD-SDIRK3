// wrf_sdirk3_transpose_probe.h -- is a claimed transpose actually a transpose?
//
// 9F.D81. Extracted from wrf_sdirk3_tile_unified_impl.cpp, where D73-D80 had grown a
// 355-line inline probe inside runAdjointReplay. Same reason as its two siblings
// (wrf_sdirk3_response_probe.h, wrf_sdirk3_implicit_diff.h): an instrument buried in the
// operator it measures cannot be validated against a known answer, and this one produced a
// wrong conclusion because of exactly that.
//
// ---------------------------------------------------------------------------------------
// THE ERROR THIS API IS SHAPED TO PREVENT
//
// D80 enabled autograd through the preconditioner, took the VJP as M^T, and measured
//
//     <Mv,w> = 396.9      <v, M^T w> = 830.9      rel = 0.5223
//
// and I read that as "M^T is 52% wrong, and the AD side being LARGER points at
// double-counting". Both halves were wrong. The preconditioner's compute path is a chain
// of raw-pointer Thomas sweeps and accessor writes under NoGradGuard; autograd recorded
// NONE of it. The only edge it saw was the `z = residual.clone()` at the top. So the VJP
// returned its own input:
//
//     M^T w = w      =>      <v, M^T w> = <v,w> = 830.87   (reproduced offline, seed-exact)
//
// rel = 0.5223 was not 52% of anything. It was the ratio between the real bilinear form and
// the trivial one for that particular random pair -- a number with no dependence on the
// preconditioner at all, which would have moved non-monotonically under a partial fix and
// happily reported "progress" in either direction.
//
// A severed VJP therefore fails PLAUSIBLY: it returns a defined, finite, correctly-shaped
// tensor, and every symmetry/linearity property still holds, because the identity is a
// perfectly good linear operator. Nothing in the numbers announces it.
//
// So identity is checked EXPLICITLY and reported as its own verdict, not folded into an
// error magnitude. But it is only ever a REASON, never the evidence: the bilinear identity
// <Mv,w> == <v,M^Tw> is what decides, because an exact transpose can leave a probe direction
// fixed (review section 7 -- see the counterexample on transpose_is_identity). SEVERED
// therefore requires a FAILED identity as well as identity-looking behaviour.
//
// The caller passes the claimed transpose as a plain operator, so this header knows nothing
// about autograd or the solver -- which is what lets Transpose_Probe_Contract run it
// against a matrix whose transpose is known exactly, including a deliberately severed one.
// ---------------------------------------------------------------------------------------

#ifndef WRF_SDIRK3_TRANSPOSE_PROBE_H
#define WRF_SDIRK3_TRANSPOSE_PROBE_H

#include <torch/torch.h>

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>

namespace wrf {
namespace sdirk3 {
namespace transpose_probe {

// A matrix-free linear operator. In production this is UnifiedPreconditioner::apply and its
// autograd VJP; in the contract it is a matmul against a known matrix.
using LinearOp = std::function<torch::Tensor(const torch::Tensor&)>;

struct TransposeReport {
    // --- properties of the FORWARD operator M. A transpose is only well defined if these
    // hold, so they are measured first and reported even when no transpose was supplied.
    double symmetry = 0;        // |<Mv,w> - <v,Mw>| / max(|.|). 0 => M^T = M, reuse it.
    double linearity = 0;       // max|M(2v) - 2M(v)| / max|M(v)|. Nonzero => no transpose.
    double repeatability = 0;   // max|M(v) - M(v)| on a second call. Nonzero => stateful.
    double additivity = 0;      // max|M(v+w) - M(v) - M(w)| / max|M(v)|

    // ||Mv|| / ||v|| on a random unit-ish direction. A LOWER bound on ||M||, and the number
    // that turns a solver's ||x|| into a statement about conditioning: if a solve returns
    // ||x|| >> ||b|| while ||b - Mx|| ~ ||b||, then sigma_min(M) <= (||b||+||r||)/||x||, and
    // gain/that ratio is a lower bound on the condition number. Measured here so the
    // argument does not depend on a sigma_max carried over from another session.
    double forward_gain = 0;

    // --- the CLAIMED transpose.
    bool   transpose_supplied = false;
    // How far the claimed transpose is from simply handing the cotangent back, and how far
    // the FORWARD is from the identity. Reported as NUMBERS, not as a bool: the first
    // version of this header asked for exact equality, and on the production preconditioner
    // that test silently failed to fire even though the operator was severed -- the
    // recorded graph was near-identity rather than bit-identity. A predicate you cannot see
    // the margin of is a predicate you cannot debug.
    double transpose_identity_residual = 0;  // max|M^Tw - w| / max|w|
    double forward_identity_residual = 0;    // max|Mw   - w| / max|w|
    double transpose_error = 0;              // |<Mv,w> - <v,M^Tw>| / max(|.|). THE verdict:
                                             // this is the definition of a transpose, so it
                                             // decides, and SEVERED requires it to fail.
    std::string threw;                       // non-empty if the claimed transpose threw

    // Severed: the claimed transpose returns its input, while M itself does not, AND it
    // fails the transpose identity.
    //
    // THAT LAST CLAUSE IS LOAD-BEARING (review section 7). Without it this has a
    // mathematical false positive, by counterexample:
    //
    //     P = [[1,1],[0,2]],  w = (1,-1)^T   =>   P^T w = w   but   P w = (0,-2) != w
    //
    // An EXACT transpose can leave a particular direction fixed while the forward does not.
    // Identity-looking behaviour on one probe direction is a HINT about why a transpose is
    // wrong; it is not evidence that it is wrong. The bilinear identity
    // <Mv,w> == <v,M^Tw> IS the definition, so it decides, and the identity residual only
    // explains the failure it has already established.
    //
    // The forward/transpose comparison is RELATIVE, not an absolute tolerance, and the
    // production measurement is why: the claimed transpose reproduced its input to 8.8e-05
    // while M moved the same vector by 1.21. An absolute bar tight enough to mean
    // "identity" (1e-6) did not fire, and one loose enough to fire (1e-3) would be a number
    // picked to make the answer come out.
    //
    // forward_identity_residual > tol keeps a genuinely-identity M from being accused: if M
    // really is the identity, returning w is the CORRECT transpose.
    bool transpose_is_identity(double frac = 0.01,
                               double tol = 1e-6,
                               double bilinear_tol = 1e-5) const {
        return transpose_supplied && threw.empty() &&
               transpose_error > bilinear_tol &&        // it must actually BE wrong
               forward_identity_residual > tol &&
               transpose_identity_residual < frac * forward_identity_residual;
    }

    // Exact zero is the WRONG bar, and demanding it is a defect this contract caught: of
    // the three, only additivity is inexact even for a perfect matmul, because v+w rounds
    // before the operator does. On the production run it measured 2.259e-07 -- float32
    // rounding over ~1e6 terms, not a nonlinearity. So the judgement takes a tolerance
    // while the report keeps the raw numbers.
    bool forward_is_fixed_linear(double tol = 1e-5) const {
        return linearity <= tol && repeatability <= tol && additivity <= tol;
    }

    // `what` names the operator under test, so two probes in one run are distinguishable
    // in the log. The same instrument is pointed at the preconditioner AND at the transpose
    // OPERATOR (9F.D85) -- forward properties are the question in both cases.
    std::string summary(const std::string& what = "SDIRK3_TRANSPOSE_PROBE") const;
};

namespace detail {

inline double rel_max(const torch::Tensor& diff, const torch::Tensor& ref) {
    torch::NoGradGuard no_grad;
    return (diff.abs().max() / ref.abs().max().clamp_min(1e-30)).template item<double>();
}

inline double dot(const torch::Tensor& a, const torch::Tensor& b) {
    torch::NoGradGuard no_grad;
    return (a * b).sum().template item<double>();
}

inline double norm_of(const torch::Tensor& t) {
    torch::NoGradGuard no_grad;
    return t.norm().template item<double>();
}

inline double rel_scalar(double a, double b) {
    const double den = std::max(std::abs(a), std::abs(b));
    return den > 0 ? std::abs(a - b) / den : 0.0;
}

}  // namespace detail

// M            -- the forward operator, as production calls it.
// M_transpose  -- the claimed transpose. Pass an empty std::function to measure only the
//                 forward properties.
//
// v and w are drawn from a seeded generator so two runs of the same binary compare
// like-for-like, and so a result can be reproduced offline.
inline TransposeReport probe_transpose(const LinearOp& M,
                                       const LinearOp& M_transpose,
                                       int64_t n,
                                       torch::TensorOptions opts,
                                       uint64_t seed) {
    // NO blanket NoGradGuard here, deliberately. 9F.D85.
    //
    // The reductions below already guard themselves (detail::dot, detail::rel_max), which is
    // the whole of what the project rule asks for: guard each .item(), and nothing that calls
    // back into an operator which may need a graph.
    //
    // A guard at this level does exactly the forbidden thing -- M and M_transpose are the
    // CALLER'S operators, and an operator whose job is to build a VJP dies under it. It cost
    // a rebuild here: the preconditioner probe survived only because apply_transpose_ad
    // forces AutoGradMode(true) and overrode the guard, so the defect stayed invisible until
    // the same instrument was pointed at an operator that does not.
    //
    // FOURTH occurrence of this pattern in the campaign (D59 power_iterate_sigma_max, D66 the
    // adjoint driver, D80's reading of it, now here) -- and this time in the instrument
    // written to prevent exactly that class of error. Grad_Required_Operator below is the
    // fixture that fails if anyone reintroduces it.
    TransposeReport r;

    auto gen = at::detail::createCPUGenerator(seed);
    auto draw = [&] {
        return torch::randn(n, gen, torch::TensorOptions().dtype(opts.dtype()))
            .to(opts.device());
    };
    const auto v = draw();
    const auto w = draw();

    const auto Mv = M(v);
    const auto Mw = M(w);

    r.forward_gain = detail::norm_of(Mv) / std::max(detail::norm_of(v), 1e-300);
    r.symmetry = detail::rel_scalar(detail::dot(Mv, w), detail::dot(v, Mw));
    r.repeatability = detail::rel_max(M(v) - Mv, Mv);
    r.linearity = detail::rel_max(M(2.0 * v) - 2.0 * Mv, Mv);
    r.additivity = detail::rel_max(M(v + w) - Mv - Mw, Mv);

    if (!M_transpose) return r;
    r.transpose_supplied = true;

    torch::Tensor MTw;
    try {
        MTw = M_transpose(w);
    } catch (const std::exception& e) {
        r.threw = e.what();
        return r;
    }
    if (!MTw.defined()) {
        r.threw = "claimed transpose returned an undefined tensor";
        return r;
    }

    // THE CHECK THAT D80 LACKED. A severed graph hands back the cotangent untouched, which
    // is a valid linear operator and passes every property above. Measure against the
    // FORWARD too: if M is genuinely the identity then returning w is correct, not severed.
    r.transpose_identity_residual = detail::rel_max(MTw - w, w);
    r.forward_identity_residual = detail::rel_max(Mw - w, w);
    r.transpose_error = detail::rel_scalar(detail::dot(Mv, w), detail::dot(v, MTw));
    return r;
}

inline std::string TransposeReport::summary(const std::string& what) const {
    std::ostringstream o;
    o << what
      << " symmetry=" << symmetry
      << " linearity=" << linearity
      << " repeatability=" << repeatability
      << " additivity=" << additivity
      << " gain=" << forward_gain;

    // Tolerance, not ==0.0 (review section 7): in float32 over ~1e6 elements the bilinear
    // reduction floor is ~1e-5, so an exact comparison never fires on a genuinely
    // self-adjoint operator and quietly withholds a true finding.
    if (symmetry <= 1e-5) {
        o << "  M IS SELF-ADJOINT: reuse M^-1 as M^-T";
    } else if (!forward_is_fixed_linear()) {
        o << "  M IS NOT A FIXED LINEAR OPERATOR: no transpose exists";
    }

    if (!transpose_supplied) {
        o << "  (no transpose supplied)";
        return o.str();
    }
    if (!threw.empty()) {
        o << "  TRANSPOSE THREW: " << threw;
        return o.str();
    }
    o << " identity_residual=" << transpose_identity_residual
      << " forward_identity_residual=" << forward_identity_residual;
    o << " transpose_error=" << transpose_error;
    if (transpose_is_identity()) {
        // transpose_error IS printed here now, because SEVERED requires it to be large --
        // it is the evidence, not a distance-to-correct. D80 misread 0.5223 as "52%
        // accurate"; the guard against that is the verdict word, not hiding the number.
        o << "  TRANSPOSE SEVERED: it fails the transpose identity AND returns its own"
             " input while M does not. The graph recorded nothing.";
        return o.str();
    }
    o << (transpose_error < 1e-5 ? "  TRANSPOSE VERIFIED" : "  TRANSPOSE WRONG");
    return o.str();
}

}  // namespace transpose_probe
}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_TRANSPOSE_PROBE_H
