# CLAUDE.md - SDIRK3 Project Documentation Index

**Last Updated**: Sat, 16 Aug 2025 17:00:00 +0900  
**Project**: WRF SDIRK3 PyTorch Integration  
**Location**: /Users/yhlee/dWRF/external/sdirk3_lib  

---

## 🎯 Project Overview

This is the main documentation index for the WRF SDIRK3 PyTorch integration project. All design documents, implementation guides, and technical specifications are organized in the `docs/` directory.

**APPROVED DESIGN**: Version 6.0 of WRF_SDIRK3_FINAL_DESIGN.md has been approved for implementation as of August 16, 2025.

**Critical Note**: The memory layout has been VERIFIED as ALREADY CORRECT. The libtorch_wrf/sdirk3 implementation already uses the correct (j,k,i) layout. Focus on bug fixes and em_b_wave testing, NOT layout migration.

---

## 📚 Documentation Structure (Simplified)

### 🌟 PRIMARY REFERENCE - START HERE

**[docs/WRF_SDIRK3_FINAL_DESIGN.md](docs/WRF_SDIRK3_FINAL_DESIGN.md)**  
- **Status**: ✅ APPROVED DESIGN (Version 6.0) - August 16, 2025
- **Priority**: ⚠️ DEFINITIVE IMPLEMENTATION REFERENCE
- **Content**: Complete restructured architecture with 6 major improvements:
  1. **System Architecture**: Overall design and integration strategy
  2. **Fortran-C++ Interface**: Zero-copy memory management and error handling
  3. **Differentiable Model Core**: Computational graph, dynamics, and physics
  4. **SDIRK3 Implicit Solver**: Newton-Krylov with unified JVP architecture
  5. **Performance & Platform Optimization**: Cross-platform support (MPS/CUDA/CPU)
  6. **Validation & Testing**: Comprehensive testing framework
- **Key Improvements**:
  - ✅ Unified JVP implementation (custom rules + finite difference fallback)
  - ✅ Scientific validation strategy for differentiable physics
  - ✅ Fortran callback performance cost documentation
  - ✅ Complete line search algorithm with Armijo condition
  - ✅ Consistent zero-copy using torch::from_blob throughout
  - ✅ Topic-based organization (not historical)
- **Purpose**: DEFINITIVE technical specification for implementation

### Critical Design Document

**[CRITICAL_LAYOUT_VERIFICATION_AND_DESIGN.md](docs/CRITICAL_LAYOUT_VERIFICATION_AND_DESIGN.md)**  
- **Status**: Comprehensive design document with layout verification
- **Priority**: ⚠️ CRITICAL - PRIMARY TECHNICAL REFERENCE
- **Contents**: 
  - Memory layout verification: (j,k,i) is already implemented correctly
  - Complete architectural design with implementation details
  - Physics integration, Newton-Krylov solver, cross-platform support
  - Performance characteristics and validation framework
- **Focus**: Bug fixes and testing, NOT layout changes

### Archive
- **Location**: `docs_archive_2025_08_16/`
- **Contents**: 19 archived documentation files
- **Purpose**: Historical reference only

---

## 💡 Critical Instructions for Claude Code

### Reading Order for New Sessions

When starting a new Claude Code session for this project:

1. **ONLY REQUIRED**: Read `docs/WRF_SDIRK3_FINAL_DESIGN.md` - The definitive implementation reference
2. **Optional**: Other documents are for historical reference only

### Key Points to Remember

⚠️ **CRITICAL**: The memory layout is ALREADY CORRECT (j,k,i). Do NOT attempt to change it.

✅ **Focus Areas**:
- Bug fixes in Newton solver convergence
- Removing .item() calls for AD support
- em_b_wave testing and validation
- Performance optimization

🚫 **Avoid**:
- Memory layout changes (already correct)
- Temporary workarounds (maintain WRF consistency)
- Adding limiters/clamping (use diagnostics instead)

---

## 🧠 Memory Log

### Development and Troubleshooting Memories

- `/sc:troubleshoot -ultrathink /Users/yhlee/dWRF/WRF-SDIRK3-design.md 숙지 후 디버깅`: Review SDIRK3 design document thoroughly before debugging session
- `clean -a ; configure 37번, 1번 사용; nohup compile -j em_b_wave`: Specific compilation steps for em_b_wave
- `nohup compile -j 4 em_b_wave 사용하고, 개별 디렉토리에서 make는 하지 않는다`: Use nohup with 4 cores, avoid individual directory makes
- `baseline test 완료되었으니 시도하지 말것`: Baseline testing is complete, do not repeat
- 임시방편은 디버깅을 어렵게 하므로 WRF 연산과정과 정합성을 유지하며 정석대로 진행할 것

---

## 🔧 Current Tasks

### High Priority
1. **Identify specific bugs** in Newton solver convergence
2. **Complete .item() removal** for full AD support
3. **Setup em_b_wave test** environment

### Medium Priority
4. Performance optimization
5. Multi-GPU support implementation
6. Documentation updates

### Completed
- ✅ Memory layout verification (j,k,i confirmed correct)
- ✅ PyTorch AD/JVP architecture design
- ✅ WRF dynamics/physics computational graph design
- ✅ Cross-platform support (MPS/CUDA/CPU)
- ✅ Fortran interface implementation

---

## 📁 Project Structure

```
sdirk3_lib/
├── CLAUDE.md (this file)
├── docs/                   # All design and documentation
│   ├── CRITICAL_LAYOUT_CORRECTION_SUMMARY.md
│   ├── PYTORCH_AD_TENSOR_ARCHITECTURE.md
│   ├── WRF_PYTORCH_COMPUTATIONAL_GRAPH_ARCHITECTURE.md
│   └── ... (other documentation)
├── src/                    # Source code
│   ├── sdirk3_solver.cpp
│   ├── newton_solver_ad_safe.cpp
│   └── ...
├── include/                # Headers
│   ├── sdirk3_solver.h
│   └── ...
└── tests/                  # Test cases
    └── em_b_wave/
```

---

## 🚀 Quick Start for New Sessions

```bash
# 1. Review unified design document (PRIMARY REFERENCE)
cat WRF_SDIRK3_UNIFIED_DESIGN.md

# 2. Verify memory layout understanding
cat docs/CRITICAL_LAYOUT_CORRECTION_SUMMARY.md | head -50

# 3. Check current implementation plan
cat docs/SDIRK3_REVISED_INTEGRATION_PLAN.md | grep -A5 "Current Week"

# 4. Start working on bugs or testing
cd /Users/yhlee/dWRF/external/libtorch_wrf/sdirk3
```

---

## 📞 Contact & Support

For questions or issues related to this project:
- Review documentation in `docs/` directory first
- Check Memory Log section for common issues
- Maintain WRF computational consistency (정석대로 진행)

---

**End of CLAUDE.md**