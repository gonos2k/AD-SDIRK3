#ifndef WRF_PHYSICS_CONSTRAINTS_H
#define WRF_PHYSICS_CONSTRAINTS_H

#include <torch/torch.h>
#include <string>
#include <vector>
#include <iostream>

namespace wrf {
namespace sdirk3 {

/**
 * Physical Constraint Validator for WRF
 * 
 * Ensures physical realizability of atmospheric variables:
 * - Positive density and pressure
 * - Bounded potential temperature
 * - Non-negative moisture variables
 * - Energy conservation
 * - Monotonicity preservation
 */
class PhysicsConstraintValidator {
private:
    // Physical bounds
    struct PhysicalBounds {
        float rho_min = 0.1f;        // kg/m³
        float rho_max = 2.0f;        // kg/m³
        float theta_min = 200.0f;    // K
        float theta_max = 500.0f;    // K
        float p_min = 100.0f;        // Pa
        float p_max = 120000.0f;     // Pa
        float qv_min = 0.0f;         // kg/kg
        float qv_max = 0.05f;        // kg/kg
        float w_max = 100.0f;        // m/s
        float u_max = 200.0f;        // m/s
    } bounds_;
    
    // Conservation tracking
    struct ConservationQuantities {
        float total_mass = 0.0f;
        torch::Tensor total_momentum = torch::zeros(3);
        float total_energy = 0.0f;
        float total_potential_vorticity = 0.0f;
    } initial_quantities_;
    
    bool conservation_initialized_ = false;
    float conservation_tolerance_ = 1e-6f;
    
public:
    struct ValidationResult {
        bool is_valid = true;
        std::vector<std::string> violations;
        std::vector<std::string> warnings;
        
        void add_violation(const std::string& msg) {
            violations.push_back(msg);
            is_valid = false;
        }
        
        void add_warning(const std::string& msg) {
            warnings.push_back(msg);
        }
    };
    
    /**
     * Validate state vector for physical constraints
     *
     * FIX 2025-12-27: Minimize GPU sync by transferring to CPU once at start.
     * Previously each .item<>() call caused a separate GPU sync.
     */
    ValidationResult validate_state(const torch::Tensor& U,
                                   const std::string& stage_name = "") {
        ValidationResult result;

        // Check tensor validity
        if (!U.defined() || U.numel() == 0) {
            result.add_violation("State tensor is undefined or empty");
            return result;
        }

        // FIX 2025-12-27: Single CPU transfer to avoid repeated GPU sync
        // All subsequent .item<>() calls work on CPU tensor
        torch::NoGradGuard no_grad;  // Diagnostics don't need AD graph
        auto U_cpu = U.to(torch::kCPU);

        // Check for NaN or Inf (on CPU tensor)
        if (torch::any(torch::isnan(U_cpu)).item<bool>()) {
            result.add_violation("State contains NaN values");
        }
        if (torch::any(torch::isinf(U_cpu)).item<bool>()) {
            result.add_violation("State contains infinite values");
        }

        // Extract variables (assuming standard ordering) - from CPU tensor
        auto rho = U_cpu.select(1, 0);    // Density
        auto u = U_cpu.select(1, 1);      // U-velocity
        auto v = U_cpu.select(1, 2);      // V-velocity
        auto w = U_cpu.select(1, 3);      // W-velocity
        auto theta = U_cpu.select(1, 4);  // Potential temperature

        // Check density bounds (all on CPU now - no sync)
        auto rho_min_actual = torch::min(rho).item<float>();
        auto rho_max_actual = torch::max(rho).item<float>();
        if (rho_min_actual < bounds_.rho_min) {
            result.add_violation("Density below minimum: " +
                std::to_string(rho_min_actual) + " < " +
                std::to_string(bounds_.rho_min));
        }
        if (rho_max_actual > bounds_.rho_max) {
            result.add_warning("Density above typical maximum: " +
                std::to_string(rho_max_actual));
        }

        // Check potential temperature bounds
        auto theta_min_actual = torch::min(theta).item<float>();
        auto theta_max_actual = torch::max(theta).item<float>();
        if (theta_min_actual < bounds_.theta_min) {
            result.add_violation("Potential temperature below minimum: " +
                std::to_string(theta_min_actual) + " < " +
                std::to_string(bounds_.theta_min));
        }
        if (theta_max_actual > bounds_.theta_max) {
            result.add_warning("Potential temperature above typical maximum: " +
                std::to_string(theta_max_actual));
        }

        // Check velocity bounds
        auto u_max = torch::max(torch::abs(u)).item<float>();
        auto v_max = torch::max(torch::abs(v)).item<float>();
        auto w_max = torch::max(torch::abs(w)).item<float>();

        if (u_max > bounds_.u_max || v_max > bounds_.u_max) {
            result.add_warning("Horizontal velocity exceeds typical bounds: " +
                std::to_string(std::max(u_max, v_max)));
        }
        if (w_max > bounds_.w_max) {
            result.add_warning("Vertical velocity exceeds typical bounds: " +
                std::to_string(w_max));
        }

        return result;
    }
    
    /**
     * Initialize conservation quantities for tracking
     *
     * FIX 2025-12-27: Single CPU transfer to avoid repeated GPU sync.
     */
    void initialize_conservation(const torch::Tensor& U,
                               const torch::Tensor& grid_volumes) {
        torch::NoGradGuard no_grad;  // Diagnostics don't need AD graph

        // Single CPU transfer for all computations
        auto U_cpu = U.to(torch::kCPU);
        auto vol_cpu = grid_volumes.to(torch::kCPU);

        auto rho = U_cpu.select(1, 0);
        auto u = U_cpu.select(1, 1);
        auto v = U_cpu.select(1, 2);
        auto w = U_cpu.select(1, 3);
        auto theta = U_cpu.select(1, 4);

        // Total mass (on CPU - no sync)
        initial_quantities_.total_mass = torch::sum(rho * vol_cpu).item<float>();

        // Total momentum (on CPU - no sync)
        auto mom_x = torch::sum(rho * u * vol_cpu).item<float>();
        auto mom_y = torch::sum(rho * v * vol_cpu).item<float>();
        auto mom_z = torch::sum(rho * w * vol_cpu).item<float>();
        initial_quantities_.total_momentum = torch::tensor({mom_x, mom_y, mom_z});

        // Total energy (kinetic + potential) - on CPU
        auto ke = 0.5f * rho * (u*u + v*v + w*w);
        auto pe = rho * theta;  // Simplified potential energy
        initial_quantities_.total_energy =
            torch::sum((ke + pe) * vol_cpu).item<float>();

        conservation_initialized_ = true;
    }
    
    /**
     * Check conservation properties
     *
     * FIX 2025-12-27: Single CPU transfer to avoid repeated GPU sync.
     */
    ValidationResult check_conservation(const torch::Tensor& U,
                                      const torch::Tensor& grid_volumes) {
        ValidationResult result;

        if (!conservation_initialized_) {
            result.add_warning("Conservation not initialized - skipping checks");
            return result;
        }

        torch::NoGradGuard no_grad;  // Diagnostics don't need AD graph

        // Single CPU transfer for all computations
        auto U_cpu = U.to(torch::kCPU);
        auto vol_cpu = grid_volumes.to(torch::kCPU);

        auto rho = U_cpu.select(1, 0);
        auto u = U_cpu.select(1, 1);
        auto v = U_cpu.select(1, 2);
        auto w = U_cpu.select(1, 3);
        auto theta = U_cpu.select(1, 4);

        // Check mass conservation (on CPU - no sync)
        float total_mass = torch::sum(rho * vol_cpu).item<float>();
        float mass_error = std::abs(total_mass - initial_quantities_.total_mass) /
                          initial_quantities_.total_mass;

        if (mass_error > conservation_tolerance_) {
            result.add_violation("Mass conservation violated: relative error = " +
                std::to_string(mass_error));
        }

        // Check momentum conservation (on CPU - no sync)
        auto mom_x = torch::sum(rho * u * vol_cpu).item<float>();
        auto mom_y = torch::sum(rho * v * vol_cpu).item<float>();
        auto mom_z = torch::sum(rho * w * vol_cpu).item<float>();
        auto momentum = torch::tensor({mom_x, mom_y, mom_z});

        float mom_error = (torch::norm(momentum - initial_quantities_.total_momentum) /
                          torch::norm(initial_quantities_.total_momentum)).item<float>();

        if (mom_error > conservation_tolerance_ * 10) {
            result.add_warning("Momentum conservation warning: relative error = " +
                std::to_string(mom_error));
        }

        // Check energy conservation (on CPU - no sync)
        auto ke = 0.5f * rho * (u*u + v*v + w*w);
        auto pe = rho * theta;
        float total_energy = torch::sum((ke + pe) * vol_cpu).item<float>();
        float energy_error = std::abs(total_energy - initial_quantities_.total_energy) /
                            initial_quantities_.total_energy;

        if (energy_error > conservation_tolerance_ * 10) {
            result.add_warning("Energy conservation warning: relative error = " +
                std::to_string(energy_error));
        }

        return result;
    }
    
    /**
     * Apply limiters to ensure physical bounds
     */
    torch::Tensor apply_limiters(const torch::Tensor& U) {
        auto U_limited = U.clone();

        // FIX 2025-12-27: Use copy_() instead of select()= (no-op in C++ LibTorch)
        // Limit density
        U_limited.select(1, 0).copy_(torch::clamp(U_limited.select(1, 0),
                                                   bounds_.rho_min, bounds_.rho_max));

        // Limit potential temperature
        U_limited.select(1, 4).copy_(torch::clamp(U_limited.select(1, 4),
                                                   bounds_.theta_min, bounds_.theta_max));

        // Limit velocities (optional - usually not needed)
        // U_limited.select(1, 1).copy_(torch::clamp(U_limited.select(1, 1),
        //                                           -bounds_.u_max, bounds_.u_max));

        return U_limited;
    }
    
    /**
     * Check monotonicity for scalar fields
     *
     * FIX 2025-12-27: Single CPU transfer to avoid repeated GPU sync.
     */
    bool check_monotonicity(const torch::Tensor& scalar_new,
                           const torch::Tensor& scalar_old) {
        torch::NoGradGuard no_grad;  // Diagnostics don't need AD graph

        // Single CPU transfer for all computations
        auto old_cpu = scalar_old.to(torch::kCPU).flatten();
        auto new_cpu = scalar_new.to(torch::kCPU).flatten();

        // torch::min and torch::max return tensor, not tuple
        auto min_old = torch::min(old_cpu);
        auto max_old = torch::max(old_cpu);
        auto min_new = torch::min(new_cpu);
        auto max_new = torch::max(new_cpu);

        // Check if new values are within old bounds (with small tolerance)
        float tol = 1e-6f;
        float min_old_val = min_old.item<float>();
        float max_old_val = max_old.item<float>();
        float min_new_val = min_new.item<float>();
        float max_new_val = max_new.item<float>();

        bool monotonic = (min_new_val >= min_old_val - tol) && (max_new_val <= max_old_val + tol);

        return monotonic;
    }
    
    /**
     * Validate tendencies (RHS) for reasonableness
     *
     * FIX 2025-12-27: Single CPU transfer to avoid repeated GPU sync.
     */
    ValidationResult validate_tendencies(const torch::Tensor& F, float dt) {
        ValidationResult result;

        torch::NoGradGuard no_grad;  // Diagnostics don't need AD graph

        // Single CPU transfer for all computations
        auto F_cpu = F.to(torch::kCPU);

        // Check for NaN or Inf (on CPU - no sync)
        if (torch::any(torch::isnan(F_cpu)).item<bool>()) {
            result.add_violation("Tendencies contain NaN values");
        }
        if (torch::any(torch::isinf(F_cpu)).item<bool>()) {
            result.add_violation("Tendencies contain infinite values");
        }

        // Check if tendencies would cause violations in one timestep
        auto F_rho = F_cpu.select(1, 0);
        auto max_rho_tendency = torch::max(torch::abs(F_rho)).item<float>();

        // Rough check: tendency shouldn't change density by more than 50% in one step
        if (max_rho_tendency * dt > 0.5f * bounds_.rho_min) {
            result.add_warning("Large density tendency detected: " +
                std::to_string(max_rho_tendency * dt) + " kg/m³ per timestep");
        }

        return result;
    }
    
    /**
     * Apply positivity preservation for tracers
     *
     * FIX 2025-12-27: Transfer only the check tensor to CPU for .item<bool>().
     * Keep computations on original device since output must match input device.
     */
    torch::Tensor apply_positivity_limiter(const torch::Tensor& tracer,
                                          const torch::Tensor& tracer_flux_div,
                                          float dt) {
        // Ensure tracer remains positive after update
        auto tracer_new = tracer + dt * tracer_flux_div;

        // Find cells that would become negative
        auto negative_mask = tracer_new < 0.0f;

        // Transfer only the scalar check to CPU to avoid GPU sync in loop
        // FIX Round192: Wrap .item() in NoGradGuard to prevent AD graph pollution
        bool has_negative;
        {
            torch::NoGradGuard no_grad;
            has_negative = torch::any(negative_mask).to(torch::kCPU).item<bool>();
        }

        if (has_negative) {
            // Compute limiting factor (on original device)
            auto limiting_factor = torch::where(
                negative_mask,
                -tracer / (dt * tracer_flux_div + 1e-10f),
                torch::ones_like(tracer)
            );

            // Apply limiter and return modified flux
            return tracer_flux_div * torch::clamp(limiting_factor, 0.0f, 1.0f);
        }

        return tracer_flux_div;
    }
    
    /**
     * Print validation report
     */
    void print_report(const ValidationResult& result, const std::string& context = "") {
        if (result.is_valid && result.warnings.empty()) {
            std::cout << "[Physics Validator] " << context << " - All checks passed" << std::endl;
            return;
        }
        
        if (!result.violations.empty()) {
            std::cout << "[Physics Validator] " << context << " - VIOLATIONS:" << std::endl;
            for (const auto& v : result.violations) {
                std::cout << "  ERROR: " << v << std::endl;
            }
        }
        
        if (!result.warnings.empty()) {
            std::cout << "[Physics Validator] " << context << " - Warnings:" << std::endl;
            for (const auto& w : result.warnings) {
                std::cout << "  Warning: " << w << std::endl;
            }
        }
    }
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_PHYSICS_CONSTRAINTS_H