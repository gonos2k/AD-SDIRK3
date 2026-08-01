// wrf_sdirk3_response_probe.h -- directional response measurement, as a testable instrument.
//
// 9F.D58 (review sections 6 and 11). The D54-D56 probes lived INSIDE computeUnifiedRHS and
// called it recursively. That is not an observer: it perturbs RHS evaluation counts, cache
// and epoch state, allocation timing and the global RNG, and its recursion guard was not
// exception-safe. The review asks for the logic to move out behind a small ProbeSpec /
// ResponseResult pair, and for the measurement itself to be central-differenced,
// amplitude-laddered and properly normalised. That is what this is.
//
// It takes the RHS as a std::function, so nothing here knows about the solver. The point is
// not decoupling for its own sake -- it is that the instrument can then be validated
// against a SYNTHETIC operator whose Jacobian is known exactly, which is the one thing the
// in-solver probes could never do.
//
// ---------------------------------------------------------------------------------------
// THE ERROR THIS API IS SHAPED TO PREVENT
//
// D55/D56 normalised with ONE set of per-channel scales, used for both the perturbation and
// the response. That is wrong here: the packed state holds UNCOUPLED velocities (measured
// u_max = 67 m/s at the b_wave jet) while the tendency components are COUPLED, d(mu*u)/dt
// -- the solver itself divides ru_tend by mu_typical to recover a velocity tendency
// (wrf_sdirk3_tile_unified_impl.cpp:20544). Dividing a coupled tendency by an uncoupled
// scale inflated ru/rv/rt by ~mu ~ 1e5, and I read the inflated number as a physical
// result before catching it.
//
// So ProbeSpec REQUIRES state_scale and tendency_scale as separate members. They cannot be
// defaulted to each other and there is no single-scale constructor. Getting the units wrong
// now takes a deliberate act rather than an oversight.
// ---------------------------------------------------------------------------------------

#ifndef WRF_SDIRK3_RESPONSE_PROBE_H
#define WRF_SDIRK3_RESPONSE_PROBE_H

#include <torch/torch.h>

#include <cstdint>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace wrf {
namespace sdirk3 {
namespace probe {

// One contiguous block of the packed state, with BOTH of its scales.
struct ChannelSpec {
    std::string name;
    int64_t     offset = 0;
    int64_t     length = 0;
    // The characteristic magnitude of the STATE component (e.g. u ~ 10 m/s).
    double      state_scale = 1.0;
    // The characteristic magnitude of the TENDENCY component, in the tendency's OWN units.
    // For a coupled momentum tendency this includes the mu factor; it is NOT state_scale/dt.
    double      tendency_scale = 1.0;
};

struct ProbeSpec {
    std::vector<ChannelSpec> channels;
    // Amplitude ladder, as multiples of state_scale. A genuine linear coupling gives the
    // same normalised response at every entry; divergence across them is the tell that the
    // number is nonlinear amplification and must not be quoted as a coupling strength.
    std::vector<double> amplitudes{0.25, 0.5, 1.0};
    std::uint64_t seed = 0;
    // Central differencing. One-sided differences carry an O(eps) Hessian term:
    //   F(U+d) - F(U) = J d + (1/2) H[d,d] + ...
    // whereas the central form cancels it to O(eps^2). D55/D56 were one-sided.
    bool central = true;
    double dt = 1.0;      // multiplies the response so entries read "per timestep"
};

struct ChannelResponse {
    std::string kicked;
    double      amplitude = 0.0;
    // Per responding channel, normalised by that channel's OWN tendency scale.
    std::vector<double> max_abs;    // worst cell -- sensitive to a single outlier
    std::vector<double> rms;        // bulk response
    std::vector<double> p99;        // the shoulder between the two
    std::vector<int64_t> argmax;    // flat index of the worst cell, so it can be located
    // The headline number: rms response / (amplitude * state_scale/tendency_scale). For a
    // unit-RMS direction this is the operator's bulk gain and is independent of the draw.
    // rms rather than max because the review asked for energy-weighted measures and
    // because max cannot distinguish one bad cell from the whole field.
    std::vector<double> gain_rms;
};

struct ResponseResult {
    std::vector<ChannelResponse> rows;
    // Linearity verdict per (kicked, responding) pair: max relative spread of the
    // amplitude-normalised response across the ladder. <= tol means linear.
    std::vector<std::vector<double>> linearity_spread;
    std::vector<std::string> channel_names;
};

// 9F.D72 (review section 15.4): REJECT a bad ProbeSpec, do not repair it.
//
// The measurement code below used `s > 0 ? s : 1.0` and `a > 0 ? a : 1.0`, which turns a
// misconfigured diagnostic into a plausible number. That is precisely the class this
// campaign has spent itself eliminating elsewhere -- eps for a broken metric, "a typical
// atmosphere" for a missing base state, dry for a NaN moisture -- and I reproduced it in
// the instrument built to catch such things.
//
// It is worse in a probe than in the solver. A wrong solver value eventually shows up in
// a trajectory; a wrong PROBE value is read as a measurement and written into a report.
//
// So the spec is validated up front and the silent fallbacks are gone. Coverage and
// overlap are checked too: a channel table that leaves a gap or double-counts a region
// still produces per-channel numbers that look entirely reasonable.
inline void validate_probe_spec(const ProbeSpec& spec, int64_t state_numel) {
    auto fail = [](const std::string& why) {
        throw std::invalid_argument("SDIRK3 ProbeSpec is not usable: " + why +
            ". Refusing to substitute a default -- a misconfigured probe returns numbers "
            "that are read as measurements.");
    };
    if (spec.channels.empty()) fail("no channels");
    if (!(spec.dt > 0.0) || !std::isfinite(spec.dt))
        fail("dt = " + std::to_string(spec.dt) + " (must be finite and > 0)");
    if (spec.amplitudes.empty()) fail("no amplitudes");
    for (double a : spec.amplitudes)
        if (!(a > 0.0) || !std::isfinite(a))
            fail("amplitude " + std::to_string(a) + " (must be finite and > 0)");

    int64_t covered = 0;
    for (size_t c = 0; c < spec.channels.size(); ++c) {
        const auto& ch = spec.channels[c];
        if (ch.length <= 0) fail("channel '" + ch.name + "' has length " +
                                 std::to_string(ch.length));
        if (ch.offset < 0) fail("channel '" + ch.name + "' has negative offset");
        if (ch.offset > state_numel - ch.length)
            fail("channel '" + ch.name + "' runs past the packed state (offset " +
                 std::to_string(ch.offset) + " + length " + std::to_string(ch.length) +
                 " > " + std::to_string(state_numel) + ")");
        if (!(ch.state_scale > 0.0) || !std::isfinite(ch.state_scale))
            fail("channel '" + ch.name + "' state_scale = " +
                 std::to_string(ch.state_scale));
        if (!(ch.tendency_scale > 0.0) || !std::isfinite(ch.tendency_scale))
            fail("channel '" + ch.name + "' tendency_scale = " +
                 std::to_string(ch.tendency_scale));
        for (size_t d = 0; d < c; ++d) {
            const auto& o = spec.channels[d];
            if (ch.offset < o.offset + o.length && o.offset < ch.offset + ch.length)
                fail("channels '" + ch.name + "' and '" + o.name + "' overlap");
        }
        covered += ch.length;
    }
    if (covered != state_numel)
        fail("channels cover " + std::to_string(covered) + " of " +
             std::to_string(state_numel) + " packed-state elements");
}

namespace detail {

inline double quantile_abs(const torch::Tensor& t, double q) {
    torch::NoGradGuard no_grad;
    auto flat = t.abs().flatten().to(torch::kFloat64);
    if (flat.numel() == 0) return 0.0;
    return flat.quantile(q).item<double>();
}

}  // namespace detail

// rhs_fn must be pure with respect to the probe: calling it must not advance solver state.
// That is the caller's obligation and is why this does not live inside computeUnifiedRHS.
inline ResponseResult measure_response(
    const std::function<torch::Tensor(const torch::Tensor&)>& rhs_fn,
    const torch::Tensor& U,
    const ProbeSpec& spec)
{
    validate_probe_spec(spec, U.numel());
    torch::NoGradGuard no_grad;
    const int nc = static_cast<int>(spec.channels.size());

    ResponseResult out;
    for (const auto& c : spec.channels) out.channel_names.push_back(c.name);

    auto slice_of = [&](const torch::Tensor& v, int r) {
        return v.narrow(0, spec.channels[r].offset, spec.channels[r].length);
    };

    const auto F0 = rhs_fn(U);

    // amplitude-normalised responses, for the linearity verdict
    std::vector<std::vector<std::vector<double>>> norm_resp(
        nc, std::vector<std::vector<double>>(nc));

    for (int c = 0; c < nc; ++c) {
        // The generator is CREATED AND USED. D54 constructed one and then called the
        // global torch::randn, so its "seeded control" was not seeded at all -- the review
        // caught that. Here the generator is threaded into randn explicitly.
        auto gen = at::detail::createCPUGenerator(spec.seed + static_cast<uint64_t>(c));
        auto dir = torch::zeros_like(U);
        {
            auto noise = torch::randn(spec.channels[c].length, gen,
                                      torch::TensorOptions().dtype(U.dtype()));
            // 9F.D58: normalise the direction to UNIT RMS before scaling. Without this the
            // reported gain depends on the random draw -- for F(U)=2U the max-based number
            // came out as 2*max|dir| ~ 4 rather than 2, i.e. the instrument's answer moved
            // with the seed. Caught by the synthetic-operator test, which is the whole
            // reason that test exists.
            auto rms = noise.pow(2).mean().sqrt();
            noise = noise / rms.clamp_min(1e-300);
            slice_of(dir, c).copy_(noise.to(U.device()) * spec.channels[c].state_scale);
        }

        for (double a : spec.amplitudes) {
            torch::Tensor diff;
            if (spec.central) {
                diff = (rhs_fn(U + a * dir) - rhs_fn(U - a * dir)) / 2.0;
            } else {
                diff = rhs_fn(U + a * dir) - F0;
            }

            ChannelResponse row;
            row.kicked = spec.channels[c].name;
            row.amplitude = a;
            for (int r = 0; r < nc; ++r) {
                auto d = slice_of(diff, r).to(torch::kFloat64);
                // validate_probe_spec has already rejected a non-positive scale, so no
                // silent fallback is needed or wanted here.
                const double f = spec.dt / spec.channels[r].tendency_scale;
                const double mx = d.numel() ? d.abs().max().item<double>() : 0.0;
                row.max_abs.push_back(mx * f);
                row.rms.push_back(d.numel() ? d.pow(2).mean().sqrt().item<double>() * f : 0.0);
                row.p99.push_back(detail::quantile_abs(d, 0.99) * f);
                row.argmax.push_back(d.numel() ? d.abs().argmax().item<int64_t>() : -1);
                const double rms_v = d.numel()
                    ? d.pow(2).mean().sqrt().item<double>() * f : 0.0;
                const double g = rms_v / a;
                row.gain_rms.push_back(g);
                norm_resp[c][r].push_back(g);
            }
            out.rows.push_back(std::move(row));
        }
    }

    out.linearity_spread.assign(nc, std::vector<double>(nc, 0.0));
    for (int c = 0; c < nc; ++c)
        for (int r = 0; r < nc; ++r) {
            const auto& v = norm_resp[c][r];
            double lo = 0.0, hi = 0.0;
            for (size_t i = 0; i < v.size(); ++i) {
                if (i == 0 || v[i] < lo) lo = v[i];
                if (i == 0 || v[i] > hi) hi = v[i];
            }
            out.linearity_spread[c][r] = (hi > 0.0) ? (hi - lo) / hi : 0.0;
        }
    return out;
}


// ---------------------------------------------------------------------------------------
// LEADING SINGULAR AMPLIFICATION (review sections 10 and 12).
//
// For a NON-NORMAL operator the eigenvalues do not bound the transient response: a matrix
// with every eigenvalue 1 can amplify by 100 in a single application. That is the whole
// reason this is worth measuring rather than inferring from the coupling matrix -- every
// individual entry can be state-invariant while the leading singular direction is not.
//
//     sigma_max = max_v |S_F^-1 J S_U v| / |v|
//
// computed by power iteration on (S_F^-1 J S_U)^T (S_F^-1 J S_U), matrix-free: J.v from
// the caller's JVP (central differences or forward AD) and J^T.w from its VJP.
//
// The scaling is the same S_U / S_F the response matrix uses, and for the same reason: an
// unscaled sigma_max on a state whose blocks differ by 1e5 in units measures the unit
// choice. It is applied as S_F^-1 J S_U so that v lives in scaled state space and the
// block decomposition of the leading right singular vector is directly readable.
struct SigmaResult {
    double sigma_max = 0.0;
    std::vector<double> iterates;          // per-iteration estimate, to show convergence
    std::vector<double> block_weight;      // fraction of |v|^2 in each channel
    bool converged = false;
};

inline SigmaResult power_iterate_sigma_max(
    const std::function<torch::Tensor(const torch::Tensor&)>& jvp,   // v -> J.v
    const std::function<torch::Tensor(const torch::Tensor&)>& vjp,   // w -> J^T.w
    const torch::Tensor& like,
    const ProbeSpec& spec,
    int iters = 20,
    double tol = 1e-4)
{
    validate_probe_spec(spec, like.numel());
    // NO blanket NoGradGuard here, deliberately. The caller's vjp is an AD reverse pass
    // and needs to BUILD a graph; a guard around this whole function would silently
    // disable it and the VJP would return zeros -- which looks like "the adjoint says the
    // operator does nothing" rather than like a bug. Each .item() gets its own guard
    // instead, which is what the project rule actually requires.
    SigmaResult out;
    const int nc = static_cast<int>(spec.channels.size());

    // diagonal scalings, as vectors over the packed layout
    auto s_u = torch::ones_like(like), s_f = torch::ones_like(like);
    for (int c = 0; c < nc; ++c) {
        s_u.narrow(0, spec.channels[c].offset, spec.channels[c].length)
            .fill_(spec.channels[c].state_scale);
        s_f.narrow(0, spec.channels[c].offset, spec.channels[c].length)
            .fill_(spec.channels[c].tendency_scale);
    }

    auto gen = at::detail::createCPUGenerator(spec.seed);
    auto v = torch::randn(like.numel(), gen,
                          torch::TensorOptions().dtype(like.dtype())).to(like.device());
    v = v / v.norm().clamp_min(1e-300);

    double prev = 0.0;
    for (int it = 0; it < iters; ++it) {
        auto Av  = jvp(s_u * v) / s_f;                 // S_F^-1 J S_U v
        double sigma;
        { torch::NoGradGuard ng; sigma = Av.norm().item<double>(); }   // |v| = 1
        out.iterates.push_back(sigma);
        auto AtAv = s_u * vjp(Av.detach() / s_f);       // S_U J^T S_F^-1 (S_F^-1 J S_U v)
        double n;
        { torch::NoGradGuard ng; n = AtAv.norm().item<double>(); }
        if (!(n > 0.0) || !std::isfinite(n)) break;
        v = AtAv.detach() / n;
        if (it > 0 && std::abs(sigma - prev) <= tol * std::max(sigma, 1e-300)) {
            out.converged = true;
            out.sigma_max = sigma;
            break;
        }
        prev = sigma;
        out.sigma_max = sigma;
    }

    torch::NoGradGuard ng_final;
    const double vn2 = v.pow(2).sum().item<double>();
    for (int c = 0; c < nc; ++c) {
        auto blk = v.narrow(0, spec.channels[c].offset, spec.channels[c].length);
        out.block_weight.push_back(vn2 > 0.0 ? blk.pow(2).sum().item<double>() / vn2 : 0.0);
    }
    return out;
}

}  // namespace probe
}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_RESPONSE_PROBE_H
