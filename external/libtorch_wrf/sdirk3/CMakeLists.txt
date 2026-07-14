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
# (PR 5: applied to the target-scoped build config below — no global
# add_compile_definitions. Scientific-validation default stays OFF.)
if(USE_FAST_POW_0875)
    message(STATUS "  Fast pow(x,0.875): ENABLED (rsqrt-based, ~0.3-0.5% max error)")
else()
    message(STATUS "  Fast pow(x,0.875): DISABLED (accurate sqrt-based, <1 ULP)")
endif()

# Find LibTorch
find_package(absl CONFIG QUIET HINTS /opt/homebrew /usr/local)
find_package(Torch REQUIRED)
# (PR 5: TORCH_CXX_FLAGS is applied target-scoped via wrf_sdirk3_build_config
# below — the previous global CMAKE_CXX_FLAGS string append is removed.)

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
# PR 5 fix: the old `option(... OFF)` followed by `if(NOT DEFINED ...)` was a
# dead branch — option() always defines the variable, so the advertised
# CI/Release auto-activation NEVER ran. Tri-state cache variable instead.
set(WRF_SDIRK3_ABI_STRICT "AUTO" CACHE STRING
    "Primary ABI-header enforcement: AUTO, ON, or OFF")
set_property(CACHE WRF_SDIRK3_ABI_STRICT PROPERTY STRINGS AUTO ON OFF)

set(_sdirk3_abi_strict_now OFF)
set(_sdirk3_abi_strict_genex "")
if(WRF_SDIRK3_ABI_STRICT STREQUAL "ON")
    set(_sdirk3_abi_strict_now ON)
elseif(WRF_SDIRK3_ABI_STRICT STREQUAL "AUTO")
    if(DEFINED ENV{CI} OR DEFINED ENV{GITHUB_ACTIONS} OR DEFINED ENV{GITLAB_CI})
        set(_sdirk3_abi_strict_now ON)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Release" OR CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(_sdirk3_abi_strict_now ON)
    else()
        # Multi-config generators: enforce per-config via generator expression.
        set(_sdirk3_abi_strict_genex
            "$<$<OR:$<CONFIG:Release>,$<CONFIG:RelWithDebInfo>>:WRF_SDIRK3_REQUIRE_PRIMARY_ABI_HEADER>")
    endif()
elseif(NOT WRF_SDIRK3_ABI_STRICT STREQUAL "OFF")
    message(FATAL_ERROR "WRF_SDIRK3_ABI_STRICT must be AUTO, ON, or OFF (got '${WRF_SDIRK3_ABI_STRICT}')")
endif()
if(_sdirk3_abi_strict_now)
    message(STATUS "  ABI Header Mode: STRICT (${WRF_SDIRK3_ABI_STRICT})")
else()
    message(STATUS "  ABI Header Mode: permissive (${WRF_SDIRK3_ABI_STRICT}; genex covers Release configs)")
endif()

# =========================================================================
# PR 5: THE real SDIRK3 core (replaces the header-only stub).
# The authoritative source list is wrf_sdirk3_core_sources.txt — shared with
# the production Makefile; neither build system owns a copy. The old stub
# (generated empty wrf_sdirk3_cmake_stub.cpp + whichever headers happened to
# exist) meant a green CMake build said NOTHING about production compile
# health. wrf_sdirk3_lib is now every production translation unit.
# =========================================================================
set(WRF_SDIRK3_CORE_MANIFEST
    "${CMAKE_CURRENT_SOURCE_DIR}/wrf_sdirk3_core_sources.txt")
if(NOT EXISTS "${WRF_SDIRK3_CORE_MANIFEST}")
    message(FATAL_ERROR "core source manifest missing: ${WRF_SDIRK3_CORE_MANIFEST}")
endif()
file(STRINGS "${WRF_SDIRK3_CORE_MANIFEST}" WRF_SDIRK3_CORE_SOURCES)
list(TRANSFORM WRF_SDIRK3_CORE_SOURCES STRIP)
list(FILTER WRF_SDIRK3_CORE_SOURCES EXCLUDE REGEX "^(#|$)")

list(LENGTH WRF_SDIRK3_CORE_SOURCES _core_n)
if(_core_n EQUAL 0)
    message(FATAL_ERROR "core source manifest is empty")
endif()
set(_core_seen "")
foreach(_src IN LISTS WRF_SDIRK3_CORE_SOURCES)
    if(NOT _src MATCHES "\\.cpp$")
        message(FATAL_ERROR "core manifest entry is not a .cpp: '${_src}'")
    endif()
    if(IS_ABSOLUTE "${_src}" OR _src MATCHES "\\.\\.")
        message(FATAL_ERROR "core manifest entry uses absolute/parent path: '${_src}'")
    endif()
    if(_src MATCHES "(^|/)(test|tests|legacy|archive)/" OR _src MATCHES "(^|/)backup[^/]*/")
        message(FATAL_ERROR "core manifest entry points into a dormant/test directory: '${_src}'")
    endif()
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_src}")
        message(FATAL_ERROR "core manifest lists nonexistent source: '${_src}'")
    endif()
    if("${_src}" IN_LIST _core_seen)
        message(FATAL_ERROR "duplicate core manifest entry: '${_src}'")
    endif()
    list(APPEND _core_seen "${_src}")
endforeach()
message(STATUS "  SDIRK3 core manifest: ${_core_n} production sources")

# Target-scoped build configuration (no global flags/include pollution).
separate_arguments(WRF_SDIRK3_TORCH_CXX_FLAGS NATIVE_COMMAND "${TORCH_CXX_FLAGS}")
add_library(wrf_sdirk3_build_config INTERFACE)
target_compile_features(wrf_sdirk3_build_config INTERFACE cxx_std_17)
target_include_directories(wrf_sdirk3_build_config
    INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        ${TORCH_INCLUDE_DIRS})
target_compile_options(wrf_sdirk3_build_config
    INTERFACE ${WRF_SDIRK3_TORCH_CXX_FLAGS})
target_link_libraries(wrf_sdirk3_build_config
    INTERFACE "${TORCH_LIBRARIES}")
if(USE_FAST_POW_0875)
    target_compile_definitions(wrf_sdirk3_build_config INTERFACE USE_FAST_POW_0875)
endif()
if(_sdirk3_abi_strict_now)
    target_compile_definitions(wrf_sdirk3_build_config
        INTERFACE WRF_SDIRK3_REQUIRE_PRIMARY_ABI_HEADER)
elseif(_sdirk3_abi_strict_genex)
    target_compile_definitions(wrf_sdirk3_build_config
        INTERFACE ${_sdirk3_abi_strict_genex})
endif()

add_library(wrf_sdirk3_core_objects OBJECT ${WRF_SDIRK3_CORE_SOURCES})
target_link_libraries(wrf_sdirk3_core_objects PRIVATE wrf_sdirk3_build_config)

add_library(wrf_sdirk3_lib STATIC $<TARGET_OBJECTS:wrf_sdirk3_core_objects>)
target_link_libraries(wrf_sdirk3_lib PUBLIC wrf_sdirk3_build_config)
set_target_properties(wrf_sdirk3_lib PROPERTIES OUTPUT_NAME wrf_sdirk3_libtorch)
add_library(wrf_sdirk3::core ALIAS wrf_sdirk3_lib)
install(TARGETS wrf_sdirk3_lib DESTINATION lib)

# Public-header install preserved from the pre-PR5 CMake (Codex: the stub
# removal must not REGRESS the install contract; a full header-install
# redesign is explicitly out of PR 5 scope). Same existence-checked list,
# same include/wrf_sdirk3 destination as before.
set(WRF_SDIRK3_INSTALL_HEADER_CANDIDATES
    wrf_sdirk3_state_vector.h
    wrf_sdirk3_zero_copy_interface.h
    wrf_sdirk3_gmres_fixed.h
    wrf_sdirk3_newton_krylov_fixed.h
    wrf_sdirk3_jvp_router.h
    wrf_sdirk3_vertical_preconditioner.h
    wrf_sdirk3_jacobian_transpose.h
    wrf_sdirk3_solver.h
    wrf_sdirk3_test_suite.h)
set(WRF_SDIRK3_INSTALL_HEADERS "")
foreach(_hdr IN LISTS WRF_SDIRK3_INSTALL_HEADER_CANDIDATES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${_hdr}")
        list(APPEND WRF_SDIRK3_INSTALL_HEADERS "${_hdr}")
    endif()
endforeach()
if(WRF_SDIRK3_INSTALL_HEADERS)
    install(FILES ${WRF_SDIRK3_INSTALL_HEADERS} DESTINATION include/wrf_sdirk3)
endif()

# Optional legacy demo (NOT part of the core manifest).
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/wrf_sdirk3_main.cpp")
    add_executable(wrf_sdirk3 wrf_sdirk3_main.cpp)
    target_link_libraries(wrf_sdirk3 PRIVATE wrf_sdirk3::core)
    install(TARGETS wrf_sdirk3 DESTINATION bin)
endif()

# Add test target
enable_testing()
if(TARGET wrf_sdirk3)
    add_test(NAME SDIRK3_Tests COMMAND wrf_sdirk3 --test)
endif()

add_executable(test_wrms_gate_metric tests/test_wrms_gate_metric.cpp)
target_link_libraries(test_wrms_gate_metric PRIVATE wrf_sdirk3::core)
add_test(NAME WRMS_Gate_Metric COMMAND test_wrms_gate_metric)

# Split-explicit acoustic-substep validation harness (2026-07-12, external review):
# 10 gates — operator smoke/invertibility, |lambda|=0.998315 von-Neumann match,
# prep<->finish roundtrip, and the AD TRIPLE (forward-dual JVP == reverse VJP == FD,
# double-backward HVP) through the stage operators and the full substep chain.
add_executable(test_acoustic_substep test_acoustic_substep.cpp)
target_link_libraries(test_acoustic_substep PRIVATE wrf_sdirk3::core)
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
add_executable(test_vjp_semantics test_vjp_semantics.cpp)
target_link_libraries(test_vjp_semantics PRIVATE wrf_sdirk3::core)
add_test(NAME VJP_Semantics COMMAND test_vjp_semantics)

# FGMRES contract matrix (full-repo review P1-1): variable-PC correctness with
# the single-cycle negative control (standard GMRES's aggregate-M_inv
# reconstruction measured at true rel residual ~7e+0 vs FGMRES ~3e-6),
# fixed-PC equivalence, unpreconditioned bitwise regression, M_inv apply-count
# (exactly once per Arnoldi vector), multi-restart + warm start. Links the
# REAL production krylov solvers. Requires libtorch.
add_executable(test_fgmres_contract test_fgmres_contract.cpp)
target_link_libraries(test_fgmres_contract PRIVATE wrf_sdirk3::core)
add_test(NAME FGMRES_Contract COMMAND test_fgmres_contract)

# =========================================================================
# PR 5 commit 3: full-object link closure + archive/manifest contracts
# =========================================================================
# Linking the static core pulls only referenced members. This target places
# EVERY production object directly in one executable link, proving
# simultaneous closure (no duplicate definitions, no unresolved externals,
# no cross-TU ABI drift). No --allow-multiple-definition, no exclusions.
add_executable(test_core_link_smoke
    tests/test_core_link_smoke.cpp
    $<TARGET_OBJECTS:wrf_sdirk3_core_objects>)
target_link_libraries(test_core_link_smoke PRIVATE wrf_sdirk3_build_config)
add_test(NAME Core_Full_Object_Link COMMAND test_core_link_smoke)

# EXACT member-set equality of the CMake archive vs the manifest (normalizes
# foo.cpp.o/foo.o/foo.obj; any wrf_sdirk3_cmake_stub* member fails outright).
add_test(NAME Core_Archive_Manifest
         COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_core_archive.sh
                 $<TARGET_FILE:wrf_sdirk3_lib>
                 ${WRF_SDIRK3_CORE_MANIFEST})

# Make/CMake archive MEMBERSHIP parity (not byte identity). FAILS if the
# production Make archive has not been built — never skip-laundered.
add_test(NAME Core_Archive_MakeParity
         COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_core_archive.sh
                 $<TARGET_FILE:wrf_sdirk3_lib>
                 ${WRF_SDIRK3_CORE_MANIFEST}
                 ${CMAKE_CURRENT_SOURCE_DIR}/libwrf_sdirk3_libtorch.a)

# MPI halo operator contract (PR 7A). The halo TUs are recompiled here WITH
# -DDMPARALLEL (the production core archive stays MPI-free; its members are
# never pulled because these fresh objects already define the symbols).
# Registered only when MPI exists; hosted CI installs MPI deliberately and
# the exact CTest inventory pins the three names, so a broken MPI setup
# fails the inventory diff instead of silently skipping the tests.
find_package(MPI COMPONENTS CXX QUIET)
if(MPI_CXX_FOUND)
    # P1 (review): the safety impl must ALSO be a fresh -DDMPARALLEL object —
    # linking the MPI-free core copy would test an MPIExchangeScope whose
    # MPI baseline path (MPI_Initialized/Query_thread) was compiled out.
    add_executable(test_mpi_halo_contract
        test_mpi_halo_contract.cpp
        wrf_sdirk3_halo_exchange.cpp
        wrf_sdirk3_ad_halo_exchange.cpp
        wrf_sdirk3_mpi_safety_impl.cpp)
    target_compile_definitions(test_mpi_halo_contract PRIVATE DMPARALLEL)
    target_link_libraries(test_mpi_halo_contract PRIVATE
        wrf_sdirk3::core wrf_sdirk3_build_config MPI::MPI_CXX)
    foreach(_np 1 2 4)
        add_test(NAME MPI_Halo_Contract_np${_np}
                 COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${_np}
                         $<TARGET_FILE:test_mpi_halo_contract>)
        # A hung exchange must read as FAILURE, not as a stuck job (PR 7D).
        set_tests_properties(MPI_Halo_Contract_np${_np} PROPERTIES TIMEOUT 300)
    endforeach()
    message(STATUS "  SDIRK3 MPI halo contract: ENABLED (mpiexec=${MPIEXEC_EXECUTABLE})")
else()
    message(STATUS "  SDIRK3 MPI halo contract: MPI not found - tests NOT registered")
endif()

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
# PR 5 NOTE: repairing this lint's (previously never-working) escaping exposed
# 70 accumulated violations in 3 production files. Fixing all of them is a
# compute-file sweep outside PR 5's scope (directive: no algorithm .cpp edits),
# so this is a RATCHET: the count may only go DOWN. Any count above the
# recorded baseline (tests/lint_from_blob_baseline.txt) fails; the remaining
# debt is printed loudly on every run — never silently green. The 8 highest-
# risk sites (raw MPI halo buffers + host coefficient pointers) were fixed in
# PR 5 itself. Follow-up sweep is a recorded finding.
add_custom_target(lint_from_blob
    COMMAND bash -c "v=$(grep -n 'from_blob(' ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h 2>/dev/null | grep -v 'make_cpu_from_blob_opts' | grep -v 'LINT_EXCEPTION' | grep -v '// from_blob' | grep -v 'FIX.*from_blob' | grep -v 'inline.*from_blob' | grep -v '@brief.*from_blob' | grep -vE ':[[:space:]]*(\\*|//)' || true); n=$(printf '%s' \"$v\" | grep -c . || true); base=$(cat ${CMAKE_CURRENT_SOURCE_DIR}/tests/lint_from_blob_baseline.txt); echo \"from_blob lint: $n violation(s), ratchet baseline $base\"; if [ \"$n\" -gt \"$base\" ]; then echo 'FAIL: NEW from_blob violations above the ratchet baseline:'; printf '%s\\n' \"$v\"; exit 1; fi; if [ \"$n\" -gt 0 ]; then echo 'KNOWN DEBT (fix reduces baseline; never increase it):'; printf '%s\\n' \"$v\" | head -5; echo '  ... (full list via: make lint output above)'; fi; echo 'OK: from_blob ratchet respected'"
    COMMENT "Linting from_blob usage for CPU device safety (ratchet)"
    VERBATIM
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
    COMMAND bash -c "vs=$(grep -rn '\\.select([^)]*)[[:space:]]*=' --include='*.cpp' --include='*.h' ${CMAKE_CURRENT_SOURCE_DIR} 2>/dev/null | grep -v '//' | grep -v 'copy_\\|fill_\\|zero_' || true); vl=$(grep -rn '\\.slice([^)]*)[[:space:]]*=' --include='*.cpp' --include='*.h' ${CMAKE_CURRENT_SOURCE_DIR} 2>/dev/null | grep -v '//' | grep -v 'copy_' || true); if [ -n \"$vs\" ] || [ -n \"$vl\" ]; then echo 'ERROR: broken select()/slice() assignment patterns:'; echo \"$vs\"; echo \"$vl\"; exit 1; else echo 'OK: no broken tensor patterns'; fi"
    COMMENT "Checking LibTorch tensor patterns for correctness"
    VERBATIM
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
    # PR 5: the old lint WARNED and succeeded when the GLIBCXX ABI macro was
    # missing. Policy now: on Linux/libstdc++ a missing or multivalued
    # _GLIBCXX_USE_CXX11_ABI is a FAILURE (never silently assume 0); on Apple
    # (libc++) the macro is genuinely N/A and the check passes with an explicit
    # N/A statement. The detected value is printed either way.
    #
    # PR 6: torch >= 2.6 wheels use the cxx11 ABI as the DEFAULT and TorchConfig
    # no longer puts the macro into TORCH_CXX_FLAGS at all, so "read it from
    # TORCH_CXX_FLAGS" alone fails closed on every modern wheel. The ABI truth
    # still exists (torch._C._GLIBCXX_USE_CXX11_ABI); the caller may pass it as
    # -DWRF_SDIRK3_TORCH_ABI=<0|1>. Resolution (still fail-closed):
    #   TORCH_CXX_FLAGS carries exactly one value -> use it; if the explicit
    #     var is ALSO set and disagrees, FATAL (a conflict is never guessed).
    #   TORCH_CXX_FLAGS silent + explicit var set -> use the var AND inject the
    #     macro into the build config, so the value is EFFECTIVE in every TU,
    #     not merely asserted by a lint.
    #   Neither -> the lint fails, exactly as before. With the var unset the
    #   behavior is identical to PR 5.
    set(WRF_SDIRK3_TORCH_ABI "" CACHE STRING
        "Explicit _GLIBCXX_USE_CXX11_ABI (0/1) of the installed torch; consulted only when TORCH_CXX_FLAGS omits the macro")
    if(NOT "${WRF_SDIRK3_TORCH_ABI}" STREQUAL "" AND NOT WRF_SDIRK3_TORCH_ABI MATCHES "^[01]$")
        message(FATAL_ERROR "WRF_SDIRK3_TORCH_ABI must be 0 or 1, got '${WRF_SDIRK3_TORCH_ABI}'")
    endif()
    string(REGEX MATCHALL "_GLIBCXX_USE_CXX11_ABI=[01]" _abi_flags "${TORCH_CXX_FLAGS}")
    list(REMOVE_DUPLICATES _abi_flags)
    list(LENGTH _abi_flags _abi_n)
    if(_abi_n GREATER 1)
        message(FATAL_ERROR "conflicting _GLIBCXX_USE_CXX11_ABI values in TORCH_CXX_FLAGS: ${_abi_flags}")
    endif()
    set(_abi_resolved "")
    if(_abi_n EQUAL 1)
        string(REGEX REPLACE ".*=([01])$" "\\1" _abi_resolved "${_abi_flags}")
        set(_abi_source "TORCH_CXX_FLAGS")
        if(NOT "${WRF_SDIRK3_TORCH_ABI}" STREQUAL "" AND NOT WRF_SDIRK3_TORCH_ABI STREQUAL _abi_resolved)
            message(FATAL_ERROR "ABI conflict: TORCH_CXX_FLAGS says _GLIBCXX_USE_CXX11_ABI=${_abi_resolved} but WRF_SDIRK3_TORCH_ABI=${WRF_SDIRK3_TORCH_ABI}")
        endif()
    elseif(NOT "${WRF_SDIRK3_TORCH_ABI}" STREQUAL "")
        set(_abi_resolved "${WRF_SDIRK3_TORCH_ABI}")
        set(_abi_source "WRF_SDIRK3_TORCH_ABI (runtime-derived)")
        if(NOT APPLE)
            target_compile_definitions(wrf_sdirk3_build_config
                INTERFACE "_GLIBCXX_USE_CXX11_ABI=${_abi_resolved}")
        endif()
    endif()
    if(NOT APPLE AND NOT _abi_resolved STREQUAL "")
        message(STATUS "  SDIRK3 torch ABI: _GLIBCXX_USE_CXX11_ABI=${_abi_resolved} (source: ${_abi_source})")
    endif()
    if(APPLE)
        add_custom_target(lint_abi_macro
            COMMAND ${CMAKE_COMMAND} -E echo "LibTorch ABI check: N/A on Apple libc++ - _GLIBCXX_USE_CXX11_ABI is a libstdc++ macro"
            COMMAND ${CMAKE_COMMAND} -E echo "  TORCH_CXX_FLAGS: ${TORCH_CXX_FLAGS}"
            COMMENT "LibTorch ABI compatibility (Apple: N/A)"
            VERBATIM
        )
    elseif(_abi_resolved STREQUAL "")
        add_custom_target(lint_abi_macro
            COMMAND ${CMAKE_COMMAND} -E echo "FAIL: _GLIBCXX_USE_CXX11_ABI unavailable from TORCH_CXX_FLAGS and WRF_SDIRK3_TORCH_ABI not provided - refusing to assume a value on a libstdc++ platform"
            COMMAND ${CMAKE_COMMAND} -E false
            COMMENT "LibTorch ABI compatibility (libstdc++: fail-closed)"
            VERBATIM
        )
    else()
        add_custom_target(lint_abi_macro
            COMMAND ${CMAKE_COMMAND} -E echo "OK: LibTorch ABI: _GLIBCXX_USE_CXX11_ABI=${_abi_resolved} (source: ${_abi_source})"
            COMMENT "LibTorch ABI compatibility (libstdc++: fail-closed)"
            VERBATIM
        )
    endif()
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