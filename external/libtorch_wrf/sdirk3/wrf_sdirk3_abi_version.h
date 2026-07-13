/**
 * @file wrf_sdirk3_abi_version.h
 * @brief SINGLE SOURCE OF TRUTH for WRFGridInfo ABI version
 *
 * FIX Round96/Round97: Centralized ABI version definition to prevent drift.
 * This is the ONLY file that should define WRF_SDIRK3_GRIDINFO_ABI_VERSION.
 *
 * LOCATION: external/libtorch_wrf/sdirk3/wrf_sdirk3_abi_version.h
 *
 * All headers defining WRFGridInfo MUST include this file. Do NOT copy this
 * file to other locations - add the include path instead:
 *   -I${PROJECT_ROOT}/external/libtorch_wrf/sdirk3
 *
 * Version history:
 *   v88 - Initial ABI version macro
 *   v89 - Added cached_device_index (int8_t) for multi-GPU
 *   v90 - Changed cached_device_index to int16_t for >127 GPU clusters
 *   v91 - Changed cached_reference_device to int16_t for consistency
 *   v92 - Added warned_dz_rdz_device_mismatch for WARN_ONCE pattern
 *   v93 - Added dz_rdz_warn_throttle_counter for per-solver warning throttle
 *   v94 - Added solver_unique_id for per-solver TLS cache invalidation (Round109)
 */

#ifndef WRF_SDIRK3_ABI_VERSION_H
#define WRF_SDIRK3_ABI_VERSION_H
#include <cstdint>  // fixed-width ints used below; libstdc++ (Linux g++) does not provide them transitively

// ═══════════════════════════════════════════════════════════════════════════
// SINGLE SOURCE OF TRUTH - Update version HERE only
// Location: external/libtorch_wrf/sdirk3/wrf_sdirk3_abi_version.h
// ═══════════════════════════════════════════════════════════════════════════
#define WRF_SDIRK3_GRIDINFO_ABI_VERSION 94
// ═══════════════════════════════════════════════════════════════════════════

// FIX Round101/Round102: Macro for headers to verify ABI compatibility at compile time
// Compatible with both C++ (static_assert) and C11 (_Static_assert)
// Usage: WRF_SDIRK3_VERIFY_ABI_VERSION(94)
//
// FIX Round102: Opt-out for legacy builds that can't use ABI verification
// Define WRF_SDIRK3_DISABLE_ABI_VERIFY to disable all ABI version checks
// WARNING: Only use this if you understand the ABI compatibility risks!
#ifdef WRF_SDIRK3_DISABLE_ABI_VERIFY
#define WRF_SDIRK3_VERIFY_ABI_VERSION(expected) /* ABI verify disabled by user */
#elif defined(__cplusplus)
#define WRF_SDIRK3_VERIFY_ABI_VERSION(expected) \
    static_assert(WRF_SDIRK3_GRIDINFO_ABI_VERSION == expected, \
        "WRFGridInfo ABI version mismatch! Expected " #expected \
        ". Update wrf_sdirk3_abi_version.h and rebuild all modules.")
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
// C11 and later: use _Static_assert
#define WRF_SDIRK3_VERIFY_ABI_VERSION(expected) \
    _Static_assert(WRF_SDIRK3_GRIDINFO_ABI_VERSION == expected, \
        "WRFGridInfo ABI version mismatch! Expected " #expected \
        ". Update wrf_sdirk3_abi_version.h and rebuild all modules.")
#else
// Pre-C11: No static assertion available, use preprocessor check only
// FIX Round102: Made less aggressive - warning instead of error, with opt-out
#ifndef WRF_SDIRK3_STRICT_ABI_CHECK
// Soft check: no-op (user can define WRF_SDIRK3_STRICT_ABI_CHECK for #error)
#define WRF_SDIRK3_VERIFY_ABI_VERSION(expected) /* no-op in pre-C11 (soft mode) */
#else
// Strict check enabled by user
#if WRF_SDIRK3_GRIDINFO_ABI_VERSION != 94
#error "WRFGridInfo ABI version mismatch! Update wrf_sdirk3_abi_version.h"
#endif
#define WRF_SDIRK3_VERIFY_ABI_VERSION(expected) /* checked via preprocessor */
#endif
#endif

// ═══════════════════════════════════════════════════════════════════════════
// FIX Round116/Round117: TLS Cache Validation Defaults (SINGLE SOURCE OF TRUTH)
// These are the canonical default values. Override via compile flags if needed.
// Used by wrf_sdirk3_interface_zerocopy.cpp when WRF_SDIRK3_RELEASE_TLS_CHECK defined.
//
// FIX Round117: CONSISTENCY REQUIREMENT
// These values MUST be uniform across ALL translation units in the build.
// If you override via -D flags, apply the SAME value to ALL compilation units.
// Inconsistent values cause undefined behavior (different TUs see different thresholds).
// The #ifndef guards allow command-line override but do NOT protect against
// per-TU variation. Use CMake/Makefile variables to ensure consistency.
// ═══════════════════════════════════════════════════════════════════════════

// WRF_SDIRK3_TLS_MISMATCH_ABORT_THRESHOLD
// Number of unique_id mismatches before aborting in release mode.
// Set high enough to allow multiple threads to hit the same bug,
// but low enough to prevent silent corruption from spreading.
#ifndef WRF_SDIRK3_TLS_MISMATCH_ABORT_THRESHOLD
#define WRF_SDIRK3_TLS_MISMATCH_ABORT_THRESHOLD 100
#endif

// WRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL
// When WRF_SDIRK3_RELEASE_TLS_CHECK is enabled, only check every N calls
// to reduce getGridInfo() overhead on the hot path. Set to 1 for every-call
// checking (maximum coverage, higher overhead).
#ifndef WRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL
#define WRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL 1000
#endif

// ═══════════════════════════════════════════════════════════════════════════
// FIX Round126: CMAKE BUILD FLAG CONSISTENCY REQUIREMENT
// ═══════════════════════════════════════════════════════════════════════════
// CRITICAL: When overriding these macros via compile flags, ALL translation
// units (TUs) MUST use the SAME values. Mixing different values across TUs
// can cause ODR (One Definition Rule) violations and undefined behavior.
//
// CMake example (ensures consistent values across all TUs):
//
//   # In CMakeLists.txt - set ONCE at top level
//   add_compile_definitions(
//       WRF_SDIRK3_RELEASE_TLS_CHECK
//       WRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL=500
//       WRF_SDIRK3_TLS_MISMATCH_ABORT_THRESHOLD=50
//   )
//
// WRONG (causes ODR violations):
//   # Different targets with different values
//   target_compile_definitions(target_A PRIVATE WRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL=100)
//   target_compile_definitions(target_B PRIVATE WRF_SDIRK3_RELEASE_TLS_CHECK_INTERVAL=500)
//
// If you must have per-target values, ensure they're in SEPARATE libraries
// that don't share inline/template code from this header.
// ═══════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════

#endif // WRF_SDIRK3_ABI_VERSION_H
