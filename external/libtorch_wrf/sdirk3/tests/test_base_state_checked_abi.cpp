// 9F.D45: the base-state C ABI's return contract (review section 4).
//
// WHY THIS EXISTS, AND WHY IT IS SEPARATE FROM THE LAYOUT TEST. Base_State_Layout_Contract
// proves the geometry helper throws on bad input. That is NOT the same claim as "the C
// entry point returns 0 on bad input" -- and the difference is precisely the defect this
// commit fixes: D44's validator threw correctly and sat OUTSIDE the caller's try, so the
// throw escaped through extern "C" instead of becoming a 0. A helper that throws and an
// ABI that converts throws into a status code are two properties, and only the second
// one is what Fortran depends on.
//
// So every case here calls the REAL exported symbol and asserts on its int return,
// never on an exception. If any case terminates the process instead of returning, the
// contract is broken in the way that matters.
//
// NEGATIVE CONTROL, VERIFIED. Reproducing D44's ordering -- calling the layout helper
// just BEFORE the try -- makes this fixture die at the first invalid-geometry case:
//     exit 134, "terminating due to uncaught exception of type std::invalid_argument:
//      base-state layout: i tile [8,2] outside memory [1,16]"
// which is exactly what the Fortran run would have done. Note the failure mode is
// necessarily an ABORT, not a clean assertion: a throw crossing extern "C" terminates,
// so the test cannot survive to report it. Here the abort IS the contract violation,
// unlike the layout fixture where an abort merely made a real regression unreadable.

#include "../wrf_sdirk3_interface.h"
#include "../wrf_sdirk3_interface_params.h"
#include "../wrf_sdirk3_config.h"
#include "tile_test_fixture.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

extern "C" void sdirk3_set_timestep_i4(int* timestep);

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const std::string& what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

void call_v2_mixed_tendency(void* solver, float* state, float* metric,
                            float* ru_tend) {
    SDIRK3_IndexBounds bounds{2, 8, 2, 6, 1, 8,
                              1, 16, 1, 12, 1, 10,
                              1, 16, 1, 12, 1, 10};
    SDIRK3_Dimensions dims{7, 5, 7, 8, 6, 8, 2};
    SDIRK3_ScalarParams scalars{1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.01f, 0.01f};
    SDIRK3_BoundaryConfig bdy{0, 0, 0, 0};
    sdirk3_tile_unified_step_zerocopy_v2(
        solver,
        state, state, state, state, state, state, state, state,
        nullptr, 0,
        ru_tend, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        metric, metric, metric, metric, metric, metric, metric, metric,
        metric, metric, metric, metric, metric, metric, metric, metric,
        metric, metric,
        nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr,
        &bounds, &dims, &scalars, &bdy);
}

// A memory domain big enough for the tile below.
constexpr int IMS = 1, IME = 16, JMS = 1, JME = 12, KMS = 1, KME = 10;
constexpr int ITS = 2, ITE = 8,  JTS = 2, JTE = 6,  KTS = 1, KTE = 8;

std::vector<float> buf3d() { return std::vector<float>((IME - IMS + 1) *
                                                       (KME - KMS + 1) *
                                                       (JME - JMS + 1), 1.0f); }
std::vector<float> buf2d() { return std::vector<float>((IME - IMS + 1) *
                                                       (JME - JMS + 1), 1.0f); }

// Calls the exported symbol with overridable geometry; solver_ptr is deliberately a
// value that is NOT in the registry unless stated otherwise.
int call(void* solver,
         float* pb, float* t_init, float* phb, float* mub,
         int its = ITS, int ite = ITE, int jts = JTS, int jte = JTE,
         int kts = KTS, int kte = KTE) {
    return sdirk3_tile_set_base_state_checked(
        solver, pb, t_init, phb, mub,
        its, ite, jts, jte, kts, kte,
        IMS, IME, JMS, JME, KMS, KME,
        nullptr, nullptr, nullptr, nullptr,
        0, 0.0f, 0, 0.0f, 0, 0.0f, 0, 0.0f, 0.0f);
}

bool registered_carry_pullback(int steps) {
    using namespace wrf::sdirk3::test;
    auto& cfg = wrf::sdirk3::g_sdirk3_config;
    const auto saved_cfg = cfg;
    void* handle = nullptr;
    try {
        cfg = wrf::sdirk3::SDIRK3Config{};
        cfg.imex_split_mode = 3;
        cfg.mass_coordinate_mode = 1;
        cfg.imex_slow_in_tangent = true;
        cfg.use_autograd = true;
        cfg.diffusion_option = 2;
        cfg.khdif = 1000.0f;
        cfg.kvdif = 10.0f;
        cfg.precond_type = 0;
        cfg.max_newton_iter = 40;
        cfg.newton_tol = 1e-10f;
        cfg.krylov_tol = 1e-8f;
        cfg.stage_fail_action = 1;
        cfg.gmres_warmstart = false;
        cfg.inn_warmstart_enable = false;

        constexpr float spacing = 5000.0f;
        TileCase tile(spacing);
        for (int j = 0; j < ny; ++j) for (int k = 0; k < nz; ++k) {
            for (int i = 0; i < nu; ++i) {
                tile.u[(j * nz + k) * nu + i] = i == nx ? 0.0f :
                    0.3f * std::sin(2.0 * std::acos(-1.0) * i / nx);
            }
            for (int i = 0; i < nx; ++i) {
                tile.theta[(j * nz + k) * nx + i] = 0.5f *
                    std::cos(2.0 * std::acos(-1.0) * i / nx) *
                    std::cos(2.0 * std::acos(-1.0) * j / (ny - 1));
            }
        }

        std::vector<float> create_rdnw(nz, static_cast<float>(nz));
        handle = sdirk3_tile_solver_create_zerocopy(
            nx, ny, nz, spacing, spacing, create_rdnw.data(), 1200 + steps,
            nu, nv, nw);
        if (!handle) throw std::runtime_error("registered carry solver creation failed");
        auto& solver = *static_cast<TileSDIRK3UnifiedSolver*>(handle);
        solver.setWRFIndices(1, nx + 1, 1, ny + 1, 1, nz,
                             1, nx + 1, 1, ny + 1, 1, nz + 1,
                             -2, nx + 4, -2, ny + 4, 1, nz + 1);
        solver.setBoundaryConditions(true, false, false, false, true, true,
                                     false, false, false, false);

        std::vector<float> pressure(st), t_initial(st, 0.0f), phi(sw), mass(sm, 80000.0f);
        const auto p_column = torch::linspace(90000.0f, 30000.0f, nz, torch::kFloat32);
        const auto alpha_column = wrf::sdirk3::compute_inverse_density(
            torch::full_like(p_column, 300.0f), p_column,
            287.0f, 717.5f, 1004.5f, 100000.0f);
        const std::vector<float> alpha(alpha_column.data_ptr<float>(),
                                       alpha_column.data_ptr<float>() + nz);
        const auto phi_column = wrf::sdirk3::integrate_phb_hydrostatic(
            std::vector<float>(nz, -nz), alpha, std::vector<float>(nz, 1.0f),
            std::vector<float>(nz, 0.0f), 80000.0f, 0.0f);
        for (int j = 0; j < ny; ++j) {
            for (int k = 0; k < nz; ++k)
                for (int i = 0; i < nx; ++i)
                    pressure[(j * nz + k) * nx + i] =
                        100000.0f - (k + 0.5f) * 80000.0f / nz;
            for (int k = 0; k < nw; ++k)
                for (int i = 0; i < nx; ++i)
                    phi[(j * nw + k) * nx + i] = phi_column[k];
        }
        solver.setBaseState(pressure.data(), t_initial.data(), phi.data(), mass.data());

        // Publish the WRF-owned auxiliary arrays before the deferred ABI request
        // activates. This is setup only; retained FP64 carry is enabled below.
        wrf::sdirk3::ZeroCopyConfig setup{};
        setup.nx = nx; setup.ny = ny; setup.nz = nz;
        setup.nx_u = nu; setup.ny_v = nv; setup.nz_w = nw;
        setup.ids = setup.its = setup.ims = 1; setup.ide = setup.ite = setup.ime = nu;
        setup.jds = setup.jts = setup.jms = 1; setup.jde = setup.jte = setup.jme = nv;
        setup.kds = setup.kts = setup.kms = 1; setup.kde = setup.kme = nw; setup.kte = nz;
        setup.u_ptr = setup.v_ptr = setup.w_ptr = setup.ph_ptr =
            setup.t_ptr = setup.p_ptr = tile.setup_state.data();
        setup.mu_ptr = tile.setup_state.data(); setup.al_ptr = tile.setup_density.data();
        setup.rdx = setup.rdy = 1.0f / spacing;
        setup.rdnw_ptr = setup.rdn_ptr = tile.metric.data();
        setup.msftx_ptr = setup.msfty_ptr = setup.msfux_ptr = setup.msfuy_ptr =
            setup.msfvx_ptr = setup.msfvy_ptr = tile.setup_map.data();
        setup.c1f_ptr = setup.c1h_ptr = tile.one.data();
        setup.c2f_ptr = setup.c2h_ptr = tile.zero.data();
        setup.fnm_ptr = setup.fnp_ptr = tile.half.data();
        setup.f_ptr = tile.setup_f.data();
        setup.e_ptr = setup.sina_ptr = tile.setup_zero.data();
        setup.cosa_ptr = tile.setup_map.data();
        solver.advanceZeroCopy(setup, 2, 0.1f);
        auto grid = std::static_pointer_cast<wrf::sdirk3::WRFGridInfoExtended>(solver.getGridInfo());
        grid->smagorinsky_opt = 1;
        grid->g = 9.81f;
        grid->fzm = torch::full({nw}, 0.5f, torch::kFloat32);
        grid->fzp = torch::full({nw}, 0.5f, torch::kFloat32);

        cfg.internal_fp64 = true;
        cfg.internal_fp64_state_carry = true;
        cfg.retain_graph_for_adjoint = true;
        std::vector<float> schedule(static_cast<size_t>(steps), 0.25f);
        if (sdirk3_tile_solver_begin_fixed_trajectory_zerocopy(
                handle, steps, schedule.data(), steps) != 1)
            throw std::runtime_error("registered C ABI trajectory request failed");

        for (int n = 0; n < steps; ++n) {
            int host_timestep = n + 1;
            sdirk3_set_timestep_i4(&host_timestep);
            solver.unifiedStep(
                tile.u.data(), tile.v.data(), tile.w.data(), tile.ph.data(),
                tile.theta.data(), tile.mu.data(), tile.ru.data(), tile.rv.data(),
                tile.rw.data(), tile.rph.data(), tile.rt.data(), tile.rm.data(),
                1.0f / spacing, 1.0f / spacing, tile.metric.data(), tile.metric.data(),
                tile.mass_map.data(), tile.mass_map.data(), tile.u_map.data(), tile.u_map.data(),
                tile.v_map.data(), tile.v_map.data(), tile.one.data(), tile.zero.data(),
                tile.one.data(), tile.zero.data(), tile.half.data(), tile.half.data(),
                1, 0.25f, nx, ny, nz, nu, nv, nw);
            if (solver.getLastStepOutcomeCode() != 0)
                throw std::runtime_error("registered FP64 carry step was not accepted");
        }

        const int state_size = sdirk3_tile_solver_get_state_vector_size_zerocopy(handle);
        std::vector<float> terminal(static_cast<size_t>(state_size));
        for (int i = 0; i < state_size; ++i)
            terminal[static_cast<size_t>(i)] = std::cos(0.031f * i);
        auto terminal64 = torch::from_blob(terminal.data(), {state_size},
            torch::TensorOptions().dtype(torch::kFloat32).device(torch::kCPU)).clone()
                .to(torch::kFloat64);
        const auto expected = solver.pullbackFixedTrajectory(terminal64)
            .detach().to(torch::kFloat32).contiguous();
        std::vector<float> initial(static_cast<size_t>(state_size), -37.0f);
        const int pullback_ok = sdirk3_tile_solver_pullback_fixed_trajectory_zerocopy(
            handle, terminal.data(), state_size, initial.data());
        bool output_matches = pullback_ok == 1 && expected.numel() == state_size;
        const auto* expected_ptr = expected.data_ptr<float>();
        for (int i = 0; output_matches && i < state_size; ++i)
            output_matches = std::isfinite(initial[static_cast<size_t>(i)]) &&
                initial[static_cast<size_t>(i)] == expected_ptr[i];
        if (sdirk3_tile_solver_close_fixed_trajectory_zerocopy(handle) != 1)
            output_matches = false;
        sdirk3_tile_solver_destroy_zerocopy(handle);
        handle = nullptr;
        cfg = saved_cfg;
        return output_matches;
    } catch (const std::exception& e) {
        std::cerr << "registered FP64 carry ABI regression ERROR: " << e.what() << std::endl;
        if (handle) {
            (void)sdirk3_tile_solver_close_fixed_trajectory_zerocopy(handle);
            sdirk3_tile_solver_destroy_zerocopy(handle);
        }
        cfg = saved_cfg;
        return false;
    }
}

}  // namespace

int main() {
    auto pb = buf3d(), ti = buf3d(), phb = buf3d();
    auto mub = buf2d();
    int dummy_solver = 0;
    void* not_registered = &dummy_solver;

    // --- null inputs return 0, they do not throw ---
    check(call(nullptr, pb.data(), ti.data(), phb.data(), mub.data()) == 0,
          "null solver -> 0");
    check(call(not_registered, nullptr, ti.data(), phb.data(), mub.data()) == 0,
          "null pb -> 0");
    check(call(not_registered, pb.data(), nullptr, phb.data(), mub.data()) == 0,
          "null t_init -> 0");
    check(call(not_registered, pb.data(), ti.data(), nullptr, mub.data()) == 0,
          "null phb -> 0");
    check(call(not_registered, pb.data(), ti.data(), phb.data(), nullptr) == 0,
          "null mub -> 0");

    // --- a solver that is not in the registry returns 0 ---
    check(call(not_registered, pb.data(), ti.data(), phb.data(), mub.data()) == 0,
          "unregistered solver -> 0");

    // --- INVALID GEOMETRY RETURNS 0 RATHER THAN THROWING ---
    // This is the section-2 defect. Before the fix these threw std::invalid_argument /
    // c10::Error out through extern "C"; reaching the assertion below at all is the
    // proof that they no longer do. The solver is unregistered, so a 0 here could also
    // come from the registry miss -- which is why the layout contract covers the
    // geometry semantics separately and this file covers only "no throw crosses the
    // boundary".
    check(call(not_registered, pb.data(), ti.data(), phb.data(), mub.data(),
               /*its*/ 8, /*ite*/ 2) == 0,
          "inverted i tile -> 0, no throw escapes");
    check(call(not_registered, pb.data(), ti.data(), phb.data(), mub.data(),
               ITS, ITE, JTS, JTE, KTS, /*kte*/ KME) == 0,
          "phb w-level overflow -> 0, no throw escapes");
    check(call(not_registered, pb.data(), ti.data(), phb.data(), mub.data(),
               /*its*/ IMS - 5, ITE) == 0,
          "tile below memory domain -> 0, no throw escapes");
    check(call(not_registered, pb.data(), ti.data(), phb.data(), mub.data(),
               ITS, /*ite*/ IME + 5) == 0,
          "tile above memory domain -> 0, no throw escapes");

    // Reaching here without terminating IS the contract: every call above returned an
    // int across a C boundary. State it as an assertion so the log says so explicitly.
    check(true, "all invalid-input calls RETURNED across the C ABI (none terminated)");

    // U04: a registered v2 handle must reject mixed tendency ownership before
    // touching the tile implementation. The all-null production mode reaches
    // the same prevalidation point; one supplied pointer is the forbidden mode.
    std::vector<float> rdnw(8, 1.0f);
    std::vector<float> state(4096, 1.0f), metric(4096, 1.0f);
    void* registered = sdirk3_tile_solver_create_zerocopy(
        7, 5, 7, 1.0f, 1.0f, rdnw.data(), 901, 8, 6, 8);
    check(registered != nullptr, "v2 regression solver handle registered");
    if (registered != nullptr) {
        check(sdirk3_tile_solver_begin_fixed_trajectory_zerocopy(
                  registered, 1, nullptr, 0) == 1,
              "fixed trajectory request accepted before first publication");
        const int trajectory_size = sdirk3_tile_solver_get_state_vector_size_zerocopy(registered);
        bool sentinel_preserved = false;
        if (trajectory_size > 0) {
            std::vector<float> terminal(static_cast<size_t>(trajectory_size), 1.0f);
            std::vector<float> initial(static_cast<size_t>(trajectory_size), -37.0f);
            sentinel_preserved =
                sdirk3_tile_solver_pullback_fixed_trajectory_zerocopy(
                    registered, terminal.data(), trajectory_size, initial.data()) == 0 &&
                initial == std::vector<float>(static_cast<size_t>(trajectory_size), -37.0f);
        }
        check(trajectory_size > 0 && sentinel_preserved,
              "incomplete C ABI pullback returns 0 and preserves sentinel output");
        check(sdirk3_tile_solver_close_fixed_trajectory_zerocopy(registered) == 1,
              "close cancels pending C ABI request before first publication");
        check(call(registered, pb.data(), ti.data(), phb.data(), mub.data()) == 1,
              "v2 regression base state initialized");
        int outcome = -1, aborted = -1, ratio_valid = -1;
        float ratio = -1.0f;
        call_v2_mixed_tendency(registered, state.data(), metric.data(), nullptr);
        check(sdirk3_tile_solver_get_last_step_outcome_zerocopy(
                  registered, &outcome, &aborted, &ratio, &ratio_valid) == 1 &&
                  outcome == SDIRK3_STEP_OUTCOME_OK_SKIPPED && aborted == 0,
              "v2 all-null tendency control reaches OK_SKIPPED");
        const std::vector<float> state_before_mixed = state;
        float sentinel = 7.0f;
        call_v2_mixed_tendency(registered, state.data(), metric.data(), &sentinel);
        outcome = -1;
        check(sdirk3_tile_solver_get_last_step_outcome_zerocopy(
                  registered, &outcome, &aborted, &ratio, &ratio_valid) == 1 &&
                  outcome == SDIRK3_STEP_OUTCOME_FATAL_INPUT && aborted == 1 &&
                  ratio_valid == 0 && sentinel == 7.0f && state == state_before_mixed,
              "v2 mixed tendency pointers -> FATAL_INPUT, state unmodified");
        sdirk3_tile_solver_destroy_zerocopy(registered);
    }

    check(registered_carry_pullback(1),
          "registered C ABI FP64 carry pullback promotes terminal for N=1");
    check(registered_carry_pullback(2),
          "registered C ABI FP64 carry pullback promotes terminal for N=2");

    constexpr int expected_checks = 20;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) { std::cout << "BASE_STATE_CHECKED_ABI: PASS" << std::endl; return 0; }
    std::cout << "BASE_STATE_CHECKED_ABI: FAIL (" << failures << ")" << std::endl;
    return 1;
}
