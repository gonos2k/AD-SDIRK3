# Documentation Cleanup Plan

**Date**: Sat, 16 Aug 2025 17:15:00 +0900  
**Action**: Documentation cleanup and consolidation  
**Review**: Final review before archiving redundant files  

---

## 📋 File Disposition Plan

### ✅ FILES TO KEEP (1 file)

1. **CRITICAL_LAYOUT_CORRECTION_SUMMARY.md**
   - **Reason**: Documents critical memory layout correction (j,k,i)
   - **Importance**: MUST NOT BE FORGOTTEN - prevents future mistakes
   - **Cannot be deleted**: Contains essential correction information

---

### 📦 FILES TO ARCHIVE (18 files)

#### Already Consolidated into WRF_SDIRK3_UNIFIED_DESIGN.md
2. **PYTORCH_AD_TENSOR_ARCHITECTURE.md** - Fully incorporated
3. **WRF_PYTORCH_COMPUTATIONAL_GRAPH_ARCHITECTURE.md** - Fully incorporated
4. **CROSS_PLATFORM_ARCHITECTURE.md** - Fully incorporated

#### Implementation Status/Progress (Not Design)
5. **INTEGRATION_STATUS.md** - Status tracking
6. **ITEM_REMOVAL_PROGRESS.md** - Bug tracking
7. **CROSS_PLATFORM_IMPLEMENTATION_SUMMARY.md** - Implementation details

#### Meta Documentation (Documentation about documentation)
8. **DOCUMENTATION_REORGANIZATION_COMPLETE.md** - Meta
9. **DESIGN_CONSOLIDATION_SUMMARY.md** - Meta
10. **DOCUMENTATION_CLEANUP_PLAN.md** - This file (Meta)

#### Old/Superseded Plans
11. **integration_roadmap.md** - Old plan
12. **SDIRK3_UNIFIED_INTEGRATION_PLAN.md** - Old plan
13. **SDIRK3_FILE_MIGRATION_MAP.md** - File organization
14. **memory_layout_design_v2.md** - Superseded design

#### Summaries/Overviews (Redundant)
15. **DESIGN_SUMMARY.md** - Summary of designs
16. **SDIRK3_INTEGRATION_EXECUTIVE_SUMMARY.md** - Executive summary
17. **INTEGRATION_DESIGN.md** - Old design overview

#### Implementation Guides (Useful but not core design)
18. **SDIRK3_REVISED_INTEGRATION_PLAN.md** - Current plan (implementation focused)
19. **GRADIENT_PRESERVATION_GUIDE.md** - Technical guide (specific technique)
20. **SDIRK3_VALIDATION_FRAMEWORK.md** - Testing framework (implementation)

---

## 🎯 Rationale

### Why Keep Only One?

1. **WRF_SDIRK3_UNIFIED_DESIGN.md** contains ALL design information
2. **Reduces confusion** from multiple overlapping documents
3. **Single source of truth** for architecture
4. **Easier maintenance** with one design document

### Why Keep CRITICAL_LAYOUT_CORRECTION_SUMMARY.md?

1. **Historical importance** - Documents a critical bug that was fixed
2. **Prevents regression** - Reminder of correct memory layout
3. **Compact and focused** - Only 50 lines, specific information
4. **Referenced by unified design** - Complements the main document

---

## 🔄 Archive Strategy

Instead of deletion, files will be moved to an archive directory:
```
sdirk3_lib/
├── docs/
│   └── CRITICAL_LAYOUT_CORRECTION_SUMMARY.md (KEEP)
└── docs_archive_2025_08_16/
    └── (18 archived files)
```

This approach:
- ✅ Preserves history if needed
- ✅ Cleans up active documentation
- ✅ Reduces confusion
- ✅ Reversible if needed

---

## ⚠️ Final Confirmation

### Documents Remaining After Cleanup:
1. `/Users/yhlee/dWRF/external/sdirk3_lib/WRF_SDIRK3_UNIFIED_DESIGN.md` (Main design)
2. `/Users/yhlee/dWRF/external/sdirk3_lib/CLAUDE.md` (Index)
3. `/Users/yhlee/dWRF/external/sdirk3_lib/docs/CRITICAL_LAYOUT_CORRECTION_SUMMARY.md` (Critical note)

### Total Reduction:
- Before: 19 files in docs/ + 2 in main = 21 files
- After: 1 file in docs/ + 2 in main = 3 files
- **Reduction: 86%**

---

## ✅ Ready to Execute?

This plan will:
1. Create archive directory `docs_archive_2025_08_16/`
2. Move 18 files to archive
3. Keep only CRITICAL_LAYOUT_CORRECTION_SUMMARY.md in docs/
4. Result in clean, focused documentation structure

**Proceed with archiving? [YES/NO]**