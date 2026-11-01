#define WRF_SDIRK3_TERMINAL_JVP_PROBE
#define WRF_SDIRK3_LINEAR_DELAY_AUDIT
#include "../wrf_sdirk3_terminal_jvp_probe.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>

int main() {
    torch::set_num_threads(1); torch::set_num_interop_threads(1);
    auto opt = torch::TensorOptions().dtype(torch::kFloat64);
    auto u = torch::tensor({1.0, 2.0}, opt), v = torch::tensor({3.0, -1.0}, opt);
    auto f = [&]() {
        auto p = torch::stack({u[0]*u[0] + u[1], u[0]*u[1]});
        auto j = torch::stack({2*u[0]*v[0] + v[1], v[0]*u[1] + u[0]*v[1]});
        return std::make_pair(p, j);
    };
    auto original = f(); int calls = 0;
    auto snapshot = [&]() { return wrf_terminal_jvp::Snapshot{"fixed", calls, 0, 0, true}; };
    const std::string path = "/tmp/test_terminal_jvp_probe_" + std::to_string(getpid()) + ".json";
    auto repeat = [&]() { ++calls; return f(); };
    assert(wrf_terminal_jvp::run_terminal_jvp_probe(path, "analytic", u, v, original.first, original.second, {}, repeat, snapshot));
    assert(calls == 5);
    std::ifstream in(path); std::string text((std::istreambuf_iterator<char>(in)), {});
    assert(text.find("terminal_diagnostic_passed") != std::string::npos);
    assert(text.find("\"sample_count\":6") != std::string::npos);
    assert(!wrf_terminal_jvp::run_terminal_jvp_probe(path, "existing", u, v, original.first, original.second, {}, repeat, snapshot));
    std::remove(path.c_str());
    auto changed = [&]() { ++calls; auto r = f(); r.second += 1; return r; };
    assert(!wrf_terminal_jvp::run_terminal_jvp_probe(path, "mismatch", u, v, original.first, original.second, {}, changed, snapshot));
    std::ifstream bad(path); text.assign(std::istreambuf_iterator<char>(bad), {});
    assert(text.find("output_mismatch") != std::string::npos);
    std::remove(path.c_str());
    auto check_failure = [&](const std::string& expected, auto fn, auto snap, wrf_terminal_jvp::Timing timing = {}) {
        std::remove(path.c_str());
        assert(!wrf_terminal_jvp::run_terminal_jvp_probe(path, "negative", u, v, original.first, original.second, timing, fn, snap));
        std::ifstream file(path); std::string report((std::istreambuf_iterator<char>(file)), {});
        assert(report.find(expected) != std::string::npos);
        if (expected == "invalid_timing") {
            assert(report.find("\"wall_seconds\":null") != std::string::npos);
            assert(report.find("\"clock_valid\":false") != std::string::npos);
        }
        std::remove(path.c_str());
    };
    auto still = [&]() { return f(); };
    check_failure("path_count_mismatch", still, snapshot);
    int fallback = 0;
    auto fall_snap = [&]() { return wrf_terminal_jvp::Snapshot{"fixed", calls, fallback, 0, true}; };
    auto fall = [&]() { ++calls; ++fallback; return f(); };
    check_failure("fallback_used", fall, fall_snap);
    std::string ctx = "fixed";
    auto ctx_snap = [&]() { return wrf_terminal_jvp::Snapshot{ctx, calls, 0, 0, true}; };
    auto change_ctx = [&]() { ++calls; ctx = "changed"; return f(); };
    check_failure("context_changed", change_ctx, ctx_snap);
    auto mutate = [&]() { ++calls; u[0] += 1; return f(); };
    check_failure("input_mutation", mutate, snapshot);
    u[0] -= 1;
    check_failure("invalid_timing", repeat, snapshot, {std::numeric_limits<double>::quiet_NaN(), 0});
}
