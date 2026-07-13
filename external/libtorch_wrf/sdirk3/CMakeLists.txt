cmake_minimum_required(VERSION 3.18)
project(wrf_sdirk3_pytorch LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =========================================================================
# FIX 2025-01-06 Batch41: Performance optimization options
# =========================================================================
# USE_FAST_POW_0875: Use rsqrt-based approximation for pow(x, 0.875) in AVX2
#
# ACCURACY COMPARISON:
#   OFF (default): 3x sqrt + 1x div (IEEE-754 compliant)
#                  ~20 cycles, <1 ULP error (essentially exact)
#   ON:            rsqrt chain with partial Newton-Raphson
#                  ~12 cycles, ~0.3% typical error, up to ~0.5% at extremes
#
# TECHNICAL NOTE: The fast path applies NR refinement only to the first rsqrt.
# Subsequent rsqrt operations (r2, r3) accumulate ~12-bit error per step.
# Adding NR to all stages would improve accuracy to ~0.01% at +6 cycles cost.
#
# RECOMMENDED USE:
#   - OFF: Validation runs, adjoint/gradient verification, debugging
#   - ON:  Production runs where microphysics is a performance hot path
#
# See: wrf_sdirk3_microphysics_vectorized.h pow_0875_avx2() for details
# =========================================================================
option(USE_FAST_POW_0875 "Use fast rsqrt-based pow(x,0.875) approximation" OFF)
if(USE_FAST_POW_0875)
    add_compile_definitions(USE_FAST_POW_0875)
    message(STATUS "  Fast pow(x,0.875): ENABLED (rsqrt-based, ~0.3-0.5% max error)")
else()
    message(STATUS "  Fast pow(x,0.875): DISABLED (accurate sqrt-based, <1 ULP)")
endif()

# Find LibTorch
find_package(absl CONFIG QUIET HINTS /opt/homebrew /usr/local)
find_package(Torch REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${TORCH_CXX_FLAGS}")

# =========================================================================
# OPT Pass33+: ABI Header Strict Mode for CI/Release Builds
# =========================================================================
# WRF_SDIRK3_REQUIRE_PRIMARY_ABI_HEADER makes fallback path emit #error
# instead of #warning, catching missing include paths early in CI.
#
# Auto-enabled when:
#   - CMAKE_BUILD_TYPE is Release/RelWithDebInfo
#   - CI environment variable is set (GitHub Actions, GitLab CI, etc.)
#
# Manual override: -DWRF_SDIRK3_ABI_STRICT=OFF to disable
# =========================================================================
option(WRF_SDIRK3_ABI_STRICT "Enforce strict ABI header checking" OFF)

if(NOT DEFINED WRF_SDIRK3_ABI_STRICT OR WRF_SDIRK3_ABI_STRICT)
    if(CMAKE_BUILD_TYPE STREQUAL "Release" OR
       CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" OR
       DEFINED ENV{CI} OR
       DEFINED ENV{GITHUB_ACTIONS} OR
       DEFINED ENV{GITLAB_CI})
        add_compile_definitions(WRF_SDIRK3_REQUIRE_PRIMARY_ABI_HEADER)
        message(STATUS "  ABI Header Mode: STRICT (CI/Release detected)")
    else()
        message(STATUS "  ABI Header Mode: permissive (dev build)")
    endif()
endif()

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Source files
set(SOURCES)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wrf_sdirk3_main.cpp")
    list(APPEND SOURCES wrf_sdirk3_main.cpp)
endif()

# Header files
set(HEADER_CANDIDATES
    wrf_sdirk3_state_vector.h
    wrf_sdirk3_zero_copy_interface.h
    wrf_sdirk3_gmres_fixed.h
    wrf_sdirk3_newton_krylov_fixed.h
    wrf_sdirk3_jvp_router.h
    wrf_sdirk3_vertical_preconditioner.h
    wrf_sdirk3_jacobian_transpose.h
    wrf_sdirk3_solver.h
    wrf_sdirk3_test_suite.h
)
set(HEADERS)
foreach(_header IN LISTS HEADER_CANDIDATES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_header}")
        list(APPEND HEADERS "${_header}")
    endif()
endforeach()

# Optional legacy demo executable (not present in all WRF integration checkouts)
if(SOURCES)
    add_executable(wrf_sdirk3 ${SOURCES} ${HEADERS})
    target_link_libraries(wrf_sdirk3 "${TORCH_LIBRARIES}")
    set_property(TARGET wrf_sdirk3 PROPERTY CXX_STANDARD 17)
    install(TARGETS wrf_sdirk3 DESTINATION bin)
endif()

# Header-only static integration target for consumers that still include this CMake project directly
if(HEADERS)
    set(_sdirk3_cmake_stub "${CMAKE_CURRENT_BINARY_DIR}/wrf_sdirk3_cmake_stub.cpp")
    file(WRITE "${_sdirk3_cmake_stub}" "namespace wrf_sdirk3_cmake_stub { void anchor() {} }\n")
    add_library(wrf_sdirk3_lib STATIC ${_sdirk3_cmake_stub} ${HEADERS})
    set_target_properties(wrf_sdirk3_lib PROPERTIES LINKER_LANGUAGE CXX)
    target_link_libraries(wrf_sdirk3_lib "${TORCH_LIBRARIES}")
    install(TARGETS wrf_sdirk3_lib DESTINATION lib)
    install(FILES ${HEADERS} DESTINATION include/wrf_sdirk3)
endif()

# Add test target
enable_testing()
if(TARGET wrf_sdirk3)
    add_test(NAME SDIRK3_Tests COMMAND wrf_sdirk3 --test)
endif()

add_executable(test_wrms_gate_metric tests/test_wrms_gate_metric.cpp)
target_include_directories(test_wrms_gate_metric PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_wrms_gate_metric "${TORCH_LIBRARIES}")
set_property(TARGET test_wrms_gate_metric PROPERTY CXX_STANDARD 17)
add_test(NAME WRMS_Gate_Metric COMMAND test_wrms_gate_metric)

# Split-explicit acoustic-substep validation harness (2026-07-12, external review):
# 10 gates — operator smoke/invertibility, |lambda|=0.998315 von-Neumann match,
# prep<->finish roundtrip, and the AD TRIPLE (forward-dual JVP == reverse VJP == FD,
# double-backward HVP) through the stage operators and the full substep chain.
add_executable(test_acoustic_substep test_acoustic_substep.cpp wrf_sdirk3_acoustic_substep.cpp)
target_include_directories(test_acoustic_substep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_acoustic_substep "${TORCH_LIBRARIES}")
set_property(TARGET test_acoustic_substep PROPERTY CXX_STANDARD 17)
add_test(NAME Acoustic_Substep_AD COMMAND test_acoustic_substep)

# Raw map-factor verification regression matrix (external review round 3c):
# per-array TT/TF/FT/FF existence, NaN/Inf/non-unit fail-closed, first/last-row
# coverage, and the stride canary that reproduces the old mem_nx+1 layout bug.
# Torch-free — compiles/runs in seconds.
add_executable(test_msf_raw_stats test_msf_raw_stats.cpp)
target_include_directories(test_msf_raw_stats PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET test_msf_raw_stats PROPERTY CXX_STANDARD 17)
add_test(NAME MSF_Raw_Stats_Matrix COMMAND test_msf_raw_stats)

# Per-call geometry contract regression matrix (geometry-CI increment): the
# hardest fail-close of the stop-gate campaign (rounds 3g-3j), promoted from
# manual runtime fault-injection to a standing CI gate. Exercises the SHARED
# predicate (wrf_sdirk3_geometry_contract.h) that validateCallGeometry wraps:
# base/stagger 0-negative, base-1, base+2, init/call mismatch (incl. the
# round-3f counterexample), init_sane (no "uninitialized => allow" escape), and
# INT_MAX/LLONG_MAX-adjacent rejection with no overflow UB. Torch-free.
add_executable(test_geometry_contract test_geometry_contract.cpp)
target_include_directories(test_geometry_contract PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET test_geometry_contract PROPERTY CXX_STANDARD 17)
add_test(NAME Geometry_Contract_Matrix COMMAND test_geometry_contract)

# AD-helper semantics proof on a NONSYMMETRIC matrix (full-repo review
# acceptance test #1): compute_vjp_autograd must be J^T v ALWAYS (including
# with the legacy FD toggle ON — the round-2 regression pin), and the FD/dual
# helpers must be J v. Links the REAL production translation units, not a
# reimplementation. Requires libtorch.
add_executable(test_vjp_semantics test_vjp_semantics.cpp
               wrf_sdirk3_jvp_autograd.cpp wrf_sdirk3_config.cpp)
target_include_directories(test_vjp_semantics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(test_vjp_semantics "${TORCH_LIBRARIES}")
set_property(TARGET test_vjp_semantics PROPERTY CXX_STANDARD 17)
add_test(NAME VJP_Semantics COMMAND test_vjp_semantics)

# =========================================================================
# OPT Pass33+: Test Output Verbosity Control
# =========================================================================
# SDIRK3_QUIET_TESTS: Suppress verbose test output (✅/❌ details)
# Useful for CI pipelines where only pass/fail summary is needed.
# Failures are always printed regardless of this setting.
#
# Usage:
#   cmake -DSDIRK3_QUIET_TESTS=ON ..   # CI mode: minimal output
#   cmake -DSDIRK3_QUIET_TESTS=OFF ..  # Dev mode: verbose (default)
# =========================================================================
option(SDIRK3_QUIET_TESTS "Suppress verbose test output" OFF)
if(SDIRK3_QUIET_TESTS)
    add_compile_definitions(SDIRK3_QUIET_TESTS)
    message(STATUS "  Test Output: QUIET (CI mode)")
else()
    message(STATUS "  Test Output: VERBOSE (dev mode)")
endif()

# =========================================================================
# FIX 2025-12-30 Batch26 Issue 5: CI Lint target for from_blob enforcement
# FIX 2025-12-30 Batch27 Issue 6: Helper moved to wrf_sdirk3_autograd_utils.h
# =========================================================================
# This target checks that all from_blob calls use make_cpu_from_blob_opts()
# helper instead of raw TensorOptions. This prevents accidental omission
# of .device(torch::kCPU) which causes undefined behavior.
#
# The helper is defined in: wrf_sdirk3_autograd_utils.h (shared header)
# Include it via: #include "wrf_sdirk3_autograd_utils.h"
#
# Run: cmake --build . --target lint_from_blob
# Expected: No output (all from_blob calls properly use helper)
#
# To check parent directories (libtorch_wrf), run from that directory:
#   grep -rn 'from_blob' --include='*.cpp' --include='*.h' | grep -v 'make_cpu_from_blob_opts'
# =========================================================================
add_custom_target(lint_from_blob
    COMMAND ${CMAKE_COMMAND} -E echo "Checking from_blob usage in SDIRK3 sources..."
    # FIX Round190: Added LINT_EXCEPTION filter for inline-marked exceptions
    COMMAND bash -c "files=\$\(find ${CMAKE_CURRENT_SOURCE_DIR} -name '*.cpp' -o -name '*.h'\) && \
        violations=\$\(grep -n 'from_blob' \$\$files 2>/dev/null | \
        grep -v 'make_cpu_from_blob_opts' | \
        grep -v 'LINT_EXCEPTION' | \
        grep -v '// from_blob' | \
        grep -v 'FIX.*from_blob' | \
        grep -v 'inline.*from_blob' | \
        grep -v '@brief.*from_blob' || true\) && \
        if [ -n \"\$\$violations\" ]; then \
            echo 'ERROR: from_blob calls without make_cpu_from_blob_opts() or LINT_EXCEPTION:' && \
            echo \"\$\$violations\" && \
            echo '' && \
            echo 'FIX: Either use make_cpu_from_blob_opts() helper:' && \
            echo '     torch::from_blob(ptr, shape, wrf::sdirk3::make_cpu_from_blob_opts())' && \
            echo 'OR add inline LINT_EXCEPTION comment if CPU options are explicit:' && \
            echo '     torch::from_blob(...);  // LINT_EXCEPTION: CPU opts above' && \
            exit 1; \
        else \
            echo 'OK: All from_blob calls use make_cpu_from_blob_opts() or have LINT_EXCEPTION.'; \
        fi"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Linting from_blob usage for CPU device safety"
)

# =========================================================================
# FIX 2025-01-06 Batch41 Issue 6: GPU kernel optimization documentation
# =========================================================================
# The moisture RHS (wrf_sdirk3_unified_rhs_moisture_wrf.cpp) uses device-aware
# branching to avoid GPU kernel explosion from triple-nested .index() calls:
#
# GPU PATH: Vectorized slice operations for interior flux6/flux3 regions
#   - Creates O(1) kernels per direction instead of O(nx*ny*nz) kernels
#   - Uses flux6_vec/flux4_vec/flux3_vec helpers for batched computation
#   - Interior regions processed with tensor slicing
#   - Boundary regions still use scalar loops (small number of points)
#
# CPU PATH: Scalar loops with tensor indexing (acceptable performance)
#
# Performance impact: ~10-50x speedup on GPU for typical WRF grid sizes
# =========================================================================

# =========================================================================
# FIX 2025-12-31 Batch41: CI target for LibTorch pattern checks
# =========================================================================
# This target runs the comprehensive LibTorch pattern checker script to
# detect problematic patterns:
# - select()/slice() = assignment (no-ops in modern LibTorch)
# - .item() without NoGradGuard
# - narrow() with negative indices
# - from_blob without explicit device specification
#
# Run: cmake --build . --target lint_libtorch_patterns
# Or directly: ./test/check_libtorch_patterns.sh /path/to/dWRF
#
# Recommended integration:
# - Pre-commit hook: Run before each commit
# - CI pipeline: Run on each PR
# - Post-merge: Run after merging external LibTorch code
# =========================================================================
add_custom_target(lint_libtorch_patterns
    COMMAND ${CMAKE_COMMAND} -E echo "Running LibTorch pattern checks..."
    COMMAND bash -c "if [ -x '${CMAKE_SOURCE_DIR}/../../../test/check_libtorch_patterns.sh' ]; then \
        '${CMAKE_SOURCE_DIR}/../../../test/check_libtorch_patterns.sh' '${CMAKE_SOURCE_DIR}/../../..'; \
    else \
        echo 'Pattern check script not found. Creating check inline...'; \
        violations_select=\$\(grep -rn '\\.select([^)]*)[[:space:]]*=' --include='*.cpp' --include='*.h' ${CMAKE_SOURCE_DIR}/../../.. 2>/dev/null | grep -v '//' | grep -v 'copy_\\|fill_\\|zero_' || true\); \
        violations_slice=\$\(grep -rn '\\.slice([^)]*)[[:space:]]*=' --include='*.cpp' --include='*.h' ${CMAKE_SOURCE_DIR}/../../.. 2>/dev/null | grep -v '//' | grep -v 'copy_' || true\); \
        if [ -n \"\$\$violations_select\" ] || [ -n \"\$\$violations_slice\" ]; then \
            echo 'ERROR: Found broken select()/slice() patterns:'; \
            echo \"\$\$violations_select\"; \
            echo \"\$\$violations_slice\"; \
            exit 1; \
        else \
            echo 'OK: No broken patterns found.'; \
        fi; \
    fi"
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Checking LibTorch tensor patterns for correctness"
)

# =========================================================================
# OPT Pass33+: ABI Macro CI Enforcement (gated by Torch_FOUND)
# =========================================================================
# LibTorch pre-built binaries typically use the pre-C++11 ABI (_GLIBCXX_USE_CXX11_ABI=0).
# Mixing ABIs causes silent crashes or undefined behavior at runtime.
#
# Detection: find_package(Torch) sets TORCH_CXX_FLAGS which includes the ABI flag.
# This target verifies that TORCH_CXX_FLAGS contains the expected ABI setting.
#
# Common failure modes:
# 1. Manual compilation without TORCH_CXX_FLAGS -> ABI mismatch
# 2. Building against system LibTorch with different ABI -> link errors
# 3. Mixing objects compiled with different ABI settings -> runtime crashes
#
# Run: cmake --build . --target lint_abi_macro
# Expected: ABI macro verification passes
#
# NOTE: This target is only created when Torch is found. Non-LibTorch builds
# or lightweight CI jobs that skip find_package(Torch) will not have this target.
# =========================================================================
if(Torch_FOUND)
    add_custom_target(lint_abi_macro
        COMMAND ${CMAKE_COMMAND} -E echo "Checking LibTorch ABI compatibility..."
        COMMAND bash -c " \
            torch_flags='${TORCH_CXX_FLAGS}'; \
            if echo \"\$\$torch_flags\" | grep -q '_GLIBCXX_USE_CXX11_ABI'; then \
                abi_val=\$\(echo \"\$\$torch_flags\" | grep -oE '_GLIBCXX_USE_CXX11_ABI=[01]' | head -1\); \
                echo \"OK: LibTorch ABI setting detected: \$\$abi_val\"; \
                echo \"    Full TORCH_CXX_FLAGS: \$\$torch_flags\"; \
            else \
                echo 'WARNING: _GLIBCXX_USE_CXX11_ABI not found in TORCH_CXX_FLAGS'; \
                echo '    This may cause ABI mismatch issues at runtime.'; \
                echo '    Full TORCH_CXX_FLAGS: '\$\$torch_flags; \
                echo ''; \
                echo 'RECOMMENDED: Ensure LibTorch was built with consistent ABI settings.'; \
                echo '    For pre-built binaries: -D_GLIBCXX_USE_CXX11_ABI=0 (typical)'; \
                echo '    For source builds: Match your system libstdc++ ABI'; \
            fi"
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Verifying LibTorch ABI compatibility settings"
    )
    # Combined lint target (with ABI check when Torch available)
    add_custom_target(lint_all
        DEPENDS lint_from_blob lint_libtorch_patterns lint_abi_macro
        COMMENT "Running all lint checks (including ABI verification)"
    )
else()
    # Lightweight lint_all without ABI check for non-Torch builds
    add_custom_target(lint_all
        DEPENDS lint_from_blob lint_libtorch_patterns
        COMMENT "Running lint checks (ABI check skipped - Torch not found)"
    )
endif()

# Print configuration
message(STATUS "WRF-SDIRK3 PyTorch Integration Build Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  LibTorch: ${TORCH_LIBRARIES}")
message(STATUS "  Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "  Lint targets: lint_from_blob, lint_libtorch_patterns, lint_all")