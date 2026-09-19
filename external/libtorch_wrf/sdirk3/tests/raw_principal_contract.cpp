#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <torch/torch.h>

#include "wrf_sdirk3_config.h"
#include "wrf_sdirk3_unified_preconditioner.h"
#include "wrf_sdirk3_unified_rhs.h"

using wrf::sdirk3::PhysicsConfig;
using wrf::sdirk3::UnifiedPreconditioner;
using wrf::sdirk3::WRFGridInfo;

namespace {

void require(bool ok, const char* what) {
    if (!ok) throw std::runtime_error(what);
}

torch::Tensor packed_state(const std::shared_ptr<WRFGridInfo>& grid,
                           double phi_shift, double theta_shift, double mu_shift) {
    const int64_t su = static_cast<int64_t>(grid->ny) * grid->nz * grid->nx_u;
    const int64_t sv = static_cast<int64_t>(grid->ny_v) * grid->nz * grid->nx;
    const int64_t sw = static_cast<int64_t>(grid->ny) * grid->nz_w * grid->nx;
    const int64_t st = static_cast<int64_t>(grid->ny) * grid->nz * grid->nx;
    const int64_t sm = static_cast<int64_t>(grid->ny) * grid->nx;
    auto u = torch::zeros({su}, torch::kFloat64);
    auto v = torch::zeros({sv}, torch::kFloat64);
    auto w = torch::zeros({sw}, torch::kFloat64);
    auto phi = torch::full({sw}, phi_shift, torch::kFloat64);
    auto theta = torch::full({st}, theta_shift, torch::kFloat64);
    auto mu = torch::full({sm}, mu_shift, torch::kFloat64);
    return torch::cat({u, v, w, phi, theta, mu});
}

} // namespace

int main() {
    try {
        auto& cfg = wrf::sdirk3::g_sdirk3_config;
        cfg.mass_coordinate_mode = static_cast<int>(
            wrf::sdirk3::SDIRK3Config::MassCoordinateMode::WRFParity);
        cfg.buoyancy_use_current_w = true;
        cfg.imex_enabled = false;
        cfg.imex_split_mode = 3;
        cfg.split_explicit = false;
        cfg.hevi_split = false;
        cfg.omega_w_blend = 1.0f;
        cfg.omega_update_ref_per_newton = false;

        auto grid = std::make_shared<WRFGridInfo>();
        grid->nx = 1; grid->ny = 1; grid->nz = 3;
        grid->nx_u = 1; grid->ny_v = 1; grid->nz_w = 4;
        grid->p_base = torch::full({1, 3, 1}, 100000.0f);
        grid->alb = torch::full({1, 3, 1}, 1.0f);
        grid->mu_base = torch::ones({1, 1});
        grid->c1h = torch::ones({3});
        grid->c2h = torch::zeros({3});
        grid->c1f = torch::ones({4});
        grid->c2f = torch::zeros({4});
        grid->rdnw = torch::ones({4});
        grid->rdn = torch::ones({4});
        grid->msfty = torch::ones({1, 1});
        grid->qv = torch::zeros({1, 3, 1});
        grid->qc = torch::zeros({1, 3, 1});
        grid->qr = torch::zeros({1, 3, 1});
        grid->qi = torch::zeros({1, 3, 1});
        grid->qs = torch::zeros({1, 3, 1});
        grid->qg = torch::zeros({1, 3, 1});
        grid->cqw = torch::zeros({1, 4, 1});

        auto physics = std::make_shared<PhysicsConfig>();
        UnifiedPreconditioner precond(grid, physics, 1.0f, 0.5f);
        require(precond.raw_principal_enabled(), "private selector is disabled");

        auto state0 = packed_state(grid, 0.0, 0.0, 0.0);
        precond.bind_raw_principal_state_or_throw(state0, 1, "state0");
        precond.update_time_coefficients(1.0f, 0.5f);
        const auto generation0 = precond.raw_principal_generation();
        require(generation0 > 0, "coefficient generation was not published");

        auto residual = torch::arange(state0.numel(), torch::TensorOptions().dtype(torch::kFloat64));
        auto solved = precond.apply(residual);
        require(solved.scalar_type() == torch::kFloat64, "apply changed residual dtype");
        require(solved.numel() == residual.numel(), "apply changed residual size");
        auto r = residual.contiguous();
        auto y = solved.contiguous();
        const int64_t su = grid->ny * grid->nz * grid->nx_u;
        const int64_t sv = grid->ny_v * grid->nz * grid->nx;
        const int64_t sw = grid->ny * grid->nz_w * grid->nx;
        const int64_t phi0 = su + sv + sw;
        const int64_t theta0 = phi0 + sw;
        const int64_t mu0 = theta0 + grid->ny * grid->nz * grid->nx;
        require(torch::equal(y.slice(0, 0, su + sv), r.slice(0, 0, su + sv)),
                "U/V identity block changed");
        require(torch::equal(y.slice(0, theta0, mu0 + grid->ny * grid->nx),
                             r.slice(0, theta0, mu0 + grid->ny * grid->nx)),
                "theta identity block changed");
        require(torch::equal(y.slice(0, mu0, y.numel()), r.slice(0, mu0, r.numel())),
                "mu identity block changed");
        require(torch::equal(y.index({su + sv}), r.index({su + sv})),
                "bottom W restriction changed the boundary value");

        // The private forward replacement must expose its own M^{-T}; the
        // legacy transpose omits K/B/G and cannot satisfy this identity.
        auto probe_x = torch::randn_like(residual);
        auto probe_y = torch::randn_like(residual);
        auto forward_probe = precond.apply(probe_x);
        auto transpose_probe = precond.apply_inverse_transpose(probe_y);
        const double lhs = (forward_probe * probe_y).sum().item<double>();
        const double rhs = (probe_x * transpose_probe).sum().item<double>();
        require(std::isfinite(lhs) && std::isfinite(rhs) &&
                    std::abs(lhs - rhs) <= 2.0e-5 *
                        std::max({1.0, std::abs(lhs), std::abs(rhs)}),
                "raw principal forward/transpose dot-product identity failed");
        require((transpose_probe.index({su + sv}) - probe_y.index({su + sv})).abs().item<double>() <
                    1.0e-6,
                "transpose changed the restricted bottom W row");
        require((transpose_probe.index({phi0}) - probe_y.index({phi0})).abs().item<float>() >
                    1.0e-8f,
                "transpose dropped the bottom Phi K^T contribution");

        auto state1 = packed_state(grid, 0.25, 0.1, 0.01);
        precond.bind_raw_principal_state_or_throw(state1, 2, "state1");
        bool stale_rejected = false;
        try { (void)precond.apply(residual); }
        catch (const c10::Error&) { stale_rejected = true; }
        require(stale_rejected, "apply accepted a dirty snapshot");
        precond.update_time_coefficients(1.0f, 0.5f);
        require(precond.raw_principal_generation() > generation0,
                "new snapshot did not publish a new generation");
        auto solved1 = precond.apply(residual);
        require(torch::isfinite(solved1).all().item<bool>(), "new generation returned NaN/Inf");
        require(!torch::equal(solved, solved1), "state refresh did not change principal apply");

        // The public snapshot type only carries the legacy mu-only state.  It
        // must fail before mutating a raw packed-state generation or action.
        const auto raw_generation_before_snapshot = precond.raw_principal_generation();
        bool snapshot_rejected = false;
        try { (void)precond.snapshot_stage_state(); }
        catch (const c10::Error&) { snapshot_rejected = true; }
        require(snapshot_rejected, "raw principal accepted a legacy stage snapshot");
        UnifiedPreconditioner::StageStateSnapshot legacy_snapshot;
        bool restore_rejected = false;
        try { precond.restore_stage_state(legacy_snapshot); }
        catch (const c10::Error&) { restore_rejected = true; }
        require(restore_rejected, "raw principal accepted a legacy stage restore");
        require(precond.raw_principal_generation() == raw_generation_before_snapshot,
                "rejected raw snapshot/restore changed coefficient generation");
        require(torch::equal(precond.apply(residual), solved1),
                "rejected raw snapshot/restore changed the principal action");

        cfg.mass_coordinate_mode = static_cast<int>(
            wrf::sdirk3::SDIRK3Config::MassCoordinateMode::Legacy);
        precond.bind_raw_principal_state_or_throw(state1, 3, "legacy-mode");
        bool mode_rejected = false;
        try { precond.update_time_coefficients(1.0f, 0.5f); }
        catch (const c10::Error&) { mode_rejected = true; }
        require(mode_rejected, "noncanonical mass-coordinate mode was accepted");
        cfg.mass_coordinate_mode = static_cast<int>(
            wrf::sdirk3::SDIRK3Config::MassCoordinateMode::WRFParity);
        grid->qv.fill_(1.0e-13f);
        precond.bind_raw_principal_state_or_throw(state1, 3, "tiny-moist-state");
        bool tiny_moisture_rejected = false;
        try { precond.update_time_coefficients(1.0f, 0.5f); }
        catch (const c10::Error&) { tiny_moisture_rejected = true; }
        require(tiny_moisture_rejected,
                "representable nonzero 1e-13 moisture was accepted by dry candidate");
        grid->qv.fill_(1.0e-4f);
        precond.bind_raw_principal_state_or_throw(state1, 3, "moist-state");
        bool moisture_rejected = false;
        try { precond.update_time_coefficients(1.0f, 0.5f); }
        catch (const c10::Error&) { moisture_rejected = true; }
        require(moisture_rejected, "nonzero moisture was accepted by dry candidate");

        // Packed Newton stage storage is intentionally limited to the two
        // floating types exercised by the production residual path.  Do not
        // silently convert an unsupported dtype into the raw model.
        grid->qv.zero_();
        auto integer_state = state1.to(torch::kInt32);
        bool dtype_rejected = false;
        try { precond.bind_raw_principal_state_or_throw(integer_state, 3, "integer-state"); }
        catch (const c10::Error&) { dtype_rejected = true; }
        require(dtype_rejected, "unsupported packed-state dtype was silently converted");

        // A noncanonical profile is retained on the existing UnifiedPreconditioner
        // path at construction; it must never be silently treated as the raw model.
        grid->qv.zero_();
        cfg.mass_coordinate_mode = static_cast<int>(
            wrf::sdirk3::SDIRK3Config::MassCoordinateMode::Legacy);
        UnifiedPreconditioner legacy_profile(grid, physics, 1.0f, 0.5f);
        require(!legacy_profile.raw_principal_enabled(),
                "legacy mass-coordinate profile selected raw principal model");

        // Existing legacy Phi-W feedback is an explicit selector guard.  The
        // raw path must not accept the option and silently drop it.
        cfg.mass_coordinate_mode = static_cast<int>(
            wrf::sdirk3::SDIRK3Config::MassCoordinateMode::WRFParity);
        cfg.precond_coupled_phi_w = true;
        UnifiedPreconditioner legacy_option(grid, physics, 1.0f, 0.5f);
        require(!legacy_option.raw_principal_enabled(),
                "legacy Phi-W option was silently ignored by raw selector");
        cfg.precond_coupled_phi_w = false;

        std::cout << "raw-principal contract: PASS generation0=" << generation0
                  << " generation1=" << precond.raw_principal_generation() << "\n";
        return EXIT_SUCCESS;
    } catch (const c10::Error& e) {
        std::cerr << "raw-principal contract: FAIL c10: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (const std::exception& e) {
        std::cerr << "raw-principal contract: FAIL: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
}
