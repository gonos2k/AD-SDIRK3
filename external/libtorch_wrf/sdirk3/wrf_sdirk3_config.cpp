#include <cstdint>  // fixed-width ints used below; libstdc++ (Linux g++) does not provide them transitively
#include "wrf_sdirk3_config.h"
#include "wrf_config_flags.h"  // For set_ad_strict_mode()
#include "wrf_sdirk3_metric_utils.h"  // FIX 2025-12-29: For setMovingNestMode()
#include "wrf_sdirk3_validation_enhanced.h"  // FIX 2025-12-31 Batch41 Issue 4: Validation policy integration
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <algorithm>
#include <cctype>
#include <unordered_set>  // FIX Round6 Item2: Per-key WARN_ONCE tracking
#include <mutex>          // FIX Round6 Item2: Thread-safe warned_keys set

namespace wrf {
namespace sdirk3 {

// Global configuration instance
SDIRK3Config g_sdirk3_config;

// DIAGNOSTIC 2026-02-01: Thread-local mask activation counters
thread_local MaskActivationCounters g_mask_counters;

// FIX 2025-01-25: Config freeze mechanism
// Atomic flag to track if workers (OpenMP threads / solver) have started.
// Once set, config changes trigger a warning to help detect configuration bugs.
static std::atomic<bool> g_workers_started{false};

// v20.14r18: Signed-char-safe tolower for std::transform (avoids UB with negative char).
static char safe_tolower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

// Shared Fortran-style bool parser for both env and namelist paths.
static bool parse_fortran_bool_value(const std::string& input) {
    std::string lv(input);
    while (!lv.empty() && lv.front() == '.') lv.erase(lv.begin());
    while (!lv.empty() && lv.back() == '.') lv.pop_back();
    std::transform(lv.begin(), lv.end(), lv.begin(), safe_tolower);
    return (lv == "1" || lv == "true" || lv == "t" || lv == "yes");
}

// v20.14 r47c-fix2: File-scope bool parser for env vars.
// Same shared parser as namelist path. Supports: "1", "true", ".true.", "T", "yes"
static bool parse_bool_env(const char* v) {
    if (!v) return false;
    return parse_fortran_bool_value(std::string(v));
}

// Internal helper: Reject ALL config changes after workers started.
// FIX Round6 Item2: Per-key WARN_ONCE policy to prevent warning spam.
// v20.14r16: All writes rejected (g_sdirk3_config is unsynchronized plain struct).
static bool warnIfWorkersStarted(const char* config_key) {
    if (!config_key) return true;  // v20.14r17: null-safe — reject
    if (g_workers_started.load(std::memory_order_acquire)) {
        static std::mutex s_warned_keys_mutex;
        static std::unordered_set<std::string> s_warned_keys;

        // v20.14r16: Canonicalize to lowercase for consistent warn-once dedup.
        std::string canon(config_key);
        std::transform(canon.begin(), canon.end(), canon.begin(), safe_tolower);
        std::lock_guard<std::mutex> lock(s_warned_keys_mutex);
        if (s_warned_keys.find(canon) == s_warned_keys.end()) {
            s_warned_keys.insert(canon);
            std::cerr << "[CONFIG REJECT] '" << canon
                      << "' — all config writes rejected after workers started." << std::endl;
        }
        return true;  // always reject after workers started
    }
    return false;
}

// FIX 2025-12-31 Batch41 Issue 4: Sync debug_level to EnhancedTensorValidator settings
// Policy:
//   debug_level=0 (production): CRITICAL only, sampling enabled for GPU/large tensors
//   debug_level=1: STANDARD validation, sampling enabled (10M threshold)
//   debug_level≥2: PARANOID validation, full stats (no sampling) for diagnostic accuracy
//
// NOTE: Validation settings are ORTHOGONAL to moving_nest mode. moving_nest controls cache
// checking aggressiveness (via grid_metric_memcmp_period), while validation is controlled
// solely by debug_level. Users can independently configure both for their use case:
//   - moving_nest debugging: moving_nest=true + debug_level≥2
//   - moving_nest production: moving_nest=true + debug_level=0
// OPT Pass34: Use correct namespace WRF_SDIRK3::Validation
static void syncValidationSettings(int debug_level) {
    using Level = WRF_SDIRK3::Validation::Level;

    if (debug_level == 0) {
        // Production mode: minimal validation, sampling enabled for performance
        WRF_SDIRK3::Validation::EnhancedTensorValidator::set_level(Level::CRITICAL);
        WRF_SDIRK3::Validation::EnhancedTensorValidator::set_sampling_threshold(10000000, true);  // 10M elems, auto-mode
    } else if (debug_level == 1) {
        // Standard mode: normal validation with sampling
        WRF_SDIRK3::Validation::EnhancedTensorValidator::set_level(Level::STANDARD);
        WRF_SDIRK3::Validation::EnhancedTensorValidator::set_sampling_threshold(10000000, true);
    } else {
        // Debug mode (≥2): paranoid validation, full stats for accuracy
        WRF_SDIRK3::Validation::EnhancedTensorValidator::set_level(Level::PARANOID);
        WRF_SDIRK3::Validation::EnhancedTensorValidator::set_sampling_threshold(0, false);  // Full stats, no sampling
    }
}

void SDIRK3Config::load_from_namelist(const std::string& namelist_content) {
    std::istringstream iss(namelist_content);
    std::string line;
    
    while (std::getline(iss, line)) {
        // Remove comments
        size_t comment_pos = line.find('!');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // Trim whitespace
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty()) continue;
        
        // Parse key = value
        size_t eq_pos = line.find('=');
        if (eq_pos != std::string::npos) {
            std::string key = line.substr(0, eq_pos);
            std::string value = line.substr(eq_pos + 1);
            
            // Trim key and value
            key.erase(0, key.find_first_not_of(" \t"));
            key.erase(key.find_last_not_of(" \t") + 1);
            value.erase(0, value.find_first_not_of(" \t"));
            value.erase(value.find_last_not_of(" \t") + 1);
            
            // Convert key to lowercase
            std::transform(key.begin(), key.end(), key.begin(), safe_tolower);
            
            // v20.14r19: Catch stoi/stof exceptions per key so one malformed
            // value doesn't abort the entire namelist parse.
            try {
            // Set values
            if (key == "sdirk3_max_newton_iter") {
                max_newton_iter = std::stoi(value);
            } else if (key == "sdirk3_newton_tol") {
                newton_tol = std::stof(value);
            } else if (key == "sdirk3_newton_rtol") {
                newton_rtol = std::stof(value);
            } else if (key == "sdirk3_gmres_restart") {
                gmres_restart = std::stoi(value);
            } else if (key == "sdirk3_max_krylov_iter") {
                max_krylov_iter = std::stoi(value);
            } else if (key == "sdirk3_krylov_tol") {
                krylov_tol = std::stof(value);
            // v20.14r48: GMRES performance tuning
            } else if (key == "sdirk3_gmres_true_residual_start_j") {
                gmres_true_residual_start_j = std::clamp(std::stoi(value), 0, 1000);
            } else if (key == "sdirk3_gmres_true_residual_period") {
                gmres_true_residual_period = std::clamp(std::stoi(value), 1, 100);
            } else if (key == "sdirk3_gmres_arnoldi_stag_window") {
                gmres_arnoldi_stag_window = std::clamp(std::stoi(value), 1, 20);
            } else if (key == "sdirk3_gmres_arnoldi_stag_ratio") {
                float parsed = std::strtof(value.c_str(), nullptr);
                gmres_arnoldi_stag_ratio = std::clamp(parsed, 0.5f, 1.0f);
            } else if (key == "sdirk3_jvp_mixed_fd_newton_switch") {
                jvp_mixed_fd_newton_switch = std::stoi(value);
            // v20.14 r49/r59: Stage-aware GMRES budget + JVP auto-bench
            } else if (key == "sdirk3_stage2_gmres_restart") {
                stage2_gmres_restart = std::clamp(std::stoi(value), 0, 100);
            } else if (key == "sdirk3_stage2_max_krylov_restarts") {
                stage2_max_krylov_restarts = std::clamp(std::stoi(value), 0, 20);
            } else if (key == "sdirk3_stage2_krylov_tol") {
                stage2_krylov_tol = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_stage2_ew_eta_min") {
                stage2_ew_eta_min = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_stage2_ew_eta_max") {
                stage2_ew_eta_max = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_stage3_gmres_restart") {
                stage3_gmres_restart = std::clamp(std::stoi(value), 0, 100);
            } else if (key == "sdirk3_stage3_max_krylov_restarts") {
                stage3_max_krylov_restarts = std::clamp(std::stoi(value), 0, 20);
            } else if (key == "sdirk3_stage3_krylov_tol") {
                stage3_krylov_tol = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_stage3_ew_eta_min") {
                stage3_ew_eta_min = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_stage3_ew_eta_max") {
                stage3_ew_eta_max = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_jvp_auto_bench_calls") {
                jvp_auto_bench_calls = std::clamp(std::stoi(value), 0, 20);
            } else if (key == "sdirk3_jvp_auto_bench_quality_gate") {
                jvp_auto_bench_quality_gate = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_jvp_auto_bench_warmup") {
                jvp_auto_bench_warmup = std::clamp(std::stoi(value), 0, 50);
            } else if (key == "sdirk3_jvp_auto_bench_seed") {
                jvp_auto_bench_seed = std::stoi(value);
            } else if (key == "sdirk3_jvp_auto_bench_lock_reset_stage1") {
                jvp_auto_bench_lock_reset_stage1 = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_solver_telemetry") {
                solver_telemetry = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_progress_invariant_enable") {
                progress_invariant_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_progress_invariant_min_ratio") {
                progress_invariant_min_ratio = std::max(0.0f, std::strtof(value.c_str(), nullptr));
            } else if (key == "sdirk3_progress_invariant_streak_limit") {
                progress_invariant_streak_limit = std::clamp(std::stoi(value), 0, 1000000);
            } else if (key == "sdirk3_variable_pc_event_log") {
                variable_pc_event_log = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_gmres_warmstart") {
                gmres_warmstart = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_gmres_warmstart_quality_gate") {
                gmres_warmstart_quality_gate = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_inn_warmstart_enable") {
                inn_warmstart_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_inn_residual_gate_enable") {
                inn_residual_gate_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_inn_q_min") {
                inn_q_min = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_inn_gate_rel_tol") {
                inn_gate_rel_tol = std::max(std::strtof(value.c_str(), nullptr), 0.0f);
            } else if (key == "sdirk3_inn_gate_q_noise") {
                inn_gate_q_noise = std::max(std::strtof(value.c_str(), nullptr), 0.0f);
            } else if (key == "sdirk3_inn_beta_max") {
                inn_beta_max = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_inn_ood_guard_enable") {
                inn_ood_guard_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_inn_tol_ramp_enable") {
                inn_tol_ramp_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_inn_tol_ramp_gamma") {
                inn_tol_ramp_gamma = std::clamp(std::strtof(value.c_str(), nullptr), 1.0e-3f, 1.0f);
            } else if (key == "sdirk3_rhs_bc_parity") {
                rhs_bc_parity = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mass_pgf_bc_guard") {
                mass_pgf_bc_guard = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_stage_boundary_sync") {
                stage_boundary_sync = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_type") {
                precond_type = std::stoi(value);
            } else if (key == "sdirk3_adaptive_timestep") {
                adaptive_timestep = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_safety_factor") {
                safety_factor = std::stof(value);
            } else if (key == "sdirk3_error_tol") {
                error_tol = std::stof(value);
            } else if (key == "sdirk3_n_threads") {
                n_threads = std::stoi(value);
            } else if (key == "sdirk3_debug_level") {
                debug_level = std::stoi(value);
                // FIX 2025-12-31 Batch41 Issue 4: Sync validation policy based on debug_level
                syncValidationSettings(debug_level);
            } else if (key == "sdirk3_grid_metric_memcmp_period") {
                // PARITY FIX 2025-12-24: Separate memcmp period from debug_level
                // 0=auto, 1=strict (moving nest), N>1=every N calls
                // PARITY FIX 2025-12-24: Use stoll and clamp negative to 0 to prevent underflow
                int64_t parsed = std::stoll(value);
                grid_metric_memcmp_period = (parsed < 0) ? 0 : static_cast<uint64_t>(parsed);
            } else if (key == "sdirk3_cuda_grid_metric_sample_period") {
                // PARITY FIX 2025-12-24: Separate CUDA sample period for refreshGridMetricEpochs
                int64_t parsed = std::stoll(value);
                cuda_grid_metric_sample_period = (parsed < 0) ? 0 : static_cast<uint64_t>(parsed);
            } else if (key == "sdirk3_cuda_phbase_sample_period") {
                // PARITY FIX 2025-12-24: Separate CUDA sample period for updatePhBaseSignature
                int64_t parsed = std::stoll(value);
                cuda_phbase_sample_period = (parsed < 0) ? 0 : static_cast<uint64_t>(parsed);
            } else if (key == "sdirk3_lat_cpu_sample_check_period") {
                // FIX 2025-12-27: Runtime-tunable sample check period for LatCpuCache
                // 0=auto (100 Release, 10 Debug), N=check every N cache hits
                int64_t parsed = std::stoll(value);
                lat_cpu_sample_check_period = (parsed < 0) ? 0 : static_cast<uint64_t>(parsed);
            } else if (key == "sdirk3_check_nan") {
                check_nan = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_use_autograd") {
                use_autograd = parse_fortran_bool_value(value);
                // Sync jvp_method with use_autograd flag
                jvp_method = use_autograd ? JVP_AUTOGRAD : JVP_FINITE_DIFF;
                std::cerr << "[CONFIG DEBUG] Parsed sdirk3_use_autograd from namelist: " << use_autograd
                          << " (value was: '" << value << "'), jvp_method = " << jvp_method << std::endl;
            } else if (key == "sdirk3_jvp_method") {
                jvp_method = static_cast<JVPMethod>(std::stoi(value));
                // v20.14 r47c-fix3b: Sync use_autograd with jvp_method (matches env var path at :604).
                // Without this, jvp_method=1 + use_autograd=false → FD actually runs.
                use_autograd = (jvp_method >= JVP_AUTOGRAD);
                std::cerr << "[CONFIG DEBUG] Parsed sdirk3_jvp_method from namelist: " << jvp_method
                          << ", use_autograd synced to " << use_autograd << std::endl;

            // JVP Optimization Options
            } else if (key == "sdirk3_jvp_checkpointing") {
                jvp_checkpointing = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_jvp_checkpoint_segments") {
                jvp_checkpoint_segments = std::stoi(value);
            } else if (key == "sdirk3_jvp_graph_caching") {
                jvp_graph_caching = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_jvp_batched") {
                jvp_batched = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_jvp_batch_size") {
                jvp_batch_size = std::stoi(value);
            } else if (key == "sdirk3_jvp_mixed_precision") {
                jvp_mixed_precision = parse_fortran_bool_value(value);
            
            // Implicit/Explicit Treatment Options
            } else if (key == "sdirk3_implicit_acoustic") {
                implicit_acoustic = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_implicit_gravity") {
                implicit_gravity = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_implicit_rayleigh") {
                implicit_rayleigh = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_implicit_wdamp") {
                implicit_wdamp = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_implicit_vdiff") {
                implicit_vdiff = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_implicit_hdiff") {
                implicit_hdiff = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_implicit_divergence") {
                implicit_divergence = parse_fortran_bool_value(value);
            
            // Preconditioner Options
            } else if (key == "sdirk3_precond_diagonal_only") {
                precond_diagonal_only = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_block_jacobi") {
                precond_block_jacobi = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_block_size") {
                precond_block_size = std::stoi(value);
            } else if (key == "sdirk3_precond_ilu") {
                precond_ilu = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_ilu_level") {
                precond_ilu_level = std::clamp(std::stoi(value), 0, 8);
            } else if (key == "sdirk3_precond_multigrid") {
                precond_multigrid = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_mg_levels") {
                precond_mg_levels = std::clamp(std::stoi(value), 1, 10);
            
            // Newton-Krylov Options
            } else if (key == "sdirk3_nk_adaptive_tol") {
                nk_adaptive_tol = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_nk_line_search") {
                nk_line_search = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_nk_trust_region") {
                nk_trust_region = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_nk_trust_radius") {
                nk_trust_radius = std::max(0.01f, std::stof(value));
            } else if (key == "sdirk3_nk_line_search_alpha") {
                nk_line_search_alpha = std::clamp(std::stof(value), 1e-6f, 0.5f);
            } else if (key == "sdirk3_nk_forcing_eta_max") {
                nk_forcing_eta_max = std::clamp(std::stof(value), 0.01f, 1.0f);
            } else if (key == "sdirk3_nk_gmres_max_nan_retries") {
                nk_gmres_max_nan_retries = std::stoi(value);

            // Preconditioner Gauss-Seidel Options
            } else if (key == "sdirk3_precond_gs_min_iterations") {
                precond_gs_min_iterations = std::stoi(value);
            } else if (key == "sdirk3_precond_gs_max_iterations") {
                precond_gs_max_iterations = std::stoi(value);
            } else if (key == "sdirk3_precond_gs_ratio_threshold") {
                precond_gs_ratio_threshold = std::stof(value);
            } else if (key == "sdirk3_precond_gs_fast_threshold") {
                precond_gs_fast_threshold = std::stof(value);
            } else if (key == "sdirk3_precond_mu_coupling_damping") {
                precond_mu_coupling_damping = std::stof(value);
            // v20.14r27j: Preconditioner relaxation via namelist
            } else if (key == "sdirk3_precond_relaxation") {
                precond_relaxation = std::clamp(std::stof(value), 0.0f, 1.0f);

            // v20.9: Preconditioner acoustic 4x4 and boost via namelist
            } else if (key == "sdirk3_precond_acoustic_4x4" || key == "precond_acoustic_4x4") {
                precond_acoustic_4x4 = std::stoi(value);
            } else if (key == "sdirk3_precond_w_acoustic_boost" || key == "precond_w_acoustic_boost") {
                precond_w_acoustic_boost = std::clamp(std::stof(value), 0.0f, 10.0f);
            } else if (key == "sdirk3_precond_theta_acoustic_factor" || key == "precond_theta_acoustic_factor") {
                ++theta_config_generation;  // explicit write → always bump
                precond_theta_acoustic_factor = std::clamp(std::stof(value), 0.0f, 0.35f);
            // v20.14r27s: precond_gs_beta and jvp_block_epsilon namelist wiring
            } else if (key == "sdirk3_precond_gs_beta" || key == "precond_gs_beta") {
                precond_gs_beta = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_jvp_block_epsilon" || key == "jvp_block_epsilon") {
                jvp_block_epsilon = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_refinement_passes") {
                precond_refinement_passes = std::clamp(std::stoi(value), 1, 5);
            } else if (key == "sdirk3_precond_coupled_phi_w" || key == "precond_coupled_phi_w") {
                precond_coupled_phi_w = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_stagnation_gate_enable" || key == "stagnation_gate_enable") {
                stagnation_gate_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_stagnation_growth_floor" || key == "stagnation_growth_floor") {
                stagnation_growth_floor = std::clamp(std::stof(value), 0.0f, 10.0f);
            } else if (key == "sdirk3_stage_require_convergence" || key == "stage_require_convergence") {
                stage_require_convergence = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_hevi_split" || key == "hevi_split") {
                hevi_split = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_split_explicit" || key == "split_explicit") {
                split_explicit = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_split_explicit_time_step_sound" || key == "split_explicit_time_step_sound") {
                split_explicit_time_step_sound = std::clamp(std::atoi(value.c_str()), 0, 1000);
            } else if (key == "sdirk3_split_explicit_epssm" || key == "split_explicit_epssm") {
                split_explicit_epssm = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_split_explicit_smdiv" || key == "split_explicit_smdiv") {
                split_explicit_smdiv = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_split_explicit_emdiv" || key == "split_explicit_emdiv") {
                split_explicit_emdiv = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_split_explicit_top_lid" || key == "split_explicit_top_lid") {
                split_explicit_top_lid = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_phi_w_coupling_scale" || key == "precond_phi_w_coupling_scale") {
                int parsed = std::atoi(value.c_str());
                if (parsed == 0 && value != "0") {
                    std::cerr << "[CONFIG WARN] precond_phi_w_coupling_scale: '"
                              << value << "' parsed as 0 (expected 0/1/2)" << std::endl;
                }
                precond_phi_w_coupling_scale = std::max(0, std::min(2, parsed));
            } else if (key == "sdirk3_precond_phi_feedback_relax" || key == "precond_phi_feedback_relax") {
                precond_phi_feedback_relax = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_precond_gs_awphi_cap_coupled" || key == "precond_gs_awphi_cap_coupled") {
                precond_gs_awphi_cap_coupled = std::stof(value);  // -1=use base, >=0=override
            } else if (key == "sdirk3_precond_phi_feedback_beta" || key == "precond_phi_feedback_beta") {
                precond_phi_feedback_beta = std::clamp(std::stof(value), 0.01f, 10.0f);
            } else if (key == "sdirk3_precond_phi_feedback_cap" || key == "precond_phi_feedback_cap") {
                precond_phi_feedback_cap = std::stof(value);  // -1=disabled, >=0=active
            } else if (key == "sdirk3_precond_dw_nosboost_floor" || key == "precond_dw_nosboost_floor") {
                precond_dw_nosboost_floor = std::clamp(std::stof(value), 0.001f, 10.0f);
            } else if (key == "sdirk3_precond_phi_feedback_max_corr" || key == "precond_phi_feedback_max_corr") {
                precond_phi_feedback_max_corr = std::stof(value);  // -1=disabled, >=0=active
            } else if (key == "sdirk3_precond_phi_feedback_fallback_gs" || key == "precond_phi_feedback_fallback_gs") {
                precond_phi_feedback_fallback_gs = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_phi_feedback_cap_high" || key == "precond_phi_feedback_cap_high") {
                precond_phi_feedback_cap_high = std::stof(value);  // -1=disabled, >=0=active
            } else if (key == "sdirk3_precond_gs_awphi_cap_coupled_high" || key == "precond_gs_awphi_cap_coupled_high") {
                precond_gs_awphi_cap_coupled_high = std::stof(value);
            } else if (key == "sdirk3_precond_phi_feedback_soft_cap" || key == "precond_phi_feedback_soft_cap") {
                precond_phi_feedback_soft_cap = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_phi_feedback_dw_blend" || key == "precond_phi_feedback_dw_blend") {
                precond_phi_feedback_dw_blend = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_precond_phi_feedback_passes" || key == "precond_phi_feedback_passes") {
                precond_phi_feedback_passes = std::clamp(std::atoi(value.c_str()), 1, 5);
            } else if (key == "sdirk3_precond_phi_feedback_cap_mode" || key == "precond_phi_feedback_cap_mode") {
                precond_phi_feedback_cap_mode = std::clamp(std::atoi(value.c_str()), 0, 2);
            } else if (key == "sdirk3_precond_phi_feedback_stage_scale" || key == "precond_phi_feedback_stage_scale") {
                precond_phi_feedback_stage_scale = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_precond_phi_w_schur_boost_cap" || key == "precond_phi_w_schur_boost_cap") {
                char* end = nullptr;
                float parsed = std::strtof(value.c_str(), &end);
                if (end == value.c_str()) {
                    std::cerr << "[CONFIG WARN] Invalid float for " << key << ": '" << value << "', using default" << std::endl;
                } else {
                    precond_phi_w_schur_boost_cap = std::clamp(parsed, 0.0f, 10.0f);
                }
            } else if (key == "sdirk3_precond_phi_w_schur_backsub_relax" || key == "precond_phi_w_schur_backsub_relax") {
                char* end = nullptr;
                float parsed = std::strtof(value.c_str(), &end);
                if (end == value.c_str()) {
                    std::cerr << "[CONFIG WARN] Invalid float for " << key << ": '" << value << "', using default" << std::endl;
                } else {
                    precond_phi_w_schur_backsub_relax = std::clamp(parsed, 0.0f, 1.0f);
                }
            // v20.14 r47c: Phase 2 ablation + hybrid GS
            } else if (key == "sdirk3_precond_phi_w_schur_boost_on" || key == "precond_phi_w_schur_boost_on") {
                precond_phi_w_schur_boost_on = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_phi_w_schur_rhs_inject_on" || key == "precond_phi_w_schur_rhs_inject_on") {
                precond_phi_w_schur_rhs_inject_on = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_phi_w_schur_backsub_on" || key == "precond_phi_w_schur_backsub_on") {
                precond_phi_w_schur_backsub_on = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_phi_w_schur_alpha_gs" || key == "precond_phi_w_schur_alpha_gs") {
                char* end = nullptr;
                float parsed = std::strtof(value.c_str(), &end);
                if (end != value.c_str()) {
                    precond_phi_w_schur_alpha_gs = std::clamp(parsed, 0.0f, 2.0f);
                }
            } else if (key == "sdirk3_precond_phi_w_schur_cross_thresh" || key == "precond_phi_w_schur_cross_thresh") {
                float parsed = std::strtof(value.c_str(), nullptr);
                precond_phi_w_schur_cross_thresh = std::clamp(parsed, 0.0f, 100.0f);
            } else if (key == "sdirk3_precond_phi_w_schur_cap_decay" || key == "precond_phi_w_schur_cap_decay") {
                float parsed = std::strtof(value.c_str(), nullptr);
                precond_phi_w_schur_cap_decay = std::clamp(parsed, 0.0f, 1.0f);
            } else if (key == "sdirk3_precond_phi_w_schur_ru_scale" || key == "precond_phi_w_schur_ru_scale") {
                float parsed = std::strtof(value.c_str(), nullptr);
                precond_phi_w_schur_ru_scale = std::clamp(parsed, 0.0f, 10.0f);
            } else if (key == "sdirk3_precond_phi_w_schur_ru_thresh" || key == "precond_phi_w_schur_ru_thresh") {
                float parsed = std::strtof(value.c_str(), nullptr);
                precond_phi_w_schur_ru_thresh = std::clamp(parsed, 0.0f, 1.0f);
            } else if (key == "sdirk3_precond_phi_w_schur_alpha_gs_start" || key == "precond_phi_w_schur_alpha_gs_start") {
                precond_phi_w_schur_alpha_gs_start = std::clamp(std::atoi(value.c_str()), 0, 20);
            } else if (key == "sdirk3_precond_du_weak_factor" || key == "precond_du_weak_factor") {
                float parsed = std::strtof(value.c_str(), nullptr);
                precond_du_weak_factor = std::clamp(parsed, 0.001f, 1.0f);
            } else if (key == "sdirk3_precond_du_weak_ru_thresh" || key == "precond_du_weak_ru_thresh") {
                float parsed = std::strtof(value.c_str(), nullptr);
                precond_du_weak_ru_thresh = std::clamp(parsed, 0.0f, 1.0f);
            // v20.14 r49: Smoothing + block-aware trust-region
            } else if (key == "sdirk3_precond_horizontal_smooth_alpha") {
                precond_horizontal_smooth_alpha = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 0.25f);
            } else if (key == "sdirk3_precond_horizontal_smooth_iters") {
                precond_horizontal_smooth_iters = std::clamp(std::stoi(value), 1, 5);
            } else if (key == "sdirk3_precond_vertical_smooth_alpha") {
                precond_vertical_smooth_alpha = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_precond_smooth_boundary_guard") {
                precond_smooth_boundary_guard = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_trust_region_block_aware_thresh") {
                trust_region_block_aware_thresh = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_trust_fallback_relax") {
                trust_fallback_relax = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_trust_fallback_ratio") {
                trust_fallback_ratio = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            } else if (key == "sdirk3_direct_u_solve_thresh") {
                direct_u_solve_thresh = std::clamp(std::strtof(value.c_str(), nullptr), 0.0f, 1.0f);
            // v20.14r35: Newton convergence thresholds via namelist
            } else if (key == "sdirk3_near_fail_floor") {
                near_fail_floor = std::clamp(std::stof(value), 0.0f, 0.1f);
            } else if (key == "sdirk3_newton_stall_threshold") {
                newton_stall_threshold = std::clamp(std::stof(value), 0.0f, 0.1f);
            } else if (key == "sdirk3_catastrophic_rel_cap") {
                catastrophic_rel_cap = std::max(100.0f, std::stof(value));
            } else if (key == "sdirk3_explosion_abs_floor") {
                explosion_abs_floor = std::max(1.0f, std::stof(value));
            } else if (key == "sdirk3_explosion_rel_multiplier") {
                explosion_rel_multiplier = std::max(1.0f, std::stof(value));
            } else if (key == "sdirk3_fd_consistency_samples") {
                fd_consistency_samples = std::clamp(std::stoi(value), 0, 10);
            // v20.14r36: Additional gate/limit thresholds via namelist
            } else if (key == "sdirk3_catastrophic_abs_floor") {
                catastrophic_abs_floor = std::max(0.0f, std::stof(value));
            } else if (key == "sdirk3_newton_zero_step_stall_limit") {
                newton_zero_step_stall_limit = std::clamp(std::stoi(value), 1, 20);
            } else if (key == "sdirk3_precond_gs_awphi_cap") {
                precond_gs_awphi_cap = std::max(0.01f, std::stof(value));
            // v20.14r38: Stage gate/damp thresholds via namelist
            } else if (key == "sdirk3_stage_gate_rel_threshold") {
                stage_gate_rel_threshold = std::max(1.0f, std::stof(value));
            } else if (key == "sdirk3_stage3_gate_rel_threshold") {
                stage3_gate_rel_threshold = std::max(0.0f, std::stof(value));
            } else if (key == "sdirk3_stage_damp_rel_threshold") {
                stage_damp_rel_threshold = std::max(1.0f, std::stof(value));
            } else if (key == "sdirk3_stage_gate_growth_cap") {
                stage_gate_growth_cap = std::max(1.0f, std::stof(value));
            } else if (key == "sdirk3_stage_gate_k_floor") {
                stage_gate_K_floor = std::max(0.01f, std::stof(value));
            } else if (key == "sdirk3_trust_region_max_rel_update") {
                trust_region_max_relative_update = std::max(0.1f, std::stof(value));
            // v20.14r34: UV vertical fraction via namelist
            } else if (key == "sdirk3_precond_uv_vertical_fraction" || key == "precond_uv_vertical_fraction") {
                precond_uv_vertical_fraction = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_uv_vfrac_warmup_start") {
                uv_vfrac_warmup_start = std::clamp(std::stof(value), 0.0f, 1.0f);
            // v20.14: Adaptive tuning constants via namelist
            } else if (key == "sdirk3_adaptive_high_threshold") {
                adaptive_high_threshold = std::stof(value);
                normalize_adaptive_thresholds();
            } else if (key == "sdirk3_adaptive_low_threshold") {
                adaptive_low_threshold = std::stof(value);
                normalize_adaptive_thresholds();
            } else if (key == "sdirk3_adaptive_step_size") {
                adaptive_step_size = std::clamp(std::stof(value), 0.001f, 0.1f);
            } else if (key == "sdirk3_adaptive_quality_gate") {
                adaptive_quality_gate = std::clamp(std::stof(value), 0.5f, 1.0f);
            } else if (key == "sdirk3_adaptive_retune_mode") {
                adaptive_retune_mode = std::clamp(std::stoi(value), 0, 2);
            // v20.14r27i/r51: Stage fail policy and NaN sanitizer via namelist
            } else if (key == "sdirk3_stage_fail_action") {
                stage_fail_action = std::clamp(std::stoi(value), 0, 2);
            } else if (key == "sdirk3_gate_metric_mode") {
                gate_metric_mode = std::clamp(std::stoi(value), 0, 2);
            } else if (key == "sdirk3_mode3_stage4_severe_abort") {
                mode3_stage4_severe_abort = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mode3_retry_nan_sanitize") {
                mode3_retry_nan_sanitize = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mode3_firsthit_probe") {
                mode3_firsthit_probe = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mode3_retry_stage4_preclip") {
                mode3_retry_stage4_preclip = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mode3_retry_stage4_clip_abs") {
                mode3_retry_stage4_clip_abs = std::max(1.0f, std::stof(value));
            } else if (key == "sdirk3_mode3_retry_handoff_clip") {
                mode3_retry_handoff_clip = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mode3_retry_handoff_clip_abs") {
                mode3_retry_handoff_clip_abs = std::max(1.0f, std::stof(value));
            } else if (key == "sdirk3_hopeless_relax") {
                hopeless_relax = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_ratio_guard_warn_only") {
                precond_ratio_guard_warn_only = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_nan_sanitize_mode") {
                nan_sanitize_mode = std::clamp(std::stoi(value), 0, 2);
            // Memory Options
            } else if (key == "sdirk3_memory_pool") {
                memory_pool = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_memory_pool_size_mb") {
                memory_pool_size_mb = std::stoi(value);
            } else if (key == "sdirk3_tensor_checkpointing") {
                tensor_checkpointing = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_gradient_checkpointing") {
                gradient_checkpointing = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_save_trajectory" || key == "save_trajectory") {
                save_trajectory = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_checkpoint_interval" || key == "checkpoint_interval") {
                checkpoint_interval = std::max(1, std::stoi(value));
            } else if (key == "sdirk3_retain_graph" || key == "retain_graph" ||
                       key == "sdirk3_retain_graph_for_adjoint" || key == "retain_graph_for_adjoint") {
                retain_graph_for_adjoint = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_obs_aware_4dvar" || key == "obs_aware_4dvar") {
                obs_aware_4dvar = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_obs_source_mode" || key == "obs_source_mode") {
                obs_source_mode = std::clamp(std::stoi(value), 0, 2);
            } else if (key == "sdirk3_obs_window_sync_mode" || key == "obs_window_sync_mode") {
                obs_window_sync_mode = std::clamp(std::stoi(value), 0, 2);
            } else if (key == "sdirk3_kdamp" || key == "dampcoef" || key == "kdamp") {
                kdamp = std::stof(value);
            } else if (key == "sdirk3_w_damp_alpha" || key == "w_damp_alpha") {
                w_damp_alpha = std::clamp(std::stof(value), 0.0f, 2.0f);
            } else if (key == "sdirk3_w_crit_cfl" || key == "w_crit_cfl") {
                w_crit_cfl = std::clamp(std::stof(value), 0.1f, 10.0f);
            } else if (key == "sdirk3_validation_level" || key == "validation_level") {
                validation_level = std::clamp(std::stoi(value), 0, 3);
                syncValidationSettings(validation_level);
            } else if (key == "sdirk3_check_staggered" || key == "check_staggered_consistency") {
                check_staggered_consistency = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_collect_stats" || key == "collect_validation_stats") {
                collect_validation_stats = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_conserv_tol_mass" || key == "conservation_tol_mass") {
                conservation_tol_mass = std::max(1.0e-12f, std::stof(value));
            } else if (key == "sdirk3_conserv_tol_energy" || key == "conservation_tol_energy") {
                conservation_tol_energy = std::max(1.0e-12f, std::stof(value));
            } else if (key == "sdirk3_conserv_tol_momentum" || key == "conservation_tol_momentum") {
                conservation_tol_momentum = std::max(1.0e-12f, std::stof(value));
            } else if (key == "sdirk3_enable_benchmarking" || key == "enable_benchmarking") {
                enable_benchmarking = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_benchmark_warmup" || key == "benchmark_warmup_iter") {
                benchmark_warmup_iter = std::max(0, std::stoi(value));
            } else if (key == "sdirk3_benchmark_measure" || key == "benchmark_measure_iter") {
                benchmark_measure_iter = std::max(1, std::stoi(value));
            } else if (key == "zdamp" || key == "damp_top") {
                rayleigh_damp_depth = std::stof(value);
            } else if (key == "damp_coef" || key == "rayleigh_damp_coef") {
                rayleigh_damp_coef = std::stof(value);
            } else if (key == "khdif" || key == "km_opt") {
                khdif = std::stof(value);
            } else if (key == "kvdif") {
                kvdif = std::stof(value);

            // PARITY FIX 2025-12-23: z_top_min/z_top_default configurable via namelist
            } else if (key == "sdirk3_z_top_min" || key == "z_top_min") {
                z_top_min = std::stof(value);
            } else if (key == "sdirk3_z_top_default" || key == "z_top_default") {
                z_top_default = std::stof(value);
            // PERF FIX 2025-12-25: c1f/c2f signature verification options
            } else if (key == "sdirk3_c1f_c2f_signature_points" || key == "c1f_c2f_signature_points") {
                int val = std::stoi(value);
                c1f_c2f_signature_points = (val == 5) ? 5 : 3;  // Only 3 or 5 valid
            } else if (key == "sdirk3_c1f_c2f_full_verify_interval" || key == "c1f_c2f_full_verify_interval") {
                int val = std::stoi(value);
                c1f_c2f_full_verify_interval = std::max(0, val);
            } else if (key == "sdirk3_c1f_c2f_signature_period" || key == "c1f_c2f_signature_period") {
                int val = std::stoi(value);
                c1f_c2f_signature_period = std::max(1, val);
            } else if (key == "sdirk3_grid_metric_full_verify_period" || key == "grid_metric_full_verify_period") {
                // GRID METRIC VERIFY 2025-12-25: Periodic O(n) verification for rdnw/rdn
                int val = std::stoi(value);
                grid_metric_full_verify_period = std::max(0, val);
                // FIX 2025-12-29 Issue 3: Sync with StaticMetricCache3D periodic signature check
                metric_utils::setStaticMetricFullVerifyPeriod(grid_metric_full_verify_period);

            // AD FIX 2025-12-26: AD strict mode configuration
            } else if (key == "sdirk3_ad_strict_mode" || key == "ad_strict_mode") {
                ad_strict_mode = parse_fortran_bool_value(value);
                // Sync with global flag for metric_utils.h enforcement
                set_ad_strict_mode(ad_strict_mode);
                std::cerr << "[CONFIG DEBUG] Setting ad_strict_mode = " << ad_strict_mode
                          << " (from namelist)" << std::endl;

            // FIX 2025-12-28: use_max_height_for_damping for Rayleigh damping profile
            } else if (key == "sdirk3_use_max_height_for_damping" || key == "use_max_height_for_damping") {
                use_max_height_for_damping = parse_fortran_bool_value(value);

            // FIX 2025-12-28: moving_nest - affects cache check aggressiveness
            } else if (key == "sdirk3_moving_nest" || key == "moving_nest" || key == "grid_allowed_to_move") {
                moving_nest = parse_fortran_bool_value(value);
                // FIX 2025-12-29 Issue 1: Sync with metric_utils isMovingNestMode flag
                metric_utils::setMovingNestMode(moving_nest);

            // FIX 2025-12-29 Batch10 Issue 1: spatial_async_min_elems for BatchSpatialDerivatives
            } else if (key == "sdirk3_spatial_async_min_elems" || key == "spatial_async_min_elems") {
                int64_t val = std::stoll(value);
                spatial_async_min_elems = std::max(int64_t(0), val);

            // IMEX post-solve split mode (2026-02-01)
            } else if (key == "sdirk3_imex_split_mode" || key == "imex_split_mode") {
                int parsed = std::stoi(value);
                if (parsed < 0 || parsed > 3) {
                    std::cerr << "[CONFIG WARNING] imex_split_mode=" << parsed
                              << " (invalid, from namelist) -> clamped to 0;"
                              << " effective may map to 1 if imex_enabled=true" << std::endl;
                    parsed = 0;
                }
                imex_split_mode = parsed;
            } else if (key == "sdirk3_imex_slow_in_tangent" || key == "imex_slow_in_tangent") {
                imex_slow_in_tangent = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_imex_phys_in_tangent" || key == "imex_phys_in_tangent") {
                imex_phys_in_tangent = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_stage1_explicit" || key == "stage1_explicit") {
                stage1_explicit = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_stage3_warmstart" || key == "stage3_warmstart") {
                stage3_warmstart = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_stage3_gate_rel_threshold" || key == "stage3_gate_rel_threshold") {
                stage3_gate_rel_threshold = std::max(0.0f, std::stof(value));
            } else if (key == "sdirk3_gate_metric_mode" || key == "gate_metric_mode") {
                gate_metric_mode = std::clamp(std::stoi(value), 0, 2);
            } else if (key == "sdirk3_mode3_stage4_severe_abort" || key == "mode3_stage4_severe_abort") {
                mode3_stage4_severe_abort = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mode3_retry_nan_sanitize" || key == "mode3_retry_nan_sanitize") {
                mode3_retry_nan_sanitize = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mode3_firsthit_probe" || key == "mode3_firsthit_probe") {
                mode3_firsthit_probe = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mode3_retry_stage4_preclip" || key == "mode3_retry_stage4_preclip") {
                mode3_retry_stage4_preclip = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mode3_retry_stage4_clip_abs" || key == "mode3_retry_stage4_clip_abs") {
                mode3_retry_stage4_clip_abs = std::max(1.0f, std::stof(value));
            } else if (key == "sdirk3_mode3_retry_handoff_clip" || key == "mode3_retry_handoff_clip") {
                mode3_retry_handoff_clip = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mode3_retry_handoff_clip_abs" || key == "mode3_retry_handoff_clip_abs") {
                mode3_retry_handoff_clip_abs = std::max(1.0f, std::stof(value));
            } else if (key == "sdirk3_jvp_auto_bench_quality_gate" || key == "jvp_auto_bench_quality_gate") {
                jvp_auto_bench_quality_gate = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_jvp_auto_bench_warmup" || key == "jvp_auto_bench_warmup") {
                jvp_auto_bench_warmup = std::clamp(std::stoi(value), 0, 50);
            } else if (key == "sdirk3_jvp_auto_bench_seed" || key == "jvp_auto_bench_seed") {
                jvp_auto_bench_seed = std::stoi(value);
            } else if (key == "sdirk3_jvp_auto_bench_lock_reset_stage1" || key == "jvp_auto_bench_lock_reset_stage1") {
                jvp_auto_bench_lock_reset_stage1 = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_solver_telemetry" || key == "solver_telemetry") {
                solver_telemetry = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_progress_invariant_enable" || key == "progress_invariant_enable") {
                progress_invariant_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_progress_invariant_min_ratio" || key == "progress_invariant_min_ratio") {
                progress_invariant_min_ratio = std::max(0.0f, std::stof(value));
            } else if (key == "sdirk3_progress_invariant_streak_limit" || key == "progress_invariant_streak_limit") {
                progress_invariant_streak_limit = std::max(0, std::stoi(value));
            } else if (key == "sdirk3_variable_pc_event_log" || key == "variable_pc_event_log") {
                variable_pc_event_log = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_gmres_warmstart" || key == "gmres_warmstart") {
                gmres_warmstart = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_gmres_warmstart_quality_gate" || key == "gmres_warmstart_quality_gate") {
                gmres_warmstart_quality_gate = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_inn_warmstart_enable" || key == "inn_warmstart_enable") {
                inn_warmstart_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_inn_residual_gate_enable" || key == "inn_residual_gate_enable") {
                inn_residual_gate_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_inn_q_min" || key == "inn_q_min") {
                inn_q_min = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_inn_gate_rel_tol" || key == "inn_gate_rel_tol") {
                inn_gate_rel_tol = std::max(std::stof(value), 0.0f);
            } else if (key == "sdirk3_inn_gate_q_noise" || key == "inn_gate_q_noise") {
                inn_gate_q_noise = std::max(std::stof(value), 0.0f);
            } else if (key == "sdirk3_inn_beta_max" || key == "inn_beta_max") {
                inn_beta_max = std::clamp(std::stof(value), 0.0f, 1.0f);
            } else if (key == "sdirk3_inn_ood_guard_enable" || key == "inn_ood_guard_enable") {
                inn_ood_guard_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_inn_tol_ramp_enable" || key == "inn_tol_ramp_enable") {
                inn_tol_ramp_enable = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_inn_tol_ramp_gamma" || key == "inn_tol_ramp_gamma") {
                inn_tol_ramp_gamma = std::clamp(std::stof(value), 1.0e-3f, 1.0f);
            } else if (key == "sdirk3_rhs_bc_parity" || key == "rhs_bc_parity") {
                rhs_bc_parity = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_mass_pgf_bc_guard" || key == "mass_pgf_bc_guard") {
                mass_pgf_bc_guard = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_stage_boundary_sync" || key == "stage_boundary_sync") {
                stage_boundary_sync = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_smooth_boundary_guard" || key == "precond_smooth_boundary_guard") {
                precond_smooth_boundary_guard = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_trust_fallback_relax" || key == "trust_fallback_relax") {
                trust_fallback_relax = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_trust_fallback_ratio" || key == "trust_fallback_ratio") {
                trust_fallback_ratio = std::clamp(std::stof(value), 0.0f, 1.0f);

            // Preconditioner-RHS scope consistency (2026-02-01)
            } else if (key == "sdirk3_precond_match_rhs" || key == "precond_match_rhs") {
                precond_match_rhs = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_extra_rayleigh" || key == "precond_extra_rayleigh") {
                precond_extra_rayleigh = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_extra_wdamp" || key == "precond_extra_wdamp") {
                precond_extra_wdamp = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_extra_vdiff" || key == "precond_extra_vdiff") {
                precond_extra_vdiff = parse_fortran_bool_value(value);
            } else if (key == "sdirk3_precond_extra_divergence" || key == "precond_extra_divergence") {
                precond_extra_divergence = parse_fortran_bool_value(value);

            // v20.9: sign_smooth_delta and gmres_stagnation_threshold
            } else if (key == "sdirk3_sign_smooth_delta" || key == "sign_smooth_delta") {
                sign_smooth_delta = std::max(1e-6f, std::stof(value));  // v20.14r27z: enforce > 0
            } else if (key == "sdirk3_gmres_stagnation_threshold" || key == "gmres_stagnation_threshold") {
                gmres_stagnation_threshold = std::clamp(std::stof(value), 0.5f, 1.0f);
            } else if (key == "sdirk3_newton_gmres_quality_threshold" || key == "newton_gmres_quality_threshold") {
                newton_gmres_quality_threshold = std::clamp(std::stof(value), 0.5f, 1.0f);
            }
            } catch (const std::exception& e) {
                std::cerr << "[CONFIG WARNING] Skipping malformed namelist entry '"
                          << key << "=" << value << "': " << e.what() << std::endl;
            }
        }
    }
}

void SDIRK3Config::load_from_env() {
    // Check environment variables
    const char* env_val;
    
    if ((env_val = std::getenv("WRF_SDIRK3_MAX_NEWTON_ITER"))) {
        max_newton_iter = std::atoi(env_val);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_NEWTON_TOL"))) {
        newton_tol = std::atof(env_val);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_GMRES_RESTART"))) {
        gmres_restart = std::atoi(env_val);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MAX_KRYLOV_ITER"))) {
        max_krylov_iter = std::atoi(env_val);
    }
    // v20.14r48: GMRES performance tuning env vars
    if ((env_val = std::getenv("WRF_SDIRK3_GMRES_TRUE_RESIDUAL_START_J"))) {
        gmres_true_residual_start_j = std::clamp(std::atoi(env_val), 0, 1000);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_GMRES_TRUE_RESIDUAL_PERIOD"))) {
        gmres_true_residual_period = std::clamp(std::atoi(env_val), 1, 100);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_GMRES_ARNOLDI_STAG_WINDOW"))) {
        gmres_arnoldi_stag_window = std::clamp(std::atoi(env_val), 1, 20);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_GMRES_ARNOLDI_STAG_RATIO"))) {
        gmres_arnoldi_stag_ratio = std::clamp(static_cast<float>(std::atof(env_val)), 0.5f, 1.0f);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_MIXED_FD_NEWTON_SWITCH"))) {
        jvp_mixed_fd_newton_switch = std::atoi(env_val);
    }
    // v20.14 r49/r59: Stage-aware GMRES budget
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE2_GMRES_RESTART"))) {
        stage2_gmres_restart = std::clamp(std::atoi(env_val), 0, 100);
        std::cerr << "[CONFIG ENV] stage2_gmres_restart = " << stage2_gmres_restart << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE2_MAX_KRYLOV_RESTARTS"))) {
        stage2_max_krylov_restarts = std::clamp(std::atoi(env_val), 0, 20);
        std::cerr << "[CONFIG ENV] stage2_max_krylov_restarts = " << stage2_max_krylov_restarts << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE2_KRYLOV_TOL"))) {
        stage2_krylov_tol = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] stage2_krylov_tol = " << stage2_krylov_tol << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE2_EW_ETA_MIN"))) {
        stage2_ew_eta_min = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] stage2_ew_eta_min = " << stage2_ew_eta_min << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE2_EW_ETA_MAX"))) {
        stage2_ew_eta_max = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] stage2_ew_eta_max = " << stage2_ew_eta_max << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE3_GMRES_RESTART"))) {
        stage3_gmres_restart = std::clamp(std::atoi(env_val), 0, 100);
        std::cerr << "[CONFIG ENV] stage3_gmres_restart = " << stage3_gmres_restart << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE3_MAX_KRYLOV_RESTARTS"))) {
        stage3_max_krylov_restarts = std::clamp(std::atoi(env_val), 0, 20);
        std::cerr << "[CONFIG ENV] stage3_max_krylov_restarts = " << stage3_max_krylov_restarts << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE3_KRYLOV_TOL"))) {
        stage3_krylov_tol = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] stage3_krylov_tol = " << stage3_krylov_tol << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE3_EW_ETA_MIN"))) {
        stage3_ew_eta_min = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] stage3_ew_eta_min = " << stage3_ew_eta_min << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE3_EW_ETA_MAX"))) {
        stage3_ew_eta_max = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] stage3_ew_eta_max = " << stage3_ew_eta_max << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_AUTO_BENCH_CALLS"))) {
        jvp_auto_bench_calls = std::clamp(std::atoi(env_val), 0, 20);
        std::cerr << "[CONFIG ENV] jvp_auto_bench_calls = " << jvp_auto_bench_calls << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_AUTO_BENCH_QUALITY_GATE"))) {
        jvp_auto_bench_quality_gate = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] jvp_auto_bench_quality_gate = "
                  << (jvp_auto_bench_quality_gate ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_AUTO_BENCH_WARMUP"))) {
        jvp_auto_bench_warmup = std::clamp(std::atoi(env_val), 0, 50);
        std::cerr << "[CONFIG ENV] jvp_auto_bench_warmup = " << jvp_auto_bench_warmup << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_AUTO_BENCH_SEED"))) {
        jvp_auto_bench_seed = std::atoi(env_val);
        std::cerr << "[CONFIG ENV] jvp_auto_bench_seed = " << jvp_auto_bench_seed << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_AUTO_BENCH_LOCK_RESET_STAGE1"))) {
        jvp_auto_bench_lock_reset_stage1 = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] jvp_auto_bench_lock_reset_stage1 = "
                  << (jvp_auto_bench_lock_reset_stage1 ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SOLVER_TELEMETRY"))) {
        solver_telemetry = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] solver_telemetry = " << (solver_telemetry ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PROGRESS_INVARIANT_ENABLE"))) {
        progress_invariant_enable = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] progress_invariant_enable = "
                  << (progress_invariant_enable ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PROGRESS_INVARIANT_MIN_RATIO"))) {
        progress_invariant_min_ratio = std::max(0.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] progress_invariant_min_ratio = "
                  << progress_invariant_min_ratio << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PROGRESS_INVARIANT_STREAK_LIMIT"))) {
        progress_invariant_streak_limit = std::max(0, std::atoi(env_val));
        std::cerr << "[CONFIG ENV] progress_invariant_streak_limit = "
                  << progress_invariant_streak_limit << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_VARIABLE_PC_EVENT_LOG"))) {
        variable_pc_event_log = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] variable_pc_event_log = "
                  << (variable_pc_event_log ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_GMRES_WARMSTART"))) {
        gmres_warmstart = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] gmres_warmstart = " << (gmres_warmstart ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_GMRES_WARMSTART_QUALITY_GATE"))) {
        gmres_warmstart_quality_gate = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] gmres_warmstart_quality_gate = " << gmres_warmstart_quality_gate << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_INN_WARMSTART_ENABLE"))) {
        inn_warmstart_enable = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] inn_warmstart_enable = "
                  << (inn_warmstart_enable ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_INN_RESIDUAL_GATE_ENABLE"))) {
        inn_residual_gate_enable = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] inn_residual_gate_enable = "
                  << (inn_residual_gate_enable ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_INN_Q_MIN"))) {
        inn_q_min = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] inn_q_min = " << inn_q_min << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_INN_GATE_REL_TOL"))) {
        inn_gate_rel_tol = std::max(static_cast<float>(std::atof(env_val)), 0.0f);
        std::cerr << "[CONFIG ENV] inn_gate_rel_tol = " << inn_gate_rel_tol << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_INN_GATE_Q_NOISE"))) {
        inn_gate_q_noise = std::max(static_cast<float>(std::atof(env_val)), 0.0f);
        std::cerr << "[CONFIG ENV] inn_gate_q_noise = " << inn_gate_q_noise << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_INN_BETA_MAX"))) {
        inn_beta_max = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] inn_beta_max = " << inn_beta_max << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_INN_OOD_GUARD_ENABLE"))) {
        inn_ood_guard_enable = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] inn_ood_guard_enable = "
                  << (inn_ood_guard_enable ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_INN_TOL_RAMP_ENABLE"))) {
        inn_tol_ramp_enable = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] inn_tol_ramp_enable = "
                  << (inn_tol_ramp_enable ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_INN_TOL_RAMP_GAMMA"))) {
        inn_tol_ramp_gamma = std::clamp(static_cast<float>(std::atof(env_val)), 1.0e-3f, 1.0f);
        std::cerr << "[CONFIG ENV] inn_tol_ramp_gamma = " << inn_tol_ramp_gamma << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_TYPE"))) {
        precond_type = std::atoi(env_val);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_DEBUG_LEVEL"))) {
        debug_level = std::atoi(env_val);
        // FIX 2025-12-31 Batch41 Issue 4: Sync validation policy based on debug_level
        syncValidationSettings(debug_level);
    }
    // PARITY FIX 2025-12-24: Add env variable for grid metric memcmp period.
    // 0=auto, 1=strict (moving nest), N>1=every N calls.
    if ((env_val = std::getenv("WRF_SDIRK3_GRID_METRIC_MEMCMP_PERIOD"))) {
        int64_t parsed = std::atoll(env_val);
        grid_metric_memcmp_period = (parsed < 0) ? 0 : static_cast<uint64_t>(parsed);
    }
    // PARITY FIX 2025-12-24: Add env variables for separate CUDA sampling periods.
    if ((env_val = std::getenv("WRF_SDIRK3_CUDA_GRID_METRIC_SAMPLE_PERIOD"))) {
        int64_t parsed = std::atoll(env_val);
        cuda_grid_metric_sample_period = (parsed < 0) ? 0 : static_cast<uint64_t>(parsed);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_CUDA_PHBASE_SAMPLE_PERIOD"))) {
        int64_t parsed = std::atoll(env_val);
        cuda_phbase_sample_period = (parsed < 0) ? 0 : static_cast<uint64_t>(parsed);
    }
    // FIX 2025-12-27: Runtime-tunable sample check period for LatCpuCache
    if ((env_val = std::getenv("WRF_SDIRK3_LAT_CPU_SAMPLE_CHECK_PERIOD"))) {
        int64_t parsed = std::atoll(env_val);
        lat_cpu_sample_check_period = (parsed < 0) ? 0 : static_cast<uint64_t>(parsed);
    }
    // PERF FIX 2025-12-25: c1f/c2f signature verification options
    if ((env_val = std::getenv("WRF_SDIRK3_C1F_C2F_SIGNATURE_POINTS"))) {
        int val = std::atoi(env_val);
        c1f_c2f_signature_points = (val == 5) ? 5 : 3;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_C1F_C2F_FULL_VERIFY_INTERVAL"))) {
        int val = std::atoi(env_val);
        c1f_c2f_full_verify_interval = std::max(0, val);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_C1F_C2F_SIGNATURE_PERIOD"))) {
        int val = std::atoi(env_val);
        c1f_c2f_signature_period = std::max(1, val);
    }
    // GRID METRIC VERIFY 2025-12-25: Periodic O(n) verification for rdnw/rdn
    if ((env_val = std::getenv("WRF_SDIRK3_GRID_METRIC_FULL_VERIFY_PERIOD"))) {
        int val = std::atoi(env_val);
        grid_metric_full_verify_period = std::max(0, val);
        // FIX 2025-12-29 Issue 3: Sync with StaticMetricCache3D periodic signature check
        metric_utils::setStaticMetricFullVerifyPeriod(grid_metric_full_verify_period);
    }
    if ((env_val = std::getenv("WRF_SDIRK3_N_THREADS"))) {
        n_threads = std::atoi(env_val);
    }
    
    // SDIRK3: Runtime switch for JVP method comparison
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_METHOD"))) {
        std::string method(env_val);
        if (method == "FINITE_DIFF" || method == "0") {
            jvp_method = JVP_FINITE_DIFF;
            use_autograd = false;
            std::cerr << "SDIRK3: JVP method set to FINITE_DIFF via environment" << std::endl;
        } else if (method == "AUTOGRAD" || method == "1") {
            jvp_method = JVP_AUTOGRAD;
            use_autograd = true;
            std::cerr << "SDIRK3: JVP method set to AUTOGRAD via environment" << std::endl;
        } else if (method == "2" || method == "3" ||
                   method == "DUAL_NUMBER" || method == "OPTIMIZED") {
            // v20.14 r47c-fix3b: 2/3 not implemented — clamp to AD with warning.
            jvp_method = JVP_AUTOGRAD;
            use_autograd = true;
            std::cerr << "SDIRK3: JVP_METHOD '" << method
                      << "' not implemented, clamped to AUTOGRAD (1)" << std::endl;
        } else {
            std::cerr << "SDIRK3: Unknown JVP_METHOD '" << method
                     << "', keeping default" << std::endl;
        }
    }
    
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_EPSILON"))) {
        jvp_epsilon = static_cast<float>(std::atof(env_val));
        // v20.14r27m: Clamp to valid range (0, 0.1]
        jvp_epsilon = std::clamp(jvp_epsilon, 1e-15f, 0.1f);
        std::cerr << "SDIRK3: JVP epsilon set to " << jvp_epsilon << " via environment" << std::endl;
    }

    // v20.14r27q: Block-aware FD epsilon (EXPERIMENTAL)
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_BLOCK_EPSILON"))) {
        jvp_block_epsilon = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] jvp_block_epsilon = "
                  << (jvp_block_epsilon ? "true" : "false") << std::endl;
        if (jvp_block_epsilon) {
            std::cerr << "[CONFIG WARNING] jvp_block_epsilon is EXPERIMENTAL: "
                      << "computes J*delta (not J*v), GMRES correctness not guaranteed."
                      << std::endl;
        }
    }

    if ((env_val = std::getenv("WRF_SDIRK3_JVP_CHECKPOINTING"))) {
        jvp_checkpointing = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] jvp_checkpointing = "
                  << (jvp_checkpointing ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_CHECKPOINT_SEGMENTS"))) {
        jvp_checkpoint_segments = std::clamp(std::atoi(env_val), 1, 64);
        std::cerr << "[CONFIG ENV] jvp_checkpoint_segments = " << jvp_checkpoint_segments << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_GRAPH_CACHING"))) {
        jvp_graph_caching = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] jvp_graph_caching = "
                  << (jvp_graph_caching ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_BATCHED"))) {
        jvp_batched = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] jvp_batched = "
                  << (jvp_batched ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_BATCH_SIZE"))) {
        jvp_batch_size = std::clamp(std::atoi(env_val), 1, 256);
        std::cerr << "[CONFIG ENV] jvp_batch_size = " << jvp_batch_size << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_JVP_MIXED_PRECISION"))) {
        jvp_mixed_precision = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] jvp_mixed_precision = "
                  << (jvp_mixed_precision ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_ILU"))) {
        precond_ilu = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_ilu = " << (precond_ilu ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_ILU_LEVEL"))) {
        precond_ilu_level = std::clamp(std::atoi(env_val), 0, 8);
        std::cerr << "[CONFIG ENV] precond_ilu_level = " << precond_ilu_level << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_MULTIGRID"))) {
        precond_multigrid = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_multigrid = "
                  << (precond_multigrid ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_MG_LEVELS"))) {
        precond_mg_levels = std::clamp(std::atoi(env_val), 1, 10);
        std::cerr << "[CONFIG ENV] precond_mg_levels = " << precond_mg_levels << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MEMORY_POOL"))) {
        memory_pool = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] memory_pool = " << (memory_pool ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MEMORY_POOL_SIZE_MB"))) {
        memory_pool_size_mb = std::clamp(std::atoi(env_val), 16, 65536);
        std::cerr << "[CONFIG ENV] memory_pool_size_mb = " << memory_pool_size_mb << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_TENSOR_CHECKPOINTING"))) {
        tensor_checkpointing = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] tensor_checkpointing = "
                  << (tensor_checkpointing ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_GRADIENT_CHECKPOINTING"))) {
        gradient_checkpointing = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] gradient_checkpointing = "
                  << (gradient_checkpointing ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SAVE_TRAJECTORY"))) {
        save_trajectory = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] save_trajectory = "
                  << (save_trajectory ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_CHECKPOINT_INTERVAL"))) {
        checkpoint_interval = std::max(1, std::atoi(env_val));
        std::cerr << "[CONFIG ENV] checkpoint_interval = " << checkpoint_interval << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_RETAIN_GRAPH_FOR_ADJOINT"))) {
        retain_graph_for_adjoint = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] retain_graph_for_adjoint = "
                  << (retain_graph_for_adjoint ? "true" : "false") << std::endl;
    } else if ((env_val = std::getenv("WRF_SDIRK3_RETAIN_GRAPH"))) {
        retain_graph_for_adjoint = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] retain_graph_for_adjoint = "
                  << (retain_graph_for_adjoint ? "true" : "false")
                  << " (from WRF_SDIRK3_RETAIN_GRAPH)" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_OBS_AWARE_4DVAR"))) {
        obs_aware_4dvar = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] obs_aware_4dvar = "
                  << (obs_aware_4dvar ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_OBS_SOURCE_MODE"))) {
        obs_source_mode = std::clamp(std::atoi(env_val), 0, 2);
        std::cerr << "[CONFIG ENV] obs_source_mode = " << obs_source_mode << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_OBS_WINDOW_SYNC_MODE"))) {
        obs_window_sync_mode = std::clamp(std::atoi(env_val), 0, 2);
        std::cerr << "[CONFIG ENV] obs_window_sync_mode = " << obs_window_sync_mode << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_KDAMP"))) {
        kdamp = std::max(0.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] kdamp = " << kdamp << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_W_DAMP_ALPHA"))) {
        w_damp_alpha = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 2.0f);
        std::cerr << "[CONFIG ENV] w_damp_alpha = " << w_damp_alpha << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_W_CRIT_CFL"))) {
        w_crit_cfl = std::clamp(static_cast<float>(std::atof(env_val)), 0.1f, 10.0f);
        std::cerr << "[CONFIG ENV] w_crit_cfl = " << w_crit_cfl << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_VALIDATION_LEVEL"))) {
        validation_level = std::clamp(std::atoi(env_val), 0, 3);
        syncValidationSettings(validation_level);
        std::cerr << "[CONFIG ENV] validation_level = " << validation_level << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_CHECK_STAGGERED"))) {
        check_staggered_consistency = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] check_staggered_consistency = "
                  << (check_staggered_consistency ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_COLLECT_STATS"))) {
        collect_validation_stats = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] collect_validation_stats = "
                  << (collect_validation_stats ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_CONSERV_TOL_MASS"))) {
        conservation_tol_mass = std::max(1.0e-12f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] conservation_tol_mass = " << conservation_tol_mass << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_CONSERV_TOL_ENERGY"))) {
        conservation_tol_energy = std::max(1.0e-12f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] conservation_tol_energy = " << conservation_tol_energy << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_CONSERV_TOL_MOMENTUM"))) {
        conservation_tol_momentum = std::max(1.0e-12f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] conservation_tol_momentum = " << conservation_tol_momentum << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_ENABLE_BENCHMARKING"))) {
        enable_benchmarking = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] enable_benchmarking = "
                  << (enable_benchmarking ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_BENCHMARK_WARMUP"))) {
        benchmark_warmup_iter = std::max(0, std::atoi(env_val));
        std::cerr << "[CONFIG ENV] benchmark_warmup_iter = " << benchmark_warmup_iter << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_BENCHMARK_MEASURE"))) {
        benchmark_measure_iter = std::max(1, std::atoi(env_val));
        std::cerr << "[CONFIG ENV] benchmark_measure_iter = " << benchmark_measure_iter << std::endl;
    }

    // PARITY FIX 2025-12-23: z_top_min/z_top_default configurable via environment
    if ((env_val = std::getenv("WRF_SDIRK3_Z_TOP_MIN"))) {
        z_top_min = std::atof(env_val);
        std::cerr << "SDIRK3: z_top_min set to " << z_top_min << " via environment" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_Z_TOP_DEFAULT"))) {
        z_top_default = std::atof(env_val);
        std::cerr << "SDIRK3: z_top_default set to " << z_top_default << " via environment" << std::endl;
    }

    // AD FIX 2025-12-26: AD strict mode via environment variable
    if ((env_val = std::getenv("WRF_SDIRK3_AD_STRICT_MODE"))) {
        ad_strict_mode = parse_bool_env(env_val);
        // Sync with global flag for metric_utils.h enforcement
        set_ad_strict_mode(ad_strict_mode);
        std::cerr << "SDIRK3: ad_strict_mode set to " << ad_strict_mode << " via environment" << std::endl;
    }

    // FIX 2025-12-28: use_max_height_for_damping via environment variable
    if ((env_val = std::getenv("WRF_SDIRK3_USE_MAX_HEIGHT_FOR_DAMPING"))) {
        use_max_height_for_damping = parse_bool_env(env_val);
        std::cerr << "SDIRK3: use_max_height_for_damping set to "
                  << (use_max_height_for_damping ? "true" : "false") << " via environment" << std::endl;
    }

    // FIX 2025-12-28: moving_nest via environment variable
    if ((env_val = std::getenv("WRF_SDIRK3_MOVING_NEST"))) {
        moving_nest = parse_bool_env(env_val);
        // FIX 2025-12-29 Issue 1: Sync with metric_utils isMovingNestMode flag
        metric_utils::setMovingNestMode(moving_nest);
        std::cerr << "SDIRK3: moving_nest set to "
                  << (moving_nest ? "true" : "false") << " via environment" << std::endl;
    }

    // FIX 2025-12-29 Batch10 Issue 1: spatial_async_min_elems via environment variable
    if ((env_val = std::getenv("WRF_SDIRK3_SPATIAL_ASYNC_MIN_ELEMS"))) {
        int64_t val = std::atoll(env_val);
        spatial_async_min_elems = std::max(int64_t(0), val);
        std::cerr << "SDIRK3: spatial_async_min_elems set to " << spatial_async_min_elems
                  << " via environment" << std::endl;
    }

    // IMEX post-solve split mode (2026-02-01) via environment variables
    // WRF_SDIRK3_IMEX_SPLIT_MODE: integer 0, 1, 2, or 3
    // WRF_SDIRK3_IMEX_SLOW_IN_TANGENT: integer 0 or 1 (0=false, non-zero=true)
    // WRF_SDIRK3_IMEX_PHYS_IN_TANGENT: integer 0 or 1 (0=false, non-zero=true)
    if ((env_val = std::getenv("WRF_SDIRK3_IMEX_SPLIT_MODE"))) {
        int val = std::atoi(env_val);
        if (val < 0 || val > 3) {
            std::cerr << "SDIRK3: imex_split_mode=" << val
                      << " (invalid, from env) -> clamped to 0;"
                      << " effective may map to 1 if imex_enabled=true" << std::endl;
            val = 0;
        }
        imex_split_mode = val;
        int eff = imex_split_mode;
        if (eff == 0 && imex_enabled) eff = 1;
        const char* eff_label = (eff == 0) ? "off/baseline" :
                                (eff == 1) ? "frozen IMEX" :
                                (eff == 2) ? "post-solve IMEX SDIRK3" :
                                (eff == 3) ? "post-solve IMEX ARK324" :
                                             "UNKNOWN -> baseline (clamped)";
        std::cerr << "[CONFIG ENV] IMEX raw: imex_split_mode=" << imex_split_mode
                  << ", imex_enabled=" << imex_enabled << std::endl;
        std::cerr << "[CONFIG ENV] IMEX effective: mode=" << eff
                  << " (" << eff_label << ")" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_IMEX_SLOW_IN_TANGENT"))) {
        imex_slow_in_tangent = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] imex_slow_in_tangent = "
                  << (imex_slow_in_tangent ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_IMEX_PHYS_IN_TANGENT"))) {
        imex_phys_in_tangent = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] imex_phys_in_tangent = "
                  << (imex_phys_in_tangent ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE1_EXPLICIT"))) {
        stage1_explicit = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] stage1_explicit = "
                  << (stage1_explicit ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE3_WARMSTART"))) {
        stage3_warmstart = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] stage3_warmstart = "
                  << (stage3_warmstart ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE_BOUNDARY_SYNC"))) {
        stage_boundary_sync = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] stage_boundary_sync = "
                  << (stage_boundary_sync ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_RHS_BC_PARITY"))) {
        rhs_bc_parity = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] rhs_bc_parity = "
                  << (rhs_bc_parity ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MASS_PGF_BC_GUARD"))) {
        mass_pgf_bc_guard = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] mass_pgf_bc_guard = "
                  << (mass_pgf_bc_guard ? "true" : "false") << std::endl;
    }

    // AD-aware halo exchange (2026-02-02) via environment variable
    if ((env_val = std::getenv("WRF_SDIRK3_ENABLE_AD_HALO_EXCHANGE"))) {
        enable_ad_halo_exchange = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] enable_ad_halo_exchange = "
                  << (enable_ad_halo_exchange ? "true" : "false") << std::endl;
    }

    // Preconditioner-RHS scope consistency (2026-02-01) via environment variables
    // WRF_SDIRK3_PRECOND_MATCH_RHS: integer 0 or 1 (0=false, non-zero=true)
    // WRF_SDIRK3_PRECOND_EXTRA_RAYLEIGH/WDAMP/VDIFF/DIVERGENCE: integer 0 or 1
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_MATCH_RHS"))) {
        precond_match_rhs = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_match_rhs = "
                  << (precond_match_rhs ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_EXTRA_RAYLEIGH"))) {
        precond_extra_rayleigh = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_extra_rayleigh = "
                  << (precond_extra_rayleigh ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_EXTRA_WDAMP"))) {
        precond_extra_wdamp = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_extra_wdamp = "
                  << (precond_extra_wdamp ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_EXTRA_VDIFF"))) {
        precond_extra_vdiff = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_extra_vdiff = "
                  << (precond_extra_vdiff ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_EXTRA_DIVERGENCE"))) {
        precond_extra_divergence = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_extra_divergence = "
                  << (precond_extra_divergence ? "true" : "false") << std::endl;
    }

    // v20.9: Preconditioner acoustic 4x4 and boost via environment
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_ACOUSTIC_4X4"))) {
        precond_acoustic_4x4 = std::atoi(env_val);
        std::cerr << "[CONFIG ENV] precond_acoustic_4x4 = " << precond_acoustic_4x4 << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_W_ACOUSTIC_BOOST"))) {
        precond_w_acoustic_boost = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 10.0f);
        std::cerr << "[CONFIG ENV] precond_w_acoustic_boost = " << precond_w_acoustic_boost << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_THETA_ACOUSTIC_FACTOR"))) {
        ++theta_config_generation;  // explicit write → always bump
        precond_theta_acoustic_factor = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 0.35f);
        std::cerr << "[CONFIG ENV] precond_theta_acoustic_factor = " << precond_theta_acoustic_factor << std::endl;
    }
    // v20.14r27z: U/V acoustic diagonal vertical blend fraction
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_UV_VERTICAL_FRACTION"))) {
        precond_uv_vertical_fraction = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_uv_vertical_fraction = "
                  << precond_uv_vertical_fraction << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_UV_VFRAC_WARMUP_START"))) {
        uv_vfrac_warmup_start = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] uv_vfrac_warmup_start = "
                  << uv_vfrac_warmup_start << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_REFINEMENT_PASSES"))) {
        precond_refinement_passes = std::clamp(std::atoi(env_val), 1, 5);
        std::cerr << "[CONFIG ENV] precond_refinement_passes = "
                  << precond_refinement_passes << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_COUPLED_PHI_W"))) {
        precond_coupled_phi_w = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_coupled_phi_w = "
                  << (precond_coupled_phi_w ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_COUPLING_SCALE"))) {
        precond_phi_w_coupling_scale = std::max(0, std::min(2, std::atoi(env_val)));
        std::cerr << "[CONFIG ENV] precond_phi_w_coupling_scale = "
                  << precond_phi_w_coupling_scale << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_RELAX"))) {
        precond_phi_feedback_relax = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_phi_feedback_relax = "
                  << precond_phi_feedback_relax << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_GS_AWPHI_CAP_COUPLED"))) {
        precond_gs_awphi_cap_coupled = static_cast<float>(std::atof(env_val));
        std::cerr << "[CONFIG ENV] precond_gs_awphi_cap_coupled = "
                  << precond_gs_awphi_cap_coupled << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_BETA"))) {
        precond_phi_feedback_beta = static_cast<float>(std::atof(env_val));
        std::cerr << "[CONFIG ENV] precond_phi_feedback_beta = "
                  << precond_phi_feedback_beta << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_CAP"))) {
        precond_phi_feedback_cap = static_cast<float>(std::atof(env_val));
        std::cerr << "[CONFIG ENV] precond_phi_feedback_cap = "
                  << precond_phi_feedback_cap << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_DW_NOSBOOST_FLOOR"))) {
        precond_dw_nosboost_floor = static_cast<float>(std::atof(env_val));
        std::cerr << "[CONFIG ENV] precond_dw_nosboost_floor = "
                  << precond_dw_nosboost_floor << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_MAX_CORR"))) {
        precond_phi_feedback_max_corr = static_cast<float>(std::atof(env_val));
        std::cerr << "[CONFIG ENV] precond_phi_feedback_max_corr = "
                  << precond_phi_feedback_max_corr << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_FALLBACK_GS"))) {
        precond_phi_feedback_fallback_gs = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_phi_feedback_fallback_gs = "
                  << (precond_phi_feedback_fallback_gs ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_CAP_HIGH"))) {
        precond_phi_feedback_cap_high = static_cast<float>(std::atof(env_val));
        std::cerr << "[CONFIG ENV] precond_phi_feedback_cap_high = "
                  << precond_phi_feedback_cap_high << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_GS_AWPHI_CAP_COUPLED_HIGH"))) {
        precond_gs_awphi_cap_coupled_high = static_cast<float>(std::atof(env_val));
        std::cerr << "[CONFIG ENV] precond_gs_awphi_cap_coupled_high = "
                  << precond_gs_awphi_cap_coupled_high << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_SOFT_CAP"))) {
        precond_phi_feedback_soft_cap = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_phi_feedback_soft_cap = "
                  << (precond_phi_feedback_soft_cap ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_CAP_MODE"))) {
        precond_phi_feedback_cap_mode = std::clamp(std::atoi(env_val), 0, 2);
        std::cerr << "[CONFIG ENV] precond_phi_feedback_cap_mode = "
                  << precond_phi_feedback_cap_mode
                  << " (0=iter, 1=ratio, 2=combined)" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_STAGE_SCALE"))) {
        precond_phi_feedback_stage_scale = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_phi_feedback_stage_scale = "
                  << precond_phi_feedback_stage_scale << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_DW_BLEND"))) {
        precond_phi_feedback_dw_blend = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_phi_feedback_dw_blend = "
                  << precond_phi_feedback_dw_blend << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_FEEDBACK_PASSES"))) {
        precond_phi_feedback_passes = std::clamp(std::atoi(env_val), 1, 5);
        std::cerr << "[CONFIG ENV] precond_phi_feedback_passes = "
                  << precond_phi_feedback_passes << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_BOOST_CAP"))) {
        precond_phi_w_schur_boost_cap = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 10.0f);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_boost_cap = "
                  << precond_phi_w_schur_boost_cap << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_BACKSUB_RELAX"))) {
        precond_phi_w_schur_backsub_relax = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_backsub_relax = "
                  << precond_phi_w_schur_backsub_relax << std::endl;
    }
    // v20.14 r47c: Phase 2 ablation flags
    // r47c-fix2: Use shared Fortran bool parser (supports .true./TRUE/yes/1)
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_BOOST_ON"))) {
        precond_phi_w_schur_boost_on = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_boost_on = "
                  << precond_phi_w_schur_boost_on << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_RHS_INJECT_ON"))) {
        precond_phi_w_schur_rhs_inject_on = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_rhs_inject_on = "
                  << precond_phi_w_schur_rhs_inject_on << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_BACKSUB_ON"))) {
        precond_phi_w_schur_backsub_on = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_backsub_on = "
                  << precond_phi_w_schur_backsub_on << std::endl;
    }
    // v20.14 r47c-fix2: Phase 2 cross-coupling threshold
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_CROSS_THRESH"))) {
        precond_phi_w_schur_cross_thresh = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 100.0f);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_cross_thresh = "
                  << precond_phi_w_schur_cross_thresh << (precond_phi_w_schur_cross_thresh == 0.0f ? " (disabled)" : "") << std::endl;
    }
    // v20.14 r47c-fix2: N3 collapse adaptive mechanisms
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_CAP_DECAY"))) {
        precond_phi_w_schur_cap_decay = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_cap_decay = "
                  << precond_phi_w_schur_cap_decay << (precond_phi_w_schur_cap_decay >= 0.999f ? " (disabled)" : "") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_RU_SCALE"))) {
        precond_phi_w_schur_ru_scale = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 10.0f);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_ru_scale = "
                  << precond_phi_w_schur_ru_scale << (precond_phi_w_schur_ru_scale == 0.0f ? " (disabled)" : "") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_RU_THRESH"))) {
        precond_phi_w_schur_ru_thresh = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_ru_thresh = "
                  << precond_phi_w_schur_ru_thresh << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_ALPHA_GS_START"))) {
        precond_phi_w_schur_alpha_gs_start = std::clamp(std::atoi(env_val), 0, 20);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_alpha_gs_start = "
                  << precond_phi_w_schur_alpha_gs_start << (precond_phi_w_schur_alpha_gs_start == 0 ? " (always)" : "") << std::endl;
    }
    // v20.14 r47c-fix3: D_u weak scaling for ru-dominated residuals
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_DU_WEAK_FACTOR"))) {
        precond_du_weak_factor = std::clamp(static_cast<float>(std::atof(env_val)), 0.001f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_du_weak_factor = " << precond_du_weak_factor
                  << (precond_du_weak_factor >= 0.999f ? " (disabled)" : "") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_DU_WEAK_RU_THRESH"))) {
        precond_du_weak_ru_thresh = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_du_weak_ru_thresh = " << precond_du_weak_ru_thresh << std::endl;
    }
    // v20.14 r49: Smoothing
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_HORIZONTAL_SMOOTH_ALPHA"))) {
        precond_horizontal_smooth_alpha = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 0.25f);
        std::cerr << "[CONFIG ENV] precond_horizontal_smooth_alpha = " << precond_horizontal_smooth_alpha << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_HORIZONTAL_SMOOTH_ITERS"))) {
        precond_horizontal_smooth_iters = std::clamp(std::atoi(env_val), 1, 5);
        std::cerr << "[CONFIG ENV] precond_horizontal_smooth_iters = " << precond_horizontal_smooth_iters << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_VERTICAL_SMOOTH_ALPHA"))) {
        precond_vertical_smooth_alpha = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_vertical_smooth_alpha = " << precond_vertical_smooth_alpha << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_SMOOTH_BOUNDARY_GUARD"))) {
        precond_smooth_boundary_guard = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_smooth_boundary_guard = "
                  << (precond_smooth_boundary_guard ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_PHI_W_SCHUR_ALPHA_GS"))) {
        precond_phi_w_schur_alpha_gs = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 2.0f);
        std::cerr << "[CONFIG ENV] precond_phi_w_schur_alpha_gs = "
                  << precond_phi_w_schur_alpha_gs << std::endl;
    }
    // v20.14r35: Newton convergence thresholds
    if ((env_val = std::getenv("WRF_SDIRK3_NEAR_FAIL_FLOOR"))) {
        near_fail_floor = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 0.1f);
        std::cerr << "[CONFIG ENV] near_fail_floor = " << near_fail_floor << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_NEWTON_STALL_THRESHOLD"))) {
        newton_stall_threshold = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 0.1f);
        std::cerr << "[CONFIG ENV] newton_stall_threshold = " << newton_stall_threshold << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_CATASTROPHIC_REL_CAP"))) {
        catastrophic_rel_cap = std::max(100.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] catastrophic_rel_cap = " << catastrophic_rel_cap << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_EXPLOSION_ABS_FLOOR"))) {
        explosion_abs_floor = std::max(1.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] explosion_abs_floor = " << explosion_abs_floor << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_EXPLOSION_REL_MULTIPLIER"))) {
        explosion_rel_multiplier = std::max(1.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] explosion_rel_multiplier = " << explosion_rel_multiplier << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_FD_CONSISTENCY_SAMPLES"))) {
        fd_consistency_samples = std::clamp(std::atoi(env_val), 0, 10);
        std::cerr << "[CONFIG ENV] fd_consistency_samples = " << fd_consistency_samples << std::endl;
    }
    // v20.14r36: Additional gate/limit thresholds
    if ((env_val = std::getenv("WRF_SDIRK3_CATASTROPHIC_ABS_FLOOR"))) {
        catastrophic_abs_floor = std::max(0.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] catastrophic_abs_floor = " << catastrophic_abs_floor << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_NEWTON_ZERO_STEP_STALL_LIMIT"))) {
        newton_zero_step_stall_limit = std::clamp(std::atoi(env_val), 1, 20);
        std::cerr << "[CONFIG ENV] newton_zero_step_stall_limit = " << newton_zero_step_stall_limit << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_GS_AWPHI_CAP"))) {
        precond_gs_awphi_cap = std::max(0.01f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] precond_gs_awphi_cap = " << precond_gs_awphi_cap << std::endl;
    }
    // v20.14r38: Stage gate/damp thresholds
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE_GATE_REL_THRESHOLD"))) {
        stage_gate_rel_threshold = std::max(1.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] stage_gate_rel_threshold = " << stage_gate_rel_threshold << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE3_GATE_REL_THRESHOLD"))) {
        stage3_gate_rel_threshold = std::max(0.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] stage3_gate_rel_threshold = " << stage3_gate_rel_threshold
                  << " (0=use stage_gate_rel_threshold)" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE_DAMP_REL_THRESHOLD"))) {
        stage_damp_rel_threshold = std::max(1.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] stage_damp_rel_threshold = " << stage_damp_rel_threshold << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE_GATE_GROWTH_CAP"))) {
        stage_gate_growth_cap = std::max(1.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] stage_gate_growth_cap = " << stage_gate_growth_cap << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE_GATE_K_FLOOR"))) {
        stage_gate_K_floor = std::max(0.01f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] stage_gate_K_floor = " << stage_gate_K_floor << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_GATE_METRIC_MODE"))) {
        gate_metric_mode = std::clamp(std::atoi(env_val), 0, 2);
        std::cerr << "[CONFIG ENV] gate_metric_mode = " << gate_metric_mode
                  << " (0=wrms_growth, 1=scaled_abs, 2=legacy_rel)" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_TRUST_REGION_MAX_REL_UPDATE"))) {
        trust_region_max_relative_update = std::max(0.1f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] trust_region_max_relative_update = " << trust_region_max_relative_update << std::endl;
    }
    // v20.14 r49: Block-aware trust-region
    if ((env_val = std::getenv("WRF_SDIRK3_TRUST_REGION_BLOCK_AWARE_THRESH"))) {
        trust_region_block_aware_thresh = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] trust_region_block_aware_thresh = " << trust_region_block_aware_thresh << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_TRUST_FALLBACK_RELAX"))) {
        trust_fallback_relax = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] trust_fallback_relax = "
                  << (trust_fallback_relax ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_TRUST_FALLBACK_RATIO"))) {
        trust_fallback_ratio = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] trust_fallback_ratio = " << trust_fallback_ratio << std::endl;
    }
    // v20.14 r49-fix: Direct U Solve
    if ((env_val = std::getenv("WRF_SDIRK3_DIRECT_U_SOLVE_THRESH"))) {
        direct_u_solve_thresh = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] direct_u_solve_thresh = " << direct_u_solve_thresh << std::endl;
    }
    // v20.14 r50: GMRES block-scaling
    if ((env_val = std::getenv("WRF_SDIRK3_GMRES_BLOCK_SCALE"))) {
        gmres_block_scale = std::atoi(env_val);
        std::cerr << "[CONFIG ENV] gmres_block_scale = " << gmres_block_scale << std::endl;
    }
    // v20.14r67/P2: Stagnation gate (opt-in, default OFF)
    if ((env_val = std::getenv("WRF_SDIRK3_STAGNATION_GATE_ENABLE"))) {
        stagnation_gate_enable = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] stagnation_gate_enable = "
                  << (stagnation_gate_enable ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGNATION_GROWTH_FLOOR"))) {
        stagnation_growth_floor = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 10.0f);
        std::cerr << "[CONFIG ENV] stagnation_growth_floor = " << stagnation_growth_floor << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE_REQUIRE_CONVERGENCE"))) {
        stage_require_convergence = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] stage_require_convergence = "
                  << (stage_require_convergence ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_HEVI_SPLIT"))) {
        hevi_split = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] hevi_split = " << (hevi_split ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SPLIT_EXPLICIT"))) {
        split_explicit = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] split_explicit = " << (split_explicit ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SPLIT_EXPLICIT_TIME_STEP_SOUND"))) {
        split_explicit_time_step_sound = std::clamp(std::atoi(env_val), 0, 1000);
        std::cerr << "[CONFIG ENV] split_explicit_time_step_sound = " << split_explicit_time_step_sound << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SPLIT_EXPLICIT_EPSSM"))) {
        split_explicit_epssm = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] split_explicit_epssm = " << split_explicit_epssm << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SPLIT_EXPLICIT_SMDIV"))) {
        split_explicit_smdiv = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] split_explicit_smdiv = " << split_explicit_smdiv << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SPLIT_EXPLICIT_EMDIV"))) {
        split_explicit_emdiv = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] split_explicit_emdiv = " << split_explicit_emdiv << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SPLIT_EXPLICIT_TOP_LID"))) {
        split_explicit_top_lid = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] split_explicit_top_lid = " << (split_explicit_top_lid ? "true" : "false") << std::endl;
    }
    // v20.14r27q: Φ→W GS damping coefficient
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_GS_BETA"))) {
        precond_gs_beta = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_gs_beta = " << precond_gs_beta << std::endl;
    }
    // v20.14r27j: Preconditioner relaxation α₀
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_RELAXATION"))) {
        precond_relaxation = std::clamp(static_cast<float>(std::atof(env_val)), 0.0f, 1.0f);
        std::cerr << "[CONFIG ENV] precond_relaxation = " << precond_relaxation << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_NK_TRUST_REGION"))) {
        nk_trust_region = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] nk_trust_region = " << (nk_trust_region ? "true" : "false") << std::endl;
    }
    // v20.14r27h: Trust-region initial radius
    if ((env_val = std::getenv("WRF_SDIRK3_NK_TRUST_RADIUS"))) {
        nk_trust_radius = std::max(0.01f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] nk_trust_radius = " << nk_trust_radius << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_NK_LINE_SEARCH_ALPHA"))) {
        nk_line_search_alpha = std::clamp(static_cast<float>(std::atof(env_val)), 1e-6f, 0.5f);
        std::cerr << "[CONFIG ENV] nk_line_search_alpha = " << nk_line_search_alpha << std::endl;
    }
    // v20.14: Adaptive tuning constants
    if ((env_val = std::getenv("WRF_SDIRK3_ADAPTIVE_HIGH_THRESHOLD"))) {
        adaptive_high_threshold = static_cast<float>(std::atof(env_val));
        normalize_adaptive_thresholds();
        std::cerr << "[CONFIG ENV] adaptive_high_threshold = " << adaptive_high_threshold << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_ADAPTIVE_LOW_THRESHOLD"))) {
        adaptive_low_threshold = static_cast<float>(std::atof(env_val));
        normalize_adaptive_thresholds();
        std::cerr << "[CONFIG ENV] adaptive_low_threshold = " << adaptive_low_threshold << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_ADAPTIVE_STEP_SIZE"))) {
        adaptive_step_size = std::clamp(static_cast<float>(std::atof(env_val)), 0.001f, 0.1f);
        std::cerr << "[CONFIG ENV] adaptive_step_size = " << adaptive_step_size << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_ADAPTIVE_QUALITY_GATE"))) {
        adaptive_quality_gate = std::clamp(static_cast<float>(std::atof(env_val)), 0.5f, 1.0f);
        std::cerr << "[CONFIG ENV] adaptive_quality_gate = " << adaptive_quality_gate << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_ADAPTIVE_RETUNE_MODE"))) {
        adaptive_retune_mode = std::clamp(std::atoi(env_val), 0, 2);
        std::cerr << "[CONFIG ENV] adaptive_retune_mode = " << adaptive_retune_mode << std::endl;
    }
    // v20.9: sign_smooth_delta and gmres_stagnation_threshold via environment
    if ((env_val = std::getenv("WRF_SDIRK3_SIGN_SMOOTH_DELTA"))) {
        sign_smooth_delta = std::max(1e-6f, static_cast<float>(std::atof(env_val)));  // v20.14r27z: enforce > 0
        std::cerr << "[CONFIG ENV] sign_smooth_delta = " << sign_smooth_delta << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_GMRES_STAGNATION_THRESHOLD"))) {
        gmres_stagnation_threshold = std::clamp(static_cast<float>(std::atof(env_val)), 0.5f, 1.0f);
        std::cerr << "[CONFIG ENV] gmres_stagnation_threshold = "
                  << gmres_stagnation_threshold << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_NEWTON_GMRES_QUALITY_THRESHOLD"))) {
        newton_gmres_quality_threshold = std::clamp(static_cast<float>(std::atof(env_val)), 0.5f, 1.0f);
        std::cerr << "[CONFIG ENV] newton_gmres_quality_threshold = "
                  << newton_gmres_quality_threshold << std::endl;
    }
    // v20.14r26: Newton non-convergence policy
    if ((env_val = std::getenv("WRF_SDIRK3_HARD_ABORT_ON_NEWTON_FAIL"))) {
        hard_abort_on_newton_fail = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] hard_abort_on_newton_fail = "
                  << (hard_abort_on_newton_fail ? "true" : "false") << std::endl;
    }
    // v20.14r27e/r51: Stage non-convergence policy
    if ((env_val = std::getenv("WRF_SDIRK3_STAGE_FAIL_ACTION"))) {
        stage_fail_action = std::clamp(std::atoi(env_val), 0, 2);
        std::cerr << "[CONFIG ENV] stage_fail_action = " << stage_fail_action
                  << " (0=continue, 1=abort, 2=retry_once_then_abort)" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MODE3_STAGE4_SEVERE_ABORT"))) {
        mode3_stage4_severe_abort = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] mode3_stage4_severe_abort = "
                  << (mode3_stage4_severe_abort ? "true" : "false")
                  << " (false skips severe_nonconv abort at stage>=4 in mode3)" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MODE3_RETRY_NAN_SANITIZE"))) {
        mode3_retry_nan_sanitize = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] mode3_retry_nan_sanitize = "
                  << (mode3_retry_nan_sanitize ? "true" : "false")
                  << " (stage_fail_action=2 retry only)" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MODE3_FIRSTHIT_PROBE"))) {
        mode3_firsthit_probe = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] mode3_firsthit_probe = "
                  << (mode3_firsthit_probe ? "true" : "false")
                  << " (mode3 one-shot non-finite origin probe)" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MODE3_RETRY_STAGE4_PRECLIP"))) {
        mode3_retry_stage4_preclip = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] mode3_retry_stage4_preclip = "
                  << (mode3_retry_stage4_preclip ? "true" : "false")
                  << " (apply clip at stage4 only when previous stage used retry)" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MODE3_RETRY_STAGE4_CLIP_ABS"))) {
        mode3_retry_stage4_clip_abs = std::max(1.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] mode3_retry_stage4_clip_abs = "
                  << mode3_retry_stage4_clip_abs << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MODE3_RETRY_HANDOFF_CLIP"))) {
        mode3_retry_handoff_clip = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] mode3_retry_handoff_clip = "
                  << (mode3_retry_handoff_clip ? "true" : "false")
                  << " (apply clip to retried-stage k_fast handoff)" << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_MODE3_RETRY_HANDOFF_CLIP_ABS"))) {
        mode3_retry_handoff_clip_abs = std::max(1.0f, static_cast<float>(std::atof(env_val)));
        std::cerr << "[CONFIG ENV] mode3_retry_handoff_clip_abs = "
                  << mode3_retry_handoff_clip_abs << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_HOPELESS_RELAX"))) {
        hopeless_relax = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] hopeless_relax = " << (hopeless_relax ? "true" : "false") << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_PRECOND_RATIO_GUARD_WARN_ONLY"))) {
        precond_ratio_guard_warn_only = parse_bool_env(env_val);
        std::cerr << "[CONFIG ENV] precond_ratio_guard_warn_only = "
                  << (precond_ratio_guard_warn_only ? "true" : "false") << std::endl;
    }
    // v20.14r27f: RHS NaN sanitizer mode
    if ((env_val = std::getenv("WRF_SDIRK3_NAN_SANITIZE_MODE"))) {
        nan_sanitize_mode = std::clamp(std::atoi(env_val), 0, 2);
        std::cerr << "[CONFIG ENV] nan_sanitize_mode = " << nan_sanitize_mode
                  << " (0=report-only, 1=sanitize)" << std::endl;
    }
    // v20.14r27f: GMRES variable scaling floors
    if ((env_val = std::getenv("WRF_SDIRK3_SCALE_U"))) {
        scale_u = std::max(0.01f, std::stof(env_val));
        std::cerr << "[CONFIG ENV] scale_u = " << scale_u << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SCALE_PH"))) {
        scale_ph = std::max(0.01f, std::stof(env_val));
        std::cerr << "[CONFIG ENV] scale_ph = " << scale_ph << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SCALE_T"))) {
        scale_t = std::max(0.01f, std::stof(env_val));
        std::cerr << "[CONFIG ENV] scale_t = " << scale_t << std::endl;
    }
    if ((env_val = std::getenv("WRF_SDIRK3_SCALE_MU"))) {
        scale_mu = std::max(0.01f, std::stof(env_val));
        std::cerr << "[CONFIG ENV] scale_mu = " << scale_mu << std::endl;
    }

    // v20.14r27s: Effective config summary (fires once at startup).
    // Shows final values after namelist + env override resolution.
    std::cerr << "[CONFIG EFFECTIVE] precond: gs_beta=" << precond_gs_beta
              << ", theta_factor=" << precond_theta_acoustic_factor
              << ", w_boost=" << precond_w_acoustic_boost
              << ", uv_vfrac=" << precond_uv_vertical_fraction
              << ", warmup_start=" << uv_vfrac_warmup_start
              << ", refinement=" << precond_refinement_passes
              << ", relaxation=" << precond_relaxation
              << ", coupled_phi_w=" << (precond_coupled_phi_w ? "ON" : "off")
              << (precond_coupled_phi_w ? (std::string(", coupling_scale=")
                  + std::to_string(precond_phi_w_coupling_scale)
                  + ", relax=" + std::to_string(precond_phi_feedback_relax)
                  + ", fb_beta=" + std::to_string(precond_phi_feedback_beta)
                  + ", fb_cap=" + (precond_phi_feedback_cap < 0 ? "off" : std::to_string(precond_phi_feedback_cap))
                  + ", dw_floor=" + std::to_string(precond_dw_nosboost_floor)
                  + ", max_corr=" + (precond_phi_feedback_max_corr < 0 ? "off" : std::to_string(precond_phi_feedback_max_corr))
                  + ", fallback_gs=" + (precond_phi_feedback_fallback_gs ? "ON" : "off")
                  + ", soft_cap=" + (precond_phi_feedback_soft_cap ? "tanh" : "hard")
                  + ", fb_cap_hi=" + (precond_phi_feedback_cap_high < 0 ? "off" : std::to_string(precond_phi_feedback_cap_high))
                  + ", gs_cap_hi=" + (precond_gs_awphi_cap_coupled_high < 0 ? "off" : std::to_string(precond_gs_awphi_cap_coupled_high))
                  + ", gs_cap_coupled=" + (precond_gs_awphi_cap_coupled < 0
                      ? "base(" + std::to_string(precond_gs_awphi_cap) + ")"
                      : std::to_string(precond_gs_awphi_cap_coupled))
                  + ", dw_blend=" + std::to_string(precond_phi_feedback_dw_blend)
                  + ", passes=" + std::to_string(precond_phi_feedback_passes)
                  + ", cap_mode=" + std::to_string(precond_phi_feedback_cap_mode)
                  + ", stage_scale=" + std::to_string(precond_phi_feedback_stage_scale)
                  + ", schur_cap=" + std::to_string(precond_phi_w_schur_boost_cap)
                  + ", bs_relax=" + std::to_string(precond_phi_w_schur_backsub_relax)
                  + ", ablation=" + std::string(precond_phi_w_schur_boost_on ? "B" : "b")
                  + std::string(precond_phi_w_schur_rhs_inject_on ? "R" : "r")
                  + std::string(precond_phi_w_schur_backsub_on ? "S" : "s")
                  + ", alpha_gs=" + std::to_string(precond_phi_w_schur_alpha_gs)
                  + ", cross_thresh=" + std::to_string(precond_phi_w_schur_cross_thresh)
                  + ", cap_decay=" + std::to_string(precond_phi_w_schur_cap_decay)
                  + ", ru_scale=" + std::to_string(precond_phi_w_schur_ru_scale)
                  + "/" + std::to_string(precond_phi_w_schur_ru_thresh)
                  + ", gs_start=" + std::to_string(precond_phi_w_schur_alpha_gs_start)) : "")
              << ", du_weak=" << precond_du_weak_factor
              << "/" << precond_du_weak_ru_thresh
              << (precond_du_weak_factor < 0.999f ? " (dual-phase)" : "")
              << std::endl;
    std::cerr << "[CONFIG EFFECTIVE] stagnation_gate=" << (stagnation_gate_enable ? "ON" : "off")
              << ", growth_floor=" << stagnation_growth_floor << std::endl;
    std::cerr << "[CONFIG EFFECTIVE] stage_require_convergence="
              << (stage_require_convergence ? "ON (retry-less converged=0 -> fail-close)" : "off (legacy soft-continue)")
              << std::endl;
    std::cerr << "[CONFIG EFFECTIVE] hevi_split="
              << (hevi_split ? "ON (horizontal-acoustic explicit, vertical implicit)" : "off (full implicit)")
              << std::endl;
    std::cerr << "[CONFIG EFFECTIVE] split_explicit="
              << (split_explicit ? "ON (WIP RK3 + acoustic-substep core)" : "off (ARK324 implicit)")
              << std::endl;
    if (split_explicit) {
        std::cerr << "[CONFIG EFFECTIVE] split_explicit acoustic: time_step_sound="
                  << split_explicit_time_step_sound
                  << (split_explicit_time_step_sound <= 0
                          ? " (UNSUPPORTED: runtime guard requires explicit even >= 4; "
                            "WRF's 0=auto formula is not implemented)"
                          : "")
                  << ", epssm=" << split_explicit_epssm
                  << ", smdiv=" << split_explicit_smdiv
                  << ", emdiv=" << split_explicit_emdiv
                  << ", top_lid=" << (split_explicit_top_lid ? "on" : "off") << std::endl;
    }
    std::cerr << "[CONFIG EFFECTIVE] solver: gmres_restart=" << gmres_restart
              << ", max_krylov=" << max_krylov_iter
              << ", max_newton=" << max_newton_iter
              << ", stage_fail=" << stage_fail_action
              << ", gate_metric_mode=" << gate_metric_mode
              << ", s4_severe_abort=" << (mode3_stage4_severe_abort ? "on" : "off")
              << ", retry_nan_sanitize=" << (mode3_retry_nan_sanitize ? "on" : "off")
              << ", firsthit_probe=" << (mode3_firsthit_probe ? "on" : "off")
              << ", s4_preclip=" << (mode3_retry_stage4_preclip ? "on" : "off")
              << ", s4_clip_abs=" << mode3_retry_stage4_clip_abs
              << ", retry_handoff_clip=" << (mode3_retry_handoff_clip ? "on" : "off")
              << ", retry_handoff_clip_abs=" << mode3_retry_handoff_clip_abs
              << ", jvp_block_eps=" << (jvp_block_epsilon ? "ON(EXPERIMENTAL)" : "off")
              << ", true_res=" << gmres_true_residual_start_j << "+" << gmres_true_residual_period
              << ", arnoldi_stag=" << gmres_arnoldi_stag_window << "/" << gmres_arnoldi_stag_ratio
              << ", mixed_fd=" << jvp_mixed_fd_newton_switch
              << std::endl;
    std::cerr << "[CONFIG EFFECTIVE] thresholds: near_fail=" << near_fail_floor
              << ", stall=" << newton_stall_threshold
              << ", catastrophic_cap=" << catastrophic_rel_cap << "/abs" << catastrophic_abs_floor
              << ", explosion=" << explosion_abs_floor << "/" << explosion_rel_multiplier
              << ", fd_samples=" << fd_consistency_samples
              << std::endl;
    std::cerr << "[CONFIG EFFECTIVE] limits: zero_step_stall=" << newton_zero_step_stall_limit
              << ", gs_awphi_cap=" << precond_gs_awphi_cap
              << ", stage_gate=" << stage_gate_rel_threshold
              << ", stage3_gate=" << stage3_gate_rel_threshold
              << ", stage_damp=" << stage_damp_rel_threshold
              << ", growth_cap=" << stage_gate_growth_cap
              << ", K_floor=" << stage_gate_K_floor
              << ", gate_metric_mode=" << gate_metric_mode
              << ", tr_max_rel=" << trust_region_max_relative_update
              << std::endl;
    std::cerr << "[CONFIG EFFECTIVE] r49: stage2_budget="
              << stage2_gmres_restart << "/" << stage2_max_krylov_restarts << "/" << stage2_krylov_tol
              << ", stage2_ew=" << stage2_ew_eta_min << "/" << stage2_ew_eta_max
              << ", stage3_budget="
              << stage3_gmres_restart << "/" << stage3_max_krylov_restarts << "/" << stage3_krylov_tol
              << ", stage3_ew=" << stage3_ew_eta_min << "/" << stage3_ew_eta_max
              << ", stage3_warmstart=" << (stage3_warmstart ? "on" : "off")
              << ", hopeless_relax=" << (hopeless_relax ? "on" : "off")
              << ", precond_guard_warn_only=" << (precond_ratio_guard_warn_only ? "on" : "off")
              << ", jvp_bench=" << jvp_auto_bench_calls
              << ", jvp_bench_qgate=" << (jvp_auto_bench_quality_gate ? "on" : "off")
              << ", jvp_bench_warmup=" << jvp_auto_bench_warmup
              << ", jvp_bench_seed=" << jvp_auto_bench_seed
              << ", jvp_bench_reset_s1=" << (jvp_auto_bench_lock_reset_stage1 ? "on" : "off")
              << ", solver_tel=" << (solver_telemetry ? "on" : "off")
              << ", p_inv=" << (progress_invariant_enable ? "on" : "off")
              << "/" << progress_invariant_min_ratio
              << "/" << progress_invariant_streak_limit
              << ", varpc_log=" << (variable_pc_event_log ? "on" : "off")
              << ", gmres_warmstart=" << (gmres_warmstart ? "on" : "off")
              << ", gmres_ws_gate=" << gmres_warmstart_quality_gate
              << ", inn_ws=" << (inn_warmstart_enable ? "on" : "off")
              << ", inn_gate=" << (inn_residual_gate_enable ? "on" : "off")
              << ", inn_qmin=" << inn_q_min
              << ", inn_gate_rel_tol=" << inn_gate_rel_tol
              << ", inn_gate_q_noise=" << inn_gate_q_noise
              << ", inn_beta=" << inn_beta_max
              << ", inn_ood=" << (inn_ood_guard_enable ? "on" : "off")
              << ", inn_tol_ramp=" << (inn_tol_ramp_enable ? "on" : "off")
              << "/" << inn_tol_ramp_gamma
              << ", rhs_bc_parity=" << (rhs_bc_parity ? "on" : "off")
              << ", mass_pgf_bc_guard=" << (mass_pgf_bc_guard ? "on" : "off")
              << ", stage_bsync=" << (stage_boundary_sync ? "on" : "off")
              << ", h_smooth=" << precond_horizontal_smooth_alpha << "/" << precond_horizontal_smooth_iters
              << ", v_smooth=" << precond_vertical_smooth_alpha
              << ", smooth_bdy_guard=" << (precond_smooth_boundary_guard ? "on" : "off")
              << ", block_tr=" << trust_region_block_aware_thresh
              << ", tr_fb_relax=" << (trust_fallback_relax ? "on" : "off")
              << ", tr_fb_ratio=" << trust_fallback_ratio
              << ", direct_u=" << direct_u_solve_thresh
              << ", blk_scale=" << gmres_block_scale
              << std::endl;
}

void SDIRK3Config::normalize_adaptive_thresholds() {
    adaptive_high_threshold = std::clamp(adaptive_high_threshold, 0.15f, 0.9f);
    adaptive_low_threshold = std::clamp(adaptive_low_threshold, 0.1f, 0.85f);
    if (adaptive_high_threshold - adaptive_low_threshold < 0.05f) {
        // Center the gap around the midpoint of the two values
        float mid = (adaptive_high_threshold + adaptive_low_threshold) * 0.5f;
        adaptive_high_threshold = std::clamp(mid + 0.025f, 0.15f, 0.9f);
        adaptive_low_threshold = std::clamp(mid - 0.025f, 0.1f, 0.85f);
    }
}

bool SDIRK3Config::validate() const {
    bool valid = true;

    if (max_newton_iter < 1 || max_newton_iter > 100) {
        std::cerr << "SDIRK3 Config Error: max_newton_iter must be between 1 and 100" << std::endl;
        valid = false;
    }
    
    if (newton_tol <= 0.0f || newton_tol > 1.0f) {
        std::cerr << "SDIRK3 Config Error: newton_tol must be between 0 and 1" << std::endl;
        valid = false;
    }
    
    if (gmres_restart < 1 || gmres_restart > 1000) {
        std::cerr << "SDIRK3 Config Error: gmres_restart must be between 1 and 1000" << std::endl;
        valid = false;
    }
    // v20.14r48: GMRES performance params (validate is const → const_cast for clamping)
    {
        auto* self = const_cast<SDIRK3Config*>(this);
        self->gmres_true_residual_start_j = std::clamp(gmres_true_residual_start_j, 0, 1000);
        self->gmres_true_residual_period = std::clamp(gmres_true_residual_period, 1, 100);
        self->gmres_arnoldi_stag_window = std::clamp(gmres_arnoldi_stag_window, 1, 20);
        self->gmres_arnoldi_stag_ratio = std::clamp(gmres_arnoldi_stag_ratio, 0.5f, 1.0f);
        // v20.14 r49/r59
        self->stage2_gmres_restart = std::clamp(stage2_gmres_restart, 0, 100);
        self->stage2_max_krylov_restarts = std::clamp(stage2_max_krylov_restarts, 0, 20);
        self->stage2_krylov_tol = std::clamp(stage2_krylov_tol, 0.0f, 1.0f);
        self->stage2_ew_eta_min = std::clamp(stage2_ew_eta_min, 0.0f, 1.0f);
        self->stage2_ew_eta_max = std::clamp(stage2_ew_eta_max, 0.0f, 1.0f);
        if (self->stage2_ew_eta_min > 0.0f && self->stage2_ew_eta_max > 0.0f &&
            self->stage2_ew_eta_max < self->stage2_ew_eta_min) {
            std::swap(self->stage2_ew_eta_min, self->stage2_ew_eta_max);
        }
        self->stage3_gmres_restart = std::clamp(stage3_gmres_restart, 0, 100);
        self->stage3_max_krylov_restarts = std::clamp(stage3_max_krylov_restarts, 0, 20);
        self->stage3_krylov_tol = std::clamp(stage3_krylov_tol, 0.0f, 1.0f);
        self->stage3_ew_eta_min = std::clamp(stage3_ew_eta_min, 0.0f, 1.0f);
        self->stage3_ew_eta_max = std::clamp(stage3_ew_eta_max, 0.0f, 1.0f);
        if (self->stage3_ew_eta_min > 0.0f && self->stage3_ew_eta_max > 0.0f &&
            self->stage3_ew_eta_max < self->stage3_ew_eta_min) {
            std::swap(self->stage3_ew_eta_min, self->stage3_ew_eta_max);
        }
        self->stage_fail_action = std::clamp(stage_fail_action, 0, 2);
        self->mode3_stage4_severe_abort = mode3_stage4_severe_abort;
        self->mode3_retry_nan_sanitize = mode3_retry_nan_sanitize;
        self->mode3_firsthit_probe = mode3_firsthit_probe;
        self->mode3_retry_stage4_preclip = mode3_retry_stage4_preclip;
        self->mode3_retry_stage4_clip_abs = std::max(mode3_retry_stage4_clip_abs, 1.0f);
        self->mode3_retry_handoff_clip = mode3_retry_handoff_clip;
        self->mode3_retry_handoff_clip_abs = std::max(mode3_retry_handoff_clip_abs, 1.0f);
        // Split-explicit core (opt-in bool, no range). Acknowledged here for completeness;
        // it is inert unless effective_imex_split_mode()==3 (WIP scaffold, Inc 0).
        self->split_explicit = split_explicit;
        self->split_explicit_time_step_sound = std::clamp(split_explicit_time_step_sound, 0, 1000);
        self->split_explicit_epssm = std::clamp(split_explicit_epssm, 0.0f, 1.0f);
        self->split_explicit_smdiv = std::clamp(split_explicit_smdiv, 0.0f, 1.0f);
        self->split_explicit_emdiv = std::clamp(split_explicit_emdiv, 0.0f, 1.0f);
        self->split_explicit_top_lid = split_explicit_top_lid;
        self->jvp_auto_bench_calls = std::clamp(jvp_auto_bench_calls, 0, 20);
        self->jvp_auto_bench_warmup = std::clamp(jvp_auto_bench_warmup, 0, 50);
        self->gmres_warmstart_quality_gate = std::clamp(gmres_warmstart_quality_gate, 0.0f, 1.0f);
        self->inn_q_min = std::clamp(inn_q_min, 0.0f, 1.0f);
        self->inn_gate_rel_tol = std::max(inn_gate_rel_tol, 0.0f);
        self->inn_gate_q_noise = std::max(inn_gate_q_noise, 0.0f);
        self->inn_beta_max = std::clamp(inn_beta_max, 0.0f, 1.0f);
        self->inn_tol_ramp_gamma = std::clamp(inn_tol_ramp_gamma, 1.0e-3f, 1.0f);
        self->progress_invariant_min_ratio = std::max(progress_invariant_min_ratio, 0.0f);
        self->progress_invariant_streak_limit = std::max(progress_invariant_streak_limit, 0);
        self->precond_horizontal_smooth_alpha = std::clamp(precond_horizontal_smooth_alpha, 0.0f, 0.25f);
        self->precond_horizontal_smooth_iters = std::clamp(precond_horizontal_smooth_iters, 1, 5);
        self->precond_vertical_smooth_alpha = std::clamp(precond_vertical_smooth_alpha, 0.0f, 1.0f);
        self->trust_region_block_aware_thresh = std::clamp(trust_region_block_aware_thresh, 0.0f, 1.0f);
        self->trust_fallback_ratio = std::clamp(trust_fallback_ratio, 0.0f, 1.0f);
        self->direct_u_solve_thresh = std::clamp(direct_u_solve_thresh, 0.0f, 1.0f);
        self->jvp_checkpoint_segments = std::clamp(jvp_checkpoint_segments, 1, 64);
        self->jvp_batch_size = std::clamp(jvp_batch_size, 1, 256);
        self->precond_ilu_level = std::clamp(precond_ilu_level, 0, 8);
        self->precond_mg_levels = std::clamp(precond_mg_levels, 1, 10);
        self->memory_pool_size_mb = std::clamp(memory_pool_size_mb, 16, 65536);
        self->checkpoint_interval = std::max(checkpoint_interval, 1);
        self->w_damp_alpha = std::clamp(w_damp_alpha, 0.0f, 2.0f);
        self->w_crit_cfl = std::clamp(w_crit_cfl, 0.1f, 10.0f);
        self->validation_level = std::clamp(validation_level, 0, 3);
        self->conservation_tol_mass = std::max(conservation_tol_mass, 1.0e-12f);
        self->conservation_tol_energy = std::max(conservation_tol_energy, 1.0e-12f);
        self->conservation_tol_momentum = std::max(conservation_tol_momentum, 1.0e-12f);
        self->benchmark_warmup_iter = std::max(benchmark_warmup_iter, 0);
        self->benchmark_measure_iter = std::max(benchmark_measure_iter, 1);
    }

    // v20.14r27x: Only 0 (none) and 2 (unified block-Jacobi) are implemented.
    // Types 1 (Jacobi) and 3 (ILU/multigrid) silently fall through to no-preconditioner,
    // which is a hidden performance trap.
    if (precond_type != 0 && precond_type != 2) {
        std::cerr << "SDIRK3 Config Error: precond_type=" << precond_type
                  << " is not implemented. Valid values: 0 (none), 2 (unified block-Jacobi)."
                  << std::endl;
        valid = false;
    }
    
    if (n_threads < 0 || n_threads > 128) {
        std::cerr << "SDIRK3 Config Error: n_threads must be between 0 (auto-detect) and 128" << std::endl;
        valid = false;
    }

    if (checkpoint_interval < 1) {
        std::cerr << "SDIRK3 Config Error: checkpoint_interval must be >= 1 (got "
                  << checkpoint_interval << ")" << std::endl;
        valid = false;
    }

    if (kdamp < 0.0f) {
        std::cerr << "SDIRK3 Config Error: kdamp must be >= 0 (got "
                  << kdamp << ")" << std::endl;
        valid = false;
    }

    if (conservation_tol_mass <= 0.0f || conservation_tol_energy <= 0.0f ||
        conservation_tol_momentum <= 0.0f) {
        std::cerr << "SDIRK3 Config Error: conservation tolerances must be > 0 (got "
                  << conservation_tol_mass << ", " << conservation_tol_energy << ", "
                  << conservation_tol_momentum << ")" << std::endl;
        valid = false;
    }

    // PARITY FIX 2025-12-23: Validate z_top_default/z_top_min to prevent inf/NaN in fallback paths
    // z_top_min >= 0 allowed (0 disables clamping for idealized tests)
    // z_top_default must be positive to avoid division-by-zero/inf
    // z_top_default >= z_top_min ensures fallback doesn't violate clamp constraint
    if (z_top_min < 0.0f) {
        std::cerr << "SDIRK3 Config Error: z_top_min must be >= 0 (use 0 to disable clamping)" << std::endl;
        valid = false;
    }

    if (z_top_default <= 0.0f) {
        std::cerr << "SDIRK3 Config Error: z_top_default must be > 0 to avoid inf/NaN in rdnw/rdn fallback" << std::endl;
        valid = false;
    }

    if (z_top_default < z_top_min) {
        std::cerr << "SDIRK3 Config Error: z_top_default (" << z_top_default
                  << ") must be >= z_top_min (" << z_top_min << ")" << std::endl;
        valid = false;
    }

    // PARITY FIX 2025-12-25: Validate c1f_c2f caching options
    if (c1f_c2f_signature_points != 3 && c1f_c2f_signature_points != 5) {
        std::cerr << "SDIRK3 Config Error: c1f_c2f_signature_points must be 3 or 5 (got "
                  << c1f_c2f_signature_points << ")" << std::endl;
        valid = false;
    }

    if (c1f_c2f_signature_period < 1) {
        std::cerr << "SDIRK3 Config Error: c1f_c2f_signature_period must be >= 1 (got "
                  << c1f_c2f_signature_period << ")" << std::endl;
        valid = false;
    }

    if (c1f_c2f_full_verify_interval < 0) {
        std::cerr << "SDIRK3 Config Error: c1f_c2f_full_verify_interval must be >= 0 (got "
                  << c1f_c2f_full_verify_interval << ")" << std::endl;
        valid = false;
    }

    // GRID METRIC VERIFY 2025-12-25: Validate grid metric full verify period
    if (grid_metric_full_verify_period < 0) {
        std::cerr << "SDIRK3 Config Error: grid_metric_full_verify_period must be >= 0 (got "
                  << grid_metric_full_verify_period << ")" << std::endl;
        valid = false;
    }

    // FIX 2025-12-27: lat_cpu_sample_check_period validation
    // Type is uint64_t so always >= 0. Value semantics:
    //   0 = auto (100 Release, 10 Debug)
    //   >= 1 = explicit period for LAT CPU sample checking in LatCpuCache
    // No explicit validation needed (all uint64_t values are valid), but document for consistency.

    // IMEX split mode validation (2026-02-01)
    // INTENTIONAL SIDE EFFECT: validate() clamps out-of-range imex_split_mode to 0.
    // Rationale: An invalid mode silently falling through to an unintended code path
    // is worse than mutating state during validation. All setters also clamp, so this
    // is a safety net for direct struct assignment or future code paths.
    if (imex_split_mode < 0 || imex_split_mode > 3) {
        std::cerr << "SDIRK3 Config Warning: imex_split_mode=" << imex_split_mode
                  << " (invalid) -> clamped to 0; effective may map to 1 if imex_enabled=true"
                  << std::endl;
        const_cast<SDIRK3Config*>(this)->imex_split_mode = 0;
    }

    // v20.14 r47c-fix3b: jvp_method 2/3 (DUAL_NUMBER/OPTIMIZED) exist in enum but
    // have no execution path — solver uses use_autograd binary branch only.
    // Clamp to 0/1 to prevent silent misinterpretation in logs/benchmarks.
    if (jvp_method > JVP_AUTOGRAD) {
        std::cerr << "SDIRK3 Config Warning: jvp_method=" << jvp_method
                  << " is not implemented (only 0=FD, 1=AD). Clamped to 1 (AD)."
                  << std::endl;
        const_cast<SDIRK3Config*>(this)->jvp_method = JVP_AUTOGRAD;
        const_cast<SDIRK3Config*>(this)->use_autograd = true;
    }

    // v20.13: Validate preconditioner tuning parameters
    if (precond_theta_acoustic_factor < 0.0f || precond_theta_acoustic_factor > 0.35f) {
        std::cerr << "SDIRK3 Config Error: precond_theta_acoustic_factor must be in [0, 0.35] (got "
                  << precond_theta_acoustic_factor << ")" << std::endl;
        valid = false;
    }

    if (precond_w_acoustic_boost < 0.0f || precond_w_acoustic_boost > 10.0f) {
        std::cerr << "SDIRK3 Config Error: precond_w_acoustic_boost must be in [0, 10] (got "
                  << precond_w_acoustic_boost << ")" << std::endl;
        valid = false;
    }

    // v20.14r67/P2: stagnation gate growth floor range check (mirrors the env/setter clamp [0,10]).
    if (stagnation_growth_floor < 0.0f || stagnation_growth_floor > 10.0f) {
        std::cerr << "SDIRK3 Config Error: stagnation_growth_floor must be in [0, 10] (got "
                  << stagnation_growth_floor << ")" << std::endl;
        valid = false;
    }

    if (precond_gs_beta < 0.0f || precond_gs_beta > 1.0f) {
        std::cerr << "SDIRK3 Config Error: precond_gs_beta must be in [0, 1] (got "
                  << precond_gs_beta << ")" << std::endl;
        valid = false;
    }

    if (precond_refinement_passes < 1 || precond_refinement_passes > 5) {
        std::cerr << "SDIRK3 Config Error: precond_refinement_passes must be in [1, 5] (got "
                  << precond_refinement_passes << ")" << std::endl;
        valid = false;
    }

    // v20.14 r46: Clamp coupling_scale to valid range
    const_cast<SDIRK3Config*>(this)->precond_phi_w_coupling_scale =
        std::max(0, std::min(2, precond_phi_w_coupling_scale));

    // v20.14r35: Newton convergence thresholds
    if (near_fail_floor < 0.0f || near_fail_floor > 0.1f) {
        std::cerr << "SDIRK3 Config Error: near_fail_floor must be in [0, 0.1] (got "
                  << near_fail_floor << ")" << std::endl;
        valid = false;
    }
    if (newton_stall_threshold < 0.0f || newton_stall_threshold > 0.1f) {
        std::cerr << "SDIRK3 Config Error: newton_stall_threshold must be in [0, 0.1] (got "
                  << newton_stall_threshold << ")" << std::endl;
        valid = false;
    }
    if (catastrophic_rel_cap < 100.0f) {
        std::cerr << "SDIRK3 Config Error: catastrophic_rel_cap must be >= 100 (got "
                  << catastrophic_rel_cap << ")" << std::endl;
        valid = false;
    }
    if (explosion_abs_floor < 1.0f || explosion_rel_multiplier < 1.0f) {
        std::cerr << "SDIRK3 Config Error: explosion_abs_floor and explosion_rel_multiplier must be >= 1 (got "
                  << explosion_abs_floor << ", " << explosion_rel_multiplier << ")" << std::endl;
        valid = false;
    }
    if (newton_stall_threshold > near_fail_floor) {
        std::cerr << "SDIRK3 Config Error: newton_stall_threshold (" << newton_stall_threshold
                  << ") must be <= near_fail_floor (" << near_fail_floor
                  << ") to avoid accepting steps then stalling" << std::endl;
        valid = false;
    }
    // v20.14r36: Validate new gate/limit fields
    if (catastrophic_abs_floor < 0.0f) {
        std::cerr << "SDIRK3 Config Error: catastrophic_abs_floor must be >= 0 (got "
                  << catastrophic_abs_floor << ")" << std::endl;
        valid = false;
    }
    if (newton_zero_step_stall_limit < 1 || newton_zero_step_stall_limit > 20) {
        std::cerr << "SDIRK3 Config Error: newton_zero_step_stall_limit must be in [1, 20] (got "
                  << newton_zero_step_stall_limit << ")" << std::endl;
        valid = false;
    }
    if (precond_gs_awphi_cap < 0.01f || precond_gs_awphi_cap > 1000.0f) {
        std::cerr << "SDIRK3 Config Error: precond_gs_awphi_cap must be in [0.01, 1000] (got "
                  << precond_gs_awphi_cap << ")" << std::endl;
        valid = false;
    }
    if (precond_gs_awphi_cap > 100.0f) {
        std::cerr << "[CONFIG WARN] precond_gs_awphi_cap=" << precond_gs_awphi_cap
                  << " > 100: high cap may destabilize GS. Use with scale>=1." << std::endl;
    }
    // v20.14 r46b: validate phi-feedback relax
    const_cast<SDIRK3Config*>(this)->precond_phi_feedback_relax =
        std::clamp(precond_phi_feedback_relax, 0.0f, 1.0f);
    // v20.14 r46b: validate coupled cap (-1=use base, or [0.01, 1000])
    if (precond_gs_awphi_cap_coupled >= 0.0f) {
        const_cast<SDIRK3Config*>(this)->precond_gs_awphi_cap_coupled =
            std::clamp(precond_gs_awphi_cap_coupled, 0.01f, 1000.0f);
    }
    // v20.14 r46e: validate phi-feedback beta, cap, dw_floor
    const_cast<SDIRK3Config*>(this)->precond_phi_feedback_beta =
        std::clamp(precond_phi_feedback_beta, 0.01f, 10.0f);
    if (precond_phi_feedback_cap >= 0.0f) {
        const_cast<SDIRK3Config*>(this)->precond_phi_feedback_cap =
            std::clamp(precond_phi_feedback_cap, 0.01f, 1000.0f);
    }
    const_cast<SDIRK3Config*>(this)->precond_dw_nosboost_floor =
        std::clamp(precond_dw_nosboost_floor, 0.001f, 10.0f);
    // v20.14 r46f: validate max_corr (-1=disabled, or [0.01, 100])
    if (precond_phi_feedback_max_corr >= 0.0f) {
        const_cast<SDIRK3Config*>(this)->precond_phi_feedback_max_corr =
            std::clamp(precond_phi_feedback_max_corr, 0.01f, 100.0f);
    }
    // v20.14 r46g: validate cap_high (-1=disabled, or [0.01, 1000])
    if (precond_phi_feedback_cap_high >= 0.0f) {
        const_cast<SDIRK3Config*>(this)->precond_phi_feedback_cap_high =
            std::clamp(precond_phi_feedback_cap_high, 0.01f, 1000.0f);
    }
    if (precond_gs_awphi_cap_coupled_high >= 0.0f) {
        const_cast<SDIRK3Config*>(this)->precond_gs_awphi_cap_coupled_high =
            std::clamp(precond_gs_awphi_cap_coupled_high, 0.01f, 1000.0f);
    }
    // v20.14 r47: validate Schur params
    const_cast<SDIRK3Config*>(this)->precond_phi_w_schur_boost_cap =
        std::clamp(precond_phi_w_schur_boost_cap, 0.0f, 10.0f);
    const_cast<SDIRK3Config*>(this)->precond_phi_w_schur_backsub_relax =
        std::clamp(precond_phi_w_schur_backsub_relax, 0.0f, 1.0f);
    const_cast<SDIRK3Config*>(this)->precond_phi_w_schur_alpha_gs =
        std::clamp(precond_phi_w_schur_alpha_gs, 0.0f, 2.0f);
    const_cast<SDIRK3Config*>(this)->precond_phi_w_schur_cross_thresh =
        std::clamp(precond_phi_w_schur_cross_thresh, 0.0f, 100.0f);
    const_cast<SDIRK3Config*>(this)->precond_phi_w_schur_cap_decay =
        std::clamp(precond_phi_w_schur_cap_decay, 0.0f, 1.0f);
    const_cast<SDIRK3Config*>(this)->precond_phi_w_schur_ru_scale =
        std::clamp(precond_phi_w_schur_ru_scale, 0.0f, 10.0f);
    const_cast<SDIRK3Config*>(this)->precond_phi_w_schur_ru_thresh =
        std::clamp(precond_phi_w_schur_ru_thresh, 0.0f, 1.0f);
    const_cast<SDIRK3Config*>(this)->precond_phi_w_schur_alpha_gs_start =
        std::clamp(precond_phi_w_schur_alpha_gs_start, 0, 20);
    // v20.14 r47c-fix3: D_u weak scaling
    const_cast<SDIRK3Config*>(this)->precond_du_weak_factor =
        std::clamp(precond_du_weak_factor, 0.001f, 1.0f);
    const_cast<SDIRK3Config*>(this)->precond_du_weak_ru_thresh =
        std::clamp(precond_du_weak_ru_thresh, 0.0f, 1.0f);
    // v20.14 r46i/j: validate feedback params
    const_cast<SDIRK3Config*>(this)->precond_phi_feedback_dw_blend =
        std::clamp(precond_phi_feedback_dw_blend, 0.0f, 1.0f);
    const_cast<SDIRK3Config*>(this)->precond_phi_feedback_passes =
        std::clamp(precond_phi_feedback_passes, 1, 5);
    const_cast<SDIRK3Config*>(this)->precond_phi_feedback_cap_mode =
        std::clamp(precond_phi_feedback_cap_mode, 0, 2);
    const_cast<SDIRK3Config*>(this)->precond_phi_feedback_stage_scale =
        std::clamp(precond_phi_feedback_stage_scale, 0.0f, 1.0f);
    if (stage_gate_rel_threshold < 1.0f || stage_gate_rel_threshold > 1e6f) {
        std::cerr << "SDIRK3 Config Error: stage_gate_rel_threshold must be in [1, 1e6] (got "
                  << stage_gate_rel_threshold << ")" << std::endl;
        valid = false;
    }
    if (split_explicit_time_step_sound < 0 || split_explicit_time_step_sound > 1000) {
        std::cerr << "SDIRK3 Config Error: split_explicit_time_step_sound must be in [0, 1000] (got "
                  << split_explicit_time_step_sound << ")" << std::endl;
        valid = false;
    }
    if (split_explicit_epssm < 0.0f || split_explicit_epssm > 1.0f) {
        std::cerr << "SDIRK3 Config Error: split_explicit_epssm must be in [0, 1] (got "
                  << split_explicit_epssm << ")" << std::endl;
        valid = false;
    }
    if (split_explicit_smdiv < 0.0f || split_explicit_smdiv > 1.0f) {
        std::cerr << "SDIRK3 Config Error: split_explicit_smdiv must be in [0, 1] (got "
                  << split_explicit_smdiv << ")" << std::endl;
        valid = false;
    }
    if (split_explicit_emdiv < 0.0f || split_explicit_emdiv > 1.0f) {
        std::cerr << "SDIRK3 Config Error: split_explicit_emdiv must be in [0, 1] (got "
                  << split_explicit_emdiv << ")" << std::endl;
        valid = false;
    }
    if (stage3_gate_rel_threshold < 0.0f || stage3_gate_rel_threshold > 1e6f) {
        std::cerr << "SDIRK3 Config Error: stage3_gate_rel_threshold must be in [0, 1e6] (got "
                  << stage3_gate_rel_threshold << ")" << std::endl;
        valid = false;
    }
    if (stage_damp_rel_threshold < 1.0f || stage_damp_rel_threshold > 1e6f) {
        std::cerr << "SDIRK3 Config Error: stage_damp_rel_threshold must be in [1, 1e6] (got "
                  << stage_damp_rel_threshold << ")" << std::endl;
        valid = false;
    }
    if (stage_gate_growth_cap < 1.0f || stage_gate_growth_cap > 1e6f) {
        std::cerr << "SDIRK3 Config Error: stage_gate_growth_cap must be in [1, 1e6] (got "
                  << stage_gate_growth_cap << ")" << std::endl;
        valid = false;
    }
    if (stage_gate_K_floor < 0.01f || stage_gate_K_floor > 1e6f) {
        std::cerr << "SDIRK3 Config Error: stage_gate_K_floor must be in [0.01, 1e6] (got "
                  << stage_gate_K_floor << ")" << std::endl;
        valid = false;
    }
    if (gate_metric_mode < 0 || gate_metric_mode > 2) {
        std::cerr << "SDIRK3 Config Error: gate_metric_mode must be 0, 1, or 2 (got "
                  << gate_metric_mode << ")" << std::endl;
        valid = false;
    }
    if (trust_region_max_relative_update < 0.1f || trust_region_max_relative_update > 10.0f) {
        std::cerr << "SDIRK3 Config Error: trust_region_max_relative_update must be in [0.1, 10] (got "
                  << trust_region_max_relative_update << ")" << std::endl;
        valid = false;
    }

    // v20.14r34: Validate UV vertical fraction
    if (precond_uv_vertical_fraction < 0.0f || precond_uv_vertical_fraction > 1.0f) {
        std::cerr << "SDIRK3 Config Error: precond_uv_vertical_fraction must be in [0, 1] (got "
                  << precond_uv_vertical_fraction << ")" << std::endl;
        valid = false;
    }
    if (uv_vfrac_warmup_start < 0.0f || uv_vfrac_warmup_start > 1.0f) {
        std::cerr << "SDIRK3 Config Error: uv_vfrac_warmup_start must be in [0, 1] (got "
                  << uv_vfrac_warmup_start << ")" << std::endl;
        valid = false;
    }

    // v20.14r27z: sign_smooth_delta must be > 0 (delta <= 0 forces hard-sign fallback → FD discontinuity)
    if (sign_smooth_delta <= 0.0f) {
        std::cerr << "SDIRK3 Config Error: sign_smooth_delta must be > 0 (got "
                  << sign_smooth_delta << "), clamping to 1e-6" << std::endl;
        const_cast<SDIRK3Config*>(this)->sign_smooth_delta = 1e-6f;
    }

    // v20.14r27i: Validate GMRES/Newton thresholds
    if (gmres_stagnation_threshold < 0.5f || gmres_stagnation_threshold > 1.0f) {
        std::cerr << "SDIRK3 Config Error: gmres_stagnation_threshold must be in [0.5, 1.0] (got "
                  << gmres_stagnation_threshold << ")" << std::endl;
        valid = false;
    }
    if (newton_gmres_quality_threshold < 0.5f || newton_gmres_quality_threshold > 1.0f) {
        std::cerr << "SDIRK3 Config Error: newton_gmres_quality_threshold must be in [0.5, 1.0] (got "
                  << newton_gmres_quality_threshold << ")" << std::endl;
        valid = false;
    }
    if (inn_q_min < 0.0f || inn_q_min > 1.0f) {
        std::cerr << "SDIRK3 Config Error: inn_q_min must be in [0, 1] (got "
                  << inn_q_min << ")" << std::endl;
        valid = false;
    }
    if (inn_beta_max < 0.0f || inn_beta_max > 1.0f) {
        std::cerr << "SDIRK3 Config Error: inn_beta_max must be in [0, 1] (got "
                  << inn_beta_max << ")" << std::endl;
        valid = false;
    }
    if (inn_gate_rel_tol < 0.0f) {
        std::cerr << "SDIRK3 Config Error: inn_gate_rel_tol must be >= 0 (got "
                  << inn_gate_rel_tol << ")" << std::endl;
        valid = false;
    }
    if (inn_gate_q_noise < 0.0f) {
        std::cerr << "SDIRK3 Config Error: inn_gate_q_noise must be >= 0 (got "
                  << inn_gate_q_noise << ")" << std::endl;
        valid = false;
    }
    if (inn_tol_ramp_gamma <= 0.0f || inn_tol_ramp_gamma > 1.0f) {
        std::cerr << "SDIRK3 Config Error: inn_tol_ramp_gamma must be in (0, 1] (got "
                  << inn_tol_ramp_gamma << ")" << std::endl;
        valid = false;
    }
    if (nk_forcing_eta_max < 0.01f || nk_forcing_eta_max > 1.0f) {
        std::cerr << "SDIRK3 Config Error: nk_forcing_eta_max must be in [0.01, 1.0] (got "
                  << nk_forcing_eta_max << ")" << std::endl;
        valid = false;
    }

    // v20.14r27m: Validate jvp_epsilon
    if (jvp_epsilon <= 0.0f || jvp_epsilon > 0.1f) {
        std::cerr << "SDIRK3 Config Error: jvp_epsilon must be in (0, 0.1] (got "
                  << jvp_epsilon << ")" << std::endl;
        valid = false;
    }

    // v20.14: Validate adaptive tuning parameters (same policy as normalize)
    if (adaptive_high_threshold < 0.15f || adaptive_high_threshold > 0.9f) {
        std::cerr << "SDIRK3 Config Error: adaptive_high_threshold must be in [0.15, 0.9] (got "
                  << adaptive_high_threshold << ")" << std::endl;
        valid = false;
    }
    if (adaptive_low_threshold < 0.1f || adaptive_low_threshold > 0.85f) {
        std::cerr << "SDIRK3 Config Error: adaptive_low_threshold must be in [0.1, 0.85] (got "
                  << adaptive_low_threshold << ")" << std::endl;
        valid = false;
    }
    if (adaptive_step_size < 0.001f || adaptive_step_size > 0.1f) {
        std::cerr << "SDIRK3 Config Error: adaptive_step_size must be in [0.001, 0.1] (got "
                  << adaptive_step_size << ")" << std::endl;
        valid = false;
    }
    if (adaptive_high_threshold - adaptive_low_threshold < 0.05f) {
        std::cerr << "SDIRK3 Config Error: adaptive thresholds gap must be >= 0.05 (got "
                  << (adaptive_high_threshold - adaptive_low_threshold) << ")" << std::endl;
        valid = false;
    }
    if (adaptive_quality_gate < 0.5f || adaptive_quality_gate > 1.0f) {
        std::cerr << "SDIRK3 Config Error: adaptive_quality_gate must be in [0.5, 1.0] (got "
                  << adaptive_quality_gate << ")" << std::endl;
        valid = false;
    }

    return valid;
}

void SDIRK3Config::print() const {
    std::cout << "===== SDIRK3 Configuration =====" << std::endl;
    std::cout << "Newton-Krylov Parameters:" << std::endl;
    std::cout << "  max_newton_iter = " << max_newton_iter << std::endl;
    std::cout << "  newton_tol = " << newton_tol << std::endl;
    std::cout << "  newton_rtol = " << newton_rtol << std::endl;
    std::cout << "  jvp_method = " << jvp_method;
    switch(jvp_method) {
        case JVP_FINITE_DIFF: std::cout << " (Finite Difference)"; break;
        case JVP_AUTOGRAD: std::cout << " (Autograd)"; break;
        case JVP_DUAL_NUMBER: std::cout << " (Dual Number)"; break;
        case JVP_OPTIMIZED: std::cout << " (Optimized Batch)"; break;
    }
    std::cout << std::endl;
    std::cout << "  jvp_epsilon = " << jvp_epsilon << std::endl;
    std::cout << "  jvp_block_epsilon = " << (jvp_block_epsilon ? "true (EXPERIMENTAL)" : "false") << std::endl;
    std::cout << "GMRES Parameters:" << std::endl;
    std::cout << "  gmres_restart = " << gmres_restart << std::endl;
    std::cout << "  max_krylov_iter = " << max_krylov_iter << std::endl;
    std::cout << "  krylov_tol = " << krylov_tol << std::endl;
    std::cout << "  gmres_true_residual_start_j = " << gmres_true_residual_start_j << std::endl;
    std::cout << "  gmres_true_residual_period = " << gmres_true_residual_period << std::endl;
    std::cout << "  gmres_arnoldi_stag_window = " << gmres_arnoldi_stag_window << std::endl;
    std::cout << "  gmres_arnoldi_stag_ratio = " << gmres_arnoldi_stag_ratio << std::endl;
    std::cout << "  jvp_mixed_fd_newton_switch = " << jvp_mixed_fd_newton_switch << std::endl;
    std::cout << "  stage2_gmres_restart = " << stage2_gmres_restart << std::endl;
    std::cout << "  stage2_max_krylov_restarts = " << stage2_max_krylov_restarts << std::endl;
    std::cout << "  stage2_krylov_tol = " << stage2_krylov_tol << std::endl;
    std::cout << "  stage2_ew_eta_min/max = " << stage2_ew_eta_min << " / " << stage2_ew_eta_max << std::endl;
    std::cout << "  stage3_gmres_restart = " << stage3_gmres_restart << std::endl;
    std::cout << "  stage3_max_krylov_restarts = " << stage3_max_krylov_restarts << std::endl;
    std::cout << "  stage3_krylov_tol = " << stage3_krylov_tol << std::endl;
    std::cout << "  stage3_ew_eta_min/max = " << stage3_ew_eta_min << " / " << stage3_ew_eta_max << std::endl;
    std::cout << "  stage3_warmstart = " << (stage3_warmstart ? "true" : "false") << std::endl;
    std::cout << "  hopeless_relax = " << (hopeless_relax ? "true" : "false") << std::endl;
    std::cout << "  precond_ratio_guard_warn_only = " << (precond_ratio_guard_warn_only ? "true" : "false") << std::endl;
    std::cout << "  jvp_auto_bench_calls = " << jvp_auto_bench_calls << std::endl;
    std::cout << "  jvp_auto_bench_quality_gate = " << (jvp_auto_bench_quality_gate ? "true" : "false") << std::endl;
    std::cout << "  jvp_auto_bench_warmup = " << jvp_auto_bench_warmup << std::endl;
    std::cout << "  jvp_auto_bench_seed = " << jvp_auto_bench_seed << std::endl;
    std::cout << "  jvp_auto_bench_lock_reset_stage1 = "
              << (jvp_auto_bench_lock_reset_stage1 ? "true" : "false") << std::endl;
    std::cout << "  solver_telemetry = " << (solver_telemetry ? "true" : "false") << std::endl;
    std::cout << "  progress_invariant_enable = " << (progress_invariant_enable ? "true" : "false")
              << ", min_ratio = " << progress_invariant_min_ratio
              << ", streak_limit = " << progress_invariant_streak_limit << std::endl;
    std::cout << "  variable_pc_event_log = " << (variable_pc_event_log ? "true" : "false") << std::endl;
    std::cout << "  gmres_warmstart = " << (gmres_warmstart ? "true" : "false") << std::endl;
    std::cout << "  gmres_warmstart_quality_gate = " << gmres_warmstart_quality_gate << std::endl;
    std::cout << "  inn_warmstart_enable = " << (inn_warmstart_enable ? "true" : "false") << std::endl;
    std::cout << "  inn_residual_gate_enable = " << (inn_residual_gate_enable ? "true" : "false") << std::endl;
    std::cout << "  inn_q_min = " << inn_q_min << std::endl;
    std::cout << "  inn_gate_rel_tol = " << inn_gate_rel_tol << std::endl;
    std::cout << "  inn_gate_q_noise = " << inn_gate_q_noise << std::endl;
    std::cout << "  inn_beta_max = " << inn_beta_max << std::endl;
    std::cout << "  inn_ood_guard_enable = " << (inn_ood_guard_enable ? "true" : "false") << std::endl;
    std::cout << "  inn_tol_ramp_enable = " << (inn_tol_ramp_enable ? "true" : "false") << std::endl;
    std::cout << "  inn_tol_ramp_gamma = " << inn_tol_ramp_gamma << std::endl;
    std::cout << "  jvp_checkpointing = " << (jvp_checkpointing ? "true" : "false")
              << ", segments = " << jvp_checkpoint_segments << std::endl;
    std::cout << "  jvp_graph_caching = " << (jvp_graph_caching ? "true" : "false")
              << ", batched = " << (jvp_batched ? "true" : "false")
              << ", batch_size = " << jvp_batch_size
              << ", mixed_precision = " << (jvp_mixed_precision ? "true" : "false") << std::endl;
    std::cout << "Preconditioner:" << std::endl;
    std::cout << "  precond_type = " << precond_type;
    switch(precond_type) {
        case 0: std::cout << " (None)"; break;
        case 1: std::cout << " (Jacobi)"; break;
        case 2: std::cout << " (Block)"; break;
        case 3: std::cout << " (Multigrid)"; break;
    }
    std::cout << std::endl;
    std::cout << "  precond_gs_beta = " << precond_gs_beta << std::endl;
    std::cout << "  precond_ilu = " << (precond_ilu ? "true" : "false")
              << ", ilu_level = " << precond_ilu_level << std::endl;
    std::cout << "  precond_multigrid = " << (precond_multigrid ? "true" : "false")
              << ", mg_levels = " << precond_mg_levels << std::endl;
    std::cout << "  precond_refinement_passes = " << precond_refinement_passes << std::endl;
    std::cout << "  precond_coupled_phi_w = " << (precond_coupled_phi_w ? "true" : "false") << std::endl;
    std::cout << "  stagnation_gate_enable = " << (stagnation_gate_enable ? "true" : "false")
              << ", stagnation_growth_floor = " << stagnation_growth_floor << std::endl;
    std::cout << "  stage_require_convergence = " << (stage_require_convergence ? "true" : "false") << std::endl;
    std::cout << "  hevi_split = " << (hevi_split ? "true" : "false") << std::endl;
    std::cout << "  split_explicit = " << (split_explicit ? "true" : "false") << std::endl;
    std::cout << "  split_explicit acoustic: time_step_sound = " << split_explicit_time_step_sound
              << ", epssm = " << split_explicit_epssm
              << ", smdiv = " << split_explicit_smdiv
              << ", emdiv = " << split_explicit_emdiv
              << ", top_lid = " << (split_explicit_top_lid ? "true" : "false") << std::endl;
    std::cout << "  precond_phi_w_coupling_scale = " << precond_phi_w_coupling_scale
              << " (0=GS, 1=physics, 2=acoustic)" << std::endl;
    std::cout << "  precond_phi_feedback_relax = " << precond_phi_feedback_relax << std::endl;
    std::cout << "  precond_gs_awphi_cap_coupled = " << precond_gs_awphi_cap_coupled
              << (precond_gs_awphi_cap_coupled < 0 ? " (using base cap)" : "") << std::endl;
    std::cout << "  precond_phi_feedback_beta = " << precond_phi_feedback_beta << std::endl;
    std::cout << "  precond_phi_feedback_cap = " << precond_phi_feedback_cap
              << (precond_phi_feedback_cap < 0 ? " (disabled)" : "") << std::endl;
    std::cout << "  precond_dw_nosboost_floor = " << precond_dw_nosboost_floor << std::endl;
    std::cout << "  precond_phi_feedback_max_corr = " << precond_phi_feedback_max_corr
              << (precond_phi_feedback_max_corr < 0 ? " (disabled)" : "") << std::endl;
    std::cout << "  precond_phi_feedback_fallback_gs = "
              << (precond_phi_feedback_fallback_gs ? "true" : "false") << std::endl;
    std::cout << "  precond_phi_feedback_cap_high = " << precond_phi_feedback_cap_high
              << (precond_phi_feedback_cap_high < 0 ? " (disabled)" : " (dynamic schedule)") << std::endl;
    std::cout << "  precond_gs_awphi_cap_coupled_high = " << precond_gs_awphi_cap_coupled_high
              << (precond_gs_awphi_cap_coupled_high < 0 ? " (disabled)" : " (dynamic schedule)") << std::endl;
    std::cout << "  precond_phi_feedback_soft_cap = "
              << (precond_phi_feedback_soft_cap ? "true (tanh)" : "false (hard min)") << std::endl;
    std::cout << "  precond_phi_feedback_dw_blend = " << precond_phi_feedback_dw_blend
              << (precond_phi_feedback_dw_blend <= 0.0f ? " (disabled)" : " (post-Thomas blend)") << std::endl;
    std::cout << "  precond_phi_feedback_passes = " << precond_phi_feedback_passes
              << (precond_phi_feedback_passes <= 1 ? "" : " (multi-pass 2a-2b)") << std::endl;
    std::cout << "  precond_phi_feedback_cap_mode = " << precond_phi_feedback_cap_mode
              << (precond_phi_feedback_cap_mode == 0 ? " (iter-based)" :
                  precond_phi_feedback_cap_mode == 1 ? " (ratio-based)" : " (combined)") << std::endl;
    std::cout << "  precond_phi_feedback_stage_scale = " << precond_phi_feedback_stage_scale
              << (precond_phi_feedback_stage_scale >= 0.999f ? " (same for all stages)" :
                  precond_phi_feedback_stage_scale <= 0.001f ? " (S2/S3 disabled)" :
                  " (S2/S3 scaled)") << std::endl;
    std::cout << "  precond_phi_w_schur_boost_cap = " << precond_phi_w_schur_boost_cap
              << " (Phase 2 Schur A_eff cap)" << std::endl;
    std::cout << "  precond_phi_w_schur_cross_thresh = " << precond_phi_w_schur_cross_thresh
              << (precond_phi_w_schur_cross_thresh == 0.0f ? " (disabled)" : " (Phase 2 auto-downgrade)") << std::endl;
    std::cout << "  precond_phi_w_schur_cap_decay = " << precond_phi_w_schur_cap_decay
              << (precond_phi_w_schur_cap_decay >= 0.999f ? " (disabled)" : " (N3 adaptive)") << std::endl;
    std::cout << "  precond_phi_w_schur_ru_scale = " << precond_phi_w_schur_ru_scale
              << ", ru_thresh = " << precond_phi_w_schur_ru_thresh
              << (precond_phi_w_schur_ru_scale == 0.0f ? " (disabled)" : " (N3 adaptive)") << std::endl;
    std::cout << "  precond_phi_w_schur_alpha_gs_start = " << precond_phi_w_schur_alpha_gs_start
              << (precond_phi_w_schur_alpha_gs_start == 0 ? " (always)" : " (staged)") << std::endl;
    std::cout << "  precond_phi_w_schur_backsub_relax = " << precond_phi_w_schur_backsub_relax
              << (precond_phi_w_schur_backsub_relax >= 0.999f ? " (full back-sub)" : " (relaxed)")
              << std::endl;
    std::cout << "  precond_du_weak_factor = " << precond_du_weak_factor
              << ", du_weak_ru_thresh = " << precond_du_weak_ru_thresh
              << (precond_du_weak_factor >= 0.999f ? " (disabled)" : " (dual-phase)") << std::endl;
    std::cout << "  precond_horizontal_smooth_alpha = " << precond_horizontal_smooth_alpha
              << ", iters = " << precond_horizontal_smooth_iters << std::endl;
    std::cout << "  precond_vertical_smooth_alpha = " << precond_vertical_smooth_alpha << std::endl;
    std::cout << "  precond_smooth_boundary_guard = "
              << (precond_smooth_boundary_guard ? "true" : "false") << std::endl;
    std::cout << "  trust_region_block_aware_thresh = " << trust_region_block_aware_thresh << std::endl;
    std::cout << "  trust_fallback_relax = " << (trust_fallback_relax ? "true" : "false")
              << ", trust_fallback_ratio = " << trust_fallback_ratio << std::endl;
    std::cout << "  direct_u_solve_thresh = " << direct_u_solve_thresh << std::endl;
    std::cout << "  precond_theta_acoustic_factor = " << precond_theta_acoustic_factor << std::endl;
    std::cout << "  precond_w_acoustic_boost = " << precond_w_acoustic_boost << std::endl;
    std::cout << "  precond_uv_vertical_fraction = " << precond_uv_vertical_fraction << std::endl;
    std::cout << "  uv_vfrac_warmup_start = " << uv_vfrac_warmup_start << std::endl;
    std::cout << "Newton Convergence Thresholds:" << std::endl;
    std::cout << "  near_fail_floor = " << near_fail_floor << std::endl;
    std::cout << "  newton_stall_threshold = " << newton_stall_threshold << std::endl;
    std::cout << "  catastrophic_rel_cap = " << catastrophic_rel_cap << std::endl;
    std::cout << "  explosion_abs_floor = " << explosion_abs_floor << std::endl;
    std::cout << "  explosion_rel_multiplier = " << explosion_rel_multiplier << std::endl;
    std::cout << "  fd_consistency_samples = " << fd_consistency_samples << std::endl;
    std::cout << "  catastrophic_abs_floor = " << catastrophic_abs_floor << std::endl;
    std::cout << "  newton_zero_step_stall_limit = " << newton_zero_step_stall_limit << std::endl;
    std::cout << "  precond_gs_awphi_cap = " << precond_gs_awphi_cap << std::endl;
    std::cout << "  stage_gate_rel_threshold = " << stage_gate_rel_threshold << std::endl;
    std::cout << "  stage3_gate_rel_threshold = " << stage3_gate_rel_threshold
              << " (0=use stage_gate_rel_threshold)" << std::endl;
    std::cout << "  stage_damp_rel_threshold = " << stage_damp_rel_threshold << std::endl;
    std::cout << "  stage_gate_growth_cap = " << stage_gate_growth_cap << std::endl;
    std::cout << "  stage_gate_K_floor = " << stage_gate_K_floor << std::endl;
    std::cout << "  gate_metric_mode = " << gate_metric_mode
              << " (0=wrms_growth, 1=scaled_abs, 2=legacy_rel)" << std::endl;
    std::cout << "  mode3_stage4_severe_abort = "
              << (mode3_stage4_severe_abort ? "true" : "false") << std::endl;
    std::cout << "  mode3_retry_nan_sanitize = "
              << (mode3_retry_nan_sanitize ? "true" : "false") << std::endl;
    std::cout << "  mode3_firsthit_probe = "
              << (mode3_firsthit_probe ? "true" : "false") << std::endl;
    std::cout << "  mode3_retry_stage4_preclip = "
              << (mode3_retry_stage4_preclip ? "true" : "false") << std::endl;
    std::cout << "  mode3_retry_stage4_clip_abs = "
              << mode3_retry_stage4_clip_abs << std::endl;
    std::cout << "  mode3_retry_handoff_clip = "
              << (mode3_retry_handoff_clip ? "true" : "false") << std::endl;
    std::cout << "  mode3_retry_handoff_clip_abs = "
              << mode3_retry_handoff_clip_abs << std::endl;
    std::cout << "Adaptive Timestep:" << std::endl;
    std::cout << "  adaptive_timestep = " << (adaptive_timestep ? "true" : "false") << std::endl;
    std::cout << "  safety_factor = " << safety_factor << std::endl;
    std::cout << "  error_tol = " << error_tol << std::endl;
    std::cout << "Performance:" << std::endl;
    std::cout << "  n_threads = " << n_threads << std::endl;
    std::cout << "  use_autograd = " << (use_autograd ? "true" : "false") << std::endl;
    std::cout << "  use_jvp_cache = " << (use_jvp_cache ? "true" : "false") << std::endl;
    std::cout << "  memory_pool = " << (memory_pool ? "true" : "false")
              << ", memory_pool_size_mb = " << memory_pool_size_mb << std::endl;
    std::cout << "  tensor_checkpointing = " << (tensor_checkpointing ? "true" : "false")
              << ", gradient_checkpointing = " << (gradient_checkpointing ? "true" : "false") << std::endl;
    std::cout << "4DVAR Replay:" << std::endl;
    std::cout << "  save_trajectory = " << (save_trajectory ? "true" : "false")
              << ", checkpoint_interval = " << checkpoint_interval
              << ", retain_graph_for_adjoint = " << (retain_graph_for_adjoint ? "true" : "false")
              << std::endl;
    std::cout << "  obs_aware_4dvar = " << (obs_aware_4dvar ? "true" : "false")
              << ", obs_source_mode = " << obs_source_mode
              << ", obs_window_sync_mode = " << obs_window_sync_mode << std::endl;
    std::cout << "  kdamp = " << kdamp
              << ", w_damp_alpha = " << w_damp_alpha
              << ", w_crit_cfl = " << w_crit_cfl << std::endl;
    std::cout << "  enable_benchmarking = " << (enable_benchmarking ? "true" : "false")
              << ", benchmark_warmup_iter = " << benchmark_warmup_iter
              << ", benchmark_measure_iter = " << benchmark_measure_iter << std::endl;
    std::cout << "Debug:" << std::endl;
    std::cout << "  debug_level = " << debug_level << std::endl;
    std::cout << "  check_nan = " << (check_nan ? "true" : "false") << std::endl;
    std::cout << "  validation_level = " << validation_level
              << ", check_staggered_consistency = " << (check_staggered_consistency ? "true" : "false")
              << ", collect_validation_stats = " << (collect_validation_stats ? "true" : "false") << std::endl;
    std::cout << "  conservation_tol_mass = " << conservation_tol_mass
              << ", conservation_tol_energy = " << conservation_tol_energy
              << ", conservation_tol_momentum = " << conservation_tol_momentum << std::endl;
    // OPT Pass33+: Debug sampling period options
    std::cout << "  debug_sample_period = " << debug_sample_period
              << (debug_sample_period == 0 ? " (every iteration)" : "") << std::endl;
    std::cout << "  debug_heavy_sample_period = " << debug_heavy_sample_period
              << (debug_heavy_sample_period == 0 ? " (every iteration)" : "") << std::endl;
    std::cout << "  grid_metric_memcmp_period = " << grid_metric_memcmp_period
              << (grid_metric_memcmp_period == 0 ? " (auto)" :
                  grid_metric_memcmp_period == 1 ? " (strict/moving-nest)" : " (relaxed)") << std::endl;
    std::cout << "  cuda_grid_metric_sample_period = " << cuda_grid_metric_sample_period
              << (cuda_grid_metric_sample_period == 0 ? " (auto)" : "") << std::endl;
    std::cout << "  cuda_phbase_sample_period = " << cuda_phbase_sample_period
              << (cuda_phbase_sample_period == 0 ? " (auto)" : "") << std::endl;
    // FIX 2025-12-27: LatCpuCache sample check period
    std::cout << "  lat_cpu_sample_check_period = " << lat_cpu_sample_check_period
              << (lat_cpu_sample_check_period == 0 ? " (auto: 100 Release, 10 Debug)" : "") << std::endl;
    // PERF FIX 2025-12-25: c1f/c2f signature verification options
    std::cout << "  c1f_c2f_signature_points = " << c1f_c2f_signature_points
              << (c1f_c2f_signature_points == 5 ? " (safer)" : " (fast)") << std::endl;
    std::cout << "  c1f_c2f_full_verify_interval = " << c1f_c2f_full_verify_interval
              << (c1f_c2f_full_verify_interval == 0 ? " (disabled)" : " (memcmp paranoid mode)") << std::endl;
    std::cout << "  c1f_c2f_signature_period = " << c1f_c2f_signature_period
              << (c1f_c2f_signature_period == 1 ? " (every call)" : "") << std::endl;
    // GRID METRIC VERIFY 2025-12-25: Periodic O(n) verification for rdnw/rdn
    std::cout << "  grid_metric_full_verify_period = " << grid_metric_full_verify_period
              << (grid_metric_full_verify_period == 0 ? " (disabled)" : " (O(n) verify every N calls)") << std::endl;
    std::cout << "Vertical Grid Fallback:" << std::endl;
    std::cout << "  z_top_min = " << z_top_min << " m (clamp floor)" << std::endl;
    std::cout << "  z_top_default = " << z_top_default << " m (when ph_base unavailable)" << std::endl;
    std::cout << "Rayleigh Damping:" << std::endl;
    std::cout << "  use_max_height_for_damping = " << (use_max_height_for_damping ? "true" : "false")
              << (use_max_height_for_damping ? " (max over terrain)" : " (mean over horizontal)") << std::endl;
    std::cout << "Nesting:" << std::endl;
    std::cout << "  moving_nest = " << (moving_nest ? "true" : "false")
              << (moving_nest ? " (aggressive lat cache check)" : "") << std::endl;
    // FIX 2025-12-29 Batch10 Issue 1: spatial_async_min_elems for BatchSpatialDerivatives
    std::cout << "Parallel Processing:" << std::endl;
    std::cout << "  spatial_async_min_elems = " << spatial_async_min_elems
              << (spatial_async_min_elems == 0 ? " (auto: 131072)" : "") << std::endl;
    // FIX Round124: Add warning control options to print output
    std::cout << "Warning Control:" << std::endl;
    std::cout << "  warn_throttle_count = " << warn_throttle_count
              << (warn_throttle_count == 0 ? " (never re-warn)" : "") << std::endl;
    std::cout << "  log_solver_pointer = " << (log_solver_pointer ? "true" : "false")
              << (log_solver_pointer ? "" : " (hidden for security)") << std::endl;
    std::cout << "  warn_on_device_index_clamp = " << (warn_on_device_index_clamp ? "true" : "false")
              << (warn_on_device_index_clamp ? " (for >32767 GPU debugging)" : " (silent clamp)") << std::endl;
    // IMEX post-solve split mode (2026-02-01)
    // Effective mode: split_mode takes priority; if 0 and imex_enabled, maps to 1
    int eff = imex_split_mode;
    if (eff == 0 && imex_enabled) eff = 1;
    const char* eff_label = (eff == 0) ? "off/baseline" :
                            (eff == 1) ? "frozen IMEX" :
                            (eff == 2) ? "post-solve IMEX SDIRK3" :
                            (eff == 3) ? "post-solve IMEX ARK324" :
                                         "UNKNOWN -> baseline (clamped)";
    std::cout << "IMEX Split:" << std::endl;
    std::cout << "  imex_split_mode = " << imex_split_mode
              << ", imex_enabled = " << (imex_enabled ? "true" : "false") << std::endl;
    std::cout << "  effective mode = " << eff << " (" << eff_label << ")" << std::endl;
    std::cout << "  imex_slow_in_tangent = " << (imex_slow_in_tangent ? "true" : "false")
              << (imex_slow_in_tangent ? " (AD graph preserved)" : " (detached)") << std::endl;
    std::cout << "  imex_phys_in_tangent = " << (imex_phys_in_tangent ? "true" : "false")
              << (imex_phys_in_tangent ? " (exact tangent)" : " (absorbed by Q_phys)") << std::endl;
    std::cout << "  stage1_explicit = " << (stage1_explicit ? "true" : "false")
              << (stage1_explicit ? " (skip Newton/GMRES on stage-1)" : "") << std::endl;
    std::cout << "  stage3_warmstart = " << (stage3_warmstart ? "true" : "false")
              << (stage3_warmstart ? " (experimental predictor path enabled)" : "") << std::endl;
    std::cout << "  jvp_auto_bench_calls/quality/warmup/seed = "
              << jvp_auto_bench_calls << " / "
              << (jvp_auto_bench_quality_gate ? "on" : "off") << " / "
              << jvp_auto_bench_warmup << " / "
              << jvp_auto_bench_seed
              << " (reset_s1=" << (jvp_auto_bench_lock_reset_stage1 ? "on" : "off") << ")"
              << std::endl;
    std::cout << "  gmres_warmstart/gate = "
              << (gmres_warmstart ? "on" : "off") << " / "
              << gmres_warmstart_quality_gate << std::endl;
    std::cout << "  inn_ws/gate/qmin/beta/ood/tol_ramp = "
              << (inn_warmstart_enable ? "on" : "off") << " / "
              << (inn_residual_gate_enable ? "on" : "off") << " / "
              << inn_q_min << " / "
              << inn_beta_max << " / "
              << (inn_ood_guard_enable ? "on" : "off") << " / "
              << (inn_tol_ramp_enable ? "on" : "off") << ":" << inn_tol_ramp_gamma
              << std::endl;
    std::cout << "  inn_gate_rel_tol/q_noise = "
              << inn_gate_rel_tol << " / " << inn_gate_q_noise << std::endl;
    std::cout << "  solver_telemetry = " << (solver_telemetry ? "true" : "false")
              << ", variable_pc_event_log = " << (variable_pc_event_log ? "true" : "false") << std::endl;
    std::cout << "  progress_invariant_enable/min_ratio/streak = "
              << (progress_invariant_enable ? "true" : "false")
              << " / " << progress_invariant_min_ratio
              << " / " << progress_invariant_streak_limit << std::endl;
    std::cout << "  rhs_bc_parity = " << (rhs_bc_parity ? "true" : "false")
              << ", mass_pgf_bc_guard = " << (mass_pgf_bc_guard ? "true" : "false") << std::endl;
    std::cout << "  precond_smooth_boundary_guard = "
              << (precond_smooth_boundary_guard ? "true" : "false") << std::endl;
    std::cout << "  trust_fallback_relax/ratio = "
              << (trust_fallback_relax ? "on" : "off")
              << " / " << trust_fallback_ratio << std::endl;
    std::cout << "Halo Exchange:" << std::endl;
    std::cout << "  use_wrf_halo_exchange = " << (use_wrf_halo_exchange ? "true" : "false") << std::endl;
    std::cout << "  halo_width = " << halo_width << std::endl;
    std::cout << "  enable_stage_halo_exchange = " << (enable_stage_halo_exchange ? "true" : "false") << std::endl;
    std::cout << "  stage_boundary_sync = " << (stage_boundary_sync ? "true" : "false") << std::endl;
    std::cout << "  enable_ad_halo_exchange = " << (enable_ad_halo_exchange ? "true" : "false") << std::endl;
    std::cout << "Preconditioner-RHS Scope:" << std::endl;
    std::cout << "  precond_match_rhs = " << (precond_match_rhs ? "true" : "false")
              << (precond_match_rhs ? " (auto-gate to IMEX mode)" : " (all terms always)") << std::endl;
    if (precond_match_rhs) {
        std::cout << "  precond_extra_rayleigh = " << (precond_extra_rayleigh ? "true" : "false") << std::endl;
        std::cout << "  precond_extra_wdamp = " << (precond_extra_wdamp ? "true" : "false") << std::endl;
        std::cout << "  precond_extra_vdiff = " << (precond_extra_vdiff ? "true" : "false") << std::endl;
        std::cout << "  precond_extra_divergence = " << (precond_extra_divergence ? "true" : "false") << std::endl;
    }
    std::cout << "================================" << std::endl;
}

// C interface implementations
//
// FIX Round129: UNIFIED CONFIG KEY REFERENCE
// ============================================================
// This block documents all runtime-settable config keys and their setter mappings.
// WHEN ADDING NEW CONFIG ITEMS: Update this table AND the appropriate setter(s).
//
// Legend:
//   [I] = set_config_int      (int value, negative clamped to 0 for uint64 storage)
//   [F] = set_config_float    (float value)
//   [B] = set_config_bool     (int value: 0=false, non-zero=true)
//   [U] = set_config_uint64   (uint64_t value, for values > INT_MAX)
//
// ┌──────────────────────────────────────┬───┬───┬───┬───┬────────────────────────────────┐
// │ Key                                  │ I │ F │ B │ U │ Notes                          │
// ├──────────────────────────────────────┼───┼───┼───┼───┼────────────────────────────────┤
// │ max_newton_iter                      │ ✓ │   │   │   │ Newton iteration limit         │
// │ gmres_restart                        │ ✓ │   │   │   │ GMRES restart size             │
// │ max_krylov_iter                      │ ✓ │   │   │   │ Max Krylov iterations          │
// │ precond_type                         │ ✓ │   │   │   │ Preconditioner type            │
// │ jvp_method                           │ ✓ │   │   │   │ JVP method (syncs use_autograd)│
// │ n_threads                            │ ✓ │   │   │   │ LibTorch thread count          │
// │ debug_level                          │ ✓ │   │   │   │ Debug verbosity + validation   │
// │ debug_sample_period                  │ ✓ │   │   │   │ 0=every,N>0=Nth (debug_level≥1)│
// │ debug_heavy_sample_period            │ ✓ │   │   │   │ 0=every,N>0=Nth (debug_level≥3)│
// │ grid_metric_memcmp_period            │ ✓ │   │   │ ✓ │ Memcmp check period            │
// │ cuda_grid_metric_sample_period       │ ✓ │   │   │ ✓ │ CUDA grid metric sampling      │
// │ cuda_phbase_sample_period            │ ✓ │   │   │ ✓ │ CUDA ph_base sampling          │
// │ lat_cpu_sample_check_period          │ ✓ │   │   │ ✓ │ LatCpuCache sample period      │
// │ c1f_c2f_signature_points             │ ✓ │   │   │   │ 3 or 5 point signature         │
// │ c1f_c2f_full_verify_interval         │ ✓ │   │   │   │ Full memcmp interval           │
// │ c1f_c2f_signature_period             │ ✓ │   │   │   │ Signature sampling period      │
// │ grid_metric_full_verify_period       │ ✓ │   │   │   │ O(n) verify period             │
// │ spatial_async_min_elems              │ ✓ │   │   │   │ Async threshold for BatchSpatial│
// │ advection_order                      │ ✓ │   │   │   │ Advection scheme order         │
// │ diffusion_option                     │ ✓ │   │   │   │ Diffusion option flag          │
// │ lateral_bc_option                    │ ✓ │   │   │   │ Lateral BC type                │
// │ top_bc_option                        │ ✓ │   │   │   │ Top BC type                    │
// │ precond_block_size                   │ ✓ │   │   │   │ Block Jacobi size              │
// │ validation_level                     │ ✓ │   │   │   │ EnhancedValidator level        │
// │ validation_sampling_threshold        │ ✓ │   │   │   │ Sampling threshold for validator│
// │ warn_throttle_count                  │ ✓ │   │   │ ✓ │ Warning re-trigger interval    │
// ├──────────────────────────────────────┼───┼───┼───┼───┼────────────────────────────────┤
// │ newton_tol                           │   │ ✓ │   │   │ Newton tolerance               │
// │ newton_rtol                          │   │ ✓ │   │   │ Newton relative tolerance      │
// │ krylov_tol                           │   │ ✓ │   │   │ Krylov tolerance               │
// │ safety_factor                        │   │ ✓ │   │   │ Adaptive dt safety factor      │
// │ error_tol                            │   │ ✓ │   │   │ Error tolerance                │
// │ dt_min_factor                        │   │ ✓ │   │   │ Min dt scaling factor          │
// │ dt_max_factor                        │   │ ✓ │   │   │ Max dt scaling factor          │
// │ diff_coef_h / diff_coef_v            │   │ ✓ │   │   │ Diffusion coefficients         │
// │ diff4_coef                           │   │ ✓ │   │   │ 4th-order diffusion coef       │
// │ rayleigh_damp_coef/depth             │   │ ✓ │   │   │ Rayleigh damping params        │
// │ cfl_target                           │   │ ✓ │   │   │ Target CFL number              │
// │ jvp_epsilon                          │   │ ✓ │   │   │ FD epsilon for JVP             │
// │ khdif / kvdif / kdamp                │   │ ✓ │   │   │ Diffusion/damping coefficients │
// │ z_top_min / z_top_default            │   │ ✓ │   │   │ Vertical grid fallback heights │
// ├──────────────────────────────────────┼───┼───┼───┼───┼────────────────────────────────┤
// │ adaptive_timestep                    │   │   │ ✓ │   │ Enable adaptive dt             │
// │ use_physics                          │   │   │ ✓ │   │ Include physics RHS            │
// │ frozen_physics_jvp                   │   │   │ ✓ │   │ Freeze physics in JVP          │
// │ use_autograd                         │   │   │ ✓ │   │ Enable autograd (syncs jvp)    │
// │ use_jvp_cache                        │   │   │ ✓ │   │ Cache JVP computations         │
// │ check_nan                            │   │   │ ✓ │   │ Enable NaN checking            │
// │ check_conservation                   │   │   │ ✓ │   │ Conservation monitoring        │
// │ check_cfl                            │   │   │ ✓ │   │ CFL condition checking         │
// │ nk_adaptive_tol                      │   │   │ ✓ │   │ Eisenstat-Walker adaptive tol  │
// │ implicit_acoustic/gravity/rayleigh   │   │   │ ✓ │   │ Implicit treatment flags       │
// │ implicit_wdamp/vdiff/hdiff/divergence│   │   │ ✓ │   │ Implicit treatment flags       │
// │ precond_diagonal_only/block_jacobi   │   │   │ ✓ │   │ Preconditioner options         │
// │ jvp_use_forward_diff                 │   │   │ ✓ │   │ Forward vs central diff        │
// │ ad_strict_mode                       │   │   │ ✓ │   │ AD strict mode enforcement     │
// │ use_max_height_for_damping           │   │   │ ✓ │   │ Rayleigh damping height calc   │
// │ moving_nest                          │   │   │ ✓ │   │ Moving nest cache mode         │
// │ warn_on_device_index_clamp           │   │   │ ✓ │   │ Device index clamp warning     │
// │ precond_match_rhs                    │   │   │ ✓ │   │ Precond scope matches IMEX RHS │
// │ precond_extra_rayleigh               │   │   │ ✓ │   │ Force Rayleigh in precond      │
// │ precond_extra_wdamp                  │   │   │ ✓ │   │ Force W-damp in precond        │
// │ precond_extra_vdiff                  │   │   │ ✓ │   │ Force vdiff in precond         │
// │ precond_extra_divergence             │   │   │ ✓ │   │ Force divergence in precond    │
// └──────────────────────────────────────┴───┴───┴───┴───┴────────────────────────────────┘
//
// OVERLAP NOTE: Keys in both [I] and [U] columns accept values from either setter.
// Use [I] for common cases (int range), [U] for values > INT_MAX.
//
// FIX Round130: SYNC VALIDATION APPROACH
// To verify table stays in sync with implementation:
//   1. STATIC CHECK (grep-based) [AVAILABLE NOW]:
//      grep -oP 'key == "\K[^"]+' wrf_sdirk3_config.cpp | sort -u > impl_keys.txt
//      # Compare against table keys extracted similarly
//   2. CI CHECK [RECOMMENDED - NOT YET IMPLEMENTED]:
//      Add test that parses this table and verifies each key appears in its setter.
//      Example: scripts/validate_config_keys.py (parses .cpp, validates table).
//   3. RUNTIME CHECK [OPTIONAL SUGGESTION - NOT IMPLEMENTED]:
//      FIX Round131: This is a design suggestion only, not currently implemented.
//      If needed, add logging wrapper around wrf_sdirk3_set_config_* calls.
//
// FIX Round132: COMPILE-TIME VALIDATION ALTERNATIVE
// For stronger guarantees, consider key registry pattern:
//   constexpr std::array<const char*, N> INT_KEYS = {"max_newton_iter", ...};
//   Then iterate over registry instead of if-else chain.
//   Benefits: Single source of truth, compile-time count verification.
//   Trade-off: Slightly more complex implementation, requires C++17 constexpr.
//
// FIX Round133: DUPLICATE/TYPO DETECTION SKETCH (C++17)
// To detect duplicate keys at compile time:
//   template<size_t N>
//   constexpr bool has_duplicates(const std::array<std::string_view, N>& arr) {
//       for (size_t i = 0; i < N; ++i)
//           for (size_t j = i + 1; j < N; ++j)
//               if (arr[i] == arr[j]) return true;
//       return false;
//   }
//   constexpr std::array<std::string_view, 3> INT_KEYS = {"key1", "key2", "key3"};
//   static_assert(!has_duplicates(INT_KEYS), "Duplicate config key detected!");
//
// For typo detection, maintain a master list and verify each setter's keys against it.
// This pattern ensures compile-time failure if keys are duplicated or misspelled.
//
// FIX Round134: CONCRETE CI SCRIPT EXAMPLE (Bash one-liner for drift detection)
// FIX Round135/Round147: FOR REFERENCE ONLY - adapt to your CI system
// PROMOTION TO ACTUAL CHECK: When CI infrastructure is available, move this script to
//   scripts/verify_config_keys.sh and add to CI pipeline (make check-config-keys).
//   Criteria: 0 false positives in 3+ test runs before promotion.
// ─────────────────────────────────────────────────────────────────────────────
// # Extract table keys (lines with │ ✓ │) and implementation keys (key == "...")
// TABLE_KEYS=$(grep -oP '│ \K[a-z_/]+(?=\s+│)' wrf_sdirk3_config.cpp | sort -u)
// IMPL_KEYS=$(grep -oP 'key == "\K[^"]+' wrf_sdirk3_config.cpp | sort -u)
//
// # Find keys in table but not in implementation (MISSING)
// comm -23 <(echo "$TABLE_KEYS") <(echo "$IMPL_KEYS")
//
// # Find keys in implementation but not in table (UNDOCUMENTED)
// comm -13 <(echo "$TABLE_KEYS") <(echo "$IMPL_KEYS")
//
// # CI integration: fail if any missing or undocumented keys
// if [ -n "$(comm -3 <(echo "$TABLE_KEYS") <(echo "$IMPL_KEYS"))" ]; then
//   echo "ERROR: Config key table out of sync with implementation"
//   exit 1
// fi
// ─────────────────────────────────────────────────────────────────────────────
// ============================================================
extern "C" {

void wrf_sdirk3_set_config_int(const char* name, int value) {
    if (!name) { std::cerr << "[CONFIG] set_config_int: null key" << std::endl; return; }
    if (warnIfWorkersStarted(name)) return;

    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(), safe_tolower);

    if (key == "max_newton_iter") {
        g_sdirk3_config.max_newton_iter = value;
    } else if (key == "gmres_restart") {
        g_sdirk3_config.gmres_restart = value;
    } else if (key == "max_krylov_iter") {
        g_sdirk3_config.max_krylov_iter = value;
    // v20.14r48: GMRES performance tuning
    } else if (key == "gmres_true_residual_start_j") {
        g_sdirk3_config.gmres_true_residual_start_j = std::clamp(value, 0, 1000);
    } else if (key == "gmres_true_residual_period") {
        g_sdirk3_config.gmres_true_residual_period = std::clamp(value, 1, 100);
    } else if (key == "gmres_arnoldi_stag_window") {
        g_sdirk3_config.gmres_arnoldi_stag_window = std::clamp(value, 1, 20);
    } else if (key == "jvp_mixed_fd_newton_switch") {
        g_sdirk3_config.jvp_mixed_fd_newton_switch = value;
    // v20.14 r49/r59: Stage-aware GMRES budget + JVP auto-bench
    } else if (key == "stage2_gmres_restart") {
        g_sdirk3_config.stage2_gmres_restart = std::clamp(value, 0, 100);
        std::cerr << "[CONFIG] stage2_gmres_restart = " << g_sdirk3_config.stage2_gmres_restart << std::endl;
    } else if (key == "stage2_max_krylov_restarts") {
        g_sdirk3_config.stage2_max_krylov_restarts = std::clamp(value, 0, 20);
        std::cerr << "[CONFIG] stage2_max_krylov_restarts = " << g_sdirk3_config.stage2_max_krylov_restarts << std::endl;
    } else if (key == "stage3_gmres_restart") {
        g_sdirk3_config.stage3_gmres_restart = std::clamp(value, 0, 100);
        std::cerr << "[CONFIG] stage3_gmres_restart = " << g_sdirk3_config.stage3_gmres_restart << std::endl;
    } else if (key == "stage3_max_krylov_restarts") {
        g_sdirk3_config.stage3_max_krylov_restarts = std::clamp(value, 0, 20);
        std::cerr << "[CONFIG] stage3_max_krylov_restarts = " << g_sdirk3_config.stage3_max_krylov_restarts << std::endl;
    } else if (key == "jvp_auto_bench_calls") {
        g_sdirk3_config.jvp_auto_bench_calls = std::clamp(value, 0, 20);
        std::cerr << "[CONFIG] jvp_auto_bench_calls = " << g_sdirk3_config.jvp_auto_bench_calls << std::endl;
    } else if (key == "jvp_auto_bench_warmup") {
        g_sdirk3_config.jvp_auto_bench_warmup = std::clamp(value, 0, 50);
        std::cerr << "[CONFIG] jvp_auto_bench_warmup = " << g_sdirk3_config.jvp_auto_bench_warmup << std::endl;
    } else if (key == "jvp_auto_bench_seed") {
        g_sdirk3_config.jvp_auto_bench_seed = value;
        std::cerr << "[CONFIG] jvp_auto_bench_seed = " << g_sdirk3_config.jvp_auto_bench_seed << std::endl;
    } else if (key == "jvp_checkpoint_segments") {
        g_sdirk3_config.jvp_checkpoint_segments = std::clamp(value, 1, 64);
    } else if (key == "jvp_batch_size") {
        g_sdirk3_config.jvp_batch_size = std::clamp(value, 1, 256);
    } else if (key == "checkpoint_interval") {
        g_sdirk3_config.checkpoint_interval = std::max(1, value);
    } else if (key == "obs_source_mode") {
        g_sdirk3_config.obs_source_mode = std::clamp(value, 0, 2);
        std::cerr << "[CONFIG] obs_source_mode = " << g_sdirk3_config.obs_source_mode << std::endl;
    } else if (key == "obs_window_sync_mode") {
        g_sdirk3_config.obs_window_sync_mode = std::clamp(value, 0, 2);
        std::cerr << "[CONFIG] obs_window_sync_mode = " << g_sdirk3_config.obs_window_sync_mode << std::endl;
    } else if (key == "precond_horizontal_smooth_iters") {
        g_sdirk3_config.precond_horizontal_smooth_iters = std::clamp(value, 1, 5);
        std::cerr << "[CONFIG] precond_horizontal_smooth_iters = " << g_sdirk3_config.precond_horizontal_smooth_iters << std::endl;
    } else if (key == "precond_type") {
        std::cerr << "[C++ CONFIG DEBUG] Setting precond_type = " << value << std::endl;
        g_sdirk3_config.precond_type = value;
    } else if (key == "jvp_method") {
        // v20.14 r47c-fix3b: Clamp 2/3 → 1 (not implemented), sync use_autograd.
        int clamped = value;
        if (clamped > 1) {
            std::cerr << "[CONFIG WARNING] jvp_method=" << clamped
                      << " not implemented, clamped to 1 (AD)" << std::endl;
            clamped = 1;
        }
        g_sdirk3_config.jvp_method = static_cast<SDIRK3Config::JVPMethod>(clamped);
        g_sdirk3_config.use_autograd = (clamped >= SDIRK3Config::JVP_AUTOGRAD);
        std::cerr << "[CONFIG DEBUG] Setting jvp_method = " << g_sdirk3_config.jvp_method
                  << " (from value " << value << "), use_autograd = " << g_sdirk3_config.use_autograd << std::endl;
    } else if (key == "n_threads") {
        g_sdirk3_config.n_threads = value;
    } else if (key == "debug_level") {
        g_sdirk3_config.debug_level = value;
        // FIX 2025-12-30 Batch19 Issue 2: Sync metric_utils debug_level for getVerifyPeriod()
        metric_utils::setMetricDebugLevel(value);
        // FIX 2025-12-31 Batch41 Issue 4: Sync validation policy based on debug_level
        syncValidationSettings(value);
    } else if (key == "debug_sample_period") {
        // OPT Pass33+: Standard diagnostic sampling period (0 = every iteration)
        g_sdirk3_config.debug_sample_period = (value < 0) ? 0 : value;
        // WARN_ONCE: Avoid log spam on repeated calls
        static bool warned_sample_period_zero = false;
        if (g_sdirk3_config.debug_sample_period == 0 && g_sdirk3_config.debug_level >= 1 && !warned_sample_period_zero) {
            std::cerr << "[CONFIG WARN] debug_sample_period=0: diagnostics every iteration (high D2H overhead)" << std::endl;
            warned_sample_period_zero = true;
        }
    } else if (key == "debug_heavy_sample_period") {
        // OPT Pass33+: Heavy diagnostic sampling period (0 = every iteration)
        g_sdirk3_config.debug_heavy_sample_period = (value < 0) ? 0 : value;
        // WARN_ONCE: Avoid log spam on repeated calls
        static bool warned_heavy_period_zero = false;
        if (g_sdirk3_config.debug_heavy_sample_period == 0 && g_sdirk3_config.debug_level >= 1 && !warned_heavy_period_zero) {
            std::cerr << "[CONFIG WARN] debug_heavy_sample_period=0: heavy diagnostics every iteration (very high overhead)" << std::endl;
            warned_heavy_period_zero = true;
        }
    } else if (key == "grid_metric_memcmp_period") {
        // PARITY FIX 2025-12-24: Separate memcmp period from debug_level
        // PARITY FIX 2025-12-24: Clamp negative to 0 to prevent underflow
        g_sdirk3_config.grid_metric_memcmp_period = (value < 0) ? 0 : static_cast<uint64_t>(value);
    } else if (key == "cuda_grid_metric_sample_period") {
        // PARITY FIX 2025-12-24: Separate CUDA sample period for refreshGridMetricEpochs
        g_sdirk3_config.cuda_grid_metric_sample_period = (value < 0) ? 0 : static_cast<uint64_t>(value);
    } else if (key == "cuda_phbase_sample_period") {
        // PARITY FIX 2025-12-24: Separate CUDA sample period for updatePhBaseSignature
        g_sdirk3_config.cuda_phbase_sample_period = (value < 0) ? 0 : static_cast<uint64_t>(value);
    } else if (key == "lat_cpu_sample_check_period") {
        // FIX 2025-12-27: Runtime-tunable sample check period for LatCpuCache
        g_sdirk3_config.lat_cpu_sample_check_period = (value < 0) ? 0 : static_cast<uint64_t>(value);
    } else if (key == "c1f_c2f_signature_points") {
        // PERF FIX 2025-12-25: c1f/c2f signature verification - only 3 or 5 valid
        g_sdirk3_config.c1f_c2f_signature_points = (value == 5) ? 5 : 3;
    } else if (key == "c1f_c2f_full_verify_interval") {
        // PERF FIX 2025-12-25: c1f/c2f full memcmp interval (0=disable, >0=every N calls)
        g_sdirk3_config.c1f_c2f_full_verify_interval = std::max(0, value);
    } else if (key == "c1f_c2f_signature_period") {
        // PERF FIX 2025-12-25: c1f/c2f signature sampling period (1=every call, >1=reduce GPU→CPU sync)
        g_sdirk3_config.c1f_c2f_signature_period = std::max(1, value);
    } else if (key == "grid_metric_full_verify_period") {
        // GRID METRIC VERIFY 2025-12-25: Periodic O(n) verification for rdnw/rdn
        g_sdirk3_config.grid_metric_full_verify_period = std::max(0, value);
        // FIX 2025-12-29 Issue 3: Sync with StaticMetricCache3D periodic signature check
        metric_utils::setStaticMetricFullVerifyPeriod(g_sdirk3_config.grid_metric_full_verify_period);
    // FIX 2025-12-29 Batch10 Issue 1: spatial_async_min_elems runtime setter
    } else if (key == "spatial_async_min_elems") {
        g_sdirk3_config.spatial_async_min_elems = (value < 0) ? 0 : static_cast<int64_t>(value);
    } else if (key == "scaling_mode") {
        g_sdirk3_config.scaling_mode = value;
    } else if (key == "precond_acoustic_4x4") {
        g_sdirk3_config.precond_acoustic_4x4 = value;
    } else if (key == "advection_order") {
        g_sdirk3_config.advection_order = value;
    } else if (key == "diffusion_option") {
        g_sdirk3_config.diffusion_option = value;
    } else if (key == "lateral_bc_option") {
        g_sdirk3_config.lateral_bc_option = value;
    } else if (key == "top_bc_option") {
        g_sdirk3_config.top_bc_option = value;
    } else if (key == "precond_block_size") {
        g_sdirk3_config.precond_block_size = value;
    } else if (key == "precond_ilu_level") {
        g_sdirk3_config.precond_ilu_level = std::clamp(value, 0, 8);
    } else if (key == "precond_mg_levels") {
        g_sdirk3_config.precond_mg_levels = std::clamp(value, 1, 10);
    } else if (key == "memory_pool_size_mb") {
        g_sdirk3_config.memory_pool_size_mb = std::clamp(value, 16, 65536);
    } else if (key == "precond_refinement_passes") {
        g_sdirk3_config.precond_refinement_passes = std::clamp(value, 1, 5);
    } else if (key == "precond_coupled_phi_w") {
        g_sdirk3_config.precond_coupled_phi_w = (value != 0);
        std::cerr << "[CONFIG] precond_coupled_phi_w = "
                  << (g_sdirk3_config.precond_coupled_phi_w ? "true" : "false") << std::endl;
    } else if (key == "stagnation_gate_enable") {
        g_sdirk3_config.stagnation_gate_enable = (value != 0);
        std::cerr << "[CONFIG] stagnation_gate_enable = "
                  << (g_sdirk3_config.stagnation_gate_enable ? "true" : "false") << std::endl;
    } else if (key == "stage_require_convergence") {
        g_sdirk3_config.stage_require_convergence = (value != 0);
        std::cerr << "[CONFIG] stage_require_convergence = "
                  << (g_sdirk3_config.stage_require_convergence ? "true" : "false") << std::endl;
    } else if (key == "hevi_split") {
        g_sdirk3_config.hevi_split = (value != 0);
        std::cerr << "[CONFIG] hevi_split = "
                  << (g_sdirk3_config.hevi_split ? "true" : "false") << std::endl;
    } else if (key == "split_explicit") {
        g_sdirk3_config.split_explicit = (value != 0);
        std::cerr << "[CONFIG] split_explicit = "
                  << (g_sdirk3_config.split_explicit ? "true" : "false") << std::endl;
    } else if (key == "split_explicit_time_step_sound") {
        g_sdirk3_config.split_explicit_time_step_sound = std::max(0, std::min(1000, value));
        std::cerr << "[CONFIG] split_explicit_time_step_sound = "
                  << g_sdirk3_config.split_explicit_time_step_sound
                  << (g_sdirk3_config.split_explicit_time_step_sound <= 0
                          ? " (UNSUPPORTED: runtime guard requires explicit even >= 4)"
                          : "") << std::endl;
    } else if (key == "precond_phi_w_coupling_scale") {
        g_sdirk3_config.precond_phi_w_coupling_scale = std::max(0, std::min(2, value));
        std::cerr << "[CONFIG] precond_phi_w_coupling_scale = "
                  << g_sdirk3_config.precond_phi_w_coupling_scale << std::endl;
    } else if (key == "precond_phi_feedback_fallback_gs") {
        g_sdirk3_config.precond_phi_feedback_fallback_gs = (value != 0);
        std::cerr << "[CONFIG] precond_phi_feedback_fallback_gs = "
                  << (g_sdirk3_config.precond_phi_feedback_fallback_gs ? "true" : "false") << std::endl;
    } else if (key == "precond_phi_feedback_soft_cap") {
        g_sdirk3_config.precond_phi_feedback_soft_cap = (value != 0);
        std::cerr << "[CONFIG] precond_phi_feedback_soft_cap = "
                  << (g_sdirk3_config.precond_phi_feedback_soft_cap ? "tanh" : "hard") << std::endl;
    } else if (key == "precond_phi_feedback_passes") {
        g_sdirk3_config.precond_phi_feedback_passes = std::clamp(value, 1, 5);
        std::cerr << "[CONFIG] precond_phi_feedback_passes = "
                  << g_sdirk3_config.precond_phi_feedback_passes << std::endl;
    } else if (key == "precond_phi_feedback_cap_mode") {
        g_sdirk3_config.precond_phi_feedback_cap_mode = std::clamp(value, 0, 2);
        std::cerr << "[CONFIG] precond_phi_feedback_cap_mode = "
                  << g_sdirk3_config.precond_phi_feedback_cap_mode
                  << " (0=iter, 1=ratio, 2=combined)" << std::endl;
    } else if (key == "precond_phi_w_schur_boost_on") {
        g_sdirk3_config.precond_phi_w_schur_boost_on = (value != 0);
    } else if (key == "precond_phi_w_schur_rhs_inject_on") {
        g_sdirk3_config.precond_phi_w_schur_rhs_inject_on = (value != 0);
    } else if (key == "precond_phi_w_schur_backsub_on") {
        g_sdirk3_config.precond_phi_w_schur_backsub_on = (value != 0);
    } else if (key == "precond_phi_w_schur_alpha_gs_start") {
        g_sdirk3_config.precond_phi_w_schur_alpha_gs_start = std::clamp(value, 0, 20);
        std::cerr << "[CONFIG] precond_phi_w_schur_alpha_gs_start = " << g_sdirk3_config.precond_phi_w_schur_alpha_gs_start << std::endl;
    } else if (key == "fd_consistency_samples") {
        g_sdirk3_config.fd_consistency_samples = std::clamp(value, 0, 10);
        std::cerr << "[CONFIG] fd_consistency_samples = " << g_sdirk3_config.fd_consistency_samples << std::endl;
    } else if (key == "newton_zero_step_stall_limit") {
        g_sdirk3_config.newton_zero_step_stall_limit = std::clamp(value, 1, 20);
        std::cerr << "[CONFIG] newton_zero_step_stall_limit = " << g_sdirk3_config.newton_zero_step_stall_limit << std::endl;
    } else if (key == "adaptive_retune_mode") {
        g_sdirk3_config.adaptive_retune_mode = std::clamp(value, 0, 2);
        std::cerr << "[CONFIG] adaptive_retune_mode = " << g_sdirk3_config.adaptive_retune_mode << std::endl;
    // FIX 2025-12-31 Batch41 Issue 4: validation_level runtime setter (0=OFF, 1=CRITICAL, 2=STANDARD, 3=PARANOID)
    } else if (key == "validation_level") {
        g_sdirk3_config.validation_level = std::clamp(value, 0, 3);
        using Level = WRF_SDIRK3::Validation::Level;
        Level level = Level::STANDARD;  // default
        if (g_sdirk3_config.validation_level <= 0) level = Level::OFF;
        else if (g_sdirk3_config.validation_level == 1) level = Level::CRITICAL;
        else if (g_sdirk3_config.validation_level == 2) level = Level::STANDARD;
        else level = Level::PARANOID;
        WRF_SDIRK3::Validation::EnhancedTensorValidator::set_level(level);
    } else if (key == "benchmark_warmup_iter") {
        g_sdirk3_config.benchmark_warmup_iter = std::max(0, value);
    } else if (key == "benchmark_measure_iter") {
        g_sdirk3_config.benchmark_measure_iter = std::max(1, value);
    // FIX 2025-12-31 Batch41 Issue 4: validation_sampling_threshold runtime setter (0=full stats, >0=sample above N elements)
    } else if (key == "validation_sampling_threshold") {
        int64_t threshold = (value < 0) ? 0 : static_cast<int64_t>(value);
        WRF_SDIRK3::Validation::EnhancedTensorValidator::set_sampling_threshold(threshold, threshold > 0);
    // FIX Round126: warn_throttle_count runtime setter with range validation
    // Since warn_throttle_count is uint64_t but we receive int, clamp negative to 0.
    // For values larger than INT_MAX, use direct C++ assignment or namelist.
    } else if (key == "warn_throttle_count") {
        g_sdirk3_config.warn_throttle_count = (value < 0) ? 0 : static_cast<uint64_t>(value);
        if (g_sdirk3_config.debug_level >= 1) {
            std::cerr << "[CONFIG DEBUG] Setting warn_throttle_count = "
                      << g_sdirk3_config.warn_throttle_count
                      << (value < 0 ? " (clamped from negative)" : "") << std::endl;
        }
    // IMEX post-solve split mode (2026-02-01)
    } else if (key == "imex_split_mode") {
        if (value < 0 || value > 3) {
            std::cerr << "[CONFIG WARNING] imex_split_mode=" << value
                      << " (invalid) -> clamped to 0; effective may map to 1 if imex_enabled=true"
                      << std::endl;
            value = 0;
        }
        g_sdirk3_config.imex_split_mode = value;
        // Log raw + effective
        int eff = value;
        if (eff == 0 && g_sdirk3_config.imex_enabled) eff = 1;
        const char* eff_label = (eff == 0) ? "off/baseline" :
                                (eff == 1) ? "frozen IMEX" :
                                (eff == 2) ? "post-solve IMEX SDIRK3" :
                                (eff == 3) ? "post-solve IMEX ARK324" :
                                             "UNKNOWN -> baseline (clamped)";
        std::cerr << "[CONFIG] IMEX raw: imex_split_mode=" << value
                  << ", imex_enabled=" << g_sdirk3_config.imex_enabled << std::endl;
        std::cerr << "[CONFIG] IMEX effective: mode=" << eff
                  << " (" << eff_label << ")" << std::endl;

    // v20.14r27i/r51: Stage fail action and NaN sanitizer runtime setters
    } else if (key == "stage_fail_action") {
        g_sdirk3_config.stage_fail_action = std::clamp(value, 0, 2);
        std::cerr << "[CONFIG] stage_fail_action = " << g_sdirk3_config.stage_fail_action
                  << " (0=continue, 1=abort, 2=retry_once_then_abort)" << std::endl;
    } else if (key == "mode3_stage4_severe_abort") {
        g_sdirk3_config.mode3_stage4_severe_abort = (value != 0);
        std::cerr << "[CONFIG] mode3_stage4_severe_abort = "
                  << (g_sdirk3_config.mode3_stage4_severe_abort ? "true" : "false")
                  << std::endl;
    } else if (key == "mode3_retry_nan_sanitize") {
        g_sdirk3_config.mode3_retry_nan_sanitize = (value != 0);
        std::cerr << "[CONFIG] mode3_retry_nan_sanitize = "
                  << (g_sdirk3_config.mode3_retry_nan_sanitize ? "true" : "false")
                  << std::endl;
    } else if (key == "mode3_firsthit_probe") {
        g_sdirk3_config.mode3_firsthit_probe = (value != 0);
        std::cerr << "[CONFIG] mode3_firsthit_probe = "
                  << (g_sdirk3_config.mode3_firsthit_probe ? "true" : "false")
                  << std::endl;
    } else if (key == "mode3_retry_stage4_preclip") {
        g_sdirk3_config.mode3_retry_stage4_preclip = (value != 0);
        std::cerr << "[CONFIG] mode3_retry_stage4_preclip = "
                  << (g_sdirk3_config.mode3_retry_stage4_preclip ? "true" : "false")
                  << std::endl;
    } else if (key == "mode3_retry_handoff_clip") {
        g_sdirk3_config.mode3_retry_handoff_clip = (value != 0);
        std::cerr << "[CONFIG] mode3_retry_handoff_clip = "
                  << (g_sdirk3_config.mode3_retry_handoff_clip ? "true" : "false")
                  << std::endl;
    } else if (key == "gate_metric_mode") {
        g_sdirk3_config.gate_metric_mode = std::clamp(value, 0, 2);
        std::cerr << "[CONFIG] gate_metric_mode = " << g_sdirk3_config.gate_metric_mode
                  << " (0=wrms_growth, 1=scaled_abs, 2=legacy_rel)" << std::endl;
    } else if (key == "progress_invariant_streak_limit") {
        g_sdirk3_config.progress_invariant_streak_limit = std::max(0, value);
        std::cerr << "[CONFIG] progress_invariant_streak_limit = "
                  << g_sdirk3_config.progress_invariant_streak_limit << std::endl;
    } else if (key == "nan_sanitize_mode") {
        g_sdirk3_config.nan_sanitize_mode = std::clamp(value, 0, 2);
        std::cerr << "[CONFIG] nan_sanitize_mode = " << g_sdirk3_config.nan_sanitize_mode
                  << " (0=report-only, 1=sanitize)" << std::endl;

    // v20.14r27m: physics_mode from Fortran — informational only.
    // Actual physics mode is determined by wrf_sdirk3_full_physics.h config.
    } else if (key == "physics_mode") {
        if (wrf::sdirk3::g_sdirk3_config.debug_level >= 2) {
            std::cerr << "[CONFIG] physics_mode = " << value
                      << " (informational, actual mode set internally)" << std::endl;
        }

    } else {
        std::cerr << "[CONFIG WARNING] Unknown int config key: " << key << std::endl;
    }
}

void wrf_sdirk3_set_config_float(const char* name, float value) {
    if (!name) { std::cerr << "[CONFIG] set_config_float: null key" << std::endl; return; }
    if (warnIfWorkersStarted(name)) return;

    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(), safe_tolower);

    if (key == "newton_tol") {
        g_sdirk3_config.newton_tol = value;
    } else if (key == "newton_rtol") {
        g_sdirk3_config.newton_rtol = value;
    } else if (key == "krylov_tol") {
        g_sdirk3_config.krylov_tol = value;
    // v20.14r48: GMRES performance float params
    } else if (key == "gmres_arnoldi_stag_ratio") {
        g_sdirk3_config.gmres_arnoldi_stag_ratio = std::clamp(value, 0.5f, 1.0f);
    } else if (key == "safety_factor") {
        g_sdirk3_config.safety_factor = value;
    } else if (key == "error_tol") {
        g_sdirk3_config.error_tol = value;
    } else if (key == "dt_min_factor") {
        g_sdirk3_config.dt_min_factor = value;
    } else if (key == "dt_max_factor") {
        g_sdirk3_config.dt_max_factor = value;
    } else if (key == "diff_coef_h") {
        g_sdirk3_config.diff_coef_h = value;
    } else if (key == "diff_coef_v") {
        g_sdirk3_config.diff_coef_v = value;
    } else if (key == "diff4_coef") {
        g_sdirk3_config.diff4_coef = value;
    } else if (key == "rayleigh_damp_coef") {
        g_sdirk3_config.rayleigh_damp_coef = value;
    } else if (key == "rayleigh_damp_depth") {
        g_sdirk3_config.rayleigh_damp_depth = value;
    } else if (key == "cfl_target") {
        g_sdirk3_config.cfl_target = value;
    } else if (key == "jvp_epsilon") {
        // v20.14r27m: Clamp to valid range (0, 0.1]
        g_sdirk3_config.jvp_epsilon = std::clamp(value, 1e-15f, 0.1f);
    } else if (key == "khdif") {
        g_sdirk3_config.khdif = value;
    } else if (key == "kvdif") {
        g_sdirk3_config.kvdif = value;
    } else if (key == "kdamp" || key == "dampcoef") {
        g_sdirk3_config.kdamp = std::max(0.0f, value);
    } else if (key == "w_damp_alpha") {
        g_sdirk3_config.w_damp_alpha = std::clamp(value, 0.0f, 2.0f);
    } else if (key == "w_crit_cfl") {
        g_sdirk3_config.w_crit_cfl = std::clamp(value, 0.1f, 10.0f);
    } else if (key == "conservation_tol_mass") {
        g_sdirk3_config.conservation_tol_mass = std::max(1.0e-12f, value);
    } else if (key == "conservation_tol_energy") {
        g_sdirk3_config.conservation_tol_energy = std::max(1.0e-12f, value);
    } else if (key == "conservation_tol_momentum") {
        g_sdirk3_config.conservation_tol_momentum = std::max(1.0e-12f, value);
    } else if (key == "damp_top" || key == "zdamp" || key == "rayleigh_damp_depth") {
        g_sdirk3_config.rayleigh_damp_depth = value;
    } else if (key == "rayleigh_damp_coef") {
        g_sdirk3_config.rayleigh_damp_coef = value;
    // PARITY FIX 2025-12-23: z_top_min/z_top_default configurable via runtime setter
    } else if (key == "z_top_min") {
        g_sdirk3_config.z_top_min = value;
    } else if (key == "z_top_default") {
        g_sdirk3_config.z_top_default = value;
    } else if (key == "omega_w_blend") {
        g_sdirk3_config.omega_w_blend = value;
        std::cerr << "[CONFIG] omega_w_blend = " << value << std::endl;
    } else if (key == "precond_relaxation") {
        g_sdirk3_config.precond_relaxation = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_relaxation = " << g_sdirk3_config.precond_relaxation << std::endl;
    } else if (key == "stagnation_growth_floor") {
        g_sdirk3_config.stagnation_growth_floor = std::clamp(value, 0.0f, 10.0f);
        std::cerr << "[CONFIG] stagnation_growth_floor = " << g_sdirk3_config.stagnation_growth_floor << std::endl;
    } else if (key == "precond_gs_beta") {
        g_sdirk3_config.precond_gs_beta = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_gs_beta = " << g_sdirk3_config.precond_gs_beta << std::endl;
    } else if (key == "precond_mu_coupling_damping") {
        g_sdirk3_config.precond_mu_coupling_damping = std::clamp(value, 0.0f, 2.0f);
        std::cerr << "[CONFIG] precond_mu_coupling_damping = "
                  << g_sdirk3_config.precond_mu_coupling_damping << std::endl;
    } else if (key == "precond_w_acoustic_boost") {
        g_sdirk3_config.precond_w_acoustic_boost = std::clamp(value, 0.0f, 10.0f);
        std::cerr << "[CONFIG] precond_w_acoustic_boost = " << g_sdirk3_config.precond_w_acoustic_boost << std::endl;
    } else if (key == "precond_theta_acoustic_factor") {
        g_sdirk3_config.precond_theta_acoustic_factor = std::clamp(value, 0.0f, 0.35f);
        ++g_sdirk3_config.theta_config_generation;  // v20.14: signal override release
        std::cerr << "[CONFIG] precond_theta_acoustic_factor = "
                  << g_sdirk3_config.precond_theta_acoustic_factor
                  << " (gen=" << g_sdirk3_config.theta_config_generation
                  << (value != g_sdirk3_config.precond_theta_acoustic_factor ? ", clamped" : "")
                  << ")" << std::endl;
    // v20.14r27h: Trust-region initial radius via runtime setter
    } else if (key == "nk_trust_radius") {
        g_sdirk3_config.nk_trust_radius = std::max(0.01f, value);
    // v20.14r27i: Armijo sufficient decrease parameter
    } else if (key == "nk_line_search_alpha") {
        g_sdirk3_config.nk_line_search_alpha = std::clamp(value, 1e-6f, 0.5f);
    // v20.14: Adaptive tuning constants via runtime setter
    } else if (key == "adaptive_high_threshold") {
        g_sdirk3_config.adaptive_high_threshold = value;
        g_sdirk3_config.normalize_adaptive_thresholds();
        std::cerr << "[CONFIG] adaptive_high_threshold = " << g_sdirk3_config.adaptive_high_threshold << std::endl;
    } else if (key == "adaptive_low_threshold") {
        g_sdirk3_config.adaptive_low_threshold = value;
        g_sdirk3_config.normalize_adaptive_thresholds();
        std::cerr << "[CONFIG] adaptive_low_threshold = " << g_sdirk3_config.adaptive_low_threshold << std::endl;
    } else if (key == "adaptive_step_size") {
        g_sdirk3_config.adaptive_step_size = std::clamp(value, 0.001f, 0.1f);
        std::cerr << "[CONFIG] adaptive_step_size = " << g_sdirk3_config.adaptive_step_size << std::endl;
    } else if (key == "adaptive_quality_gate") {
        g_sdirk3_config.adaptive_quality_gate = std::clamp(value, 0.5f, 1.0f);
        std::cerr << "[CONFIG] adaptive_quality_gate = " << g_sdirk3_config.adaptive_quality_gate << std::endl;
    } else if (key == "precond_uv_vertical_fraction") {
        g_sdirk3_config.precond_uv_vertical_fraction = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_uv_vertical_fraction = " << g_sdirk3_config.precond_uv_vertical_fraction << std::endl;
    } else if (key == "uv_vfrac_warmup_start") {
        g_sdirk3_config.uv_vfrac_warmup_start = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] uv_vfrac_warmup_start = " << g_sdirk3_config.uv_vfrac_warmup_start << std::endl;
    } else if (key == "near_fail_floor") {
        g_sdirk3_config.near_fail_floor = std::clamp(value, 0.0f, 0.1f);
        std::cerr << "[CONFIG] near_fail_floor = " << g_sdirk3_config.near_fail_floor << std::endl;
    } else if (key == "newton_stall_threshold") {
        g_sdirk3_config.newton_stall_threshold = std::clamp(value, 0.0f, 0.1f);
        std::cerr << "[CONFIG] newton_stall_threshold = " << g_sdirk3_config.newton_stall_threshold << std::endl;
    } else if (key == "catastrophic_rel_cap") {
        g_sdirk3_config.catastrophic_rel_cap = std::max(100.0f, value);
        std::cerr << "[CONFIG] catastrophic_rel_cap = " << g_sdirk3_config.catastrophic_rel_cap << std::endl;
    } else if (key == "explosion_abs_floor") {
        g_sdirk3_config.explosion_abs_floor = std::max(1.0f, value);
        std::cerr << "[CONFIG] explosion_abs_floor = " << g_sdirk3_config.explosion_abs_floor << std::endl;
    } else if (key == "explosion_rel_multiplier") {
        g_sdirk3_config.explosion_rel_multiplier = std::max(1.0f, value);
        std::cerr << "[CONFIG] explosion_rel_multiplier = " << g_sdirk3_config.explosion_rel_multiplier << std::endl;
    } else if (key == "catastrophic_abs_floor") {
        g_sdirk3_config.catastrophic_abs_floor = std::max(0.0f, value);
        std::cerr << "[CONFIG] catastrophic_abs_floor = " << g_sdirk3_config.catastrophic_abs_floor << std::endl;
    } else if (key == "precond_gs_awphi_cap") {
        g_sdirk3_config.precond_gs_awphi_cap = std::max(0.01f, value);
        std::cerr << "[CONFIG] precond_gs_awphi_cap = " << g_sdirk3_config.precond_gs_awphi_cap << std::endl;
    } else if (key == "precond_phi_feedback_relax") {
        g_sdirk3_config.precond_phi_feedback_relax = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_phi_feedback_relax = " << g_sdirk3_config.precond_phi_feedback_relax << std::endl;
    } else if (key == "precond_gs_awphi_cap_coupled") {
        g_sdirk3_config.precond_gs_awphi_cap_coupled = value;
        std::cerr << "[CONFIG] precond_gs_awphi_cap_coupled = " << g_sdirk3_config.precond_gs_awphi_cap_coupled << std::endl;
    } else if (key == "precond_phi_feedback_beta") {
        g_sdirk3_config.precond_phi_feedback_beta = std::clamp(value, 0.01f, 10.0f);
        std::cerr << "[CONFIG] precond_phi_feedback_beta = " << g_sdirk3_config.precond_phi_feedback_beta << std::endl;
    } else if (key == "precond_phi_feedback_cap") {
        g_sdirk3_config.precond_phi_feedback_cap = value;
        std::cerr << "[CONFIG] precond_phi_feedback_cap = " << g_sdirk3_config.precond_phi_feedback_cap << std::endl;
    } else if (key == "precond_dw_nosboost_floor") {
        g_sdirk3_config.precond_dw_nosboost_floor = std::clamp(value, 0.001f, 10.0f);
        std::cerr << "[CONFIG] precond_dw_nosboost_floor = " << g_sdirk3_config.precond_dw_nosboost_floor << std::endl;
    } else if (key == "precond_phi_feedback_max_corr") {
        g_sdirk3_config.precond_phi_feedback_max_corr = value;
        std::cerr << "[CONFIG] precond_phi_feedback_max_corr = " << g_sdirk3_config.precond_phi_feedback_max_corr << std::endl;
    } else if (key == "precond_phi_feedback_cap_high") {
        g_sdirk3_config.precond_phi_feedback_cap_high = value;
        std::cerr << "[CONFIG] precond_phi_feedback_cap_high = " << g_sdirk3_config.precond_phi_feedback_cap_high << std::endl;
    } else if (key == "precond_gs_awphi_cap_coupled_high") {
        g_sdirk3_config.precond_gs_awphi_cap_coupled_high = value;
        std::cerr << "[CONFIG] precond_gs_awphi_cap_coupled_high = " << g_sdirk3_config.precond_gs_awphi_cap_coupled_high << std::endl;
    } else if (key == "precond_phi_feedback_dw_blend") {
        g_sdirk3_config.precond_phi_feedback_dw_blend = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_phi_feedback_dw_blend = " << g_sdirk3_config.precond_phi_feedback_dw_blend << std::endl;
    } else if (key == "precond_phi_feedback_stage_scale") {
        g_sdirk3_config.precond_phi_feedback_stage_scale = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_phi_feedback_stage_scale = " << g_sdirk3_config.precond_phi_feedback_stage_scale << std::endl;
    } else if (key == "precond_phi_w_schur_boost_cap") {
        g_sdirk3_config.precond_phi_w_schur_boost_cap = std::clamp(value, 0.0f, 10.0f);
        std::cerr << "[CONFIG] precond_phi_w_schur_boost_cap = " << g_sdirk3_config.precond_phi_w_schur_boost_cap << std::endl;
    } else if (key == "precond_phi_w_schur_backsub_relax") {
        g_sdirk3_config.precond_phi_w_schur_backsub_relax = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_phi_w_schur_backsub_relax = " << g_sdirk3_config.precond_phi_w_schur_backsub_relax << std::endl;
    } else if (key == "precond_phi_w_schur_alpha_gs") {
        g_sdirk3_config.precond_phi_w_schur_alpha_gs = std::clamp(value, 0.0f, 2.0f);
        std::cerr << "[CONFIG] precond_phi_w_schur_alpha_gs = " << g_sdirk3_config.precond_phi_w_schur_alpha_gs << std::endl;
    // rhs_source is int-only — use set_config_int (not float). Removed from float path.
    } else if (key == "precond_phi_w_schur_cross_thresh") {
        g_sdirk3_config.precond_phi_w_schur_cross_thresh = std::clamp(value, 0.0f, 100.0f);
        std::cerr << "[CONFIG] precond_phi_w_schur_cross_thresh = " << g_sdirk3_config.precond_phi_w_schur_cross_thresh << std::endl;
    } else if (key == "precond_phi_w_schur_cap_decay") {
        g_sdirk3_config.precond_phi_w_schur_cap_decay = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_phi_w_schur_cap_decay = " << g_sdirk3_config.precond_phi_w_schur_cap_decay << std::endl;
    } else if (key == "precond_phi_w_schur_ru_scale") {
        g_sdirk3_config.precond_phi_w_schur_ru_scale = std::clamp(value, 0.0f, 10.0f);
        std::cerr << "[CONFIG] precond_phi_w_schur_ru_scale = " << g_sdirk3_config.precond_phi_w_schur_ru_scale << std::endl;
    } else if (key == "precond_phi_w_schur_ru_thresh") {
        g_sdirk3_config.precond_phi_w_schur_ru_thresh = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_phi_w_schur_ru_thresh = " << g_sdirk3_config.precond_phi_w_schur_ru_thresh << std::endl;
    } else if (key == "precond_du_weak_factor") {
        g_sdirk3_config.precond_du_weak_factor = std::clamp(value, 0.001f, 1.0f);
        std::cerr << "[CONFIG] precond_du_weak_factor = " << g_sdirk3_config.precond_du_weak_factor << std::endl;
    } else if (key == "precond_du_weak_ru_thresh") {
        g_sdirk3_config.precond_du_weak_ru_thresh = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_du_weak_ru_thresh = " << g_sdirk3_config.precond_du_weak_ru_thresh << std::endl;
    } else if (key == "stage_gate_rel_threshold") {
        g_sdirk3_config.stage_gate_rel_threshold = std::max(1.0f, value);
        std::cerr << "[CONFIG] stage_gate_rel_threshold = " << g_sdirk3_config.stage_gate_rel_threshold << std::endl;
    } else if (key == "stage3_gate_rel_threshold") {
        g_sdirk3_config.stage3_gate_rel_threshold = std::max(0.0f, value);
        std::cerr << "[CONFIG] stage3_gate_rel_threshold = " << g_sdirk3_config.stage3_gate_rel_threshold
                  << " (0=use stage_gate_rel_threshold)" << std::endl;
    } else if (key == "split_explicit_epssm") {
        g_sdirk3_config.split_explicit_epssm = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] split_explicit_epssm = " << g_sdirk3_config.split_explicit_epssm << std::endl;
    } else if (key == "split_explicit_smdiv") {
        g_sdirk3_config.split_explicit_smdiv = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] split_explicit_smdiv = " << g_sdirk3_config.split_explicit_smdiv << std::endl;
    } else if (key == "split_explicit_emdiv") {
        g_sdirk3_config.split_explicit_emdiv = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] split_explicit_emdiv = " << g_sdirk3_config.split_explicit_emdiv << std::endl;
    } else if (key == "stage_damp_rel_threshold") {
        g_sdirk3_config.stage_damp_rel_threshold = std::max(1.0f, value);
        std::cerr << "[CONFIG] stage_damp_rel_threshold = " << g_sdirk3_config.stage_damp_rel_threshold << std::endl;
    } else if (key == "stage_gate_growth_cap") {
        g_sdirk3_config.stage_gate_growth_cap = std::max(1.0f, value);
        std::cerr << "[CONFIG] stage_gate_growth_cap = " << g_sdirk3_config.stage_gate_growth_cap << std::endl;
    } else if (key == "stage_gate_K_floor") {
        g_sdirk3_config.stage_gate_K_floor = std::max(0.01f, value);
        std::cerr << "[CONFIG] stage_gate_K_floor = " << g_sdirk3_config.stage_gate_K_floor << std::endl;
    } else if (key == "mode3_retry_stage4_clip_abs") {
        g_sdirk3_config.mode3_retry_stage4_clip_abs = std::max(1.0f, value);
        std::cerr << "[CONFIG] mode3_retry_stage4_clip_abs = "
                  << g_sdirk3_config.mode3_retry_stage4_clip_abs << std::endl;
    } else if (key == "mode3_retry_handoff_clip_abs") {
        g_sdirk3_config.mode3_retry_handoff_clip_abs = std::max(1.0f, value);
        std::cerr << "[CONFIG] mode3_retry_handoff_clip_abs = "
                  << g_sdirk3_config.mode3_retry_handoff_clip_abs << std::endl;
    } else if (key == "progress_invariant_min_ratio") {
        g_sdirk3_config.progress_invariant_min_ratio = std::max(0.0f, value);
        std::cerr << "[CONFIG] progress_invariant_min_ratio = "
                  << g_sdirk3_config.progress_invariant_min_ratio << std::endl;
    } else if (key == "trust_region_max_relative_update") {
        g_sdirk3_config.trust_region_max_relative_update = std::max(0.1f, value);
        std::cerr << "[CONFIG] trust_region_max_relative_update = " << g_sdirk3_config.trust_region_max_relative_update << std::endl;
    // v20.14 r49/r59: Smoothing + block-aware trust-region + stage2/3 krylov tol
    } else if (key == "stage2_krylov_tol") {
        g_sdirk3_config.stage2_krylov_tol = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] stage2_krylov_tol = " << g_sdirk3_config.stage2_krylov_tol << std::endl;
    } else if (key == "stage2_ew_eta_min") {
        g_sdirk3_config.stage2_ew_eta_min = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] stage2_ew_eta_min = " << g_sdirk3_config.stage2_ew_eta_min << std::endl;
    } else if (key == "stage2_ew_eta_max") {
        g_sdirk3_config.stage2_ew_eta_max = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] stage2_ew_eta_max = " << g_sdirk3_config.stage2_ew_eta_max << std::endl;
    } else if (key == "stage3_krylov_tol") {
        g_sdirk3_config.stage3_krylov_tol = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] stage3_krylov_tol = " << g_sdirk3_config.stage3_krylov_tol << std::endl;
    } else if (key == "stage3_ew_eta_min") {
        g_sdirk3_config.stage3_ew_eta_min = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] stage3_ew_eta_min = " << g_sdirk3_config.stage3_ew_eta_min << std::endl;
    } else if (key == "stage3_ew_eta_max") {
        g_sdirk3_config.stage3_ew_eta_max = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] stage3_ew_eta_max = " << g_sdirk3_config.stage3_ew_eta_max << std::endl;
    } else if (key == "gmres_warmstart_quality_gate") {
        g_sdirk3_config.gmres_warmstart_quality_gate = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] gmres_warmstart_quality_gate = "
                  << g_sdirk3_config.gmres_warmstart_quality_gate << std::endl;
    } else if (key == "inn_q_min") {
        g_sdirk3_config.inn_q_min = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] inn_q_min = "
                  << g_sdirk3_config.inn_q_min << std::endl;
    } else if (key == "inn_gate_rel_tol") {
        g_sdirk3_config.inn_gate_rel_tol = std::max(value, 0.0f);
        std::cerr << "[CONFIG] inn_gate_rel_tol = "
                  << g_sdirk3_config.inn_gate_rel_tol << std::endl;
    } else if (key == "inn_gate_q_noise") {
        g_sdirk3_config.inn_gate_q_noise = std::max(value, 0.0f);
        std::cerr << "[CONFIG] inn_gate_q_noise = "
                  << g_sdirk3_config.inn_gate_q_noise << std::endl;
    } else if (key == "inn_beta_max") {
        g_sdirk3_config.inn_beta_max = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] inn_beta_max = "
                  << g_sdirk3_config.inn_beta_max << std::endl;
    } else if (key == "inn_tol_ramp_gamma") {
        g_sdirk3_config.inn_tol_ramp_gamma = std::clamp(value, 1.0e-3f, 1.0f);
        std::cerr << "[CONFIG] inn_tol_ramp_gamma = "
                  << g_sdirk3_config.inn_tol_ramp_gamma << std::endl;
    } else if (key == "precond_horizontal_smooth_alpha") {
        g_sdirk3_config.precond_horizontal_smooth_alpha = std::clamp(value, 0.0f, 0.25f);
        std::cerr << "[CONFIG] precond_horizontal_smooth_alpha = " << g_sdirk3_config.precond_horizontal_smooth_alpha << std::endl;
    } else if (key == "precond_vertical_smooth_alpha") {
        g_sdirk3_config.precond_vertical_smooth_alpha = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] precond_vertical_smooth_alpha = " << g_sdirk3_config.precond_vertical_smooth_alpha << std::endl;
    } else if (key == "trust_fallback_ratio") {
        g_sdirk3_config.trust_fallback_ratio = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] trust_fallback_ratio = " << g_sdirk3_config.trust_fallback_ratio << std::endl;
    } else if (key == "trust_region_block_aware_thresh") {
        g_sdirk3_config.trust_region_block_aware_thresh = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] trust_region_block_aware_thresh = " << g_sdirk3_config.trust_region_block_aware_thresh << std::endl;
    } else if (key == "direct_u_solve_thresh") {
        g_sdirk3_config.direct_u_solve_thresh = std::clamp(value, 0.0f, 1.0f);
        std::cerr << "[CONFIG] direct_u_solve_thresh = " << g_sdirk3_config.direct_u_solve_thresh << std::endl;
    } else if (key == "sign_smooth_delta") {
        g_sdirk3_config.sign_smooth_delta = std::max(1e-6f, value);  // v20.14r27z: enforce > 0
        std::cerr << "[CONFIG] sign_smooth_delta = " << g_sdirk3_config.sign_smooth_delta << std::endl;
    } else if (key == "gmres_stagnation_threshold") {
        g_sdirk3_config.gmres_stagnation_threshold = std::clamp(value, 0.5f, 1.0f);
        std::cerr << "[CONFIG] gmres_stagnation_threshold = " << g_sdirk3_config.gmres_stagnation_threshold << std::endl;
    } else if (key == "newton_gmres_quality_threshold") {
        g_sdirk3_config.newton_gmres_quality_threshold = std::clamp(value, 0.5f, 1.0f);
        std::cerr << "[CONFIG] newton_gmres_quality_threshold = " << g_sdirk3_config.newton_gmres_quality_threshold << std::endl;
    // v20.14r27l: S-scaling weights via runtime setter
    } else if (key == "scale_u") {
        g_sdirk3_config.scale_u = std::max(0.01f, value);
    } else if (key == "scale_ph") {
        g_sdirk3_config.scale_ph = std::max(0.01f, value);
    } else if (key == "scale_t") {
        g_sdirk3_config.scale_t = std::max(0.01f, value);
    } else if (key == "scale_mu") {
        g_sdirk3_config.scale_mu = std::max(0.01f, value);
    } else {
        std::cerr << "[CONFIG WARNING] Unknown float config key: " << key << std::endl;
    }
}

void wrf_sdirk3_set_config_bool(const char* name, int value) {
    if (!name) { std::cerr << "[CONFIG] set_config_bool: null key" << std::endl; return; }
    if (warnIfWorkersStarted(name)) return;

    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(), safe_tolower);

    if (key == "adaptive_timestep") {
        g_sdirk3_config.adaptive_timestep = (value != 0);
    } else if (key == "use_physics") {
        g_sdirk3_config.use_physics = (value != 0);
    } else if (key == "frozen_physics_jvp") {
        g_sdirk3_config.frozen_physics_jvp = (value != 0);
    } else if (key == "use_autograd") {
        g_sdirk3_config.use_autograd = (value != 0);
        // Sync jvp_method with use_autograd flag
        g_sdirk3_config.jvp_method = g_sdirk3_config.use_autograd ? SDIRK3Config::JVP_AUTOGRAD : SDIRK3Config::JVP_FINITE_DIFF;
        std::cerr << "[CONFIG DEBUG] Setting use_autograd = " << g_sdirk3_config.use_autograd
                  << " (from value " << value << "), jvp_method = " << g_sdirk3_config.jvp_method << std::endl;
    } else if (key == "use_jvp_cache") {
        g_sdirk3_config.use_jvp_cache = (value != 0);
    } else if (key == "jvp_checkpointing") {
        g_sdirk3_config.jvp_checkpointing = (value != 0);
    } else if (key == "jvp_graph_caching") {
        g_sdirk3_config.jvp_graph_caching = (value != 0);
    } else if (key == "jvp_batched") {
        g_sdirk3_config.jvp_batched = (value != 0);
    } else if (key == "jvp_mixed_precision") {
        g_sdirk3_config.jvp_mixed_precision = (value != 0);
    } else if (key == "check_nan") {
        g_sdirk3_config.check_nan = (value != 0);
    } else if (key == "check_conservation") {
        g_sdirk3_config.check_conservation = (value != 0);
    } else if (key == "check_cfl") {
        g_sdirk3_config.check_cfl = (value != 0);
    } else if (key == "nk_adaptive_tol") {
        g_sdirk3_config.nk_adaptive_tol = (value != 0);
    } else if (key == "nk_line_search") {
        g_sdirk3_config.nk_line_search = (value != 0);
    } else if (key == "implicit_acoustic") {
        g_sdirk3_config.implicit_acoustic = (value != 0);
    } else if (key == "implicit_gravity") {
        g_sdirk3_config.implicit_gravity = (value != 0);
    } else if (key == "implicit_rayleigh") {
        g_sdirk3_config.implicit_rayleigh = (value != 0);
    } else if (key == "implicit_wdamp") {
        g_sdirk3_config.implicit_wdamp = (value != 0);
    } else if (key == "implicit_vdiff") {
        g_sdirk3_config.implicit_vdiff = (value != 0);
    } else if (key == "implicit_hdiff") {
        g_sdirk3_config.implicit_hdiff = (value != 0);
    } else if (key == "implicit_divergence") {
        g_sdirk3_config.implicit_divergence = (value != 0);
    } else if (key == "precond_diagonal_only") {
        g_sdirk3_config.precond_diagonal_only = (value != 0);
    } else if (key == "precond_block_jacobi") {
        g_sdirk3_config.precond_block_jacobi = (value != 0);
    } else if (key == "precond_ilu") {
        g_sdirk3_config.precond_ilu = (value != 0);
    } else if (key == "precond_multigrid") {
        g_sdirk3_config.precond_multigrid = (value != 0);
    } else if (key == "memory_pool") {
        g_sdirk3_config.memory_pool = (value != 0);
    } else if (key == "tensor_checkpointing") {
        g_sdirk3_config.tensor_checkpointing = (value != 0);
    } else if (key == "gradient_checkpointing") {
        g_sdirk3_config.gradient_checkpointing = (value != 0);
    } else if (key == "save_trajectory") {
        g_sdirk3_config.save_trajectory = (value != 0);
    } else if (key == "retain_graph" || key == "retain_graph_for_adjoint") {
        g_sdirk3_config.retain_graph_for_adjoint = (value != 0);
    } else if (key == "obs_aware_4dvar" || key == "observation_aware_4dvar") {
        g_sdirk3_config.obs_aware_4dvar = (value != 0);
    } else if (key == "check_staggered_consistency") {
        g_sdirk3_config.check_staggered_consistency = (value != 0);
    } else if (key == "collect_validation_stats") {
        g_sdirk3_config.collect_validation_stats = (value != 0);
    } else if (key == "enable_benchmarking") {
        g_sdirk3_config.enable_benchmarking = (value != 0);
    } else if (key == "benchmark_memory_tracking") {
        g_sdirk3_config.benchmark_memory_tracking = (value != 0);
    } else if (key == "benchmark_auto_export") {
        g_sdirk3_config.benchmark_auto_export = (value != 0);
    } else if (key == "jvp_use_forward_diff") {
        g_sdirk3_config.jvp_use_forward_diff = (value != 0);
        std::cerr << "[CONFIG DEBUG] Setting jvp_use_forward_diff = "
                  << g_sdirk3_config.jvp_use_forward_diff << std::endl;

    // AD FIX 2025-12-26: AD strict mode runtime setter
    } else if (key == "ad_strict_mode") {
        g_sdirk3_config.ad_strict_mode = (value != 0);
        // Sync with global flag for metric_utils.h enforcement
        set_ad_strict_mode(g_sdirk3_config.ad_strict_mode);
        std::cerr << "[CONFIG DEBUG] Setting ad_strict_mode = "
                  << g_sdirk3_config.ad_strict_mode << std::endl;

    // FIX 2025-12-28: use_max_height_for_damping runtime setter
    } else if (key == "use_max_height_for_damping") {
        g_sdirk3_config.use_max_height_for_damping = (value != 0);

    // FIX 2025-12-28: moving_nest runtime setter
    } else if (key == "moving_nest") {
        g_sdirk3_config.moving_nest = (value != 0);
        // FIX 2025-12-29 Issue 1: Sync with metric_utils isMovingNestMode flag
        metric_utils::setMovingNestMode(g_sdirk3_config.moving_nest);

    // FIX Round123: Device index clamp warning control
    } else if (key == "warn_on_device_index_clamp") {
        g_sdirk3_config.warn_on_device_index_clamp = (value != 0);

    } else if (key == "omega_update_ref_per_newton") {
        g_sdirk3_config.omega_update_ref_per_newton = (value != 0);
        std::cerr << "[CONFIG] omega_update_ref_per_newton = " << g_sdirk3_config.omega_update_ref_per_newton << std::endl;

    } else if (key == "imex_enabled") {
        g_sdirk3_config.imex_enabled = (value != 0);
        // Log raw + effective (imex_enabled may change effective mode)
        int eff = g_sdirk3_config.imex_split_mode;
        if (eff == 0 && g_sdirk3_config.imex_enabled) eff = 1;
        const char* eff_label = (eff == 0) ? "off/baseline" :
                                (eff == 1) ? "frozen IMEX" :
                                (eff == 2) ? "post-solve IMEX SDIRK3" :
                                (eff == 3) ? "post-solve IMEX ARK324" :
                                             "UNKNOWN -> baseline (clamped)";
        std::cerr << "[CONFIG] IMEX raw: imex_split_mode=" << g_sdirk3_config.imex_split_mode
                  << ", imex_enabled=" << g_sdirk3_config.imex_enabled << std::endl;
        std::cerr << "[CONFIG] IMEX effective: mode=" << eff
                  << " (" << eff_label << ")" << std::endl;

    // IMEX post-solve DA tangent-linear flags (2026-02-01)
    } else if (key == "imex_slow_in_tangent") {
        g_sdirk3_config.imex_slow_in_tangent = (value != 0);
        std::cerr << "[CONFIG] imex_slow_in_tangent = " << g_sdirk3_config.imex_slow_in_tangent << std::endl;

    } else if (key == "imex_phys_in_tangent") {
        g_sdirk3_config.imex_phys_in_tangent = (value != 0);
        std::cerr << "[CONFIG] imex_phys_in_tangent = " << g_sdirk3_config.imex_phys_in_tangent << std::endl;

    } else if (key == "stage1_explicit") {
        g_sdirk3_config.stage1_explicit = (value != 0);
        std::cerr << "[CONFIG] stage1_explicit = "
                  << g_sdirk3_config.stage1_explicit
                  << (g_sdirk3_config.stage1_explicit ? " (stage-1 Newton/GMRES bypass)" : "")
                  << std::endl;
    } else if (key == "stage3_warmstart") {
        g_sdirk3_config.stage3_warmstart = (value != 0);
        std::cerr << "[CONFIG] stage3_warmstart = "
                  << g_sdirk3_config.stage3_warmstart
                  << (g_sdirk3_config.stage3_warmstart ? " (experimental Stage-3 predictor path)" : "")
                  << std::endl;
    } else if (key == "jvp_auto_bench_quality_gate") {
        g_sdirk3_config.jvp_auto_bench_quality_gate = (value != 0);
        std::cerr << "[CONFIG] jvp_auto_bench_quality_gate = "
                  << (g_sdirk3_config.jvp_auto_bench_quality_gate ? "true" : "false") << std::endl;
    } else if (key == "jvp_auto_bench_lock_reset_stage1") {
        g_sdirk3_config.jvp_auto_bench_lock_reset_stage1 = (value != 0);
        std::cerr << "[CONFIG] jvp_auto_bench_lock_reset_stage1 = "
                  << (g_sdirk3_config.jvp_auto_bench_lock_reset_stage1 ? "true" : "false") << std::endl;
    } else if (key == "solver_telemetry") {
        g_sdirk3_config.solver_telemetry = (value != 0);
        std::cerr << "[CONFIG] solver_telemetry = "
                  << (g_sdirk3_config.solver_telemetry ? "true" : "false") << std::endl;
    } else if (key == "progress_invariant_enable") {
        g_sdirk3_config.progress_invariant_enable = (value != 0);
        std::cerr << "[CONFIG] progress_invariant_enable = "
                  << (g_sdirk3_config.progress_invariant_enable ? "true" : "false") << std::endl;
    } else if (key == "variable_pc_event_log") {
        g_sdirk3_config.variable_pc_event_log = (value != 0);
        std::cerr << "[CONFIG] variable_pc_event_log = "
                  << (g_sdirk3_config.variable_pc_event_log ? "true" : "false") << std::endl;
    } else if (key == "gmres_warmstart") {
        g_sdirk3_config.gmres_warmstart = (value != 0);
        std::cerr << "[CONFIG] gmres_warmstart = "
                  << (g_sdirk3_config.gmres_warmstart ? "true" : "false") << std::endl;
    } else if (key == "inn_warmstart_enable") {
        g_sdirk3_config.inn_warmstart_enable = (value != 0);
        std::cerr << "[CONFIG] inn_warmstart_enable = "
                  << (g_sdirk3_config.inn_warmstart_enable ? "true" : "false") << std::endl;
    } else if (key == "inn_residual_gate_enable") {
        g_sdirk3_config.inn_residual_gate_enable = (value != 0);
        std::cerr << "[CONFIG] inn_residual_gate_enable = "
                  << (g_sdirk3_config.inn_residual_gate_enable ? "true" : "false") << std::endl;
    } else if (key == "inn_ood_guard_enable") {
        g_sdirk3_config.inn_ood_guard_enable = (value != 0);
        std::cerr << "[CONFIG] inn_ood_guard_enable = "
                  << (g_sdirk3_config.inn_ood_guard_enable ? "true" : "false") << std::endl;
    } else if (key == "inn_tol_ramp_enable") {
        g_sdirk3_config.inn_tol_ramp_enable = (value != 0);
        std::cerr << "[CONFIG] inn_tol_ramp_enable = "
                  << (g_sdirk3_config.inn_tol_ramp_enable ? "true" : "false") << std::endl;
    } else if (key == "rhs_bc_parity") {
        g_sdirk3_config.rhs_bc_parity = (value != 0);
        std::cerr << "[CONFIG] rhs_bc_parity = "
                  << (g_sdirk3_config.rhs_bc_parity ? "true" : "false") << std::endl;
    } else if (key == "mass_pgf_bc_guard") {
        g_sdirk3_config.mass_pgf_bc_guard = (value != 0);
        std::cerr << "[CONFIG] mass_pgf_bc_guard = "
                  << (g_sdirk3_config.mass_pgf_bc_guard ? "true" : "false") << std::endl;
    } else if (key == "stage_boundary_sync") {
        g_sdirk3_config.stage_boundary_sync = (value != 0);
        std::cerr << "[CONFIG] stage_boundary_sync = "
                  << (g_sdirk3_config.stage_boundary_sync ? "true" : "false") << std::endl;

    } else if (key == "enable_stage_halo_exchange") {
        g_sdirk3_config.enable_stage_halo_exchange = (value != 0);
        std::cerr << "[CONFIG] enable_stage_halo_exchange = "
                  << g_sdirk3_config.enable_stage_halo_exchange << std::endl;

    } else if (key == "enable_ad_halo_exchange") {
        g_sdirk3_config.enable_ad_halo_exchange = (value != 0);
        std::cerr << "[CONFIG] enable_ad_halo_exchange = "
                  << g_sdirk3_config.enable_ad_halo_exchange << std::endl;

    } else if (key == "hopeless_relax") {
        g_sdirk3_config.hopeless_relax = (value != 0);
        std::cerr << "[CONFIG] hopeless_relax = "
                  << (g_sdirk3_config.hopeless_relax ? "true" : "false") << std::endl;

    } else if (key == "precond_ratio_guard_warn_only") {
        g_sdirk3_config.precond_ratio_guard_warn_only = (value != 0);
        std::cerr << "[CONFIG] precond_ratio_guard_warn_only = "
                  << (g_sdirk3_config.precond_ratio_guard_warn_only ? "true" : "false") << std::endl;

    // Preconditioner-RHS scope consistency flags (2026-02-01)
    } else if (key == "precond_match_rhs") {
        g_sdirk3_config.precond_match_rhs = (value != 0);
        std::cerr << "[CONFIG] precond_match_rhs = " << g_sdirk3_config.precond_match_rhs << std::endl;
    } else if (key == "precond_extra_rayleigh") {
        g_sdirk3_config.precond_extra_rayleigh = (value != 0);
        std::cerr << "[CONFIG] precond_extra_rayleigh = " << g_sdirk3_config.precond_extra_rayleigh << std::endl;
    } else if (key == "precond_extra_wdamp") {
        g_sdirk3_config.precond_extra_wdamp = (value != 0);
        std::cerr << "[CONFIG] precond_extra_wdamp = " << g_sdirk3_config.precond_extra_wdamp << std::endl;
    } else if (key == "precond_extra_vdiff") {
        g_sdirk3_config.precond_extra_vdiff = (value != 0);
        std::cerr << "[CONFIG] precond_extra_vdiff = " << g_sdirk3_config.precond_extra_vdiff << std::endl;
    } else if (key == "precond_extra_divergence") {
        g_sdirk3_config.precond_extra_divergence = (value != 0);
        std::cerr << "[CONFIG] precond_extra_divergence = " << g_sdirk3_config.precond_extra_divergence << std::endl;
    } else if (key == "precond_smooth_boundary_guard") {
        g_sdirk3_config.precond_smooth_boundary_guard = (value != 0);
        std::cerr << "[CONFIG] precond_smooth_boundary_guard = "
                  << (g_sdirk3_config.precond_smooth_boundary_guard ? "true" : "false") << std::endl;
    } else if (key == "trust_fallback_relax") {
        g_sdirk3_config.trust_fallback_relax = (value != 0);
        std::cerr << "[CONFIG] trust_fallback_relax = "
                  << (g_sdirk3_config.trust_fallback_relax ? "true" : "false") << std::endl;
    } else if (key == "mode3_stage4_severe_abort") {
        g_sdirk3_config.mode3_stage4_severe_abort = (value != 0);
        std::cerr << "[CONFIG] mode3_stage4_severe_abort = "
                  << (g_sdirk3_config.mode3_stage4_severe_abort ? "true" : "false") << std::endl;
    } else if (key == "mode3_retry_nan_sanitize") {
        g_sdirk3_config.mode3_retry_nan_sanitize = (value != 0);
        std::cerr << "[CONFIG] mode3_retry_nan_sanitize = "
                  << (g_sdirk3_config.mode3_retry_nan_sanitize ? "true" : "false") << std::endl;
    } else if (key == "mode3_firsthit_probe") {
        g_sdirk3_config.mode3_firsthit_probe = (value != 0);
        std::cerr << "[CONFIG] mode3_firsthit_probe = "
                  << (g_sdirk3_config.mode3_firsthit_probe ? "true" : "false") << std::endl;
    } else if (key == "mode3_retry_stage4_preclip") {
        g_sdirk3_config.mode3_retry_stage4_preclip = (value != 0);
        std::cerr << "[CONFIG] mode3_retry_stage4_preclip = "
                  << (g_sdirk3_config.mode3_retry_stage4_preclip ? "true" : "false") << std::endl;
    } else if (key == "mode3_retry_handoff_clip") {
        g_sdirk3_config.mode3_retry_handoff_clip = (value != 0);
        std::cerr << "[CONFIG] mode3_retry_handoff_clip = "
                  << (g_sdirk3_config.mode3_retry_handoff_clip ? "true" : "false") << std::endl;

    // v20.14r26: Newton non-convergence policy
    } else if (key == "hard_abort_on_newton_fail") {
        g_sdirk3_config.hard_abort_on_newton_fail = (value != 0);
        std::cerr << "[CONFIG] hard_abort_on_newton_fail = "
                  << (g_sdirk3_config.hard_abort_on_newton_fail ? "true" : "false") << std::endl;

    // v20.14r27i: Trust-region enable/disable
    } else if (key == "nk_trust_region") {
        g_sdirk3_config.nk_trust_region = (value != 0);
        std::cerr << "[CONFIG] nk_trust_region = "
                  << (g_sdirk3_config.nk_trust_region ? "true" : "false") << std::endl;

    // v20.14 Phase 2: Coupled Φ-W preconditioner
    } else if (key == "precond_coupled_phi_w") {
        g_sdirk3_config.precond_coupled_phi_w = (value != 0);
        std::cerr << "[CONFIG] precond_coupled_phi_w = "
                  << (g_sdirk3_config.precond_coupled_phi_w ? "true" : "false") << std::endl;
    } else if (key == "stagnation_gate_enable") {
        g_sdirk3_config.stagnation_gate_enable = (value != 0);
        std::cerr << "[CONFIG] stagnation_gate_enable = "
                  << (g_sdirk3_config.stagnation_gate_enable ? "true" : "false") << std::endl;
    } else if (key == "stage_require_convergence") {
        g_sdirk3_config.stage_require_convergence = (value != 0);
        std::cerr << "[CONFIG] stage_require_convergence = "
                  << (g_sdirk3_config.stage_require_convergence ? "true" : "false") << std::endl;
    } else if (key == "hevi_split") {
        g_sdirk3_config.hevi_split = (value != 0);
        std::cerr << "[CONFIG] hevi_split = "
                  << (g_sdirk3_config.hevi_split ? "true" : "false") << std::endl;
    } else if (key == "split_explicit") {
        g_sdirk3_config.split_explicit = (value != 0);
        std::cerr << "[CONFIG] split_explicit = "
                  << (g_sdirk3_config.split_explicit ? "true" : "false") << std::endl;
    } else if (key == "split_explicit_top_lid") {
        g_sdirk3_config.split_explicit_top_lid = (value != 0);
        std::cerr << "[CONFIG] split_explicit_top_lid = "
                  << (g_sdirk3_config.split_explicit_top_lid ? "true" : "false") << std::endl;
    } else if (key == "precond_phi_feedback_fallback_gs") {
        g_sdirk3_config.precond_phi_feedback_fallback_gs = (value != 0);
        std::cerr << "[CONFIG] precond_phi_feedback_fallback_gs = "
                  << (g_sdirk3_config.precond_phi_feedback_fallback_gs ? "true" : "false") << std::endl;
    } else if (key == "precond_phi_feedback_soft_cap") {
        g_sdirk3_config.precond_phi_feedback_soft_cap = (value != 0);
        std::cerr << "[CONFIG] precond_phi_feedback_soft_cap = "
                  << (g_sdirk3_config.precond_phi_feedback_soft_cap ? "tanh" : "hard") << std::endl;

    } else {
        std::cerr << "[CONFIG WARNING] Unknown bool config key: " << key << std::endl;
    }
}

void wrf_sdirk3_load_config_from_namelist(const char* filename) {
    if (!filename) { std::cerr << "[CONFIG] load_config_from_namelist: null filename" << std::endl; return; }
    // v20.14r17: Reject if workers already started (init-only function).
    if (warnIfWorkersStarted("__load_config_from_namelist__")) return;

    std::ifstream file(filename);
    if (file.is_open()) {
        std::stringstream buffer;
        buffer << file.rdbuf();
        g_sdirk3_config.load_from_namelist(buffer.str());
        file.close();
    } else {
        std::cerr << "Warning: Could not open SDIRK3 config file: " << filename << std::endl;
    }
    
    // Also check environment variables
    g_sdirk3_config.load_from_env();

    // CRITICAL: Log final config values to verify settings
    std::cerr << "\n=== SDIRK3 CONFIG LOADED ===" << std::endl;
    std::cerr << "  debug_level: " << g_sdirk3_config.debug_level << std::endl;
    std::cerr << "  n_threads: " << g_sdirk3_config.n_threads << std::endl;
    std::cerr << "  use_autograd: " << (g_sdirk3_config.use_autograd ? "TRUE" : "FALSE") << std::endl;
    std::cerr << "  max_newton_iter: " << g_sdirk3_config.max_newton_iter << std::endl;
    std::cerr << "  newton_tol: " << g_sdirk3_config.newton_tol << std::endl;
    std::cerr << "=========================" << std::endl;

    // v20.14r18: Full reset on validation failure.
    // Previous partial fallback could leave invalid fields (e.g. gmres_restart, newton_tol).
    if (!g_sdirk3_config.validate()) {
        std::cerr << "Error: Invalid SDIRK3 configuration — full reset to defaults" << std::endl;
        uint32_t saved_gen = g_sdirk3_config.theta_config_generation;
        g_sdirk3_config = SDIRK3Config();
        g_sdirk3_config.theta_config_generation = saved_gen + 1;  // signal override release
        // v20.14r19: Re-sync subsystems after full reset.
        syncValidationSettings(g_sdirk3_config.debug_level);
        metric_utils::setMetricDebugLevel(g_sdirk3_config.debug_level);
        // Sanity check: default config must pass validation.
        if (!g_sdirk3_config.validate()) {
            std::cerr << "FATAL: Default SDIRK3Config fails validation — aborting" << std::endl;
            std::abort();
        }
    }
}

void wrf_sdirk3_print_config() {
    g_sdirk3_config.print();
}

// ⚠️ FIX Round133: FORTRAN-OPTIMIZED API - C/C++ CALLERS SEE NOTE BELOW ⚠️
// This setter includes Fortran negative-value detection that CLAMPS values > INT64_MAX to 0.
// Pure C/C++ callers needing the full uint64 range should use direct assignment instead.
//
// FIX Round127: 64-bit unsigned integer setter for config values that need full uint64_t range.
// This avoids truncation when callers pass values > INT_MAX through the int setter.
//
// FIX Round129: NEGATIVE VALUE HANDLING (Fortran C_INT64_T compatibility)
// Fortran INTEGER(C_INT64_T) is SIGNED (-2^63 to 2^63-1), but this setter takes uint64_t.
// When Fortran passes a negative value, bit reinterpretation yields large positive uint64:
//   -1 (Fortran) → 0xFFFFFFFFFFFFFFFF (C++) = UINT64_MAX
//   -N (Fortran) → 2^64 - N (C++)
// Detection: value > INT64_MAX indicates likely negative Fortran input.
// Policy: WARN and CLAMP to 0 (matching int setter behavior for consistency).
//
// FIX Round130: ONE-LINE SUMMARY FOR FORTRAN USERS:
//   "Fortran 음수 → 2's complement unsigned 변환 → 큰 값 검출 → 0으로 클램프 + 경고"
//
// FIX Round132/Round145: C/C++ CALLER NOTE - ★ RECOMMENDED: Use direct assignment ★
// This clamp is FORTRAN-SPECIFIC. Pure C/C++ callers who legitimately need values
// > INT64_MAX (e.g., 0x8000000000000001 = 9223372036854775809) will be incorrectly
// clamped. For such cases:
//   - ★ RECOMMENDED ★: Use direct C++ assignment (no clamp, no overhead):
//       g_sdirk3_config.warn_throttle_count = large_value;
//       g_sdirk3_config.grid_metric_memcmp_period = large_value;
//   - Alternative: Define wrf_sdirk3_set_config_uint64_unchecked() if API needed.
// This setter is designed for Fortran interop; C/C++ callers should prefer direct
// assignment for both performance (no string parsing) and correctness (no clamp).
// Current config items rarely need values > INT64_MAX, but direct assignment is
// still preferred for C/C++ code for clarity and efficiency.
// FIX Round146: THREAD-SAFETY WARNING - Direct assignment is NOT thread-safe.
//   In multi-threaded environments, concurrent writes to g_sdirk3_config can cause
//   data races. Configure BEFORE spawning worker threads, or use external locking.
//
// FIX Round134: KEY-SPECIFIC VALUE POLICIES
// FIX Round137: POLICY SUMMARY
//   - Zero meaning: KEY-DEPENDENT (0=auto for periods, 0=sticky for throttle)
//   - Range: [0, UINT64_MAX] - no explicit max enforcement by setter
// FIX Round139/Round144: Added Unit column for clarity
// FIX Round144: Unit "API calls" = internal function invocations (not Fortran C API calls)
//   - warn_throttle_count: per device-check iteration in solver step
//   - *_period keys: per call to respective internal cache/metric functions
// ┌────────────────────────────────┬──────────┬──────────┬─────────────┬─────────────────────────────┐
// │ Key                            │ Min      │ Max      │ Unit        │ Semantics                   │
// ├────────────────────────────────┼──────────┼──────────┼─────────────┼─────────────────────────────┤
// │ warn_throttle_count            │ 0        │ UINT64_MAX│ check iters │ 0=sticky(never re-warn)    │
// │ grid_metric_memcmp_period      │ 0        │ UINT64_MAX│ func calls  │ 0=auto, 1=strict, N>1=N    │
// │ cuda_grid_metric_sample_period │ 0        │ UINT64_MAX│ func calls  │ 0=auto, N>0=every N        │
// │ cuda_phbase_sample_period      │ 0        │ UINT64_MAX│ func calls  │ 0=auto, N>0=every N        │
// │ lat_cpu_sample_check_period    │ 0        │ UINT64_MAX│ cache hits  │ 0=auto(100/10), N>0=N      │
// └────────────────────────────────┴──────────┴──────────┴─────────────┴─────────────────────────────┘
// NOTE: No explicit max enforcement is done (all uint64 values valid).
// The "0=auto" semantics are handled by the consuming code, not this setter.
//
// FIX Round139/Round144/Round148: RESETTING TO DEFAULTS
// ★ WARNING ★: No reset_config_defaults() API or sentinel value exists.
// Fortran callers must EXPLICITLY set known default values to restore:
//   C++: g_sdirk3_config.warn_throttle_count = 100;
//   Fortran: CALL wrf_sdirk3_set_config_uint64('warn_throttle_count'//C_NULL_CHAR, 100_C_INT64_T)
// FIX Round144: DEFAULT VALUES MAY CHANGE - Always reference wrf_sdirk3_config.h
//   struct SDIRK3Config for current default values. The examples above use values
//   correct at time of writing; see header for authoritative defaults.
// FIX Round148: SENTINEL POLICY - Currently NO sentinel (e.g., -1 or UINT64_MAX) means "use default".
//   FUTURE: If needed, add wrf_sdirk3_reset_config_to_defaults() API or define sentinel.
//   WORKAROUND: Fortran callers should save original values at init if reset is needed.
//
// FIX Round139: DOWNCAST SAFETY WARNING
// Usage sites may use modulo (%) or compare with int/size_t counters.
// Values > INT_MAX (2^31-1) may cause overflow or unexpected period behavior.
// FIX Round140/Round141: KEY-SPECIFIC PRACTICAL LIMITS (example values, tune as needed)
//   - warn_throttle_count: ~10^9 max (default=100, typical <10^4)
//   - *_period keys: ~10^6 max (default=0=auto, typical <10^3)
//   - These are EXAMPLE upper bounds - actual safe max depends on usage site.
//   - Current defaults (100, 0) are well within int32 range - no overflow risk.
//
// FIX Round142: USAGE SITE MAPPING (counter types at each usage site)
// All usage sites use uint64_t for both config value AND counter - NO DOWNCAST RISK.
// ┌────────────────────────────────┬───────────────────────────────┬────────────────────────┐
// │ Config Key                     │ Usage Site                    │ Counter Type           │
// ├────────────────────────────────┼───────────────────────────────┼────────────────────────┤
// │ warn_throttle_count            │ tile_unified_impl.cpp:21026   │ uint64_t (GridInfo)    │
// │ grid_metric_memcmp_period      │ tile_unified_impl.cpp:1117    │ uint64_t (class member)│
// │ cuda_grid_metric_sample_period │ tile_unified_impl.cpp:1137    │ uint64_t (class member)│
// │ cuda_phbase_sample_period      │ tile_unified_impl.cpp:1510    │ uint64_t (class member)│
// │ lat_cpu_sample_check_period    │ boundary_ad.cpp:419           │ uint64_t (mutable)     │
// └────────────────────────────────┴───────────────────────────────┴────────────────────────┘
// All operations: uint64_t % uint64_t (modulo) or uint64_t >= uint64_t (comparison).
// No narrowing conversion occurs at any usage site.
//
// FIX Round149: PERIODIC DOWNCAST VERIFICATION
// Run this to detect accidental int/size_t downcasts at usage sites:
//   grep -rn 'g_sdirk3_config\.\(warn_throttle\|memcmp_period\|sample.*period\)' *.cpp | \
//     grep -v 'uint64_t' | grep -E '\bint\b|\bsize_t\b|\blong\b'
// If any matches: Add explicit uint64_t cast or change counter type to uint64_t.
// WARNING: Implicit narrowing in (int)config.value or config.value % (int)counter is UNSAFE.
//
// FIX Round143: CI MAINTENANCE NOTE
// This table is documentation-only. When adding new uint64 config keys:
//   1. Add key to switch statement below
//   2. Update USAGE SITE MAPPING table above with usage location + counter type
//   3. CI script example (grep-based verification):
//      grep -n "g_sdirk3_config\.\(new_key\)" *.cpp | head -5
//   4. Verify counter type at usage site is uint64_t to prevent downcast issues
// See CI script example in external/sdirk3_lib/docs/ for automated verification.
//
void wrf_sdirk3_set_config_uint64(const char* name, uint64_t value) {
    if (!name) { std::cerr << "[CONFIG] set_config_uint64: null key" << std::endl; return; }
    if (warnIfWorkersStarted(name)) return;

    std::string key(name);
    std::transform(key.begin(), key.end(), key.begin(), safe_tolower);

    // FIX Round129: Detect and handle negative values from Fortran C_INT64_T
    // If value > INT64_MAX, it was likely a negative signed value.
    constexpr uint64_t SIGNED_NEGATIVE_THRESHOLD = static_cast<uint64_t>(INT64_MAX);
    bool was_negative = (value > SIGNED_NEGATIVE_THRESHOLD);
    if (was_negative) {
        // Reconstruct approximate original negative for diagnostic
        int64_t approx_original = static_cast<int64_t>(value);  // Bit-reinterpret back
        std::cerr << "[CONFIG WARNING] wrf_sdirk3_set_config_uint64(\"" << name << "\"): "
                  << "received value " << value << " > INT64_MAX, likely negative Fortran input (~"
                  << approx_original << "). Clamping to 0." << std::endl;
        value = 0;
    }

    if (key == "warn_throttle_count") {
        g_sdirk3_config.warn_throttle_count = value;
        if (g_sdirk3_config.debug_level >= 1) {
            std::cerr << "[CONFIG DEBUG] Setting warn_throttle_count = "
                      << g_sdirk3_config.warn_throttle_count
                      << (was_negative ? " (clamped from negative)" : " (via uint64 setter)") << std::endl;
        }
    } else if (key == "grid_metric_memcmp_period") {
        g_sdirk3_config.grid_metric_memcmp_period = value;
    } else if (key == "cuda_grid_metric_sample_period") {
        g_sdirk3_config.cuda_grid_metric_sample_period = value;
    } else if (key == "cuda_phbase_sample_period") {
        g_sdirk3_config.cuda_phbase_sample_period = value;
    } else if (key == "lat_cpu_sample_check_period") {
        g_sdirk3_config.lat_cpu_sample_check_period = value;
    } else {
        std::cerr << "[CONFIG WARNING] Unknown uint64 config key: " << key << std::endl;
    }
}

// FIX 2025-01-25: Config freeze mechanism C interface
void wrf_sdirk3_mark_workers_started(void) {
    bool was_started = g_workers_started.exchange(true, std::memory_order_release);
    if (!was_started && g_sdirk3_config.debug_level >= 1) {
        std::cerr << "[CONFIG] Workers started - config changes will now trigger warnings"
                  << std::endl;
    }
}

int wrf_sdirk3_is_workers_started(void) {
    return g_workers_started.load(std::memory_order_acquire) ? 1 : 0;
}

// v20.14r27n: Reset freeze flag for reinit/restart scenarios.
// Called from finalize_implicit_sdirk3 before re-initialization.
void wrf_sdirk3_reset_workers_started(void) {
    bool was_started = g_workers_started.exchange(false, std::memory_order_release);
    if (was_started && g_sdirk3_config.debug_level >= 1) {
        std::cerr << "[CONFIG] Workers started flag reset - config changes allowed again"
                  << std::endl;
    }
}

} // extern "C"

} // namespace sdirk3
} // namespace wrf
