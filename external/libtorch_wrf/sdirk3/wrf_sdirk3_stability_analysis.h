#ifndef WRF_SDIRK3_STABILITY_ANALYSIS_H
#define WRF_SDIRK3_STABILITY_ANALYSIS_H

#include <complex>
#include <vector>
#include <cmath>

namespace wrf {
namespace sdirk3 {

/**
 * WRF SDIRK-3 Mathematical Stability Analysis
 * 
 * Filename: wrf_sdirk3_stability_analysis.h
 * Purpose: L-stability verification and numerical accuracy assurance
 */

/**
 * Complex eigenvalue analysis
 */
class StabilityAnalysis {
public:
    /**
     * Compute stability function R(z)
     * R(z) = P(z)/Q(z) where Q(z) = (1-γz)³
     */
    static std::complex<float> stability_function(std::complex<float> z);
    
    /**
     * Verify L-stability: |R(∞)| = 0
     */
    static bool verify_L_stability(float tolerance = 1e-12f);
    
    /**
     * Verify A-stability: |R(z)| ≤ 1 for Re(z) ≤ 0
     */
    static bool verify_A_stability(int n_samples = 1000);
    
    /**
     * Verify order conditions
     */
    struct OrderConditions {
        bool first_order;   // R(z) = 1 + z + O(z²)
        bool second_order;  // R(z) = 1 + z + z²/2 + O(z³)
        bool third_order;   // R(z) = 1 + z + z²/2 + z³/6 + O(z⁴)
        float error_norm;
    };
    
    static OrderConditions verify_order_conditions(float h = 1e-6f);
    
    /**
     * Compute stability region boundary
     */
    static std::vector<std::complex<float>> compute_stability_boundary(
        int n_points = 360
    );
    
    /**
     * Amplification factor contour plot data
     */
    struct AmplificationData {
        std::vector<float> x_grid;
        std::vector<float> y_grid;
        std::vector<std::vector<float>> amplification;
    };
    
    static AmplificationData compute_amplification_contours(
        float x_min = -10.0f, float x_max = 2.0f,
        float y_min = -10.0f, float y_max = 10.0f,
        int nx = 200, int ny = 200
    );
};

/**
 * Mathematical verification of Butcher tableau
 */
class ButcherTableauVerification {
public:
    /**
     * Row sum condition: Σⱼ aᵢⱼ = cᵢ
     */
    static bool verify_row_sum_condition(float tolerance = 1e-14f);
    
    /**
     * Stiff accuracy: bᵢ = a₃ᵢ
     */
    static bool verify_stiff_accuracy(float tolerance = 1e-14f);
    
    /**
     * DIRK structure: aᵢⱼ = 0 for j > i
     */
    static bool verify_dirk_structure();
    
    /**
     * Diagonal consistency: aᵢᵢ = γ for all i
     */
    static bool verify_diagonal_consistency();
    
    /**
     * Verify stage order
     */
    static int compute_stage_order();
};

/**
 * Eigenvalue analysis (linear stability)
 */
class EigenvalueAnalysis {
public:
    /**
     * Compute eigenvalues of linearized system
     * ∂F/∂U at equilibrium
     */
    struct EigenSystem {
        std::vector<std::complex<float>> eigenvalues;
        float max_real_part;
        float spectral_radius;
        float condition_number;
    };
    
    static EigenSystem analyze_acoustic_system(
        float c_sound, float dx, float dy, float dz
    );
    
    static EigenSystem analyze_gravity_wave_system(
        float N_freq, float dx, float dz
    );
    
    /**
     * Stiffness detection
     */
    static float compute_stiffness_ratio(const EigenSystem& system);
    
    /**
     * Recommend optimal γ value
     */
    static float recommend_gamma_value(const EigenSystem& system);
};

/**
 * Energy stability analysis
 */
class EnergyStabilityAnalysis {
public:
    /**
     * Energy conservation/dissipation properties
     */
    struct EnergyBehavior {
        bool is_energy_conserving;
        bool is_energy_dissipating;
        float dissipation_rate;
        float phase_error;
    };
    
    static EnergyBehavior analyze_energy_behavior(
        float dt, float dx, float wave_number
    );
    
    /**
     * Numerical dispersion relation
     */
    static std::complex<float> numerical_dispersion_relation(
        float k, float dt, float c
    );
    
    /**
     * Group velocity error
     */
    static float compute_group_velocity_error(
        float k, float dt, float c
    );
};

/**
 * Convergence analysis
 */
class ConvergenceAnalysis {
public:
    /**
     * Estimate convergence order via Richardson extrapolation
     */
    static float estimate_convergence_order(
        const std::vector<float>& errors,
        const std::vector<float>& dt_values
    );
    
    /**
     * Determine convergence region
     */
    struct ConvergenceRegion {
        float dt_max;      // Maximum stable timestep
        float dt_optimal;  // Optimal timestep (accuracy vs efficiency)
        float efficiency;  // Computational efficiency metric
    };
    
    static ConvergenceRegion determine_convergence_region(
        float dx, float c_max, float target_error = 1e-6f
    );
};

/**
 * Implementation accuracy verification
 */
namespace implementation_tests {
    
    /**
     * Accuracy test using manufactured solution
     */
    bool test_manufactured_solution(
        float L = 1.0f,      // Domain size
        int n = 64,          // Grid points
        float T = 1.0f,      // Final time
        float tol = 1e-6f    // Error tolerance
    );
    
    /**
     * Linear wave propagation test
     */
    bool test_linear_wave_propagation(
        float wavelength,
        float c_sound,
        float courant_number,
        int n_periods = 10
    );
    
    /**
     * Nonlinear stability test
     */
    bool test_nonlinear_stability(
        float amplitude,
        float dt,
        int n_steps
    );
    
    /**
     * Conservation properties verification
     */
    struct ConservationErrors {
        float mass_error;
        float momentum_error;
        float energy_error;
        float vorticity_error;
    };
    
    ConservationErrors test_conservation_properties(
        int nx, int ny, int nz,
        float dt, int n_steps
    );
}

/**
 * Numerical precision enhancement techniques
 */
class NumericalPrecision {
public:
    /**
     * Kahan summation for improved accuracy
     */
    template<typename T>
    static T kahan_sum(const std::vector<T>& values);
    
    /**
     * Compensated summation
     */
    template<typename T>
    static T compensated_sum(const std::vector<T>& values);
    
    /**
     * Roundoff error estimation
     */
    static float estimate_roundoff_error(
        int n_operations,
        float typical_value
    );
    
    /**
     * Precision requirements based on condition number
     */
    static int required_precision_digits(float condition_number);
};

} // namespace sdirk3
} // namespace wrf

#endif // WRF_SDIRK3_STABILITY_ANALYSIS_H