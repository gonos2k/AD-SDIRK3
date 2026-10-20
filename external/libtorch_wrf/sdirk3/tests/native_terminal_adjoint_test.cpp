#include "native_terminal_adjoint_test.h"

#ifdef WRF_SDIRK3_NATIVE_TERMINAL_TEST

#include "../wrf_sdirk3_state_layout.h"
#include "../wrf_sdirk3_tile_unified.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <unordered_map>
#include <utility>
#include <vector>

using TileSDIRK3UnifiedSolver = ::TileSDIRK3UnifiedSolver;
extern std::mutex g_tile_solvers_mutex;
extern std::unordered_map<void*, std::unique_ptr<TileSDIRK3UnifiedSolver>> g_tile_solvers;

namespace {
constexpr int kSteps = 256;
constexpr int kWindowSteps = 32;
constexpr std::size_t kMemoryCapBytes = 18ULL * 1024ULL * 1024ULL * 1024ULL;

struct RunState {
    std::mutex mutex;
    std::string mode_name;
    std::string chain_name;
    std::string output_prefix;
    std::string reference_path;
    int mode = -1;  // 0=baseline, 1=replay, 2=FD, 3=prefix.
    int expected_steps = kSteps;
    int graph_start_step = kSteps - kWindowSteps + 1;
    float eta = 0.0f;
    int host_step = 0;
    int window_start_step = 0;
    int reference_match_count = 0;
    int control_input64_calls = 0;
    int warm_capture_count = 0;
    int omega_ordinal = 0;
    int nx = 0, ny = 0, nz = 0, nx_u = 0, ny_v = 0, nz_w = 0;
    bool prepared = false;
    bool forward_active = false;
    bool warm_forward = false;
    bool endpoint_recorded = false;
    bool suffix_seed_matched = false;
    bool initial_control_seed_applied = false;
    torch::Tensor initial_seed64;
    torch::Tensor lambda64_chain;
    torch::Tensor lambda64_in;
    torch::Tensor lambda32_wrapper;
    torch::Tensor wrapper_gradient64;
    torch::Tensor direct_gradient64;
    torch::Tensor reference_checkpoints64;
    std::vector<torch::Tensor> endpoints64;
    torch::Tensor direction32;
    torch::Tensor terminal32;
    std::ofstream omega_binary;
    std::ofstream omega_csv;
    std::size_t rss_window_step4 = 0;
};
RunState run;

std::string required_env(const char* name) {
    const char* value = std::getenv(name);
    if (!value || !*value) throw std::runtime_error(std::string("missing ") + name);
    return value;
}

std::size_t peak_rss_bytes() {
    struct rusage usage {};
    if (getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#ifdef __APPLE__
    return static_cast<std::size_t>(usage.ru_maxrss);
#else
    return static_cast<std::size_t>(usage.ru_maxrss) * 1024ULL;
#endif
}

int parse_mode(const std::string& value) {
    if (value == "baseline") return 0;
    if (value == "replay") return 1;
    if (value == "prefix") return 3;
    if (value == "plus001" || value == "minus001" ||
        value == "plus003" || value == "minus003" ||
        value == "plus_0p01" || value == "minus_0p01" ||
        value == "plus_0p03" || value == "minus_0p03" || value == "fd") return 2;
    throw std::runtime_error("mode must be baseline, replay, prefix, or one of the four frozen FD runs");
}

int parse_steps() {
    const char* value = std::getenv("WRF_SDIRK3_NATIVE_TERMINAL_STEPS");
    if (!value || !*value) return kSteps;
    char* end = nullptr;
    const long steps = std::strtol(value, &end, 10);
    if (!end || *end || steps < kWindowSteps || steps > kSteps || steps % kWindowSteps != 0)
        throw std::runtime_error("STEPS must be a multiple of 32 in [32,256]");
    return static_cast<int>(steps);
}

float parse_eta(const std::string& value) {
    char* end = nullptr;
    const float eta = std::strtof(value.c_str(), &end);
    if (!end || *end || !std::isfinite(eta)) throw std::runtime_error("invalid eta");
    return eta;
}

uint64_t fnv1a64(const void* bytes, std::size_t size) {
    const auto* p = static_cast<const uint8_t*>(bytes);
    uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= p[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

void write_bytes(const std::string& path, const void* data, std::size_t bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot create " + path);
    out.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    if (!out.good()) throw std::runtime_error("failed writing " + path);
}

std::vector<uint8_t> read_bytes(const std::string& path, std::size_t expected_size) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    TORCH_CHECK(size >= 0 && static_cast<std::size_t>(size) == expected_size,
                "wrong byte size for ", path, ": got ", size, " expected ", expected_size);
    in.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(expected_size);
    in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(expected_size));
    TORCH_CHECK(in.good() || in.eof(), "failed reading ", path);
    return data;
}

bool bitwise_equal(const torch::Tensor& left, const torch::Tensor& right) {
    if (!left.defined() || !right.defined() || left.sizes() != right.sizes() ||
        left.scalar_type() != right.scalar_type()) return false;
    const auto a = left.detach().to(torch::kCPU).contiguous();
    const auto b = right.detach().to(torch::kCPU).contiguous();
    return a.nbytes() == b.nbytes() &&
        std::memcmp(a.const_data_ptr(), b.const_data_ptr(), static_cast<std::size_t>(a.nbytes())) == 0;
}

void write_tensor(const std::string& path, const torch::Tensor& value, torch::ScalarType dtype) {
    auto x = value.detach().to(torch::kCPU, dtype).contiguous();
    write_bytes(path, x.data_ptr(), static_cast<std::size_t>(x.numel()) * x.element_size());
}

TileSDIRK3UnifiedSolver& unique_solver_locked() {
    if (g_tile_solvers.size() != 1 || !g_tile_solvers.begin()->second)
        throw std::runtime_error("native terminal probe requires exactly one live WRF tile solver");
    return *g_tile_solvers.begin()->second;
}

int64_t index3(int j, int k, int i, int nz, int nx) {
    return (static_cast<int64_t>(j) * nz + k) * nx + i;
}

void make_direction(const wrf::sdirk3::StateLayout& layout,
                    int nx, int ny, int nz, int nx_u, int ny_v, int nz_w,
                    const torch::Tensor& x0, torch::Tensor& direction,
                    std::array<double, 6>& scales) {
    direction.zero_();
    auto d = direction.accessor<float, 1>();
    const auto x = x0.accessor<float, 1>();
    const std::array<double, 6> floors{
        7.744417044064484e-4, 1.0122007244795247e-4,
        7.79443985679733e-6, 1.0249750539744869e-2,
        3.619571391542998e-5, 1.4590900457561187e-2};
    const std::array<int, 6> dims_k{nz, nz, nz_w, nz_w, nz, 1};

    for (int b = 0; b < 6; ++b) {
        const auto& block = layout.blocks.at(static_cast<std::size_t>(b));
        long double sum_sq = 0.0L;
        for (int64_t q = 0; q < block.size; ++q) {
            const double value = x[block.start + q];
            sum_sq += static_cast<long double>(value) * value;
        }
        scales[b] = std::max(std::sqrt(static_cast<double>(sum_sq / block.size)), floors[b]);
    }

    auto set = [&](int b, int j, int k, int i, int nz_b, int nx_b, double raw) {
        const auto& block = layout.blocks.at(static_cast<std::size_t>(b));
        d[block.start + index3(j, k, i, nz_b, nx_b)] = static_cast<float>(raw);
    };
    auto raw = [](int b, int i, int j, int k) {
        const double phase = 0.37 * b;
        return std::sin(0.13 * (i + 1) + 0.19 * (j + 1) + 0.11 * (k + 1) + phase) +
               0.3 * std::cos(0.07 * (i + 1) - 0.17 * (j + 1) + 0.23 * (k + 1) - phase);
    };

    // U: 16 unique periodic faces, then the two Q aliases; reflected north row.
    for (int j = 0; j < ny - 1; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nx - 1; ++i)
                set(0, j, k, i, nz, nx_u, raw(0, i, j, k));
    for (int j = 0; j < ny - 1; ++j)
        for (int k = 0; k < nz; ++k) {
            set(0, j, k, nx - 1, nz, nx_u, d[layout.blocks[0].start + index3(j,k,0,nz,nx_u)]);
            set(0, j, k, nx, nz, nx_u, d[layout.blocks[0].start + index3(j,k,1,nz,nx_u)]);
        }
    for (int k = 0; k < nz; ++k)
        for (int i = 0; i < nx_u; ++i)
            set(0, ny - 1, k, i, nz, nx_u,
                d[layout.blocks[0].start + index3(ny - 2,k,i,nz,nx_u)]);

    // V: periodic X alias, zero physical wall faces, antisymmetric ghost row.
    for (int j = 1; j < ny - 1; ++j)
        for (int k = 0; k < nz; ++k)
            for (int i = 0; i < nx - 1; ++i)
                set(1, j, k, i, nz, nx, raw(1, i, j, k));
    for (int j = 1; j < ny - 1; ++j)
        for (int k = 0; k < nz; ++k)
            set(1, j, k, nx - 1, nz, nx,
                d[layout.blocks[1].start + index3(j,k,0,nz,nx)]);
    for (int k = 0; k < nz; ++k) {
        for (int i = 0; i < nx; ++i) {
            set(1, 0, k, i, nz, nx, 0.0);
            set(1, ny - 1, k, i, nz, nx, 0.0);
            set(1, ny, k, i, nz, nx,
                -d[layout.blocks[1].start + index3(ny - 2,k,i,nz,nx)]);
        }
    }

    // W/PH use mass Q extension; fixed lower boundary is zero and top stays active.
    for (int b : {2, 3}) {
        for (int j = 0; j < ny - 1; ++j)
            for (int k = 1; k < nz_w; ++k)
                for (int i = 0; i < nx - 1; ++i)
                    set(b, j, k, i, nz_w, nx, raw(b, i, j, k));
        for (int j = 0; j < ny - 1; ++j)
            for (int k = 1; k < nz_w; ++k)
                set(b, j, k, nx - 1, nz_w, nx,
                    d[layout.blocks[b].start + index3(j,k,0,nz_w,nx)]);
        for (int k = 0; k < nz_w; ++k)
            for (int i = 0; i < nx; ++i) {
                set(b, ny - 1, k, i, nz_w, nx,
                    d[layout.blocks[b].start + index3(ny - 2,k,i,nz_w,nx)]);
                if (k == 0)
                    for (int j = 0; j < ny; ++j) set(b, j, k, i, nz_w, nx, 0.0);
            }
    }

    // T and MU use mass Q extension.
    for (int b : {4, 5}) {
        for (int j = 0; j < ny - 1; ++j)
            for (int k = 0; k < dims_k[b]; ++k)
                for (int i = 0; i < nx - 1; ++i)
                    set(b, j, k, i, dims_k[b], nx, raw(b, i, j, k));
        for (int j = 0; j < ny - 1; ++j)
            for (int k = 0; k < dims_k[b]; ++k)
                set(b, j, k, nx - 1, dims_k[b], nx,
                    d[layout.blocks[b].start + index3(j,k,0,dims_k[b],nx)]);
        for (int k = 0; k < dims_k[b]; ++k)
            for (int i = 0; i < nx; ++i)
                set(b, ny - 1, k, i, dims_k[b], nx,
                    d[layout.blocks[b].start + index3(ny - 2,k,i,dims_k[b],nx)]);
    }

    // One equal L2 share per prognostic block after Q extension and scale removal.
    for (const auto& block : layout.blocks) {
        auto view = direction.slice(0, block.start, block.start + block.size);
        const double norm = view.to(torch::kFloat64).norm().item<double>();
        TORCH_CHECK(std::isfinite(norm) && norm > 0.0, "empty direction block ", block.name);
        view.mul_(static_cast<float>((scales.at(&block - layout.blocks.data()) /
                                      std::sqrt(6.0)) / norm));
    }
}

void make_terminal(const wrf::sdirk3::StateLayout& layout, int nx, int ny, int nz_w,
                   torch::Tensor& terminal) {
    terminal.zero_();
    auto l = terminal.accessor<float, 1>();
    const auto& w = layout.blocks.at(2);
    long double norm_sq = 0.0L;
    for (int j = 0; j < ny - 1; ++j)
        for (int k = 1; k < nz_w; ++k)
            for (int i = 0; i < nx - 1; ++i) {
                const double c = std::sin(0.31 * (i + 1) + 0.17 * (j + 1) + 0.11 * (k + 1));
                norm_sq += static_cast<long double>(c) * c;
            }
    const double scale = 1.0e-3;
    const double norm = std::sqrt(static_cast<double>(norm_sq));
    TORCH_CHECK(std::isfinite(norm) && norm > 0.0, "invalid terminal owner norm");
    for (int j = 0; j < ny - 1; ++j)
        for (int k = 1; k < nz_w; ++k)
            for (int i = 0; i < nx - 1; ++i) {
                const double c = std::sin(0.31 * (i + 1) + 0.17 * (j + 1) + 0.11 * (k + 1));
                l[w.start + index3(j,k,i,nz_w,nx)] = static_cast<float>((c / norm) / scale);
            }
}

}  // namespace

namespace wrf::sdirk3::native_terminal_test {

torch::Tensor control_input64(const torch::Tensor& packed) {
    std::lock_guard<std::mutex> lock(run.mutex);
    if (!run.prepared || !run.forward_active) return packed;
    ++run.control_input64_calls;
    TORCH_CHECK(packed.defined() && packed.numel() == 24531 && packed.is_cpu(),
                "native terminal control input has an invalid packed tensor");
    if (run.warm_forward && run.host_step == 1) {
        auto published = run.initial_seed64.to(torch::kFloat32).contiguous();
        TORCH_CHECK(bitwise_equal(packed.to(torch::kFloat32).contiguous(), published),
                    "warm-forward initial FP32 publication differs from frozen FP64 seed");
        run.initial_control_seed_applied = true;
        return run.initial_seed64;
    }
    if (!run.warm_forward && run.expected_steps > kWindowSteps &&
        run.host_step == run.graph_start_step) {
        TORCH_CHECK(!run.endpoints64.empty() &&
                    bitwise_equal(packed, run.endpoints64.back()),
                    "retained graph did not start from the exact warm FP64 checkpoint");
        run.suffix_seed_matched = true;
    }
    return packed;
}

void capture_accepted_state(const torch::Tensor& state) {
    std::lock_guard<std::mutex> lock(run.mutex);
    if (!run.prepared || !run.forward_active) return;
    if (!run.warm_forward) return;
    TORCH_CHECK(run.host_step > 0 && run.host_step < run.graph_start_step,
                "accepted warm state captured outside the configured warm prefix");
    TORCH_CHECK(state.defined() && state.is_cpu() &&
                state.scalar_type() == torch::kFloat64 && state.numel() == 24531 &&
                torch::isfinite(state).all().item<bool>(),
                "invalid accepted warm FP64 state");
    TORCH_CHECK(run.endpoints64.size() == static_cast<std::size_t>(run.host_step),
                "warm FP64 endpoint ledger is not consecutive");
    auto endpoint = state.detach().to(torch::kCPU, torch::kFloat64).contiguous().clone();
    if (run.mode == 1 || run.mode == 3) {
        TORCH_CHECK(run.reference_checkpoints64.defined() &&
                    run.host_step < run.reference_checkpoints64.size(0) &&
                    bitwise_equal(endpoint, run.reference_checkpoints64[run.host_step]),
                    "warm-prefix FP64 endpoint differs from baseline at host step ", run.host_step);
        ++run.reference_match_count;
    }
    run.endpoints64.push_back(std::move(endpoint));
    ++run.warm_capture_count;
}

void capture_gradient64(const torch::Tensor& gradient) {
    std::lock_guard<std::mutex> lock(run.mutex);
    TORCH_CHECK(run.prepared && run.endpoint_recorded && !run.forward_active,
                "gradient captured outside the terminal test pullback phase");
    TORCH_CHECK(gradient.defined() && gradient.numel() > 0 &&
                torch::isfinite(gradient).all().item<bool>(),
                "native terminal FP64 gradient is invalid");
    TORCH_CHECK(peak_rss_bytes() <= kMemoryCapBytes,
                "peak RSS exceeded the 18 GiB cap during reverse");
    run.wrapper_gradient64 = gradient.detach().to(torch::kCPU, torch::kFloat64).contiguous().clone();
}

void capture_theta_omega(const torch::Tensor& omega) {
    std::lock_guard<std::mutex> lock(run.mutex);
    if (!run.prepared || !run.forward_active) return;
    TORCH_CHECK(omega.defined() && omega.dim() == 3,
                "native terminal Omega capture expects [j,k,i]");
    TORCH_CHECK(omega.size(0) >= run.ny && omega.size(1) >= run.nz &&
                omega.size(2) >= run.nx,
                "Omega capture shape is smaller than the native mass tile");
    auto values = omega.detach().to(torch::kCPU, torch::kFloat64).contiguous();
    const auto a = values.accessor<double, 3>();
    const int k0 = 2;
    const int k1 = run.nz - 2;
    int zeros = 0, positive = 0, negative = 0;
    double min_abs = std::numeric_limits<double>::infinity();
    std::vector<int8_t> signs;
    signs.reserve(static_cast<std::size_t>(run.nx - 1) * (run.ny - 1) *
                  std::max(0, k1 - k0 + 1));
    for (int j = 0; j < run.ny - 1; ++j)
        for (int k = k0; k <= k1; ++k)
            for (int i = 0; i < run.nx - 1; ++i) {
                const double value = a[j][k][i];
                TORCH_CHECK(std::isfinite(value), "non-finite hard-sign Omega");
                min_abs = std::min(min_abs, std::abs(value));
                const double flux_sign_argument = -value;
                const int8_t sign = flux_sign_argument > 0.0 ? 1 :
                    (flux_sign_argument < 0.0 ? -1 : 0);
                signs.push_back(sign);
                positive += sign > 0;
                negative += sign < 0;
                zeros += sign == 0;
            }
    if (run.omega_binary.good() && !signs.empty())
        run.omega_binary.write(reinterpret_cast<const char*>(signs.data()),
                               static_cast<std::streamsize>(signs.size()));
    if (run.omega_csv.good())
        run.omega_csv << run.host_step << ',' << run.omega_ordinal++ << ','
                      << omega.size(0) << ',' << omega.size(1) << ',' << omega.size(2) << ','
                      << k0 << ',' << k1 << ',' << positive << ',' << negative << ','
                      << zeros << ',' << std::setprecision(17) << min_abs << '\n';
}

}  // namespace wrf::sdirk3::native_terminal_test

extern "C" int sdirk3_native_terminal_prepare(
    float* packed_x0, int n, int nx, int ny, int nz,
    int nx_u, int ny_v, int nz_w, float* direction_out,
    float* terminal_out, float* lambda_wrapper_out,
    int* mode_out, int* expected_steps_out) {
    try {
        TORCH_CHECK(packed_x0 && direction_out && terminal_out && lambda_wrapper_out &&
                    mode_out && expected_steps_out,
                    "null native terminal prepare buffer");
        TORCH_CHECK(nx == 17 && ny == 17 && nz == 16 && nx_u == 18 &&
                    ny_v == 18 && nz_w == 17 && n == 24531,
                    "native terminal probe expects the registered 17x17x16 tile layout");
        const auto mode_name = required_env("WRF_SDIRK3_NATIVE_TERMINAL_MODE");
        const int mode = parse_mode(mode_name);
        const int expected_steps = parse_steps();
        const float eta = parse_eta(required_env("WRF_SDIRK3_NATIVE_TERMINAL_ETA"));
        if ((mode != 2 && eta != 0.0f) ||
            (mode == 2 && eta != 0.01f && eta != -0.01f && eta != 0.03f && eta != -0.03f))
            throw std::runtime_error("mode/eta is outside the frozen baseline/replay/±.01/±.03 set");
        const std::string prefix = required_env("WRF_SDIRK3_NATIVE_TERMINAL_OUT");
        const std::string chain_name = mode == 3 ? required_env("WRF_SDIRK3_NATIVE_TERMINAL_CHAIN") : mode_name;
        TORCH_CHECK(mode == 3 || expected_steps == kSteps,
                    "only prefix mode may shorten the frozen timestep horizon");
        TORCH_CHECK(mode != 3 || (expected_steps < kSteps &&
                    (chain_name == "baseline" || chain_name == "replay")),
                    "prefix mode requires baseline/replay chain and a shortened horizon");

        auto x0 = torch::from_blob(packed_x0, {n}, torch::TensorOptions().dtype(torch::kFloat32)
                                                    .device(torch::kCPU)).clone();
        TORCH_CHECK(torch::isfinite(x0).all().item<bool>(), "non-finite initial packed state");
        const auto layout = wrf::sdirk3::StateLayout::from_grid_dims(
            nx, ny, nz, nx_u, ny_v, nz_w);
        TORCH_CHECK(layout.total_size == n && layout.blocks.size() == 6,
                    "StateLayout disagrees with Fortran pack");
        auto direction = torch::zeros({n}, torch::kFloat32);
        auto terminal = torch::zeros({n}, torch::kFloat32);
        auto lambda_wrapper = torch::zeros({n}, torch::kFloat32);
        std::array<double, 6> scales{};
        make_direction(layout, nx, ny, nz, nx_u, ny_v, nz_w, x0, direction, scales);
        make_terminal(layout, nx, ny, nz_w, terminal);

        torch::Tensor lambda64_chain = terminal.to(torch::kFloat64);
        torch::Tensor reference;
        std::string reference_path;
        int initial_reference_matches = 0;
        if (mode == 3) {
            const auto f64_path = required_env("WRF_SDIRK3_NATIVE_TERMINAL_LAMBDA64_IN");
            const auto f32_path = required_env("WRF_SDIRK3_NATIVE_TERMINAL_LAMBDA32_IN");
            auto f64_bytes = read_bytes(f64_path, static_cast<std::size_t>(n) * sizeof(double));
            auto f32_bytes = read_bytes(f32_path, static_cast<std::size_t>(n) * sizeof(float));
            lambda64_chain = torch::from_blob(f64_bytes.data(), {n},
                torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU)).clone();
            lambda_wrapper = torch::from_blob(f32_bytes.data(), {n},
                torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)).clone();
        }
        if (mode == 1 || mode == 3) {
            reference_path = required_env("WRF_SDIRK3_NATIVE_TERMINAL_REFERENCE");
            auto reference_bytes = read_bytes(reference_path,
                static_cast<std::size_t>(kSteps + 1) * n * sizeof(double));
            reference = torch::from_blob(reference_bytes.data(),
                {kSteps + 1, n}, torch::TensorOptions().dtype(torch::kFloat64)
                                      .device(torch::kCPU)).clone();
            TORCH_CHECK(bitwise_equal(x0.to(torch::kFloat64),
                                      reference[0]),
                        "replay/prefix initial state differs from baseline reference seed");
            initial_reference_matches = 1;
        }
        if (mode != 3) {
            lambda_wrapper.copy_(terminal);
        }

        auto seed64 = eta == 0.0f
            ? x0.to(torch::kFloat64)
            : x0.to(torch::kFloat64) + static_cast<double>(eta) * direction.to(torch::kFloat64);
        auto publication = seed64.to(torch::kFloat32).contiguous();
        auto dcontig = direction.contiguous();
        auto lcontig = terminal.contiguous();
        auto lincontig = lambda_wrapper.contiguous();

        std::lock_guard<std::mutex> registry_lock(g_tile_solvers_mutex);
        auto& solver = unique_solver_locked();
        TORCH_CHECK(solver.fixedTrajectoryRequested(),
                    "Fortran begin_fixed_trajectory did not arm the registered request");
        TORCH_CHECK(wrf::sdirk3::g_sdirk3_config.internal_fp64_state_carry &&
                    wrf::sdirk3::g_sdirk3_config.retain_graph_for_adjoint,
                    "native terminal case requires FP64 carry and retained graph from test namelist");
        solver.cancelFixedTrajectoryRequest();
        if (expected_steps == kWindowSteps) {
            solver.requestFixedTrajectory(kWindowSteps,
                std::vector<float>(kWindowSteps, 0.9375f), seed64);
        } else {
            wrf::sdirk3::g_sdirk3_config.retain_graph_for_adjoint = false;
            wrf::sdirk3::g_sdirk3_config.imex_slow_in_tangent = false;
        }

        std::copy(publication.data_ptr<float>(), publication.data_ptr<float>() + n, packed_x0);
        std::copy(dcontig.data_ptr<float>(), dcontig.data_ptr<float>() + n, direction_out);
        std::copy(lcontig.data_ptr<float>(), lcontig.data_ptr<float>() + n, terminal_out);
        std::copy(lincontig.data_ptr<float>(), lincontig.data_ptr<float>() + n, lambda_wrapper_out);

        {
            std::lock_guard<std::mutex> lock(run.mutex);
            TORCH_CHECK(!run.prepared, "native terminal support file reused in one process");
            run.mode_name = mode_name;
            run.chain_name = chain_name;
            run.output_prefix = prefix;
            run.mode = mode;
            run.expected_steps = expected_steps;
            run.graph_start_step = expected_steps - kWindowSteps + 1;
            run.eta = eta;
            run.nx = nx; run.ny = ny; run.nz = nz;
            run.nx_u = nx_u; run.ny_v = ny_v; run.nz_w = nz_w;
            run.initial_seed64 = seed64.detach().clone();
            run.endpoints64.clear();
            run.endpoints64.push_back(seed64.detach().clone());
            run.lambda64_chain = lambda64_chain.detach().clone();
            run.lambda64_in = lambda64_chain.detach().clone();
            run.lambda32_wrapper = lambda_wrapper.detach().clone();
            run.reference_checkpoints64 = reference;
            run.reference_path = reference_path;
            run.reference_match_count = initial_reference_matches;
            run.window_start_step = 0;
            run.direction32 = direction.detach().clone();
            run.terminal32 = terminal.detach().clone();
            run.prepared = true;
            run.forward_active = true;
            run.warm_forward = expected_steps > kWindowSteps;
            run.omega_binary.open(prefix + ".omega_sign.bin", std::ios::binary | std::ios::trunc);
            run.omega_csv.open(prefix + ".omega_sign.csv", std::ios::trunc);
            TORCH_CHECK(run.omega_binary && run.omega_csv, "cannot open Omega trace outputs");
            run.omega_csv << "host_step,ordinal,shape_j,shape_k,shape_i,k_first,k_last,positive,negative,zero,min_abs\n";
        }
        *mode_out = mode;
        *expected_steps_out = expected_steps;
        write_tensor(prefix + ".x0_published_f32.bin", publication, torch::kFloat32);
        write_tensor(prefix + ".x0_internal_f64.bin", seed64, torch::kFloat64);
        write_tensor(prefix + ".direction_f32.bin", direction, torch::kFloat32);
        write_tensor(prefix + ".terminal_f32.bin", terminal, torch::kFloat32);
        {
            std::ofstream meta(prefix + ".prepare.json", std::ios::trunc);
            TORCH_CHECK(meta, "cannot create prepare metadata");
            meta << std::setprecision(17) << "{\n"
                 << "  \"mode\": \"" << mode_name << "\",\n"
                 << "  \"chain\": \"" << chain_name << "\",\n"
                 << "  \"expected_steps\": " << expected_steps << ",\n"
                 << "  \"warm_steps\": " << std::max(0, expected_steps - kWindowSteps) << ",\n"
                 << "  \"graph_start_step\": " << (expected_steps - kWindowSteps + 1) << ",\n"
                 << "  \"warm_endpoint_capture_target\": " << std::max(0, expected_steps - kWindowSteps) << ",\n"
                 << "  \"warm_graph_flags\": \"retain_graph_for_adjoint=false,imex_slow_in_tangent=false\",\n"
                 << "  \"suffix_graph_flags\": \"retain_graph_for_adjoint=true,imex_slow_in_tangent=true\",\n"
                 << "  \"eta\": " << eta << ",\n"
                 << "  \"packed_size\": " << n << ",\n"
                 << "  \"dims\": [17,17,16,18,18,17],\n"
                 << "  \"steps\": " << expected_steps << ",\n"
                 << "  \"dt_seconds\": 0.9375,\n"
                 << "  \"terminal\": \"normalized W physical owners, scale 0.001, FP32 cotangent\",\n"
                 << "  \"input_scales\": [";
            for (int b = 0; b < 6; ++b) meta << (b ? "," : "") << scales[b];
            meta << "]\n}\n";
        }
        std::cerr << "NATIVE_TERMINAL_PREPARE mode=" << mode_name << " eta=" << eta
                  << " n=" << n << " seed64_xor="
                  << std::hex << fnv1a64(seed64.contiguous().data_ptr<double>(),
                                        static_cast<std::size_t>(n) * sizeof(double))
                  << std::dec << "\n";
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "NATIVE_TERMINAL_PREPARE_ERROR: " << e.what() << '\n';
        return 0;
    }
}

extern "C" int sdirk3_native_terminal_before_step(int host_step) {
    try {
        std::lock_guard<std::mutex> lock(run.mutex);
        TORCH_CHECK(run.prepared && run.forward_active,
                    "native terminal before_step outside active forward");
        TORCH_CHECK(host_step == run.host_step + 1 && host_step <= run.expected_steps,
                    "native terminal host timestep is not consecutive");
        run.host_step = host_step;
        run.omega_ordinal = 0;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "NATIVE_TERMINAL_PROGRESS_ERROR: " << e.what() << '\n';
        return 0;
    }
}

extern "C" int sdirk3_native_terminal_after_step(int host_step) {
    try {
        std::lock_guard<std::mutex> lock(run.mutex);
        TORCH_CHECK(run.prepared && run.forward_active && host_step == run.host_step,
                    "native terminal after_step does not match the completed host step");
        if (run.warm_forward) {
            TORCH_CHECK(host_step < run.graph_start_step &&
                        run.endpoints64.size() == static_cast<std::size_t>(host_step + 1),
                        "warm forward did not capture the completed FP64 endpoint");
            if (host_step == 1 || host_step == 2 || host_step == 4 || host_step == 8 ||
                host_step == 16 || host_step % 32 == 0) {
                const auto rss = peak_rss_bytes();
                TORCH_CHECK(rss <= kMemoryCapBytes,
                            "peak RSS exceeded the 18 GiB cap during warm forward");
                std::cerr << "NATIVE_TERMINAL_WARM_PROGRESS step=" << host_step
                          << " peak_rss_bytes=" << rss << '\n';
            }
            return 1;
        }
        const int window_step = host_step - run.window_start_step;
        TORCH_CHECK(window_step >= 1 && window_step <= kWindowSteps,
                    "native terminal completed step is outside its 32-step window");
        if (window_step == 1 || window_step == 2 || window_step == 4 || window_step == 8 ||
            window_step == 16 || window_step == kWindowSteps) {
            const auto rss = peak_rss_bytes();
            TORCH_CHECK(rss <= kMemoryCapBytes, "peak RSS exceeded the 18 GiB cap");
            std::cerr << "NATIVE_TERMINAL_PROGRESS step=" << host_step
                      << " window_step=" << window_step
                      << " peak_rss_bytes=" << rss << '\n';
            if (window_step == 4) run.rss_window_step4 = rss;
            if (window_step == 8) {
                TORCH_CHECK(run.rss_window_step4 > 0, "missing window-step-4 RSS sample");
                const double growth_per_step =
                    std::max(0.0, static_cast<double>(rss) - run.rss_window_step4) / 4.0;
                const double projected = rss + 1.5 * growth_per_step * (kWindowSteps - window_step);
                std::cerr << "NATIVE_TERMINAL_MEMORY_ESTIMATE step=8 projected_peak_bytes="
                          << std::fixed << std::setprecision(0) << projected << std::defaultfloat << '\n';
                TORCH_CHECK(projected <= static_cast<double>(kMemoryCapBytes),
                            "step-8 memory estimate exceeds the 18 GiB cap");
            }
        }
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "NATIVE_TERMINAL_PROGRESS_ERROR: " << e.what() << '\n';
        return 0;
    }
}

extern "C" int sdirk3_native_terminal_record_endpoint(
    const float* packed_terminal, int n, int host_step) {
    try {
        std::lock_guard<std::mutex> run_lock(run.mutex);
        TORCH_CHECK(packed_terminal && n == 24531 && run.prepared && run.forward_active &&
                    host_step == run.host_step && host_step > 0 &&
                    host_step % kWindowSteps == 0 && host_step <= run.expected_steps &&
                    host_step == run.expected_steps && !run.warm_forward &&
                    host_step - run.window_start_step == kWindowSteps,
                    "native terminal endpoint is not the final retained 32-step window");
        TORCH_CHECK(run.expected_steps == kWindowSteps || run.suffix_seed_matched,
                    "suffix FP64 seed was not byte-verified before retained steps");
        TORCH_CHECK(run.control_input64_calls == run.expected_steps &&
                    run.warm_capture_count == run.expected_steps - kWindowSteps &&
                    run.initial_control_seed_applied == (run.expected_steps > kWindowSteps),
                    "control/accepted-state hook counts do not match the warm-plus-suffix schedule");

        std::vector<torch::Tensor> checkpoints;
        {
            std::lock_guard<std::mutex> registry_lock(g_tile_solvers_mutex);
            auto& solver = unique_solver_locked();
            checkpoints = solver.getFixedTrajectoryFp64Checkpoints();
            TORCH_CHECK(checkpoints.size() == kWindowSteps,
                        "fixed trajectory did not retain exactly 32 endpoint states");
        }
        TORCH_CHECK(!checkpoints.empty(), "native terminal trajectory has no endpoints");
        for (const auto& checkpoint : checkpoints) {
            TORCH_CHECK(checkpoint.scalar_type() == torch::kFloat64 &&
                        checkpoint.numel() == 24531 &&
                        torch::isfinite(checkpoint).all().item<bool>(),
                        "invalid retained FP64 checkpoint");
            auto c = checkpoint.to(torch::kCPU, torch::kFloat64).contiguous();
            const int absolute_endpoint = static_cast<int>(run.endpoints64.size());
            if (run.mode == 1 || run.mode == 3) {
                TORCH_CHECK(run.reference_checkpoints64.defined() && absolute_endpoint <= host_step,
                            "prefix has no baseline endpoint reference");
                TORCH_CHECK(bitwise_equal(c, run.reference_checkpoints64[absolute_endpoint]),
                            "prefix endpoint differs from the full baseline before pullback at step ",
                            absolute_endpoint);
                ++run.reference_match_count;
            }
            run.endpoints64.push_back(c.detach().clone());
        }
        TORCH_CHECK(run.endpoints64.size() == static_cast<std::size_t>(host_step + 1),
                    "native endpoint ledger is not contiguous");
        TORCH_CHECK(peak_rss_bytes() <= kMemoryCapBytes,
                    "peak RSS exceeded the 18 GiB cap while copying retained endpoints");
        auto terminal_state64 = run.endpoints64.back().to(torch::kCPU, torch::kFloat64).contiguous();
        const double local_terminal_c_dot = torch::dot(
            terminal_state64, run.terminal32.to(torch::kFloat64)).item<double>();
        TORCH_CHECK(std::isfinite(local_terminal_c_dot), "non-finite terminal linear diagnostic");
        auto published = torch::from_blob(const_cast<float*>(packed_terminal), {n},
            torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)).clone();
        const auto expected_published = terminal_state64.to(torch::kFloat32).contiguous();
        TORCH_CHECK(torch::equal(published, expected_published),
                    "Fortran endpoint publication differs from FP64 checkpoint cast");
        if (host_step == run.expected_steps) {
            run.forward_active = false;
            run.omega_binary.flush(); run.omega_csv.flush();
            TORCH_CHECK(run.omega_binary.good() && run.omega_csv.good(), "Omega trace write failed");
            std::vector<double> all;
            all.reserve(run.endpoints64.size() * static_cast<std::size_t>(n));
            for (const auto& state : run.endpoints64) {
                const auto c = state.to(torch::kCPU, torch::kFloat64).contiguous();
                const double* data = c.data_ptr<double>();
                all.insert(all.end(), data, data + c.numel());
            }
            const uint64_t actual_prefix_hash = fnv1a64(all.data(), all.size() * sizeof(double));
            uint64_t reference_prefix_hash = 0;
            if (run.mode == 1 || run.mode == 3) {
                auto reference_prefix = run.reference_checkpoints64
                    .slice(0, 0, host_step + 1).contiguous();
                reference_prefix_hash = fnv1a64(reference_prefix.data_ptr<double>(),
                    static_cast<std::size_t>(reference_prefix.numel()) * sizeof(double));
                TORCH_CHECK(run.reference_match_count == host_step + 1 &&
                            actual_prefix_hash == reference_prefix_hash,
                            "endpoint reference count/hash mismatch before any VJP");
            }
            write_bytes(run.output_prefix + ".checkpoints_f64.bin", all.data(), all.size() * sizeof(double));
            write_tensor(run.output_prefix + ".terminal_state_f64.bin", terminal_state64, torch::kFloat64);
            write_tensor(run.output_prefix + ".terminal_state_published_f32.bin", published, torch::kFloat32);
            {
                std::ofstream endpoint_meta(run.output_prefix + ".endpoint.json", std::ios::trunc);
                TORCH_CHECK(endpoint_meta, "cannot create final endpoint metadata");
                endpoint_meta << std::setprecision(17) << "{\n"
                    << "  \"mode\": \"" << run.mode_name << "\",\n"
                    << "  \"chain\": \"" << run.chain_name << "\",\n"
                    << "  \"eta\": " << run.eta << ",\n"
                    << "  \"host_steps\": " << host_step << ",\n"
                    << "  \"expected_steps\": " << run.expected_steps << ",\n"
                    << "  \"warm_steps\": " << (run.expected_steps - kWindowSteps) << ",\n"
                    << "  \"graph_start_step\": " << run.graph_start_step << ",\n"
                    << "  \"suffix_seed_bitwise_match\": "
                    << (run.expected_steps == kWindowSteps || run.suffix_seed_matched ? "true" : "false") << ",\n"
                    << "  \"control_input64_calls\": " << run.control_input64_calls << ",\n"
                    << "  \"initial_control_seed_applied\": "
                    << (run.initial_control_seed_applied ? "true" : "false") << ",\n"
                    << "  \"warm_endpoint_capture_count\": " << run.warm_capture_count << ",\n"
                    << "  \"endpoint_count_including_seed\": " << run.endpoints64.size() << ",\n"
                    << "  \"objective_is_registered_terminal\": " <<
                        (run.mode == 3 ? "false" : "true") << ",\n"
                    << "  \"objective\": ";
                if (run.mode == 3) endpoint_meta << "null";
                else endpoint_meta << local_terminal_c_dot;
                endpoint_meta << ",\n  \"local_terminal_c_dot\": " << local_terminal_c_dot << ",\n"
                    << "  \"reference_path\": \"" << run.reference_path << "\",\n"
                    << "  \"reference_checkpoints_expected\": "
                    << ((run.mode == 1 || run.mode == 3) ? host_step + 1 : 0) << ",\n"
                    << "  \"reference_checkpoints_matched\": " << run.reference_match_count << ",\n"
                    << "  \"endpoint_reference_exact\": "
                    << ((run.mode == 1 || run.mode == 3) ? "true" : "null") << ",\n"
                    << "  \"actual_prefix_fnv1a64\": \"" << std::hex << actual_prefix_hash << "\",\n"
                    << "  \"reference_prefix_fnv1a64\": \"" << reference_prefix_hash << std::dec << "\",\n"
                    << "  \"peak_rss_bytes\": " << peak_rss_bytes() << "\n}\n";
            }
        }
        {
            std::ofstream meta(run.output_prefix + ".window_" + std::to_string(host_step) + ".json",
                               std::ios::trunc);
            TORCH_CHECK(meta, "cannot create endpoint metadata");
            meta << std::setprecision(17) << "{\n"
                 << "  \"mode\": \"" << run.mode_name << "\",\n"
                 << "  \"chain\": \"" << run.chain_name << "\",\n"
                 << "  \"eta\": " << run.eta << ",\n"
                 << "  \"expected_steps\": " << run.expected_steps << ",\n"
                 << "  \"warm_steps\": " << (run.expected_steps - kWindowSteps) << ",\n"
                 << "  \"graph_start_step\": " << run.graph_start_step << ",\n"
                 << "  \"suffix_seed_bitwise_match\": "
                 << (run.expected_steps == kWindowSteps || run.suffix_seed_matched ? "true" : "false") << ",\n"
                 << "  \"window_start\": " << (host_step - kWindowSteps) << ",\n"
                 << "  \"window_end\": " << host_step << ",\n"
                 << "  \"window_endpoint_count\": " << checkpoints.size() << ",\n"
                 << "  \"local_terminal_c_dot\": " << local_terminal_c_dot << ",\n"
                 << "  \"reference_checkpoints_expected\": " <<
                    ((run.mode == 1 || run.mode == 3) ? host_step + 1 : 0) << ",\n"
                 << "  \"reference_checkpoints_matched\": " << run.reference_match_count << ",\n"
                 << "  \"endpoint_reference_exact\": " <<
                    ((run.mode == 1 || run.mode == 3) ? "true" : "null") << ",\n"
                 << "  \"window_endpoint_f64_fnv1a64\": \"" << std::hex
                 << fnv1a64(checkpoints.back().to(torch::kCPU, torch::kFloat64).contiguous().data_ptr<double>(),
                           static_cast<std::size_t>(n) * sizeof(double))
                 << std::dec << "\",\n"
                 << "  \"peak_rss_bytes\": " << peak_rss_bytes() << "\n}\n";
        }
        if (host_step == run.expected_steps) run.endpoint_recorded = true;
        std::cerr << "NATIVE_TERMINAL_WINDOW mode=" << run.mode_name
                  << " end_step=" << host_step
                  << " local_terminal_c_dot=" << std::setprecision(17) << local_terminal_c_dot
                  << " peak_rss_bytes=" << peak_rss_bytes() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "NATIVE_TERMINAL_ENDPOINT_ERROR: " << e.what() << '\n';
        return 0;
    }
}

extern "C" int sdirk3_native_terminal_rearm_window() {
    try {
        std::lock_guard<std::mutex> run_lock(run.mutex);
        TORCH_CHECK(run.prepared && run.forward_active && run.warm_forward &&
                    run.host_step == run.expected_steps - kWindowSteps &&
                    run.endpoints64.size() == static_cast<std::size_t>(run.host_step + 1),
                    "rearm requested outside the warm-prefix/suffix boundary");
        std::lock_guard<std::mutex> registry_lock(g_tile_solvers_mutex);
        auto& solver = unique_solver_locked();
        TORCH_CHECK(solver.fixedTrajectoryRequested(), "Fortran did not request the next trajectory window");
        solver.cancelFixedTrajectoryRequest();
        wrf::sdirk3::g_sdirk3_config.retain_graph_for_adjoint = true;
        wrf::sdirk3::g_sdirk3_config.imex_slow_in_tangent = true;
        solver.resetInternalFp64Carry();
        write_tensor(run.output_prefix + ".suffix_seed_f64.bin",
                     run.endpoints64.back(), torch::kFloat64);
        solver.requestFixedTrajectory(kWindowSteps,
            std::vector<float>(kWindowSteps,0.9375f), run.endpoints64.back());
        run.window_start_step = run.host_step;
        run.rss_window_step4 = 0;
        run.warm_forward = false;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "NATIVE_TERMINAL_REARM_ERROR: " << e.what() << '\n';
        return 0;
    }
}

extern "C" int sdirk3_native_terminal_pullback64(double* lambda_initial, int n) {
    try {
        TORCH_CHECK(lambda_initial && n == 24531 && run.prepared && run.endpoint_recorded &&
                    !run.forward_active && run.lambda64_chain.defined(),
                    "invalid exact FP64 chain pullback request");
        std::lock_guard<std::mutex> registry_lock(g_tile_solvers_mutex);
        auto& solver = unique_solver_locked();
        run.lambda64_in = run.lambda64_chain.detach().clone();
        auto gradient = solver.pullbackFixedTrajectory(run.lambda64_chain)
                            .detach().to(torch::kCPU, torch::kFloat64).contiguous();
        TORCH_CHECK(gradient.numel() == n && torch::isfinite(gradient).all().item<bool>(),
                    "exact FP64 window pullback is invalid");
        std::copy(gradient.data_ptr<double>(), gradient.data_ptr<double>() + n, lambda_initial);
        run.direct_gradient64 = gradient.clone();
        run.lambda64_chain = gradient.clone();
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "NATIVE_TERMINAL_VJP64_ERROR: " << e.what() << '\n';
        return 0;
    }
}

extern "C" int sdirk3_native_terminal_finish(
    const float* direction, const float* terminal,
    const float* initial_gradient_f32, const double* lambda_initial_f64,
    int n, int mode, int have_gradient) {
    try {
        TORCH_CHECK(direction && terminal && initial_gradient_f32 &&
                    lambda_initial_f64 && n == 24531,
                    "invalid native terminal finish arguments");
        std::lock_guard<std::mutex> lock(run.mutex);
        TORCH_CHECK(run.prepared && run.endpoint_recorded && !run.forward_active && mode == run.mode,
                    "native terminal finish phase/mode mismatch");
        const auto f32_options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        auto d32 = torch::from_blob(const_cast<float*>(direction), {n}, f32_options).clone();
        auto l32 = torch::from_blob(const_cast<float*>(terminal), {n}, f32_options).clone();
        auto g32 = torch::from_blob(const_cast<float*>(initial_gradient_f32), {n}, f32_options).clone();
        auto g64 = torch::from_blob(const_cast<double*>(lambda_initial_f64), {n},
            torch::TensorOptions().dtype(torch::kFloat64).device(torch::kCPU)).clone();
        auto d = d32.to(torch::kFloat64);
        const bool vjp_expected = run.mode != 2;
        TORCH_CHECK(have_gradient == (vjp_expected ? 1 : 0),
                    "Fortran wrapper pullback gate disagrees with run role");
        TORCH_CHECK(run.lambda64_in.defined() && run.lambda32_wrapper.defined(),
                    "run has no incoming cotangent");
        write_tensor(run.output_prefix + ".direction_f32.bin", d32, torch::kFloat32);
        write_tensor(run.output_prefix + ".terminal_f32.bin", l32, torch::kFloat32);
        double directional64 = std::numeric_limits<double>::quiet_NaN();
        double directional32 = std::numeric_limits<double>::quiet_NaN();
        bool wrapper_cast_equal = false;
        if (vjp_expected) {
            TORCH_CHECK(torch::isfinite(g32).all().item<bool>() &&
                        torch::isfinite(g64).all().item<bool>() &&
                        run.direct_gradient64.defined() &&
                        bitwise_equal(g64, run.direct_gradient64) &&
                        run.wrapper_gradient64.defined() &&
                        torch::isfinite(run.wrapper_gradient64).all().item<bool>(),
                        "one of the native terminal cotangents/pullbacks is non-finite");
            wrapper_cast_equal = bitwise_equal(g32,
                run.wrapper_gradient64.to(torch::kFloat32).contiguous());
            TORCH_CHECK(wrapper_cast_equal,
                        "Fortran FP32 pullback differs bytewise from captured wrapper FP64 cast");
            directional64 = torch::dot(d, g64).item<double>();
            directional32 = torch::dot(d, g32.to(torch::kFloat64)).item<double>();
            write_tensor(run.output_prefix + ".lambda_in_f64.bin", run.lambda64_in, torch::kFloat64);
            write_tensor(run.output_prefix + ".lambda_in_f32.bin", run.lambda32_wrapper, torch::kFloat32);
            write_tensor(run.output_prefix + ".gradient_f64.bin", g64, torch::kFloat64);
            write_tensor(run.output_prefix + ".gradient_f32.bin", g32, torch::kFloat32);
            write_tensor(run.output_prefix + ".wrapper_gradient_f64.bin",
                         run.wrapper_gradient64, torch::kFloat64);
            // Keep v2 aliases for the primary-case consumer; these have the same dtype and content.
            write_tensor(run.output_prefix + ".gradient64.bin", g64, torch::kFloat64);
            write_tensor(run.output_prefix + ".gradient32.bin", g32, torch::kFloat32);
            write_tensor(run.output_prefix + ".lambda_out_f64.bin", g64, torch::kFloat64);
            write_tensor(run.output_prefix + ".lambda_out_f32.bin", g32, torch::kFloat32);
        } else {
            TORCH_CHECK(run.mode == 2, "unexpected non-VJP run mode");
        }
        TORCH_CHECK(peak_rss_bytes() <= kMemoryCapBytes,
                    "peak RSS exceeded the 18 GiB cap during finalization");
        {
            std::ofstream meta(run.output_prefix + ".result.json", std::ios::trunc);
            TORCH_CHECK(meta, "cannot create result metadata");
            meta << std::setprecision(17) << "{\n"
                 << "  \"mode\": \"" << run.mode_name << "\",\n"
                 << "  \"chain\": \"" << run.chain_name << "\",\n"
                 << "  \"eta\": " << run.eta << ",\n"
                 << "  \"expected_steps\": " << run.expected_steps << ",\n"
                 << "  \"warm_steps\": " << (run.expected_steps - kWindowSteps) << ",\n"
                 << "  \"graph_start_step\": " << run.graph_start_step << ",\n"
                 << "  \"suffix_seed_bitwise_match\": "
                 << (run.expected_steps == kWindowSteps || run.suffix_seed_matched ? "true" : "false") << ",\n"
                 << "  \"control_input64_calls\": " << run.control_input64_calls << ",\n"
                 << "  \"initial_control_seed_applied\": "
                 << (run.initial_control_seed_applied ? "true" : "false") << ",\n"
                 << "  \"warm_endpoint_capture_count\": " << run.warm_capture_count << ",\n"
                 << "  \"gradient_control_step\": " << (run.expected_steps - kWindowSteps) << ",\n"
                 << "  \"pullback_checked_by_fortran\": " << (vjp_expected ? "true" : "false") << ",\n"
                 << "  \"wrapper_gradient_f32_equals_cast_f64\": "
                 << (wrapper_cast_equal ? "true" : (vjp_expected ? "false" : "null")) << ",\n"
                 << "  \"reference_checkpoints_expected\": "
                 << ((run.mode == 1 || run.mode == 3) ? run.expected_steps + 1 : 0) << ",\n"
                 << "  \"reference_checkpoints_matched\": " << run.reference_match_count << ",\n"
                 << "  \"endpoint_reference_exact\": "
                 << ((run.mode == 1 || run.mode == 3) ? "true" : "null") << ",\n"
                 << "  \"directional_vjp_f64\": ";
            if (vjp_expected && run.mode == 3 && run.expected_steps == kWindowSteps)
                meta << directional64;
            else meta << "null";
            meta << ",\n  \"directional_vjp_f32_abi\": ";
            if (vjp_expected && run.mode == 3 && run.expected_steps == kWindowSteps)
                meta << directional32;
            else meta << "null";
            meta << ",\n"
                 << "  \"peak_rss_bytes\": " << peak_rss_bytes() << "\n}\n";
        }
        std::cerr << "NATIVE_TERMINAL_FINISH mode=" << run.mode_name
                  << " expected_steps=" << run.expected_steps
                  << " directional_vjp_f64=" << directional64
                  << " directional_vjp_f32_abi=" << directional32 << '\n';
        run.prepared = false;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "NATIVE_TERMINAL_FINISH_ERROR: " << e.what() << '\n';
        return 0;
    }
}

#endif  // WRF_SDIRK3_NATIVE_TERMINAL_TEST
