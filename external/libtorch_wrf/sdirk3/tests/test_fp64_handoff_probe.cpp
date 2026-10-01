// Isolate inter-step FP32 publication from the same autonomous ARK324 tile map.
// This excludes Fortran-side physics, boundary refresh, and MPI communication.
#include "tile_test_fixture.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <thread>
#include <utility>

extern "C" void sdirk3_set_timestep_i4(int* timestep);
namespace wrf::sdirk3::mpi_safety {
uint64_t currentHostTimestep() noexcept;
}

namespace {
using namespace wrf::sdirk3::test;

void set_host_timestep(int timestep) {
    sdirk3_set_timestep_i4(&timestep);
}

void check_host_timestep_publication() {
    set_host_timestep(17);
    uint64_t worker_step = 0;
    std::thread worker([&] {
        worker_step = wrf::sdirk3::mpi_safety::currentHostTimestep();
    });
    worker.join();
    TORCH_CHECK(worker_step == 17,
                "host timestep was not published across worker threads");
}

struct Result {
    torch::Tensor first_internal;
    torch::Tensor internal;
};

void initialize(TileCase& tile) {
    for (int j = 0; j < ny; ++j) {
        for (int k = 0; k < nz; ++k) {
            for (int i = 0; i < nu; ++i)
                tile.u[(j*nz+k)*nu+i] = i == nx ? 0.0f :
                    std::sin(2.0*std::acos(-1.0)*i/nx);
            for (int i = 0; i < nx; ++i)
                tile.theta[(j*nz+k)*nx+i] = 0.5f*
                    std::cos(2.0*std::acos(-1.0)*i/nx)*
                    std::cos(2.0*std::acos(-1.0)*j/(ny-1));
        }
    }
}

Result integrate(int steps, bool continuous, float total_time = 4.0f) {
    TileCase tile(5000.0f);
    tile.solver.captureArkBudgetTraceForTest(true);
    initialize(tile);
    torch::Tensor internal, first_internal;
    const float dt = total_time/steps;
    for (int s = 0; s < steps; ++s) {
        if (continuous && s > 0) tile.stepWithInternalState(dt, internal);
        else tile.step(dt);
        const auto& trace = tile.solver.getLastArkBudgetTrace();
        TORCH_CHECK(trace.projected_final.defined() &&
                    trace.projected_final.scalar_type() == torch::kFloat64,
                    "missing internal FP64 completed state");
        internal = trace.projected_final.detach().clone();
        if (s == 0) first_internal = internal.clone();
        TORCH_CHECK(torch::isfinite(internal).all().item<bool>() &&
                    (internal.slice(0, total-sm, total)+80000.0).min().item<double>() > 0.0 &&
                    (internal.slice(0, su+sv+2*sw, total-sm)+300.0).min().item<double>() > 0.0,
                    "non-finite or nonphysical internal state");
        TORCH_CHECK(torch::equal(tile.state(), internal.to(torch::kFloat32)),
                    "published state differs from the FP64 result rounded to FP32");
        for (const auto* forcing : tile.tendencies())
            TORCH_CHECK(std::all_of(forcing->begin(), forcing->end(),
                                    [](float x) { return x == 0.0f; }),
                        "external forcing changed during the handoff probe");
    }
    return {first_internal, internal};
}

Result integrate_solver_carry(int steps, float total_time = 4.0f) {
    TileCase tile(5000.0f);
    tile.solver.captureArkBudgetTraceForTest(true);
    initialize(tile);
    torch::Tensor internal, first_internal;
    const float dt = total_time/steps;
    for (int s = 0; s < steps; ++s) {
        // The WRF v2 ABI republishes unchanged tile/domain bounds every call.
        if (s > 0) tile.solver.setWRFIndices(1, nx+1, 1, ny+1, 1, nz,
                                              1, nx+1, 1, ny+1, 1, nz+1,
                                              -2, nx+4, -2, ny+4, 1, nz+1);
        set_host_timestep(s+1);
        tile.step(dt);
        internal = tile.solver.getLastArkBudgetTrace().projected_final.detach().clone();
        if (s == 0) first_internal = internal.clone();
        TORCH_CHECK(torch::equal(tile.state(), internal.to(torch::kFloat32)),
                    "solver-owned carry did not publish its FP32 projection");
    }
    return {first_internal, internal};
}

void check_carry_guards() {
    constexpr float dt = 1.0f;
    TileCase changed(5000.0f);
    initialize(changed);
    set_host_timestep(1);
    changed.step(dt);
    changed.u[nz*nu+1] += 0.25f;
    set_host_timestep(2);
    bool rejected = false;
    try { changed.step(dt); }
    catch (const std::exception& e) {
        rejected = std::string(e.what()).find("FP64 state carry rejected") !=
                   std::string::npos;
    }
    TORCH_CHECK(rejected, "solver-owned carry accepted a changed host state");

    TileCase changed_context(5000.0f);
    initialize(changed_context);
    set_host_timestep(1);
    changed_context.step(dt);
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    const float original_k = cfg.khdif;
    cfg.khdif += 1.0f;
    set_host_timestep(2);
    rejected = false;
    try { changed_context.step(dt); }
    catch (const std::exception& e) {
        rejected = std::string(e.what()).find("FP64 state carry rejected") !=
                   std::string::npos;
    }
    cfg.khdif = original_k;
    TORCH_CHECK(rejected, "solver-owned carry accepted a changed RHS coefficient");

    TileCase skipped(5000.0f);
    initialize(skipped);
    set_host_timestep(1);
    skipped.step(dt);
    set_host_timestep(3);
    rejected = false;
    try { skipped.step(dt); }
    catch (const std::exception& e) {
        rejected = std::string(e.what()).find("FP64 state carry rejected") !=
                   std::string::npos;
    }
    TORCH_CHECK(rejected, "solver-owned carry accepted a skipped host timestep");

    TileCase first(5000.0f), second(5000.0f);
    initialize(first);
    initialize(second);
    second.u[nz*nu+1] += 0.1f;
    set_host_timestep(1);
    first.step(dt);
    second.step(dt);
    set_host_timestep(2);
    first.step(dt);
    second.step(dt);
    TORCH_CHECK(!torch::equal(first.state(), second.state()),
                "independent solvers lost their distinct trajectories");

    first.solver.resetInternalFp64Carry();
    first.u[nz*nu+1] += 0.25f;
    set_host_timestep(3);
    first.step(dt);
}

double block_rms(const torch::Tensor& difference, int64_t start, int64_t size) {
    return difference.slice(0, start, start+size).square().mean().sqrt().item<double>();
}

void check_rhs_dt_invariance() {
    TileCase reference(5000.0f);
    initialize(reference);
    reference.step(1.0e-4f);
    const auto state = reference.state().to(torch::kFloat64);
    for (const auto mode : {wrf::sdirk3::RhsMode::Full,
                            wrf::sdirk3::RhsMode::ExplicitOnly,
                            wrf::sdirk3::RhsMode::ImplicitOnly}) {
        torch::Tensor baseline;
        for (const auto schedule : {std::array<float,3>{1.0f,0.5f,2.0f},
                                    std::array<float,3>{2.0f,0.5f,1.0f}}) {
            for (const float dt : schedule) {
                TileCase tile(5000.0f);
                initialize(tile);
                tile.step(1.0e-4f);
                TORCH_CHECK(torch::equal(tile.state().to(torch::kFloat64), state),
                            "fixed-state RHS fixtures differ before changing dt");
                const auto rhs = tile.rhsAt(state, mode, dt);
                TORCH_CHECK(rhs.scalar_type() == torch::kFloat64 &&
                            torch::isfinite(rhs).all().item<bool>(),
                            "fixed-state RHS is not finite FP64");
                if (!baseline.defined()) baseline = rhs.clone();
                else {
                    const double difference = (rhs-baseline).abs().max().item<double>();
                    std::cout << "RHS_DT mode=" << static_cast<int>(mode)
                              << " dt=" << dt << " max_diff=" << difference << '\n';
                    TORCH_CHECK(torch::equal(rhs, baseline),
                                "fixed-state physical RHS changed with timestep");
                }
            }
        }
        TORCH_CHECK(baseline.abs().max().item<double>() > 1e-6,
                    "fixed-state RHS is too small to test timestep invariance");
    }
}

void check_imported_diagnostic_independence() {
    for (const auto mode : {wrf::sdirk3::RhsMode::Full,
                            wrf::sdirk3::RhsMode::ExplicitOnly,
                            wrf::sdirk3::RhsMode::ImplicitOnly}) {
        for (const bool displaced : {false, true}) {
            TileCase tile(5000.0f);
            initialize(tile);
            tile.step(1.0e-4f);
            const auto state = tile.state().to(torch::kFloat64);
            auto stage_reference = state.clone();
            if (displaced)
                stage_reference.slice(0, su+sv, su+sv+sw).add_(1.0e-3);
            const auto baseline = tile.rhsAt(state, mode, 1.0f, stage_reference).clone();
            tile.replaceImportedDiagnosticsForTest(125000.0f, 0.75f);
            const auto changed = tile.rhsAt(state, mode, 1.0f, stage_reference);
            TORCH_CHECK(torch::equal(baseline, changed),
                        "dry ARK RHS consumed stale imported p/al diagnostics");
        }
    }
}
}  // namespace

int main() {
    try {
        torch::set_num_threads(1);
        auto& cfg = wrf::sdirk3::g_sdirk3_config;
        cfg = wrf::sdirk3::SDIRK3Config{};
        cfg.internal_fp64 = true;
        cfg.imex_split_mode = 3;
        cfg.mass_coordinate_mode = 1;
        cfg.diffusion_option = 2;
        cfg.khdif = 1000.0f;
        cfg.kvdif = 0.0f;
        cfg.use_autograd = true;
        cfg.precond_type = 0;
        cfg.n_threads = 1;
        cfg.max_newton_iter = 40;
        cfg.newton_tol = 1e-9f;
        cfg.krylov_tol = 1e-7f;
        cfg.stage_fail_action = 1;
        cfg.gmres_warmstart = false;
        cfg.inn_warmstart_enable = false;

        check_host_timestep_publication();
        check_rhs_dt_invariance();
        check_imported_diagnostic_independence();

        std::array<int,4> counts{4,8,16,32};
        std::array<Result,4> rounded, continuous;
        for (size_t n = 0; n < counts.size(); ++n) {
            rounded[n] = integrate(counts[n], false);
            continuous[n] = integrate(counts[n], true);
            TORCH_CHECK(torch::equal(rounded[n].first_internal,
                                     continuous[n].first_internal),
                        "A/B first step differs before any handoff");
        }
        cfg.internal_fp64_state_carry = true;
        for (size_t n = 0; n < 3; ++n) {
            const auto owned = integrate_solver_carry(counts[n]);
            TORCH_CHECK(torch::equal(owned.first_internal,
                                     continuous[n].first_internal) &&
                        torch::equal(owned.internal, continuous[n].internal),
                        "solver-owned carry differs from the isolated FP64 handoff arm");
        }
        check_carry_guards();
        cfg.internal_fp64_state_carry = false;
        constexpr std::array<int64_t,6> starts{0,su,su+sv,su+sv+sw,
                                                su+sv+2*sw,total-sm};
        constexpr std::array<int64_t,6> sizes{su,sv,sw,sw,st,sm};
        constexpr std::array<const char*,6> names{"U","V","W","PH","T","MU"};
        for (size_t block = 0; block < names.size(); ++block) {
            std::cout << std::setprecision(12) << names[block];
            for (int arm = 0; arm < 2; ++arm) {
                const auto& runs = arm == 0 ? rounded : continuous;
                const auto d1 = block_rms(runs[0].internal-runs[1].internal,
                                          starts[block],sizes[block]);
                const auto d2 = block_rms(runs[1].internal-runs[2].internal,
                                          starts[block],sizes[block]);
                const auto d3 = block_rms(runs[2].internal-runs[3].internal,
                                          starts[block],sizes[block]);
                std::cout << (arm == 0 ? " rounded" : " continuous")
                          << " D=" << d1 << "," << d2 << "," << d3
                          << " p=" << std::log2(d1/d2) << "," << std::log2(d2/d3);
                if (arm == 1) {
                    TORCH_CHECK(std::isfinite(d1) && std::isfinite(d2) &&
                                std::isfinite(d3) && d3 > 0.0 &&
                                std::log2(d1/d2) > 2.6 && std::log2(d1/d2) < 3.4 &&
                                std::log2(d2/d3) > 2.6 && std::log2(d2/d3) < 3.4,
                                "continuous FP64 handoff lost third-order convergence in ",
                                names[block]);
                }
            }
            std::cout << " handoff="
                      << block_rms(rounded[3].internal-continuous[3].internal,
                                   starts[block],sizes[block]) << '\n';
        }
        std::array<double,3> local_u{}, local_t{};
        for (size_t i = 0; i < local_u.size(); ++i) {
            const float h = 1.0f / (1 << i);
            const auto whole = integrate(1, true, h);
            const auto halves = integrate(2, true, h);
            local_u[i] = block_rms(whole.internal-halves.internal, 0, su);
            local_t[i] = block_rms(whole.internal-halves.internal,
                                   su+sv+2*sw, st);
        }
        for (const auto& result : {std::pair{"U", local_u},
                                   std::pair{"T", local_t}})
            std::cout << "LOCAL " << result.first << " D=" << result.second[0]
                      << "," << result.second[1] << "," << result.second[2]
                      << " p=" << std::log2(result.second[0]/result.second[1])
                      << "," << std::log2(result.second[1]/result.second[2]) << '\n';
        for (const auto& defect : {local_u, local_t}) {
            TORCH_CHECK(defect[2] > 0.0 &&
                        std::log2(defect[0]/defect[1]) > 3.5 &&
                        std::log2(defect[0]/defect[1]) < 4.5 &&
                        std::log2(defect[1]/defect[2]) > 3.5 &&
                        std::log2(defect[1]/defect[2]) < 4.5,
                        "continuous FP64 one-step/two-half-step defect is not fourth order");
        }
        const auto t_start = su+sv+2*sw;
        const auto rounded_t_d3 = block_rms(rounded[2].internal-rounded[3].internal,
                                             t_start, st);
        const auto continuous_t_d3 = block_rms(continuous[2].internal-continuous[3].internal,
                                                t_start, st);
        TORCH_CHECK(rounded_t_d3 > 20.0*continuous_t_d3,
                    "FP32 handoff did not produce a distinguishable theta roundoff signal");
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
