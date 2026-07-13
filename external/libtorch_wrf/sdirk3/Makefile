# Simplified WRF SDIRK-3 LibTorch Makefile
# For minimal integration testing

# LibTorch configuration
LIBTORCH_ROOT ?= /opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch
TORCH_INCLUDES := -I$(LIBTORCH_ROOT)/include -I$(LIBTORCH_ROOT)/include/torch/csrc/api/include
TORCH_LDFLAGS := -L$(LIBTORCH_ROOT)/lib -ltorch -ltorch_cpu -lc10

# Compiler settings (use Homebrew clang to match WRF link)
CXX = /opt/homebrew/opt/llvm/bin/clang++
CXXFLAGS = -std=c++17 -O2 -Wall -Wno-unused-parameter -Wno-missing-field-initializers
# Review round 3j: configure.wrf passes CXXFLAGS=... as a COMMAND-LINE variable,
# which overrides plain += additions. Structural requirements (Torch includes,
# ABI macro below) must survive that override => use `override`. The former
# `CXXFLAGS += -DNDEBUG` line is deliberately DROPPED, not overridden: the
# configured build has never actually carried -DNDEBUG (the command-line
# override voided it), and silently enabling it now would strip assert()
# semantics from the validated production library — a behavior change this
# build-hygiene fix must not smuggle in.
override CXXFLAGS += $(TORCH_INCLUDES)

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
override CXXFLAGS += -D_GLIBCXX_USE_CXX11_ABI=$(LIBTORCH_ABI)

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
# THE authoritative core source list lives in wrf_sdirk3_core_sources.txt
# (full-repo review PR 5): both this Makefile and CMake read it; neither owns
# a copy. Fail-close manifest validation below runs at parse time — before any
# compilation — and rejects a missing/empty manifest, duplicates, nonexistent
# sources, non-.cpp entries, absolute or parent paths, and test/backup/legacy/
# archive directories (a manifest pointing into a dormant directory is exactly
# the Makefile.kslab-class hazard this replaces).
CORE_SOURCE_MANIFEST := wrf_sdirk3_core_sources.txt

ifeq ($(wildcard $(CORE_SOURCE_MANIFEST)),)
$(error SDIRK3 core source manifest '$(CORE_SOURCE_MANIFEST)' is missing)
endif

SOURCES := $(shell awk 'NF && $$1 !~ /^\#/ { print $$1 }' $(CORE_SOURCE_MANIFEST))

ifeq ($(strip $(SOURCES)),)
$(error SDIRK3 core source manifest is empty)
endif

CORE_DUPS := $(shell printf '%s\n' $(SOURCES) | sort | uniq -d)
ifneq ($(strip $(CORE_DUPS)),)
$(error duplicate sources in core manifest: $(CORE_DUPS))
endif

CORE_NONCPP := $(filter-out %.cpp,$(SOURCES))
ifneq ($(strip $(CORE_NONCPP)),)
$(error non-.cpp entries in core manifest: $(CORE_NONCPP))
endif

CORE_BADPATH := $(shell printf '%s\n' $(SOURCES) | grep -E '^/|\.\.|(^|/)(test|tests|legacy|archive)/|(^|/)backup[^/]*/' || true)
ifneq ($(strip $(CORE_BADPATH)),)
$(error forbidden paths in core manifest (absolute, .., or test/backup/legacy/archive dirs): $(CORE_BADPATH))
endif

CORE_MISSING := $(foreach s,$(SOURCES),$(if $(wildcard $(s)),,$(s)))
ifneq ($(strip $(CORE_MISSING)),)
$(error core manifest lists nonexistent sources: $(CORE_MISSING))
endif

OBJECTS = $(SOURCES:.cpp=.o)

# Target library (must match WRF's expectation)
LIB_NAME = libwrf_sdirk3_libtorch.a

.PHONY: all clean check-core-manifest check-core-archive

all: $(LIB_NAME)

AR      ?= ar
RANLIB  ?= ranlib

# Atomic exact-set reconstruction (PR 5): the old `ar rcs $@ $^` UPDATED an
# existing archive, so a source removed from the manifest left its member
# behind forever. Build into a temp archive and rename — a failed build never
# clobbers the previous good archive, and the member set is exactly $(OBJECTS).
$(LIB_NAME): $(CORE_SOURCE_MANIFEST) $(OBJECTS)
	@set -e; \
	tmp="$@.tmp"; \
	rm -f "$$tmp"; \
	$(AR) rcs "$$tmp" $(OBJECTS); \
	$(RANLIB) "$$tmp"; \
	mv -f "$$tmp" "$@"
	@echo "Successfully built production SDIRK3 core $@"

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

# Review round 3d: the raw map-factor safety predicate lives in an inline
# header consumed by the impl. Without a dependency, a header-only fix would
# leave the production archive on the OLD inline predicate while CTest passes
# on the new one. Explicit dep covers builds before .d files exist; -MMD -MP
# below keeps every future header edit tracked automatically.
wrf_sdirk3_tile_unified_impl.o: wrf_sdirk3_msf_raw_stats.h wrf_sdirk3_geometry_contract.h

# Compiler-generated header dependencies (-MMD writes %.d alongside %.o;
# -MP adds phony targets so deleted headers don't break the build).
DEPFILES = $(OBJECTS:.o=.d)
-include $(DEPFILES)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -f $(OBJECTS) $(DEPFILES) $(LIB_NAME) $(LIB_NAME).tmp

# Manifest validation is parse-time (above); this target exists so callers can
# assert it explicitly and get a positive confirmation line.
check-core-manifest:
	@echo "check-core-manifest OK: $(words $(SOURCES)) sources, no duplicates, all present"

# EXACT member-set equality between the built archive and the manifest:
# missing member, extra member, duplicate member, or a retired old-source
# member all FAIL. "Expected members are present" alone is NOT the contract.
check-core-archive: $(LIB_NAME)
	@set -e; \
	printf '%s\n' $(SOURCES) | sed 's/\.cpp$$//' | sort > $(LIB_NAME).expected; \
	ar t $(LIB_NAME) | grep -v '^__\.SYMDEF' | sed -e 's/\.cpp\.o$$//' -e 's/\.obj$$//' -e 's/\.o$$//' | sort > $(LIB_NAME).actual; \
	dups=$$(uniq -d < $(LIB_NAME).actual); \
	if [ -n "$$dups" ]; then echo "check-core-archive FAIL: duplicate members:"; echo "$$dups"; rm -f $(LIB_NAME).expected $(LIB_NAME).actual; exit 1; fi; \
	if ! cmp -s $(LIB_NAME).expected $(LIB_NAME).actual; then \
	  echo "check-core-archive FAIL: archive/manifest member sets differ:"; \
	  diff $(LIB_NAME).expected $(LIB_NAME).actual || true; \
	  rm -f $(LIB_NAME).expected $(LIB_NAME).actual; exit 1; \
	fi; \
	rm -f $(LIB_NAME).expected $(LIB_NAME).actual; \
	echo "check-core-archive OK: exact member-set equality ($(words $(SOURCES)) members)"

test: $(LIB_NAME)
	@echo "Library built successfully: $(LIB_NAME)"
	@echo "Ready for integration with WRF"