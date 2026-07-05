# Simplified WRF SDIRK-3 LibTorch Makefile
# For minimal integration testing

# LibTorch configuration
LIBTORCH_ROOT ?= /opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch
TORCH_INCLUDES := -I$(LIBTORCH_ROOT)/include -I$(LIBTORCH_ROOT)/include/torch/csrc/api/include
TORCH_LDFLAGS := -L$(LIBTORCH_ROOT)/lib -ltorch -ltorch_cpu -lc10

# Compiler settings (use Homebrew clang to match WRF link)
CXX = /opt/homebrew/opt/llvm/bin/clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-unused-parameter -Wno-missing-field-initializers
CXXFLAGS += $(TORCH_INCLUDES)
CXXFLAGS += -DNDEBUG

# =========================================================================
# OPT Pass33+: ABI Macro Enforcement (explicit opt-in required)
# =========================================================================
# CRITICAL: LibTorch pre-built binaries use pre-C++11 ABI (_GLIBCXX_USE_CXX11_ABI=0).
# Omitting this flag causes silent runtime crashes or undefined behavior.
#
# Set LIBTORCH_ABI to match your LibTorch installation:
#   0 = Pre-C++11 ABI (default for PyTorch pip/conda packages)
#   1 = C++11 ABI (if built from source with CXX11 ABI)
#
# To check your LibTorch ABI:
#   python -c "import torch; print(torch._C._GLIBCXX_USE_CXX11_ABI)"
#
# Example usage:
#   make LIBTORCH_ABI=0   # For pip/conda PyTorch
#   make LIBTORCH_ABI=1   # For source-built with CXX11 ABI
# =========================================================================
ifndef LIBTORCH_ABI
    $(warning ========================================)
    $(warning LIBTORCH_ABI not set - defaulting to 0)
    $(warning Check your LibTorch ABI with:)
    $(warning   python -c "import torch; print(torch._C._GLIBCXX_USE_CXX11_ABI)")
    $(warning Set explicitly: make LIBTORCH_ABI=0 or LIBTORCH_ABI=1)
    $(warning ========================================)
    LIBTORCH_ABI := 0
endif
CXXFLAGS += -D_GLIBCXX_USE_CXX11_ABI=$(LIBTORCH_ABI)

# Surface ABI value in build output for visibility
$(info [SDIRK3] Building with _GLIBCXX_USE_CXX11_ABI=$(LIBTORCH_ABI))

# Phase 4.7: 3D batching prototype - DISABLED (CPU performance NO-GO)
# Status: Implementation correct, preserved under compile guard for future GPU testing
# Validation results: PHASE_4.7_VALIDATION_RESULTS.md
# CXXFLAGS += -DENABLE_PHASE_4_7_3D_BATCHING

# Complete source list for SDIRK3 implementation
# NOTE (2025-12-05): Removed dead code:
#   - wrf_sdirk3_unified_rhs.cpp (RHS logic is in wrf_sdirk3_tile_unified_impl.cpp)
#   - wrf_sdirk3_dynamics_ad.cpp (only used by test files, not runtime)
# FIX 2025-01-26: Added proper source files for cache invalidation and Fortran symbols
#   - wrf_sdirk3_boundary_ad.cpp: Contains invalidateDzMinCache, invalidateLatCpuCache
#   - wrf_sdirk3_rayleigh_damping_ad.cpp: Contains invalidateZ1DCache
#   - wrf_sdirk3_mpi_safety_impl.cpp: Fortran-compatible extern C symbols (with trailing underscore)
SOURCES = \
    wrf_sdirk3_globals.cpp \
    wrf_sdirk3_tile_unified_interface.cpp \
    wrf_sdirk3_interface_zerocopy.cpp \
    wrf_sdirk3_zero_copy_interface.cpp \
    wrf_sdirk3_tile_unified.cpp \
    wrf_sdirk3_tile_unified_impl.cpp \
    wrf_sdirk3_newton_solver.cpp \
    wrf_sdirk3_jvp_autograd.cpp \
    wrf_hydrostatic_pressure.cpp \
    wrf_sdirk3_base_state_zerocopy.cpp \
    wrf_tile_boundary_optimizer.cpp \
    wrf_sdirk3_config.cpp \
    wrf_sdirk3_unified_preconditioner.cpp \
    wrf_sdirk3_unified_rhs_extended.cpp \
    wrf_sdirk3_cache_utils.cpp \
    wrf_sdirk3_boundary_ad.cpp \
    wrf_sdirk3_rayleigh_damping_ad.cpp \
    wrf_sdirk3_mpi_safety_impl.cpp \
    wrf_sdirk3_halo_exchange.cpp \
    wrf_sdirk3_ad_halo_exchange.cpp

OBJECTS = $(SOURCES:.cpp=.o)

# Target library (must match WRF's expectation)
LIB_NAME = libwrf_sdirk3_libtorch.a

.PHONY: all clean

all: $(LIB_NAME)

$(LIB_NAME): $(OBJECTS)
	ar rcs $@ $^
	@echo "Successfully built minimal library $@"

# Header dependency: all .cpp files that include wrf_sdirk3_tile_unified.h
# must recompile when the header changes (class layout affects ABI).
UNIFIED_HEADER = wrf_sdirk3_tile_unified.h
UNIFIED_HEADER_DEPS = \
    wrf_sdirk3_tile_unified.o \
    wrf_sdirk3_tile_unified_impl.o \
    wrf_sdirk3_tile_unified_interface.o \
    wrf_sdirk3_interface.o \
    wrf_sdirk3_interface_zerocopy.o \
    wrf_sdirk3_base_state_zerocopy.o

$(UNIFIED_HEADER_DEPS): $(UNIFIED_HEADER)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(LIB_NAME)

test: $(LIB_NAME)
	@echo "Library built successfully: $(LIB_NAME)"
	@echo "Ready for integration with WRF"