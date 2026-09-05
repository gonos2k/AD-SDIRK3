// Production-linked regression tests for configuration parsing and validation.
// This test intentionally exercises SDIRK3Config::load_from_env() itself.
#include "../wrf_sdirk3_config.h"

#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
int failures = 0;

void clear_knobs() {
    unsetenv("WRF_SDIRK3_JVP_EPSILON");
    unsetenv("WRF_SDIRK3_STAGE2_KRYLOV_TOL");
    unsetenv("WRF_SDIRK3_STAGE3_KRYLOV_TOL");
    unsetenv("WRF_SDIRK3_STAGE2_EW_ETA_MIN");
    unsetenv("WRF_SDIRK3_STAGE3_EW_ETA_MAX");
    unsetenv("WRF_SDIRK3_ADAPTIVE_HIGH_THRESHOLD");
    unsetenv("WRF_SDIRK3_ADAPTIVE_LOW_THRESHOLD");
    unsetenv("WRF_SDIRK3_IMEX_SPLIT_MODE");
    unsetenv("WRF_SDIRK3_SPLIT_EXPLICIT_TIME_STEP_SOUND");
}

void expect_throw(const char* name, const char* value) {
    using wrf::sdirk3::g_sdirk3_config;
    clear_knobs();
    g_sdirk3_config = wrf::sdirk3::SDIRK3Config{};
    setenv(name, value, 1);
    bool threw = false;
    try {
        g_sdirk3_config.load_from_env();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    if (!threw) {
        ++failures;
        std::cerr << "FAIL expected rejection: " << name << "=" << value << '\n';
    }
}

void expect_namelist_throw(const char* key, const char* value) {
    using wrf::sdirk3::g_sdirk3_config;
    g_sdirk3_config = wrf::sdirk3::SDIRK3Config{};
    bool threw = false;
    try {
        g_sdirk3_config.load_from_namelist(std::string(key) + " = " + value + "\n");
    } catch (const std::exception&) {
        threw = true;
    }
    if (!threw) {
        ++failures;
        std::cerr << "FAIL expected namelist rejection: " << key << "=" << value << '\n';
    }
}

void expect_invalid_direct(float& field, const char* name, float value) {
    using wrf::sdirk3::g_sdirk3_config;
    g_sdirk3_config = wrf::sdirk3::SDIRK3Config{};
    field = value;
    if (g_sdirk3_config.validate()) {
        ++failures;
        std::cerr << "FAIL validate accepted non-finite " << name << '\n';
    }
}
}  // namespace

int main() {
    using wrf::sdirk3::SDIRK3Config;
    using wrf::sdirk3::g_sdirk3_config;

    // Complete integer parsing rejects trailing junk, overflow, and empty text.
    const char* bad_ints[][2] = {
        {"WRF_SDIRK3_IMEX_SPLIT_MODE", "2junk"},
        {"WRF_SDIRK3_IMEX_SPLIT_MODE", "999999999999999999999999"},
        {"WRF_SDIRK3_IMEX_SPLIT_MODE", ""},
        {"WRF_SDIRK3_SPLIT_EXPLICIT_TIME_STEP_SOUND", "2junk"},
        {"WRF_SDIRK3_SPLIT_EXPLICIT_TIME_STEP_SOUND", "999999999999999999999999"},
        {"WRF_SDIRK3_SPLIT_EXPLICIT_TIME_STEP_SOUND", ""},
    };
    for (const auto& c : bad_ints) expect_throw(c[0], c[1]);

    // Non-finite environment values are rejected before clamping.
    const char* bad_float[][2] = {
        {"WRF_SDIRK3_JVP_EPSILON", "nan"},
        {"WRF_SDIRK3_JVP_EPSILON", "inf"},
        {"WRF_SDIRK3_STAGE2_KRYLOV_TOL", "nan"},
        {"WRF_SDIRK3_STAGE2_KRYLOV_TOL", "inf"},
        {"WRF_SDIRK3_STAGE3_KRYLOV_TOL", "nan"},
        {"WRF_SDIRK3_STAGE3_KRYLOV_TOL", "inf"},
        {"WRF_SDIRK3_STAGE2_EW_ETA_MIN", "inf"},
        {"WRF_SDIRK3_STAGE3_EW_ETA_MAX", "nan"},
        {"WRF_SDIRK3_ADAPTIVE_HIGH_THRESHOLD", "nan"},
        {"WRF_SDIRK3_ADAPTIVE_HIGH_THRESHOLD", "inf"},
        {"WRF_SDIRK3_ADAPTIVE_LOW_THRESHOLD", "nan"},
        {"WRF_SDIRK3_ADAPTIVE_LOW_THRESHOLD", "inf"},
    };
    for (const auto& c : bad_float) expect_throw(c[0], c[1]);

    expect_namelist_throw("sdirk3_stage2_krylov_tol", "nan");
    expect_namelist_throw("sdirk3_stage3_krylov_tol", "1junk");
    expect_namelist_throw("sdirk3_stage2_ew_eta_min", "inf");
    expect_namelist_throw("sdirk3_adaptive_high_threshold", "inf");
    expect_namelist_throw("sdirk3_newton_tol", "nan");
    expect_namelist_throw("sdirk3_krylov_tol", "1junk");
    expect_namelist_throw("sdirk3_imex_split_mode", "2junk");
    expect_namelist_throw("sdirk3_split_explicit_time_step_sound", "2junk");

    // Valid zero and ordinary values retain their existing policy semantics.
    clear_knobs();
    g_sdirk3_config = SDIRK3Config{};
    setenv("WRF_SDIRK3_IMEX_SPLIT_MODE", "0", 1);
    setenv("WRF_SDIRK3_SPLIT_EXPLICIT_TIME_STEP_SOUND", "0", 1);
    setenv("WRF_SDIRK3_JVP_EPSILON", "0.01", 1);
    setenv("WRF_SDIRK3_STAGE2_KRYLOV_TOL", "0.05", 1);
    setenv("WRF_SDIRK3_STAGE3_KRYLOV_TOL", "0.10", 1);
    setenv("WRF_SDIRK3_ADAPTIVE_HIGH_THRESHOLD", "0.70", 1);
    setenv("WRF_SDIRK3_ADAPTIVE_LOW_THRESHOLD", "0.30", 1);
    g_sdirk3_config.imex_enabled = true;
    g_sdirk3_config.load_from_env();
    if (g_sdirk3_config.split_explicit_time_step_sound != 0 ||
        g_sdirk3_config.effective_imex_split_mode() != 1 ||
        g_sdirk3_config.jvp_epsilon != 0.01f ||
        g_sdirk3_config.stage2_krylov_tol != 0.05f ||
        g_sdirk3_config.stage3_krylov_tol != 0.10f) {
        ++failures;
        std::cerr << "FAIL valid zero/value settings changed semantics\n";
    }
    if (g_sdirk3_config.adaptive_high_threshold != 0.70f ||
        g_sdirk3_config.adaptive_low_threshold != 0.30f) {
        ++failures;
        std::cerr << "FAIL valid adaptive thresholds changed semantics\n";
    }

    expect_invalid_direct(g_sdirk3_config.jvp_epsilon, "jvp_epsilon", std::numeric_limits<float>::quiet_NaN());
    expect_invalid_direct(g_sdirk3_config.jvp_epsilon, "jvp_epsilon", std::numeric_limits<float>::infinity());
    expect_invalid_direct(g_sdirk3_config.stage2_krylov_tol, "stage2_krylov_tol", std::numeric_limits<float>::quiet_NaN());
    expect_invalid_direct(g_sdirk3_config.stage3_krylov_tol, "stage3_krylov_tol", std::numeric_limits<float>::infinity());
    expect_invalid_direct(g_sdirk3_config.adaptive_high_threshold, "adaptive_high_threshold", std::numeric_limits<float>::infinity());
    expect_invalid_direct(g_sdirk3_config.adaptive_low_threshold, "adaptive_low_threshold", std::numeric_limits<float>::quiet_NaN());
    expect_invalid_direct(g_sdirk3_config.newton_tol, "newton_tol", std::numeric_limits<float>::quiet_NaN());
    expect_invalid_direct(g_sdirk3_config.newton_rtol, "newton_rtol", std::numeric_limits<float>::infinity());
    expect_invalid_direct(g_sdirk3_config.krylov_tol, "krylov_tol", std::numeric_limits<float>::quiet_NaN());
    expect_invalid_direct(g_sdirk3_config.stage2_ew_eta_min, "stage2_ew_eta_min", std::numeric_limits<float>::infinity());

    g_sdirk3_config = SDIRK3Config{};
    const float old_jvp = g_sdirk3_config.jvp_epsilon;
    const float old_stage_ew = g_sdirk3_config.stage2_ew_eta_min;
    wrf::sdirk3::wrf_sdirk3_set_config_float("jvp_epsilon", std::numeric_limits<float>::infinity());
    wrf::sdirk3::wrf_sdirk3_set_config_float("stage2_ew_eta_min", std::numeric_limits<float>::quiet_NaN());
    if (g_sdirk3_config.jvp_epsilon != old_jvp ||
        g_sdirk3_config.stage2_ew_eta_min != old_stage_ew) {
        ++failures;
        std::cerr << "FAIL non-finite setter changed existing values\n";
    }

    clear_knobs();
    std::cout << (failures == 0 ? "CONFIG_NUMERIC_CONTRACT: PASS\n"
                                : "CONFIG_NUMERIC_CONTRACT: FAIL\n");
    return failures == 0 ? 0 : 1;
}
