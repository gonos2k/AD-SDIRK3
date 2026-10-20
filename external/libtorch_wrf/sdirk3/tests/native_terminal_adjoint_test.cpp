#include "native_terminal_adjoint_test.h"

#ifdef WRF_SDIRK3_NATIVE_TERMINAL_TEST

#include "../wrf_sdirk3_state_layout.h"
#include "../wrf_sdirk3_tile_unified.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
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
#include <vector>

using TileSDIRK3UnifiedSolver = ::TileSDIRK3UnifiedSolver;
extern std::mutex g_tile_solvers_mutex;
extern std::unordered_map<void*, std::unique_ptr<TileSDIRK3UnifiedSolver>> g_tile_solvers;

namespace {
constexpr int kSteps = 256;
constexpr std::size_t kMemoryCapBytes = 18ULL * 1024ULL * 1024ULL * 1024ULL;

struct RunState {
    std::mutex mutex;
    std::string mode_name;
    std::string output_prefix;
    int mode = -1;  // 0=baseline, 1=replay, 2=FD; only 0/1 pull back.
    float eta = 0.0f;
    int host_step = 0;
    int omega_ordinal = 0;
    int nx = 0, ny = 0, nz = 0, nx_u = 0, ny_v = 0, nz_w = 0;
    bool prepared = false;
    bool forward_active = false;
    bool endpoint_recorded = false;
    torch::Tensor initial_seed64;
    torch::Tensor gradient64;
    torch::Tensor direction32;
    torch::Tensor terminal32;
    std::ofstream omega_binary;
    std::ofstream omega_csv;
    std::size_t rss_step4 = 0;
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
    if (value == "plus001" || value == "minus001" ||
        value == "plus003" || value == "minus003" ||
        value == "plus_0p01" || value == "minus_0p01" ||
        value == "plus_0p03" || value == "minus_0p03" || value == "fd") return 2;
    throw std::runtime_error("mode must name baseline/replay or one of the four frozen FD runs");
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

void capture_gradient64(const torch::Tensor& gradient) {
    std::lock_guard<std::mutex> lock(run.mutex);
    TORCH_CHECK(run.prepared && run.endpoint_recorded && !run.forward_active,
                "gradient captured outside the terminal test pullback phase");
    TORCH_CHECK(gradient.defined() && gradient.numel() > 0 &&
                torch::isfinite(gradient).all().item<bool>(),
                "native terminal FP64 gradient is invalid");
    TORCH_CHECK(peak_rss_bytes() <= kMemoryCapBytes,
                "peak RSS exceeded the 18 GiB cap during reverse");
    run.gradient64 = gradient.detach().to(torch::kCPU, torch::kFloat64).contiguous().clone();
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
    float* terminal_out, int* mode_out) {
    try {
        TORCH_CHECK(packed_x0 && direction_out && terminal_out && mode_out,
                    "null native terminal prepare buffer");
        TORCH_CHECK(nx == 17 && ny == 17 && nz == 16 && nx_u == 18 &&
                    ny_v == 18 && nz_w == 17 && n == 24531,
                    "native terminal probe expects the registered 17x17x16 tile layout");
        const auto mode_name = required_env("WRF_SDIRK3_NATIVE_TERMINAL_MODE");
        const int mode = parse_mode(mode_name);
        const float eta = parse_eta(required_env("WRF_SDIRK3_NATIVE_TERMINAL_ETA"));
        if ((mode < 2 && eta != 0.0f) ||
            (mode == 2 && eta != 0.01f && eta != -0.01f && eta != 0.03f && eta != -0.03f))
            throw std::runtime_error("mode/eta is outside the frozen baseline/replay/±.01/±.03 set");
        const std::string prefix = required_env("WRF_SDIRK3_NATIVE_TERMINAL_OUT");

        auto x0 = torch::from_blob(packed_x0, {n}, torch::TensorOptions().dtype(torch::kFloat32)
                                                    .device(torch::kCPU)).clone();
        TORCH_CHECK(torch::isfinite(x0).all().item<bool>(), "non-finite initial packed state");
        const auto layout = wrf::sdirk3::StateLayout::from_grid_dims(
            nx, ny, nz, nx_u, ny_v, nz_w);
        TORCH_CHECK(layout.total_size == n && layout.blocks.size() == 6,
                    "StateLayout disagrees with Fortran pack");
        auto direction = torch::zeros({n}, torch::kFloat32);
        auto terminal = torch::zeros({n}, torch::kFloat32);
        std::array<double, 6> scales{};
        make_direction(layout, nx, ny, nz, nx_u, ny_v, nz_w, x0, direction, scales);
        make_terminal(layout, nx, ny, nz_w, terminal);

        auto seed64 = eta == 0.0f
            ? x0.to(torch::kFloat64)
            : x0.to(torch::kFloat64) + static_cast<double>(eta) * direction.to(torch::kFloat64);
        auto publication = seed64.to(torch::kFloat32).contiguous();
        auto dcontig = direction.contiguous();
        auto lcontig = terminal.contiguous();

        std::lock_guard<std::mutex> registry_lock(g_tile_solvers_mutex);
        auto& solver = unique_solver_locked();
        TORCH_CHECK(solver.fixedTrajectoryRequested(),
                    "Fortran begin_fixed_trajectory did not arm the registered request");
        TORCH_CHECK(wrf::sdirk3::g_sdirk3_config.internal_fp64_state_carry &&
                    wrf::sdirk3::g_sdirk3_config.retain_graph_for_adjoint,
                    "native terminal case requires FP64 carry and retained graph from test namelist");
        solver.cancelFixedTrajectoryRequest();
        solver.requestFixedTrajectory(kSteps, std::vector<float>(kSteps, 0.9375f), seed64);

        std::copy(publication.data_ptr<float>(), publication.data_ptr<float>() + n, packed_x0);
        std::copy(dcontig.data_ptr<float>(), dcontig.data_ptr<float>() + n, direction_out);
        std::copy(lcontig.data_ptr<float>(), lcontig.data_ptr<float>() + n, terminal_out);

        {
            std::lock_guard<std::mutex> lock(run.mutex);
            TORCH_CHECK(!run.prepared, "native terminal support file reused in one process");
            run.mode_name = mode_name;
            run.output_prefix = prefix;
            run.mode = mode;
            run.eta = eta;
            run.nx = nx; run.ny = ny; run.nz = nz;
            run.nx_u = nx_u; run.ny_v = ny_v; run.nz_w = nz_w;
            run.initial_seed64 = seed64.detach().clone();
            run.direction32 = direction.detach().clone();
            run.terminal32 = terminal.detach().clone();
            run.prepared = true;
            run.forward_active = true;
            run.omega_binary.open(prefix + ".omega_sign.bin", std::ios::binary | std::ios::trunc);
            run.omega_csv.open(prefix + ".omega_sign.csv", std::ios::trunc);
            TORCH_CHECK(run.omega_binary && run.omega_csv, "cannot open Omega trace outputs");
            run.omega_csv << "host_step,ordinal,shape_j,shape_k,shape_i,k_first,k_last,positive,negative,zero,min_abs\n";
        }
        *mode_out = mode;
        write_tensor(prefix + ".x0_published_f32.bin", publication, torch::kFloat32);
        write_tensor(prefix + ".x0_internal_f64.bin", seed64, torch::kFloat64);
        write_tensor(prefix + ".direction_f32.bin", direction, torch::kFloat32);
        write_tensor(prefix + ".terminal_f32.bin", terminal, torch::kFloat32);
        {
            std::ofstream meta(prefix + ".prepare.json", std::ios::trunc);
            TORCH_CHECK(meta, "cannot create prepare metadata");
            meta << std::setprecision(17) << "{\n"
                 << "  \"mode\": \"" << mode_name << "\",\n"
                 << "  \"eta\": " << eta << ",\n"
                 << "  \"packed_size\": " << n << ",\n"
                 << "  \"dims\": [17,17,16,18,18,17],\n"
                 << "  \"steps\": 256,\n"
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
        TORCH_CHECK(host_step == run.host_step + 1 && host_step <= kSteps,
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
        if (host_step == 1 || host_step == 2 || host_step == 4 || host_step == 8 ||
            host_step % 16 == 0) {
            const auto rss = peak_rss_bytes();
            TORCH_CHECK(rss <= kMemoryCapBytes, "peak RSS exceeded the 18 GiB cap");
            std::cerr << "NATIVE_TERMINAL_PROGRESS step=" << host_step
                      << " peak_rss_bytes=" << rss << '\n';
            if (host_step == 4) run.rss_step4 = rss;
            if (host_step == 8) {
                TORCH_CHECK(run.rss_step4 > 0, "missing step-4 RSS sample");
                const double growth_per_step =
                    std::max(0.0, static_cast<double>(rss) - run.rss_step4) / 4.0;
                const double projected = rss + 1.5 * growth_per_step * (kSteps - host_step);
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

extern "C" int sdirk3_native_terminal_record_endpoint(const float* packed_terminal, int n) {
    try {
        std::lock_guard<std::mutex> run_lock(run.mutex);
        TORCH_CHECK(packed_terminal && n == 24531 && run.prepared && run.forward_active && run.host_step == kSteps,
                    "terminal endpoint requested before all 256 accepted host steps");
        run.forward_active = false;
        run.omega_binary.flush(); run.omega_csv.flush();
        TORCH_CHECK(run.omega_binary.good() && run.omega_csv.good(), "Omega trace write failed");

        std::vector<torch::Tensor> checkpoints;
        {
            std::lock_guard<std::mutex> registry_lock(g_tile_solvers_mutex);
            auto& solver = unique_solver_locked();
            checkpoints = solver.getFixedTrajectoryFp64Checkpoints();
            TORCH_CHECK(checkpoints.size() == kSteps,
                        "fixed trajectory did not retain all 256 endpoint states");
        }
        TORCH_CHECK(!checkpoints.empty(), "native terminal trajectory has no endpoints");
        std::vector<double> all;
        all.reserve(static_cast<std::size_t>(kSteps + 1) * 24531);
        auto seed = run.initial_seed64.to(torch::kCPU, torch::kFloat64).contiguous();
        const double* seed_ptr = seed.data_ptr<double>();
        all.insert(all.end(), seed_ptr, seed_ptr + seed.numel());
        for (const auto& checkpoint : checkpoints) {
            TORCH_CHECK(checkpoint.scalar_type() == torch::kFloat64 &&
                        checkpoint.numel() == 24531 &&
                        torch::isfinite(checkpoint).all().item<bool>(),
                        "invalid retained FP64 checkpoint");
            auto c = checkpoint.to(torch::kCPU, torch::kFloat64).contiguous();
            const double* p = c.data_ptr<double>();
            all.insert(all.end(), p, p + c.numel());
        }
        TORCH_CHECK(peak_rss_bytes() <= kMemoryCapBytes,
                    "peak RSS exceeded the 18 GiB cap while copying retained endpoints");
        auto terminal_state64 = checkpoints.back().to(torch::kCPU, torch::kFloat64).contiguous();
        auto lambda64 = run.terminal32.to(torch::kFloat64);
        const double objective = torch::dot(terminal_state64, lambda64).item<double>();
        TORCH_CHECK(std::isfinite(objective), "non-finite terminal objective");
        auto published = torch::from_blob(const_cast<float*>(packed_terminal), {n},
            torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)).clone();
        const auto expected_published = terminal_state64.to(torch::kFloat32).contiguous();
        TORCH_CHECK(torch::equal(published, expected_published),
                    "Fortran endpoint publication differs from FP64 checkpoint cast");
        write_bytes(run.output_prefix + ".checkpoints_f64.bin", all.data(), all.size() * sizeof(double));
        write_tensor(run.output_prefix + ".terminal_state_f64.bin", terminal_state64, torch::kFloat64);
        write_tensor(run.output_prefix + ".terminal_state_published_f32.bin", published, torch::kFloat32);
        {
            std::ofstream meta(run.output_prefix + ".endpoint.json", std::ios::trunc);
            TORCH_CHECK(meta, "cannot create endpoint metadata");
            meta << std::setprecision(17) << "{\n"
                 << "  \"mode\": \"" << run.mode_name << "\",\n"
                 << "  \"eta\": " << run.eta << ",\n"
                 << "  \"host_steps\": " << run.host_step << ",\n"
                 << "  \"endpoint_count_including_seed\": " << checkpoints.size() + 1 << ",\n"
                 << "  \"objective\": " << objective << ",\n"
                 << "  \"terminal_f64_fnv1a64\": \"" << std::hex
                 << fnv1a64(terminal_state64.data_ptr<double>(),
                            static_cast<std::size_t>(terminal_state64.numel()) * sizeof(double))
                 << std::dec << "\",\n"
                 << "  \"trajectory_f64_fnv1a64\": \"" << std::hex
                 << fnv1a64(all.data(), all.size() * sizeof(double)) << std::dec << "\",\n"
                 << "  \"peak_rss_bytes\": " << peak_rss_bytes() << "\n}\n";
        }
        run.endpoint_recorded = true;
        std::cerr << "NATIVE_TERMINAL_ENDPOINT mode=" << run.mode_name
                  << " objective=" << std::setprecision(17) << objective
                  << " endpoints=" << checkpoints.size() + 1
                  << " peak_rss_bytes=" << peak_rss_bytes() << '\n';
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "NATIVE_TERMINAL_ENDPOINT_ERROR: " << e.what() << '\n';
        return 0;
    }
}

extern "C" int sdirk3_native_terminal_finish(
    const float* direction, const float* terminal,
    const float* initial_gradient_f32, int n, int mode, int have_gradient) {
    try {
        TORCH_CHECK(direction && terminal && n == 24531,
                    "invalid native terminal finish arguments");
        std::lock_guard<std::mutex> lock(run.mutex);
        TORCH_CHECK(run.prepared && run.endpoint_recorded && !run.forward_active && mode == run.mode,
                    "native terminal finish phase/mode mismatch");
        const auto f32_options = torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU);
        auto d32 = torch::from_blob(const_cast<float*>(direction), {n}, f32_options).clone();
        auto l32 = torch::from_blob(const_cast<float*>(terminal), {n}, f32_options).clone();
        auto d = d32.to(torch::kFloat64);
        auto l = l32.to(torch::kFloat64);
        write_tensor(run.output_prefix + ".direction_f32.bin", d, torch::kFloat32);
        write_tensor(run.output_prefix + ".terminal_f32.bin", l, torch::kFloat32);
        double directional64 = std::numeric_limits<double>::quiet_NaN();
        double directional32 = std::numeric_limits<double>::quiet_NaN();
        if (run.mode < 2) {
            TORCH_CHECK(have_gradient == 1, "baseline/replay did not request checked pullback");
            TORCH_CHECK(initial_gradient_f32 && run.gradient64.defined(),
                        "baseline/replay missing checked FP32 or captured FP64 pullback");
            auto g32 = torch::from_blob(const_cast<float*>(initial_gradient_f32), {n}, torch::kFloat32).clone();
            TORCH_CHECK(torch::isfinite(g32).all().item<bool>(), "Fortran pullback contains non-finite values");
            auto g64 = run.gradient64;
            TORCH_CHECK(g64.numel() == n && torch::isfinite(g64).all().item<bool>(),
                        "captured FP64 pullback is invalid");
            TORCH_CHECK(torch::equal(g32, g64.to(torch::kFloat32).contiguous()),
                        "Fortran FP32 pullback differs from captured FP64 gradient cast");
            directional64 = torch::dot(d, g64).item<double>();
            directional32 = torch::dot(d, g32.to(torch::kFloat64)).item<double>();
            write_tensor(run.output_prefix + ".gradient64.bin", g64, torch::kFloat64);
            write_tensor(run.output_prefix + ".gradient32.bin", g32, torch::kFloat32);
        } else {
            TORCH_CHECK(have_gradient == 0, "FD run unexpectedly requested a pullback");
        }
        {
            std::ofstream meta(run.output_prefix + ".result.json", std::ios::trunc);
            TORCH_CHECK(meta, "cannot create result metadata");
            meta << std::setprecision(17) << "{\n"
                 << "  \"mode\": \"" << run.mode_name << "\",\n"
                 << "  \"eta\": " << run.eta << ",\n"
                 << "  \"steps\": 256,\n"
                 << "  \"pullback_checked_by_fortran\": " << (run.mode < 2 ? "true" : "false") << ",\n"
                 << "  \"directional_vjp_f64\": ";
            if (run.mode < 2) meta << directional64;
            else meta << "null";
            meta << ",\n  \"directional_vjp_f32_abi\": ";
            if (run.mode < 2) meta << directional32;
            else meta << "null";
            meta << ",\n"
                 << "  \"peak_rss_bytes\": " << peak_rss_bytes() << "\n}\n";
        }
        std::cerr << "NATIVE_TERMINAL_FINISH mode=" << run.mode_name
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
