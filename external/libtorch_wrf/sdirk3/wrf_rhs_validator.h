#ifndef WRF_RHS_VALIDATOR_H
#define WRF_RHS_VALIDATOR_H

#include <torch/torch.h>
#include <vector>
#include <string>
#include <iostream>

namespace wrf {
namespace sdirk3 {

/**
 * @brief Structure to hold validation results
 */
struct ValidationResult {
    std::string component;      // e.g., "u_advection_x", "v_pgf", etc.
    float max_diff;            // Maximum absolute difference
    float rms_diff;            // Root mean square difference
    float relative_error;      // Maximum relative error
    bool passed;               // Whether validation passed
    std::string message;       // Additional information
    
    // Location of maximum difference
    int max_diff_i, max_diff_j, max_diff_k;
    float wrf_value_at_max;
    float sdirk3_value_at_max;
};

/**
 * @brief WRF-SDIRK3 RHS Validator for systematic conformity checking
 * 
 * This class provides methods to compare each RHS component between
 * WRF's native implementation and SDIRK3's implementation
 */
class RHSValidator {
public:
    RHSValidator(float abs_tolerance = 1e-6f, 
                 float rel_tolerance = 1e-4f,
                 bool verbose = true) 
        : abs_tol_(abs_tolerance), 
          rel_tol_(rel_tolerance),
          verbose_(verbose) {}
    
    // ========================================================================
    // Individual component validators
    // ========================================================================
    
    /**
     * Validate advection terms
     */
    ValidationResult validate_u_advection(
        const torch::Tensor& wrf_u_adv_x,
        const torch::Tensor& wrf_u_adv_y,
        const torch::Tensor& wrf_u_adv_z,
        const torch::Tensor& sdirk3_u_adv_x,
        const torch::Tensor& sdirk3_u_adv_y,
        const torch::Tensor& sdirk3_u_adv_z);
    
    ValidationResult validate_v_advection(
        const torch::Tensor& wrf_v_adv_x,
        const torch::Tensor& wrf_v_adv_y,
        const torch::Tensor& wrf_v_adv_z,
        const torch::Tensor& sdirk3_v_adv_x,
        const torch::Tensor& sdirk3_v_adv_y,
        const torch::Tensor& sdirk3_v_adv_z);
    
    ValidationResult validate_w_advection(
        const torch::Tensor& wrf_w_adv,
        const torch::Tensor& sdirk3_w_adv);
    
    ValidationResult validate_theta_advection(
        const torch::Tensor& wrf_theta_adv,
        const torch::Tensor& sdirk3_theta_adv);
    
    /**
     * Validate pressure gradient force
     */
    ValidationResult validate_pgf_u(
        const torch::Tensor& wrf_pgf_u,
        const torch::Tensor& sdirk3_pgf_u);
    
    ValidationResult validate_pgf_v(
        const torch::Tensor& wrf_pgf_v,
        const torch::Tensor& sdirk3_pgf_v);
    
    ValidationResult validate_pgf_w(
        const torch::Tensor& wrf_pgf_w,
        const torch::Tensor& sdirk3_pgf_w);
    
    /**
     * Validate geopotential equation
     */
    ValidationResult validate_geopotential(
        const torch::Tensor& wrf_ph_tend,
        const torch::Tensor& sdirk3_ph_tend);
    
    /**
     * Validate buoyancy terms
     */
    ValidationResult validate_buoyancy_w(
        const torch::Tensor& wrf_buoy,
        const torch::Tensor& sdirk3_buoy);
    
    /**
     * Validate Coriolis terms
     */
    ValidationResult validate_coriolis(
        const torch::Tensor& wrf_cor_u,
        const torch::Tensor& wrf_cor_v,
        const torch::Tensor& wrf_cor_w,
        const torch::Tensor& sdirk3_cor_u,
        const torch::Tensor& sdirk3_cor_v,
        const torch::Tensor& sdirk3_cor_w);
    
    /**
     * Validate diffusion terms
     */
    ValidationResult validate_diffusion(
        const torch::Tensor& wrf_diff,
        const torch::Tensor& sdirk3_diff,
        const std::string& variable_name);
    
    // ========================================================================
    // Full RHS validation
    // ========================================================================
    
    /**
     * Validate complete RHS for all variables
     */
    std::vector<ValidationResult> validate_full_rhs(
        // WRF RHS components
        const torch::Tensor& wrf_ru_tend,
        const torch::Tensor& wrf_rv_tend,
        const torch::Tensor& wrf_rw_tend,
        const torch::Tensor& wrf_rth_tend,
        const torch::Tensor& wrf_ph_tend,
        const torch::Tensor& wrf_mu_tend,
        // SDIRK3 RHS components
        const torch::Tensor& sdirk3_ru_tend,
        const torch::Tensor& sdirk3_rv_tend,
        const torch::Tensor& sdirk3_rw_tend,
        const torch::Tensor& sdirk3_rth_tend,
        const torch::Tensor& sdirk3_ph_tend,
        const torch::Tensor& sdirk3_mu_tend);
    
    // ========================================================================
    // Diagnostic tools
    // ========================================================================
    
    /**
     * Print validation summary
     */
    void print_summary(const std::vector<ValidationResult>& results);
    
    /**
     * Write detailed comparison to file
     */
    void write_detailed_comparison(
        const std::vector<ValidationResult>& results,
        const std::string& filename);
    
    /**
     * Generate difference plots (requires Python backend)
     */
    void plot_differences(
        const torch::Tensor& wrf_field,
        const torch::Tensor& sdirk3_field,
        const std::string& field_name,
        const std::string& output_dir);
    
    // ========================================================================
    // Staggered grid validation
    // ========================================================================
    
    /**
     * Validate staggered grid interpolations
     */
    ValidationResult validate_interpolation_mass_to_u(
        const torch::Tensor& mass_field,
        const torch::Tensor& expected_u_field,
        const torch::Tensor& computed_u_field);
    
    ValidationResult validate_interpolation_mass_to_v(
        const torch::Tensor& mass_field,
        const torch::Tensor& expected_v_field,
        const torch::Tensor& computed_v_field);
    
    ValidationResult validate_interpolation_mass_to_w(
        const torch::Tensor& mass_field,
        const torch::Tensor& expected_w_field,
        const torch::Tensor& computed_w_field);
    
    // ========================================================================
    // Conservation checks
    // ========================================================================
    
    /**
     * Check mass conservation
     */
    ValidationResult check_mass_conservation(
        const torch::Tensor& mu_tend,
        const torch::Tensor& u, 
        const torch::Tensor& v,
        const torch::Tensor& w);
    
    /**
     * Check momentum conservation
     */
    ValidationResult check_momentum_conservation(
        const torch::Tensor& ru_tend,
        const torch::Tensor& rv_tend,
        const torch::Tensor& rw_tend,
        const torch::Tensor& advection_terms,
        const torch::Tensor& pgf_terms,
        const torch::Tensor& external_forces);
    
    /**
     * Check energy conservation
     */
    ValidationResult check_energy_conservation(
        const torch::Tensor& theta_tend,
        const torch::Tensor& ke_tend,
        const torch::Tensor& pe_tend);
    
private:
    float abs_tol_;
    float rel_tol_;
    bool verbose_;
    
    /**
     * Helper function to compute validation metrics
     */
    ValidationResult compute_metrics(
        const torch::Tensor& wrf_field,
        const torch::Tensor& sdirk3_field,
        const std::string& component_name);
    
    /**
     * Find location of maximum difference
     */
    void find_max_diff_location(
        const torch::Tensor& diff,
        int& i, int& j, int& k);
    
    /**
     * Compute relative error avoiding division by zero
     */
    torch::Tensor compute_relative_error(
        const torch::Tensor& wrf_field,
        const torch::Tensor& sdirk3_field);
};

// ============================================================================
// Utility functions for debugging
// ============================================================================

/**
 * Print tensor statistics
 */
void print_tensor_stats(const torch::Tensor& tensor, 
                       const std::string& name);

/**
 * Save tensor to file for offline analysis
 */
void save_tensor_to_file(const torch::Tensor& tensor,
                        const std::string& filename);

/**
 * Load WRF reference data from file
 */
torch::Tensor load_wrf_reference(const std::string& filename);

/**
 * Create test case with known analytical solution
 */
struct AnalyticalTestCase {
    torch::Tensor u, v, w, theta, p, mu;
    torch::Tensor expected_u_tend, expected_v_tend;
    torch::Tensor expected_w_tend, expected_theta_tend;
    
    static AnalyticalTestCase create_constant_flow();
    static AnalyticalTestCase create_linear_flow();
    static AnalyticalTestCase create_rotating_flow();
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_RHS_VALIDATOR_H