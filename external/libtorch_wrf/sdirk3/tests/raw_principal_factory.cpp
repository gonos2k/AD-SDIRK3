#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <stdexcept>

#include <torch/torch.h>

#include "wrf_sdirk3_config.h"
#include "wrf_sdirk3_tile_unified.h"
#include "tile_test_fixture_u03.h"

namespace wrf::sdirk3::test {

struct PrincipalFactoryCase {
    struct EnvironmentGuard {
        std::string name;
        std::string prior;
        bool had_prior = false;

        EnvironmentGuard(const char* key, const char* value) : name(key) {
            if (const char* old = std::getenv(key)) {
                had_prior = true;
                prior = old;
            }
            ::setenv(name.c_str(), value, /*overwrite=*/1);
        }

        ~EnvironmentGuard() {
            if (had_prior) {
                ::setenv(name.c_str(), prior.c_str(), /*overwrite=*/1);
            } else {
                ::unsetenv(name.c_str());
            }
        }
    };

    static void require(bool ok, const char* what) {
        if (!ok) throw std::runtime_error(what);
    }

    static void set_representative_canonical_profile() {
        auto& cfg = wrf::sdirk3::g_sdirk3_config;
        // Values copied from the accepted PC2 representative namelist/effective
        // diagnostics.  No private selector or test-only runtime knob is used.
        cfg.precond_type = 2;
        cfg.precond_block_size = 4; // Actual WRF Registry default.
        cfg.precond_mu_coupling_damping = 0.1f; // Archived C++ profile default.
        cfg.mass_coordinate_mode = static_cast<int>(
            SDIRK3Config::MassCoordinateMode::WRFParity);
        cfg.buoyancy_use_current_w = true;
        cfg.imex_enabled = false;
        cfg.imex_split_mode = 3;
        cfg.split_explicit = false;
        cfg.hevi_split = false;
        cfg.omega_w_blend = 1.0f;
        cfg.omega_update_ref_per_newton = false;
        cfg.non_hydrostatic = true;
        cfg.do_curvature = true;
        cfg.precond_coupled_phi_w = false;
    }

    static void exercise_adjoint_replay(TileCaseU03& tile,
                                        UnifiedPreconditioner* production) {
        require(production != nullptr && production->raw_principal_enabled(),
                "replay fixture requires canonical raw preconditioner");

        const auto state = tile.state();
        const auto state_size = tile.solver.getStateVectorSize();
        require(state.numel() == state_size,
                "replay fixture state size disagrees with solver packed size");

        // Two checkpoints are essential: a one-checkpoint run cannot detect a
        // bind accidentally hidden behind a one-shot provenance latch.
        auto second = state.clone();
        second.slice(0, total - sm, total).add_(1000.0f);
        tile.solver.setSavedTrajectory({state.clone(), second}, 0);
        require(tile.solver.getSavedTrajectoryCount() == 2,
                "replay fixture did not install two checkpoints");

        const auto before_fp = production->stage_state_fingerprint();
        const auto before_stage_generation = production->stage_state_generation();
        const auto before_coeff_generation = production->coefficient_generation();
        const auto before_stale = production->coefficients_stale();
        EnvironmentGuard probe("WRF_SDIRK3_RHS_GRAD_PROBE", "1");

        // Zero terminal: exercises both full-state binds and the replay guard
        // with an exactly converged transpose solve.
        const auto zero_terminal = torch::zeros({state_size}, state.options());
        const auto zero_result = tile.solver.runAdjointReplay(
            zero_terminal, 1.0e-6f, 0.4358665215f, 2, 1, 1.0e-5f,
            wrf::sdirk3::implicit_diff::StageCotangent::State);
        require(zero_result.numel() == state_size &&
                    torch::isfinite(zero_result).all().item<bool>() &&
                    zero_result.abs().max().item<float>() == 0.0f,
                "zero-terminal replay was not finite packed zero");

        // Nonzero terminal: exercises the real legacy A^{-T} solve and its
        // physical true-residual stopping authority. This is deliberately not
        // presented as a full SDIRK/ARK discrete-adjoint verification.
        auto nonzero_terminal = torch::arange(state_size, state.options()).add_(1.0f);
        nonzero_terminal = nonzero_terminal / nonzero_terminal.norm();
        const auto nonzero_result = tile.solver.runAdjointReplay(
            nonzero_terminal, 1.0e-6f, 0.4358665215f, 4, 4, 1.0e-4f,
            wrf::sdirk3::implicit_diff::StageCotangent::State);
        require(nonzero_result.numel() == state_size &&
                    torch::isfinite(nonzero_result).all().item<bool>() &&
                    nonzero_result.norm().item<float>() > 0.0f,
                "nonzero legacy transpose replay was not finite and nonzero");

        // runAdjointReplay must leave the production preconditioner untouched;
        // this also catches a restore path that only restores values but bumps
        // lifecycle generations or leaves coefficients stale.
        require(production->stage_state_fingerprint() == before_fp &&
                    production->stage_state_generation() == before_stage_generation &&
                    production->coefficient_generation() == before_coeff_generation &&
                    production->coefficients_stale() == before_stale,
                "adjoint replay changed production preconditioner lifecycle state");
    }

    static int run() {
        try {
            auto& cfg = wrf::sdirk3::g_sdirk3_config;
            set_representative_canonical_profile();

            // TileCaseU03 enters the production TileSDIRK3UnifiedSolver factory
            // and zero-copy setup path.  The friend seam exposes only the
            // solver-owned object for this private wiring assertion.
            TileCaseU03 canonical;
            canonical.solver.setNonHydrostatic(true);
            auto* selected = canonical.solver.unified_precond_.get();
            require(selected != nullptr,
                    "type-2 factory did not install UnifiedPreconditioner");
            require(selected->raw_principal_enabled(),
                    "canonical representative factory profile did not select raw principal");
            require(canonical.solver.newton_solver_ != nullptr,
                    "type-2 factory did not create Newton solver");
            require(canonical.solver.newton_solver_->get_preconditioner() == selected,
                    "factory preconditioner was not connected to Newton solver");

            // A real tile step exercises the Newton-side U_stage/U_eval binding
            // before coefficient publication.  The generation must advance on
            // the actual factory-owned object, not on a manually constructed
            // stand-in.
            canonical.step(0.1f);
            require(selected->stage_state_generation() > 0,
                    "factory-owned raw preconditioner saw no Newton stage snapshot");
            require(selected->raw_principal_generation() > 0,
                    "factory-owned raw preconditioner published no coefficients");

            auto residual = torch::randn({canonical.solver.getStateVectorSize()}, torch::kFloat32);
            auto forward = selected->apply(residual);
            auto transpose = selected->apply_inverse_transpose(residual);
            require(forward.numel() == residual.numel() &&
                        transpose.numel() == residual.numel(),
                    "factory-owned forward/transpose changed packed size");
            require(torch::isfinite(forward).all().item<bool>() &&
                        torch::isfinite(transpose).all().item<bool>(),
                    "factory-owned forward/transpose returned NaN/Inf");

            exercise_adjoint_replay(canonical, selected);

            // The canonical raw model consumes the full packed stage state.
            // A legacy mu-only setter must fail closed rather than leave the
            // previous raw snapshot silently active.
            const auto stage_before_mu_only = selected->stage_state_generation();
            const auto raw_before_mu_only = selected->raw_principal_generation();
            bool mu_only_rejected = false;
            try {
                selected->set_stage_state(torch::zeros({ny, nx}, torch::kFloat32), 2);
            } catch (const c10::Error&) {
                mu_only_rejected = true;
            }
            require(mu_only_rejected,
                    "raw principal accepted a mu-only stage bind");
            require(selected->stage_state_generation() == stage_before_mu_only &&
                        selected->raw_principal_generation() == raw_before_mu_only,
                    "rejected mu-only bind mutated raw stage generations");

            // Rebinding a different full state must rebuild the raw coefficients,
            // and rebinding the original state must restore the original action.
            // This exercises the state snapshot lifetime independently of the
            // Newton caller's storage reuse.
            const auto state0 = canonical.state();
            auto state_perturbed = state0.clone();
            constexpr int64_t phi0 = static_cast<int64_t>(su) + sv + sw;
            constexpr int64_t theta0 = phi0 + sw;
            constexpr int64_t mu0 = theta0 + st;
            state_perturbed.slice(0, theta0, theta0 + st).add_(0.01f);
            state_perturbed.slice(0, mu0, mu0 + sm).add_(1.0f);
            constexpr float replay_dt = 1.0f;
            constexpr float replay_gamma = 0.4358665215f;

            selected->bind_raw_principal_state_or_throw(state0, 2, "factory-rebind-base");
            selected->update_time_coefficients(replay_dt, replay_gamma);
            const auto base_action = selected->apply(residual);
            const auto base_generation = selected->raw_principal_generation();
            selected->update_time_coefficients(replay_dt + 1.0e-7f, replay_gamma);
            require(selected->raw_principal_generation() > base_generation,
                    "raw principal reused coefficients for a changed h within cache tolerance");
            selected->update_time_coefficients(replay_dt, replay_gamma);

            selected->bind_raw_principal_state_or_throw(
                state_perturbed, 3, "factory-rebind-perturbed");
            selected->update_time_coefficients(replay_dt, replay_gamma);
            const auto perturbed_action = selected->apply(residual);
            require((perturbed_action - base_action).abs().max().item<float>() > 1.0e-8f,
                    "raw principal rebind did not change the state-dependent action");
            require(selected->raw_principal_generation() > base_generation,
                    "raw principal rebind did not publish a new coefficient generation");

            selected->bind_raw_principal_state_or_throw(state0, 1, "factory-rebind-restore");
            selected->update_time_coefficients(replay_dt, replay_gamma);
            const auto restored_action = selected->apply(residual);
            require(torch::equal(restored_action, base_action),
                    "raw principal restore did not reproduce the base action");

            // These type-2 options are ignored by the fixed owner. Exercise
            // the full ignored-key surface together, including the adaptive
            // GS thresholds that the legacy implementation no longer reads.
            cfg.precond_block_size = 0;
            cfg.precond_diagonal_only = true;
            cfg.precond_block_jacobi = false;
            cfg.precond_ilu = true;
            cfg.precond_ilu_level = 4;
            cfg.precond_multigrid = true;
            cfg.precond_mg_levels = 7;
            cfg.precond_gs_ratio_threshold = 0.2f;
            cfg.precond_gs_fast_threshold = 0.3f;
            TileCaseU03 ignored_options;
            ignored_options.solver.setNonHydrostatic(true);
            require(ignored_options.solver.unified_precond_->raw_principal_enabled(),
                    "ignored type-2 options changed the factory model");
            ignored_options.step(0.1f);
            require(torch::equal(canonical.state(), ignored_options.state()),
                    "ignored type-2 options changed the accepted tile state");
            require(torch::equal(forward,
                        ignored_options.solver.unified_precond_->apply(residual)),
                    "ignored type-2 options changed the preconditioner action");
            cfg.precond_diagonal_only = false;
            cfg.precond_block_jacobi = true;
            cfg.precond_ilu = false;
            cfg.precond_ilu_level = 0;
            cfg.precond_multigrid = false;
            cfg.precond_mg_levels = 3;
            cfg.precond_gs_ratio_threshold = 0.9f;
            cfg.precond_gs_fast_threshold = 0.7f;
            cfg.precond_block_size = 4;

            // The Fortran Registry default for the legacy damping knob is
            // 0.7, while the archived C++ profile is 0.1. Raw M ignores this
            // legacy-only value, so both front-end defaults must preserve the
            // same canonical owner, state, and action.
            cfg.precond_mu_coupling_damping = 0.7f;
            TileCaseU03 registry_default;
            registry_default.solver.setNonHydrostatic(true);
            require(registry_default.solver.unified_precond_->raw_principal_enabled(),
                    "Registry damping default changed the factory model");
            registry_default.step(0.1f);
            require(torch::equal(canonical.state(), registry_default.state()),
                    "Registry damping default changed the accepted tile state");
            require(torch::equal(forward,
                        registry_default.solver.unified_precond_->apply(residual)),
                    "Registry damping default changed the preconditioner action");

            // A user-selected damping value outside the two known front-end
            // defaults remains a legacy request and must retain the legacy
            // owner rather than being silently ignored by raw M.
            cfg.precond_mu_coupling_damping = 0.55f;
            TileCaseU03 custom_damping;
            custom_damping.solver.setNonHydrostatic(true);
            require(custom_damping.solver.unified_precond_ != nullptr &&
                        !custom_damping.solver.unified_precond_->raw_principal_enabled(),
                    "custom damping value was silently accepted by raw principal");
            cfg.precond_mu_coupling_damping = 0.1f;

            // Logging is observational: it must change neither the selected
            // operator nor the accepted state and preconditioner action.
            cfg.precond_log_raw_values = false;
            TileCaseU03 quiet;
            quiet.solver.setNonHydrostatic(true);
            auto* quiet_selected = quiet.solver.unified_precond_.get();
            require(quiet_selected && quiet_selected->raw_principal_enabled(),
                    "logging changed the factory's numerical model");
            quiet.step(0.1f);
            require(torch::equal(canonical.state(), quiet.state()),
                    "logging changed the accepted tile state");
            require(torch::equal(forward, quiet_selected->apply(residual)),
                    "logging changed the preconditioner action");
            cfg.precond_log_raw_values = true;

            // A non-default legacy option must retain the legacy owner even
            // through the same type-2 factory.
            cfg.precond_coupled_phi_w = true;
            TileCaseU03 legacy_option;
            auto* legacy = legacy_option.solver.unified_precond_.get();
            require(legacy != nullptr,
                    "legacy-option type-2 factory did not install legacy owner");
            require(!legacy->raw_principal_enabled(),
                    "factory silently dropped legacy Phi-W option");
            cfg.precond_coupled_phi_w = false;

            // Existing PC0 remains no-preconditioner; the normal build does not
            // need a compile-time default-off branch.
            cfg.precond_type = 0;
            TileCaseU03 no_preconditioner;
            require(no_preconditioner.solver.unified_precond_ == nullptr,
                    "precond_type=0 factory installed a preconditioner");
            require(no_preconditioner.solver.newton_solver_ != nullptr &&
                        no_preconditioner.solver.newton_solver_->get_preconditioner() == nullptr,
                    "precond_type=0 factory connected a preconditioner to Newton");

            std::cout << "raw-principal factory: PASS type2=canonical legacy-option=legacy type0=off"
                      << " stage_generation=" << selected->stage_state_generation()
                      << " raw_generation=" << selected->raw_principal_generation() << "\n";
            return EXIT_SUCCESS;
        } catch (const c10::Error& e) {
            std::cerr << "raw-principal factory: FAIL c10: " << e.what() << "\n";
            return EXIT_FAILURE;
        } catch (const std::exception& e) {
            std::cerr << "raw-principal factory: FAIL: " << e.what() << "\n";
            return EXIT_FAILURE;
        }
    }
};

} // namespace wrf::sdirk3::test

int main() {
    return wrf::sdirk3::test::PrincipalFactoryCase::run();
}
