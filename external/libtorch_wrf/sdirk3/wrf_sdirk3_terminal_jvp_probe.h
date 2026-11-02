#ifndef WRF_SDIRK3_TERMINAL_JVP_PROBE_H
#define WRF_SDIRK3_TERMINAL_JVP_PROBE_H

#if (defined(WRF_SDIRK3_TERMINAL_JVP_PROBE) || defined(WRF_SDIRK3_MATCHED_PRESSURE_PROBE)) && !defined(WRF_SDIRK3_LINEAR_DELAY_AUDIT)
#error "terminal JVP probe requires WRF_SDIRK3_LINEAR_DELAY_AUDIT"
#endif
#if defined(WRF_SDIRK3_TERMINAL_JVP_PROBE) || defined(WRF_SDIRK3_MATCHED_PRESSURE_PROBE)
#include <torch/torch.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <unistd.h>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

namespace wrf_terminal_jvp {
struct Snapshot { std::string context; int64_t path_count = 0, fallback_count = 0, internal_fd_count = 0; bool valid = true; };
struct Timing { double wall_seconds = 0, cpu_seconds = 0; };
inline bool clock_valid(Timing t) {
    return std::isfinite(t.wall_seconds) && std::isfinite(t.cpu_seconds) &&
           t.wall_seconds >= 0 && t.cpu_seconds >= 0;
}
inline std::string json_number(double v) {
    if (!std::isfinite(v)) return "null";
    std::ostringstream out; out << std::setprecision(17) << v; return out.str();
}
inline double cpu_seconds() {
    rusage r{}; if (getrusage(RUSAGE_SELF, &r) != 0) throw std::runtime_error("getrusage failed");
    return r.ru_utime.tv_sec + r.ru_utime.tv_usec * 1.e-6 +
           r.ru_stime.tv_sec + r.ru_stime.tv_usec * 1.e-6;
}
inline std::string quote(const std::string& s) {
    std::ostringstream o; o << '"';
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') o << '\\' << c;
        else if (c < 32) o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c) << std::dec;
        else o << c;
    }
    return o.str() + '"';
}
inline uint64_t hash(const torch::Tensor& t) {
    auto c = t.detach().contiguous();
    const auto* p = static_cast<const unsigned char*>(c.data_ptr());
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < c.nbytes(); ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
struct Comparison { bool exact = false; double max_abs = 0, relative = 0; };
inline Comparison compare(const torch::Tensor& a, const torch::Tensor& b) {
    torch::NoGradGuard guard;
    Comparison c;
    if (!a.defined() || !b.defined() || a.device() != b.device() ||
        a.scalar_type() != torch::kFloat64 || b.scalar_type() != torch::kFloat64 ||
        a.sizes() != b.sizes() || !a.device().is_cpu() ||
        !torch::isfinite(a).all().item<bool>() || !torch::isfinite(b).all().item<bool>()) return c;
    auto x = a.detach().contiguous(), y = b.detach().contiguous();
    c.exact = std::memcmp(x.data_ptr(), y.data_ptr(), x.nbytes()) == 0;
    if (x.numel()) {
        c.max_abs = (x - y).abs().max().item<double>();
        double scale = x.abs().max().item<double>();
        c.relative = scale == 0 ? (c.max_abs == 0 ? 0 : std::numeric_limits<double>::max()) : c.max_abs / scale;
        if (!std::isfinite(c.max_abs)) c.max_abs = std::numeric_limits<double>::max();
        if (!std::isfinite(c.relative)) c.relative = std::numeric_limits<double>::max();
    }
    return c;
}
// repeat returns {primal,tangent}; snapshot is captured after each call.
// The caller supplies the original call's timing and never resumes the solve.
template<class Repeat, class GetSnapshot>
bool run_terminal_jvp_probe(const std::string& path, const std::string& metadata,
        const torch::Tensor& u, const torch::Tensor& v,
        const torch::Tensor& primal, const torch::Tensor& tangent, Timing original,
        Repeat repeat, GetSnapshot snapshot) {
    torch::NoGradGuard guard;
    bool ok = true;
    std::string reason;
    auto fail = [&](const char* s) { ok = false; if (reason.empty()) reason = s; };
    struct Sample { Timing t; Comparison p, j; Snapshot s; bool inputs_unchanged = false; };
    std::vector<Sample> samples;
    Snapshot initial = snapshot();
    if (!u.defined() || !v.defined() || !u.device().is_cpu() || !v.device().is_cpu() ||
        u.scalar_type() != torch::kFloat64 || v.scalar_type() != torch::kFloat64 ||
        u.sizes() != v.sizes() || !u.numel() || !torch::isfinite(u).all().item<bool>() ||
        !torch::isfinite(v).all().item<bool>() || !v.ne(0).any().item<bool>() ||
        torch::get_num_threads() != 1 || torch::get_num_interop_threads() != 1) fail("invalid_input_or_threads");
    if (!clock_valid(original)) fail("invalid_timing");
    if (!initial.valid) fail("invalid_context");
    if (!compare(primal, primal).exact || !compare(tangent, tangent).exact) fail("invalid_original_output");
    uint64_t uh = 0, vh = 0, ph = 0, jh = 0;
    if (ok) { uh = hash(u); vh = hash(v); ph = hash(primal); jh = hash(tangent); }
    Snapshot previous = initial;
    const bool can_repeat = ok;
    for (int i = 0; i < 5 && can_repeat; ++i) {
        Sample s;
        try {
            const double c0 = cpu_seconds();
            const auto w0 = std::chrono::steady_clock::now();
            auto result = repeat();
            const auto w1 = std::chrono::steady_clock::now();
            const double c1 = cpu_seconds();
            s.t = {std::chrono::duration<double>(w1-w0).count(), c1-c0};
            s.p = compare(primal, result.first); s.j = compare(tangent, result.second);
            s.inputs_unchanged = hash(u) == uh && hash(v) == vh;
            s.s = snapshot();
            samples.push_back(s);
            if (!clock_valid(s.t)) { fail("invalid_timing"); break; }
            if (!s.inputs_unchanged) { fail("input_mutation"); break; }
            if (!s.p.exact || !s.j.exact) fail("output_mismatch");
            if (!s.s.valid || s.s.context != initial.context) { fail("context_changed"); break; }
            if (s.s.path_count - previous.path_count != 1) { fail("path_count_mismatch"); break; }
            previous = s.s;
            if (s.s.internal_fd_count != initial.internal_fd_count) { fail("internal_fd_used"); break; }
            if (s.s.fallback_count != initial.fallback_count) { fail("fallback_used"); break; }
        } catch (...) { fail("repeat_exception"); break; }
    }
    std::ostringstream out; out << std::setprecision(17);
    out << "{\"status\":" << quote(ok ? "terminal_diagnostic_passed" : "terminal_diagnostic_failed")
        << ",\"reason\":" << quote(reason) << ",\"metadata\":" << quote(metadata)
        << ",\"forecast_complete\":false,\"context_scope\":\"observed_subset\",\"hash_algorithm\":\"fnv1a64\",\"u_hash\":" << quote(std::to_string(uh)) << ",\"v_hash\":" << quote(std::to_string(vh)) << ",\"primal_hash\":" << quote(std::to_string(ph)) << ",\"tangent_hash\":" << quote(std::to_string(jh))
        << ",\"sample_count\":" << 1 + samples.size() << ",\"original\":{\"wall_seconds\":" << json_number(original.wall_seconds)
        << ",\"cpu_seconds\":" << json_number(original.cpu_seconds) << ",\"clock_valid\":" << (clock_valid(original) ? "true" : "false") << ",\"valid\":" << (initial.valid ? "true" : "false") << ",\"context\":" << quote(initial.context)
        << ",\"path_count\":" << initial.path_count << ",\"fallback_count\":" << initial.fallback_count << ",\"internal_fd_count\":" << initial.internal_fd_count << "},\"repeats\":[";
    for (size_t i=0; i<samples.size(); ++i) {
        const auto& s = samples[i]; if (i) out << ',';
        out << "{\"wall_seconds\":" << json_number(s.t.wall_seconds) << ",\"cpu_seconds\":" << json_number(s.t.cpu_seconds)
            << ",\"clock_valid\":" << (clock_valid(s.t) ? "true" : "false") << ",\"primal_exact\":" << (s.p.exact ? "true":"false") << ",\"primal_max_abs\":" << s.p.max_abs
            << ",\"primal_relative\":" << s.p.relative << ",\"tangent_exact\":" << (s.j.exact ? "true":"false")
            << ",\"tangent_max_abs\":" << s.j.max_abs << ",\"tangent_relative\":" << s.j.relative
            << ",\"inputs_unchanged\":" << (s.inputs_unchanged ? "true":"false") << ",\"valid\":" << (s.s.valid ? "true" : "false") << ",\"context\":" << quote(s.s.context)
            << ",\"path_count\":" << s.s.path_count << ",\"fallback_count\":" << s.s.fallback_count << ",\"internal_fd_count\":" << s.s.internal_fd_count << '}';
    }
    out << "]}\n";
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) return false;
    const auto text = out.str(); size_t n = 0;
    while (n < text.size()) { auto k = ::write(fd, text.data()+n, text.size()-n); if (k <= 0) break; n += k; }
    int close_result = ::close(fd);
    bool written = n == text.size() && close_result == 0;
    return ok && written;
}
}
#endif
#endif
