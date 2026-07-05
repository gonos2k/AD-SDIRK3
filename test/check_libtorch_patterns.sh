#!/bin/bash
# =============================================================================
# check_libtorch_patterns.sh
# LibTorch tensor pattern checker for WRF-SDIRK3 integration
#
# FIX 2025-12-31 Batch41: Periodic check script for CI/pre-commit
#
# PURPOSE:
#   Detects problematic LibTorch tensor patterns that may cause:
#   - Silent no-ops (select()= or slice()= assignments)
#   - AD graph issues (.item() without NoGradGuard)
#   - Potential memory leaks or incorrect gradients
#   - Device mismatch (from_blob without explicit device)
#
# USAGE:
#   ./check_libtorch_patterns.sh [path]
#   ./check_libtorch_patterns.sh --key-dirs   # Check only key LibTorch dirs
#
#   If path is not specified, checks the entire dWRF directory.
#   Use --key-dirs for faster focused scans of dyn_em/, external/libtorch_wrf/,
#   and external/sdirk3_lib/.
#
# EXIT CODES:
#   0 - No issues found
#   1 - Issues detected (see output for details)
#
# RECOMMENDED:
#   Run this script:
#   - Before each commit (pre-commit hook)
#   - As part of CI pipeline
#   - After merging external LibTorch code
# =============================================================================

set -e

# Color codes for output
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
NC='\033[0m' # No Color

# Handle options for focused scanning
KEY_DIRS_MODE=false
CPU_ONLY_MODE=false
JSON_MODE=false
SUMMARY_MODE=false
STRICT_LINT_MODE=false
SKIP_DOCS_MODE=false   # Skip documentation/log files (*.md, *.log, *.patch)
SKIP_TESTS_MODE=false  # Skip test files (test_*, *_test.cpp, tests/)
DIAG_ONLY_MODE=false   # Report only diagnostic .item() paths (for tracking)
AUTO_POLICY_MODE=true  # Parse CPU-only policy from documentation by default
BASE_PATH="/Users/yhlee/dWRF"
DOCS_PATH="$BASE_PATH/external/sdirk3_lib/docs/LIBTORCH_ITEM_USAGE_GUIDE.md"
EXCEPTIONS_PATH="$BASE_PATH/test/lint_exceptions.json"
EXPECTED_SCHEMA_VERSION="1.0"

# JSON output tracking arrays
declare -a JSON_ISSUES=()
declare -a JSON_WARNINGS=()

# Exception patterns (loaded from JSON or defaults)
# These are used in grep -v to exclude false positives
EXCEPT_SELECT="copy_\|fill_\|zero_"
EXCEPT_SLICE="copy_\|fill_\|zero_"
EXCEPT_FROM_BLOB_FILES="torch_cpu\.h\|_autograd_utils\.h\|_zero_copy"
EXCEPT_FROM_BLOB_PATTERNS="make_cpu_from_blob_opts\|\.device(\|kCPU"
EXCEPT_BRACKET="index_put_\|fill_\|copy_\|zero_"
EXCEPT_INDEX="index_put_"

# Load exceptions from JSON if available
EXCEPTIONS_LOADED=false
EXCEPTIONS_SCHEMA_VALID=false
CONFIG_ERRORS=0
if [ -f "$EXCEPTIONS_PATH" ]; then
    # Check if jq is available for proper JSON parsing
    if command -v jq &> /dev/null; then
        # Validate schema version first
        SCHEMA_VERSION=$(jq -r '.schema_version // "unknown"' "$EXCEPTIONS_PATH" 2>/dev/null)
        if [ "$SCHEMA_VERSION" = "$EXPECTED_SCHEMA_VERSION" ]; then
            EXCEPTIONS_SCHEMA_VALID=true
            EXCEPT_SELECT=$(jq -r '.check_1_select_assignment.exclude_patterns | join("\\|")' "$EXCEPTIONS_PATH" 2>/dev/null || echo "$EXCEPT_SELECT")
            EXCEPT_SLICE=$(jq -r '.check_2_slice_assignment.exclude_patterns | join("\\|")' "$EXCEPTIONS_PATH" 2>/dev/null || echo "$EXCEPT_SLICE")
            EXCEPT_FROM_BLOB_FILES=$(jq -r '.check_5_from_blob.exclude_files | join("\\|")' "$EXCEPTIONS_PATH" 2>/dev/null | sed 's/\./\\./g' || echo "$EXCEPT_FROM_BLOB_FILES")
            EXCEPT_BRACKET=$(jq -r '.check_7_bracket_assignment.exclude_patterns | join("\\|")' "$EXCEPTIONS_PATH" 2>/dev/null || echo "$EXCEPT_BRACKET")
            EXCEPT_INDEX=$(jq -r '.check_8_index_assignment.exclude_patterns | join("\\|")' "$EXCEPTIONS_PATH" 2>/dev/null || echo "$EXCEPT_INDEX")
            EXCEPTIONS_LOADED=true
        else
            echo -e "${RED}ERROR: lint_exceptions.json schema version mismatch${NC}"
            echo -e "${RED}  Expected: $EXPECTED_SCHEMA_VERSION, Found: $SCHEMA_VERSION${NC}"
            echo -e "${RED}  Using fallback defaults${NC}"
            echo ""
            JSON_ISSUES+=("{\"check\":\"config\",\"issue\":\"schema_version_mismatch\",\"expected\":\"$EXPECTED_SCHEMA_VERSION\",\"found\":\"$SCHEMA_VERSION\"}")
            CONFIG_ERRORS=$((CONFIG_ERRORS + 1))
            if [ "$STRICT_LINT_MODE" = true ]; then
                echo -e "${RED}FATAL: --strict-lint enabled, exiting on config error${NC}"
                exit 2
            fi
        fi
    else
        echo -e "${YELLOW}WARNING: jq not installed, using fallback exception patterns${NC}"
        JSON_WARNINGS+=("{\"check\":\"config\",\"issue\":\"jq_not_installed\"}")
        EXCEPTIONS_LOADED=true  # File exists, using defaults is OK
    fi
else
    echo -e "${YELLOW}WARNING: lint_exceptions.json not found${NC}"
    echo -e "${YELLOW}  Expected: $EXCEPTIONS_PATH${NC}"
    echo -e "${YELLOW}  Using fallback defaults${NC}"
    echo ""
    JSON_WARNINGS+=("{\"check\":\"config\",\"issue\":\"exceptions_not_found\",\"path\":\"$EXCEPTIONS_PATH\"}")
    if [ "$STRICT_LINT_MODE" = true ]; then
        echo -e "${RED}FATAL: --strict-lint enabled, exiting on config error${NC}"
        exit 2
    fi
fi

# Parse arguments
for arg in "$@"; do
    case $arg in
        --key-dirs|--fixed-tree)
            # --fixed-tree is alias for --key-dirs (CI-friendly name)
            KEY_DIRS_MODE=true
            ;;
        --cpu-only)
            CPU_ONLY_MODE=true
            ;;
        --json)
            JSON_MODE=true
            ;;
        --summary)
            SUMMARY_MODE=true
            ;;
        --strict-lint)
            STRICT_LINT_MODE=true
            ;;
        --no-auto-policy)
            AUTO_POLICY_MODE=false
            ;;
        --skip-docs)
            SKIP_DOCS_MODE=true
            ;;
        --skip-tests)
            SKIP_TESTS_MODE=true
            ;;
        --diag-only)
            DIAG_ONLY_MODE=true
            ;;
        --help|-h)
            echo "Usage: $0 [OPTIONS] [PATH]"
            echo ""
            echo "Options:"
            echo "  --key-dirs, --fixed-tree  Scan only key LibTorch directories (CI mode)"
            echo "  --cpu-only                Enable CPU-only policy enforcement (CHECK 9)"
            echo "  --json                    Output JSON summary for CI integration"
            echo "  --summary                 Output one-line human-readable summary"
            echo "  --strict-lint             Fail on config parse errors (recommended for CI)"
            echo "  --skip-docs               Skip docs/logs (*.md, *.log, *.patch, *.txt)"
            echo "  --skip-tests              Skip test files (test_*, *_test.cpp, tests/)"
            echo "  --diag-only               Report only diagnostic .item() paths (CHECK 3)"
            echo "  --no-auto-policy          Disable auto-parsing CPU-only policy from docs"
            echo "  --help, -h                Show this help message"
            echo ""
            echo "Key directories scanned with --key-dirs/--fixed-tree:"
            echo "  - dyn_em"
            echo "  - external/libtorch_wrf"
            echo "  - external/sdirk3_lib"
            echo ""
            echo "Output modes (can be combined for CI):"
            echo "  --json     {\"status\":\"pass|fail\",\"issues\":[...],\"warnings\":[...]}"
            echo "  --summary  PASS: 0 issues, 1 warning (unified_state_vector.h, ...)"
            echo ""
            echo "Example CI usage:"
            echo "  $0 --fixed-tree --skip-tests --summary --json > report.json"
            exit 0
            ;;
        *)
            SEARCH_PATH="$arg"
            ;;
    esac
done

# Default search path if not specified
SEARCH_PATH="${SEARCH_PATH:-$BASE_PATH}"

# FIX 2025-12-31 Batch41: CPU-only policy files with STRICT/RELAXED levels
# Sync with: docs/LIBTORCH_ITEM_USAGE_GUIDE.md "CPU-Only Policy Files" section
#
# STRICT: TORCH_CHECK(is_cpu) required, no .to(kCPU) fallbacks
# RELAXED: NoGradGuard + .to(kCPU) allowed for diagnostic norms

# Default policy table (fallback if docs not found)
# Using parallel arrays for bash 3.2 compatibility (macOS /bin/bash)
CPU_POLICY_FILES=("unified_state_vector.h" "tile_based_solver.h")
CPU_POLICY_LEVELS=("STRICT" "RELAXED")

# Helper function to get policy level for a file
get_policy_level() {
    local target_file="$1"
    local i=0
    for file in "${CPU_POLICY_FILES[@]}"; do
        if [ "$file" = "$target_file" ]; then
            echo "${CPU_POLICY_LEVELS[$i]}"
            return
        fi
        i=$((i + 1))
    done
    echo "STRICT"  # Default policy
}

# Auto-parse CPU-only policy from documentation
POLICY_PARSE_SUCCESS=false
POLICY_PARSE_COUNT=0
if [ "$AUTO_POLICY_MODE" = true ]; then
    if [ -f "$DOCS_PATH" ]; then
        # Parse the policy table from markdown (format: | file.h | STRICT/RELAXED | ... |)
        # Example: | unified_state_vector.h | STRICT | Required | No |
        # Reset arrays for fresh parse
        CPU_POLICY_FILES=()
        CPU_POLICY_LEVELS=()

        while IFS='|' read -r _ file policy _; do
            file=$(echo "$file" | xargs)  # trim whitespace
            policy=$(echo "$policy" | xargs)
            # Check file extension and policy value
            if echo "$file" | grep -qE '\.(h|cpp)$' && echo "$policy" | grep -qE '^(STRICT|RELAXED)$'; then
                CPU_POLICY_FILES+=("$file")
                CPU_POLICY_LEVELS+=("$policy")
                POLICY_PARSE_COUNT=$((POLICY_PARSE_COUNT + 1))
            fi
        done < <(grep -E '^\|[[:space:]]*[a-zA-Z_]+\.(h|cpp)[[:space:]]*\|[[:space:]]*(STRICT|RELAXED)' "$DOCS_PATH" 2>/dev/null || true)

        if [ $POLICY_PARSE_COUNT -gt 0 ]; then
            POLICY_PARSE_SUCCESS=true
        else
            echo -e "${YELLOW}WARNING: Failed to parse CPU-only policy from documentation${NC}"
            echo -e "${YELLOW}  Doc path: $DOCS_PATH${NC}"
            echo -e "${YELLOW}  Expected format: | file.h | STRICT/RELAXED | ... |${NC}"
            echo -e "${YELLOW}  Using fallback defaults: unified_state_vector.h=STRICT, tile_based_solver.h=RELAXED${NC}"
            echo ""
            JSON_WARNINGS+=("{\"check\":\"config\",\"issue\":\"policy_parse_failed\",\"path\":\"$DOCS_PATH\"}")
            # Restore defaults
            CPU_POLICY_FILES=("unified_state_vector.h" "tile_based_solver.h")
            CPU_POLICY_LEVELS=("STRICT" "RELAXED")
            if [ "$STRICT_LINT_MODE" = true ]; then
                echo -e "${RED}FATAL: --strict-lint enabled, exiting on config error${NC}"
                exit 2
            fi
        fi
    else
        echo -e "${YELLOW}WARNING: CPU-only policy documentation not found${NC}"
        echo -e "${YELLOW}  Expected: $DOCS_PATH${NC}"
        echo -e "${YELLOW}  Using fallback defaults${NC}"
        echo ""
        JSON_WARNINGS+=("{\"check\":\"config\",\"issue\":\"docs_not_found\",\"path\":\"$DOCS_PATH\"}")
        if [ "$STRICT_LINT_MODE" = true ]; then
            echo -e "${RED}FATAL: --strict-lint enabled, exiting on config error${NC}"
            exit 2
        fi
    fi
fi

# Build legacy array from policy files
CPU_ONLY_FILES=("${CPU_POLICY_FILES[@]}")

# FIX 2025-12-31 Batch41: Skip docs/logs filter for cleaner CI output
# Filters out lines referencing documentation or log patterns
filter_docs_noise() {
    if [ "$SKIP_DOCS_MODE" = true ]; then
        grep -v '\.md\|\.log\|\.patch\|\.txt\|README\|CHANGELOG\|LICENSE\|TODO\|FIXME.*doc'
    else
        cat
    fi
}

# FIX 2025-12-31 Batch41: Skip test files filter for cleaner CI output
# Filters out test file matches (test_*, *_test.cpp, tests/, unittest, mock)
filter_tests_noise() {
    if [ "$SKIP_TESTS_MODE" = true ]; then
        grep -v 'test_\|_test\.\|/tests/\|/test/\|unittest\|mock\|Mock\|TEST_'
    else
        cat
    fi
}

# Combined filter: apply all active noise filters
filter_noise() {
    filter_docs_noise | filter_tests_noise
}

# Expected paths for CPU-only files (for existence verification)
CPU_ONLY_EXPECTED_PATHS=(
    "external/sdirk3_lib/include/sdirk3/unified_state_vector.h"
    "external/sdirk3_lib/include/sdirk3/tile_based_solver.h"
)

# FIX 2025-12-31 Batch41: Key directories to check (for focused scanning)
# These are the primary LibTorch integration points
KEY_DIRS=(
    "$BASE_PATH/dyn_em"
    "$BASE_PATH/external/libtorch_wrf"
    "$BASE_PATH/external/sdirk3_lib"
)

echo "============================================="
echo "LibTorch Pattern Checker for WRF-SDIRK3"
echo "============================================="

# Show configuration status
if [ "$EXCEPTIONS_LOADED" = true ] && [ "$EXCEPTIONS_SCHEMA_VALID" = true ]; then
    echo -e "Exceptions: ${GREEN}Loaded (schema v$EXPECTED_SCHEMA_VERSION)${NC}"
elif [ "$EXCEPTIONS_LOADED" = true ]; then
    echo -e "Exceptions: ${YELLOW}Loaded (schema validation skipped)${NC}"
else
    echo -e "Exceptions: ${YELLOW}Using built-in defaults${NC}"
fi
if [ "$POLICY_PARSE_SUCCESS" = true ]; then
    echo -e "CPU Policy: ${GREEN}Parsed from docs ($POLICY_PARSE_COUNT files)${NC}"
elif [ "$AUTO_POLICY_MODE" = true ]; then
    echo -e "CPU Policy: ${YELLOW}Using fallback defaults${NC}"
fi
if [ "$STRICT_LINT_MODE" = true ]; then
    echo -e "Strict Mode: ${GREEN}Enabled (config errors = CI failure)${NC}"
fi
if [ "$SKIP_DOCS_MODE" = true ]; then
    echo -e "Skip Docs: ${GREEN}Enabled (*.md, *.log, *.patch, *.txt excluded)${NC}"
fi
if [ "$SKIP_TESTS_MODE" = true ]; then
    echo -e "Skip Tests: ${GREEN}Enabled (test_*, *_test.cpp, tests/ excluded)${NC}"
fi
if [ "$KEY_DIRS_MODE" = true ]; then
    echo "Mode: KEY DIRECTORIES ONLY (fast scan)"
    # FIX 2025-12-31 Batch41: Skip empty directories for cleaner CI logs
    GREP_PATHS=""
    CHECKED_DIRS=""
    SKIPPED_DIRS=""
    for dir in "${KEY_DIRS[@]}"; do
        if [ -d "$dir" ]; then
            # Check if directory has any .cpp or .h files
            file_count=$(find "$dir" -name '*.cpp' -o -name '*.h' 2>/dev/null | head -1 | wc -l)
            if [ "$file_count" -gt 0 ]; then
                GREP_PATHS="$GREP_PATHS $dir"
                CHECKED_DIRS="$CHECKED_DIRS $(basename $dir)"
            else
                SKIPPED_DIRS="$SKIPPED_DIRS $(basename $dir)"
            fi
        else
            SKIPPED_DIRS="$SKIPPED_DIRS $(basename $dir)(not found)"
        fi
    done
    echo "Checking:$CHECKED_DIRS"
    if [ -n "$SKIPPED_DIRS" ]; then
        echo "Skipped (empty/missing):$SKIPPED_DIRS"
    fi
    SEARCH_PATH="$GREP_PATHS"
else
    echo "Mode: FULL SCAN"
    echo "Checking: $SEARCH_PATH"
fi
echo ""

ISSUES_FOUND=0

# -----------------------------------------------------------------------------
# CHECK 1: select() = assignment (no-op in modern LibTorch)
# -----------------------------------------------------------------------------
echo "CHECK 1: select() = assignment patterns (CRITICAL)"
echo "  Pattern: .select(...) = value"
echo "  Issue: This is a NO-OP - the value is not assigned!"
echo "  Fix: Use .select(...).copy_(value) or .select(...).fill_(scalar)"
echo ""

MATCHES=$(grep -rn '\.select([^)]*)\s*=' --include="*.cpp" --include="*.h" "$SEARCH_PATH" 2>/dev/null | grep -v '//' | grep -v "$EXCEPT_SELECT" || true)
if [ -n "$MATCHES" ]; then
    echo -e "${RED}FOUND ISSUES:${NC}"
    echo "$MATCHES"
    MATCH_COUNT=$(echo "$MATCHES" | wc -l)
    JSON_ISSUES+=("{\"check\":1,\"pattern\":\"select()=\",\"count\":$MATCH_COUNT,\"severity\":\"CRITICAL\"}")
    echo ""
    echo -e "${YELLOW}  Recommended fix:${NC}"
    echo "    tensor.select(dim, idx) = value;        // ❌ NO-OP"
    echo "    tensor.select(dim, idx).fill_(value);   // ✅ scalar"
    echo "    tensor.select(dim, idx).copy_(other);   // ✅ tensor"
    echo ""
    ISSUES_FOUND=1
else
    echo -e "${GREEN}  OK - No issues found${NC}"
fi
echo ""

# -----------------------------------------------------------------------------
# CHECK 2: slice() = assignment (no-op in modern LibTorch)
# -----------------------------------------------------------------------------
echo "CHECK 2: slice() = assignment patterns (CRITICAL)"
echo "  Pattern: .slice(...) = value"
echo "  Issue: This is a NO-OP - the value is not assigned!"
echo "  Fix: Use .slice(...).copy_(value)"
echo ""

MATCHES=$(grep -rn '\.slice([^)]*)\s*=' --include="*.cpp" --include="*.h" "$SEARCH_PATH" 2>/dev/null | grep -v '//' | grep -v "$EXCEPT_SLICE" || true)
if [ -n "$MATCHES" ]; then
    echo -e "${RED}FOUND ISSUES:${NC}"
    echo "$MATCHES"
    MATCH_COUNT=$(echo "$MATCHES" | wc -l)
    JSON_ISSUES+=("{\"check\":2,\"pattern\":\"slice()=\",\"count\":$MATCH_COUNT,\"severity\":\"CRITICAL\"}")
    echo ""
    echo -e "${YELLOW}  Recommended fix:${NC}"
    echo "    tensor.slice(dim, start, end) = value;        // ❌ NO-OP"
    echo "    tensor.slice(dim, start, end).copy_(other);   // ✅ correct"
    echo ""
    ISSUES_FOUND=1
else
    echo -e "${GREEN}  OK - No issues found${NC}"
fi
echo ""

# -----------------------------------------------------------------------------
# CHECK 3: .item() without NoGradGuard (AD graph pollution)
# FIX 2025-12-31 Batch41: Enhanced context-aware detection
# FIX 2025-12-31 Batch41: Added LINT:DIAG_OK and AD_SAFE_* macro recognition
# -----------------------------------------------------------------------------
echo "CHECK 3: .item() usage without NoGradGuard (WARNING)"
echo "  Pattern: .item<...>() outside NoGradGuard block"
echo "  Issue: May cause AD graph issues or prevent gradient computation"
echo "  Fix: Wrap with torch::NoGradGuard no_grad; block"
echo "       Or use AD_SAFE_SCALAR()/AD_SAFE_BOOL() macros"
echo "       Or add // LINT:DIAG_OK comment for explicit exception"
echo ""

# Safe patterns that don't require NoGradGuard check:
# - NoGradGuard / no_grad / AutoGradMode in context
# - AD_SAFE_SCALAR / AD_SAFE_BOOL macros (diagnostic wrappers)
# - LINT:DIAG_OK comment tag (explicit exception)
# - to_scalar / ad_safe_ helper functions
SAFE_PATTERNS='NoGradGuard\|no_grad\|torch::AutoGradMode\|AD_SAFE_SCALAR\|AD_SAFE_BOOL\|AD_SAFE_DOUBLE\|LINT:DIAG_OK\|to_scalar\|ad_safe_'

# Enhanced heuristic: Check if safe pattern exists within 10 lines before .item()
CHECK3_ISSUES=0
CHECK3_SKIPPED=0
CHECK3_DIAG_COUNT=0  # FIX 2025-12-31: Track diagnostic paths for --diag-only mode
declare -a CHECK3_DIAG_PATHS=()
ITEM_LOCATIONS=$(grep -rn '\.item<' --include="*.cpp" --include="*.h" $SEARCH_PATH 2>/dev/null | filter_noise || true)

if [ -n "$ITEM_LOCATIONS" ]; then
    echo "  Scanning .item<>() locations for safe patterns..."
    while IFS=: read -r filepath linenum rest; do
        # Skip if file doesn't exist
        [ -f "$filepath" ] || continue

        # Calculate start line for context (10 lines before, minimum 1)
        start_line=$((linenum > 10 ? linenum - 10 : 1))

        # Also check current line for inline LINT:DIAG_OK or AD_SAFE_* usage
        current_line=$(sed -n "${linenum}p" "$filepath" 2>/dev/null || true)

        # Check if safe pattern exists in context window or current line
        context=$(sed -n "${start_line},${linenum}p" "$filepath" 2>/dev/null || true)
        if echo "$context" | grep -q "$SAFE_PATTERNS"; then
            # Safe pattern found in context - OK
            CHECK3_SKIPPED=$((CHECK3_SKIPPED + 1))
            # Track diagnostic paths (AD_SAFE_* or LINT:DIAG_OK)
            if echo "$context" | grep -qE 'AD_SAFE_SCALAR|AD_SAFE_BOOL|AD_SAFE_DOUBLE|LINT:DIAG_OK'; then
                CHECK3_DIAG_COUNT=$((CHECK3_DIAG_COUNT + 1))
                CHECK3_DIAG_PATHS+=("$filepath:$linenum")
            fi
        elif echo "$current_line" | grep -q 'LINT:DIAG_OK\|AD_SAFE_'; then
            # Inline exception - OK
            CHECK3_SKIPPED=$((CHECK3_SKIPPED + 1))
            CHECK3_DIAG_COUNT=$((CHECK3_DIAG_COUNT + 1))
            CHECK3_DIAG_PATHS+=("$filepath:$linenum")
        else
            # No protection found - potential issue
            echo -e "${YELLOW}  ⚠ $filepath:$linenum - No safe pattern in 10-line context${NC}"
            CHECK3_ISSUES=$((CHECK3_ISSUES + 1))
        fi
    done <<< "$ITEM_LOCATIONS"

    if [ $CHECK3_ISSUES -eq 0 ]; then
        echo -e "${GREEN}  OK - All .item() calls have safe patterns ($CHECK3_SKIPPED verified)${NC}"
    else
        echo ""
        echo -e "${YELLOW}  Found $CHECK3_ISSUES potential issues ($CHECK3_SKIPPED passed)${NC}"
        echo "  Note: Use one of these to suppress warnings:"
        echo "    1. torch::NoGradGuard no_grad;  // in enclosing scope"
        echo "    2. AD_SAFE_SCALAR(expr)         // diagnostic macro"
        echo "    3. // LINT:DIAG_OK              // explicit exception"
        JSON_WARNINGS+=("{\"check\":3,\"pattern\":\".item<>() without NoGradGuard\",\"count\":$CHECK3_ISSUES,\"severity\":\"WARNING\"}")
    fi

    # --diag-only mode: Report diagnostic paths
    if [ "$DIAG_ONLY_MODE" = true ] && [ $CHECK3_DIAG_COUNT -gt 0 ]; then
        echo ""
        echo -e "${YELLOW}DIAGNOSTIC .item() PATHS ($CHECK3_DIAG_COUNT total):${NC}"
        for diag_path in "${CHECK3_DIAG_PATHS[@]}"; do
            echo "  📊 $diag_path"
        done
    fi
else
    echo -e "${GREEN}  OK - No .item<>() usage found${NC}"
fi
echo ""

# -----------------------------------------------------------------------------
# CHECK 4: narrow() with negative length (deprecated pattern)
# -----------------------------------------------------------------------------
echo "CHECK 4: narrow() with negative indices (WARNING)"
echo "  Pattern: .narrow(..., -1) or similar negative values"
echo "  Issue: May not work as expected in newer LibTorch"
echo "  Fix: Use .slice(dim, start, tensor.size(dim)-1) instead"
echo ""

MATCHES=$(grep -rn '\.narrow([^)]*-[0-9]' --include="*.cpp" --include="*.h" "$SEARCH_PATH" 2>/dev/null | grep -v '//' || true)
if [ -n "$MATCHES" ]; then
    echo -e "${YELLOW}POTENTIAL ISSUES:${NC}"
    echo "$MATCHES"
    echo ""
    ISSUES_FOUND=1
else
    echo -e "${GREEN}  OK - No issues found${NC}"
fi
echo ""

# -----------------------------------------------------------------------------
# CHECK 5: from_blob without explicit device (may default to wrong device)
# -----------------------------------------------------------------------------
echo "CHECK 5: from_blob without explicit .device() (WARNING)"
echo "  Pattern: from_blob(...) without make_cpu_from_blob_opts() or .device(kCPU)"
echo "  Issue: May create tensor on wrong device (undefined behavior)"
echo "  Fix: Use make_cpu_from_blob_opts() helper from wrf_sdirk3_autograd_utils.h"
echo "       or explicitly specify .device(torch::kCPU)"
echo ""

# This is a heuristic check - may have false positives for legitimate cases
# Exception list loaded from lint_exceptions.json or using defaults
MATCHES=$(grep -rn 'from_blob' --include="*.cpp" --include="*.h" "$SEARCH_PATH" 2>/dev/null | \
    grep -v "$EXCEPT_FROM_BLOB_PATTERNS" | \
    grep -v '//' | \
    grep -v 'FIX.*from_blob' | \
    grep -v '@brief.*from_blob' | \
    grep -v "$EXCEPT_FROM_BLOB_FILES" || true)
if [ -n "$MATCHES" ]; then
    echo -e "${YELLOW}POTENTIAL ISSUES (verify manually):${NC}"
    echo "$MATCHES"
    echo ""
    echo "  Note: These from_blob calls may be missing explicit device specification."
    echo "  Verify each location uses make_cpu_from_blob_opts() or .device(torch::kCPU)."
else
    echo -e "${GREEN}  OK - All from_blob calls appear to have explicit device${NC}"
fi
echo ""

# -----------------------------------------------------------------------------
# CHECK 6: flatten()[idx].item pattern (potential GPU sync in loops)
# FIX 2025-12-31 Batch41 Issue 5
# -----------------------------------------------------------------------------
echo "CHECK 6: flatten()[idx].item() pattern (WARNING)"
echo "  Pattern: .flatten()[...].item<...>()"
echo "  Issue: In loops, this causes GPU sync per iteration (performance)"
echo "  Fix: Use data_ptr<T>() for CPU, or batch with index_select for GPU"
echo ""

# This is a heuristic check - may have false positives for single-use cases
MATCHES=$(grep -rn 'flatten()\[.*\]\.item' --include="*.cpp" --include="*.h" "$SEARCH_PATH" 2>/dev/null | \
    grep -v '//' | \
    grep -v 'FIX.*flatten' || true)
if [ -n "$MATCHES" ]; then
    echo -e "${YELLOW}POTENTIAL ISSUES (verify if in loop):${NC}"
    echo "$MATCHES"
    echo ""
    echo "  Note: Single-use item() calls are OK. Check if these are in loops."
    echo "  For loops: Use data_ptr<T>() (CPU) or batch with index_select (GPU)."
else
    echo -e "${GREEN}  OK - No potential flatten().item() issues found${NC}"
fi
echo ""

# -----------------------------------------------------------------------------
# CHECK 7: tensor[idx][var] = value pattern (no-op in LibTorch)
# FIX 2025-12-31 Batch41: Prevent accidental no-op assignments
# -----------------------------------------------------------------------------
echo "CHECK 7: tensor[idx][var] = value pattern (CRITICAL)"
echo "  Pattern: tensor[...][...] = value or data_[...][...] = value"
echo "  Issue: This is a NO-OP in LibTorch C++ - assignment to temporary!"
echo "  Fix: Use tensor.index_put_({idx, var}, value) or .select().fill_()"
echo ""

# Match patterns like: tensor[idx][var] = value, data_[i][j] = x, etc.
# Exclude lines with index_put_, fill_, copy_, compound assignments, and comments
MATCHES=$(grep -rn '\[.*\]\[.*\]\s*=' --include="*.cpp" --include="*.h" "$SEARCH_PATH" 2>/dev/null | \
    grep -v "$EXCEPT_BRACKET" | \
    grep -v '//' | \
    grep -v '==' | \
    grep -v '!=' | \
    grep -v '>=' | \
    grep -v '<=' | \
    grep -Ev '\+=' | \
    grep -Ev '\-=' | \
    grep -Ev '\*=' | \
    grep -Ev '/=' | \
    grep -v 'FIX.*=' | \
    grep -v '@' | \
    grep -v 'no-op' || true)
if [ -n "$MATCHES" ]; then
    echo -e "${YELLOW}POTENTIAL ISSUES (verify if tensor assignment):${NC}"
    echo "$MATCHES"
    MATCH_COUNT=$(echo "$MATCHES" | wc -l)
    JSON_WARNINGS+=("{\"check\":7,\"pattern\":\"tensor[idx][var]=\",\"count\":$MATCH_COUNT,\"severity\":\"WARNING\"}")
    echo ""
    echo "  Note: Not all matches are issues. Check if LHS is a LibTorch tensor."
    echo -e "${YELLOW}  Recommended fix:${NC}"
    echo "    tensor[idx][var] = value;                      // ❌ NO-OP"
    echo "    tensor.index_put_({idx, var}, value);          // ✅ correct"
    echo "    tensor.select(0, idx).select(0, var).fill_(v); // ✅ alternative"
    echo ""
else
    echo -e "${GREEN}  OK - No potential no-op tensor assignments found${NC}"
fi
echo ""

# -----------------------------------------------------------------------------
# CHECK 8: .index({...}) = value pattern (no-op in LibTorch)
# FIX 2025-12-31 Batch41: Prevent no-op index() assignments
# -----------------------------------------------------------------------------
echo "CHECK 8: .index({...}) = value pattern (CRITICAL)"
echo "  Pattern: .index({...}) = value"
echo "  Issue: This is a NO-OP in LibTorch C++ - assignment to temporary!"
echo "  Fix: Use .index_put_({...}, value) for in-place assignment"
echo ""

# Match patterns like: .index({...}) = value
# Exception patterns loaded from lint_exceptions.json
MATCHES=$(grep -rn '\.index({[^}]*})\s*=' --include="*.cpp" --include="*.h" "$SEARCH_PATH" 2>/dev/null | \
    grep -v "$EXCEPT_INDEX" | \
    grep -v '//' | \
    grep -v '==' | \
    grep -v 'FIX' || true)
if [ -n "$MATCHES" ]; then
    echo -e "${RED}FOUND ISSUES:${NC}"
    echo "$MATCHES"
    MATCH_COUNT=$(echo "$MATCHES" | wc -l)
    JSON_ISSUES+=("{\"check\":8,\"pattern\":\".index()=\",\"count\":$MATCH_COUNT,\"severity\":\"CRITICAL\"}")
    echo ""
    echo -e "${YELLOW}  Recommended fix:${NC}"
    echo "    tensor.index({idx, var}) = value;     // ❌ NO-OP"
    echo "    tensor.index_put_({idx, var}, value); // ✅ correct"
    echo ""
    ISSUES_FOUND=1
else
    echo -e "${GREEN}  OK - No .index() = assignment issues found${NC}"
fi
echo ""

# -----------------------------------------------------------------------------
# CHECK 9: CPU-only policy enforcement (optional, --cpu-only mode)
# FIX 2025-12-31 Batch41: Enforce CPU-only policy in designated files
# -----------------------------------------------------------------------------
if [ "$CPU_ONLY_MODE" = true ]; then
    echo "CHECK 9: CPU-only policy enforcement (--cpu-only mode)"
    echo "  Target files: ${CPU_ONLY_FILES[*]}"
    echo "  Policy: Must have TORCH_CHECK(is_cpu) and avoid .to(kCPU) fallbacks"
    echo ""

    CPU_POLICY_ISSUES=0

    # FIX 2025-12-31 Batch41: Verify CPU-only file existence (sync with LIBTORCH_ITEM_USAGE_GUIDE.md)
    echo "  Checking CPU-only policy file existence..."
    MISSING_FILES=0
    for expected_path in "${CPU_ONLY_EXPECTED_PATHS[@]}"; do
        full_path="$BASE_PATH/$expected_path"
        if [ -f "$full_path" ]; then
            echo -e "${GREEN}    ✓ Found: $expected_path${NC}"
        else
            echo -e "${RED}    ✗ Missing: $expected_path${NC}"
            MISSING_FILES=1
            CPU_POLICY_ISSUES=1
        fi
    done
    if [ $MISSING_FILES -eq 1 ]; then
        echo -e "${YELLOW}  WARNING: Some CPU-only policy files are missing.${NC}"
        echo "  Sync documentation with actual file paths in LIBTORCH_ITEM_USAGE_GUIDE.md"
    fi
    echo ""

    for file_pattern in "${CPU_ONLY_FILES[@]}"; do
        # Get policy level from table (default to STRICT if not found)
        POLICY_LEVEL=$(get_policy_level "$file_pattern")

        # Check for TORCH_CHECK with is_cpu() presence (various patterns)
        TORCH_CHECK_COUNT=0
        TO_CPU_MATCHES=""
        for search_dir in $SEARCH_PATH; do
            count=$(grep -rn 'TORCH_CHECK.*\.is_cpu()' --include="$file_pattern" "$search_dir" 2>/dev/null | wc -l)
            TORCH_CHECK_COUNT=$((TORCH_CHECK_COUNT + count))

            # Check for .to(kCPU) usage (potential policy violation for STRICT)
            matches=$(grep -rn '\.to(torch::kCPU)' --include="$file_pattern" "$search_dir" 2>/dev/null | \
                grep -v '//' | \
                grep -v 'FIX' || true)
            if [ -n "$matches" ]; then
                TO_CPU_MATCHES="${TO_CPU_MATCHES}${matches}\n"
            fi
        done

        # Apply policy-level specific checks
        if [ "$POLICY_LEVEL" = "STRICT" ]; then
            # STRICT: TORCH_CHECK required, no .to(kCPU) allowed
            if [ "$TORCH_CHECK_COUNT" -eq 0 ]; then
                echo -e "${RED}  FAIL: $file_pattern [STRICT] - Missing TORCH_CHECK(is_cpu)${NC}"
                CPU_POLICY_ISSUES=1
                JSON_ISSUES+=("{\"check\":9,\"file\":\"$file_pattern\",\"policy\":\"STRICT\",\"issue\":\"missing_torch_check\"}")
            else
                echo -e "${GREEN}  OK: $file_pattern [STRICT] - TORCH_CHECK(is_cpu) present ($TORCH_CHECK_COUNT)${NC}"
            fi
            if [ -n "$TO_CPU_MATCHES" ]; then
                echo -e "${RED}  FAIL: $file_pattern [STRICT] - .to(kCPU) forbidden in STRICT policy:${NC}"
                echo -e "$TO_CPU_MATCHES" | head -3
                CPU_POLICY_ISSUES=1
                JSON_ISSUES+=("{\"check\":9,\"file\":\"$file_pattern\",\"policy\":\"STRICT\",\"issue\":\"to_kcpu_forbidden\"}")
            fi
        else
            # RELAXED: TORCH_CHECK optional, .to(kCPU) allowed for diagnostic norms
            if [ "$TORCH_CHECK_COUNT" -eq 0 ]; then
                echo -e "${YELLOW}  INFO: $file_pattern [RELAXED] - No TORCH_CHECK (acceptable)${NC}"
                JSON_WARNINGS+=("{\"check\":9,\"file\":\"$file_pattern\",\"policy\":\"RELAXED\",\"info\":\"no_torch_check\"}")
            else
                echo -e "${GREEN}  OK: $file_pattern [RELAXED] - TORCH_CHECK(is_cpu) present ($TORCH_CHECK_COUNT)${NC}"
            fi
            if [ -n "$TO_CPU_MATCHES" ]; then
                echo -e "${YELLOW}  INFO: $file_pattern [RELAXED] - .to(kCPU) usage (allowed for norms)${NC}"
            fi
        fi
    done

    if [ $CPU_POLICY_ISSUES -eq 0 ]; then
        echo -e "${GREEN}  CPU-only policy checks passed${NC}"
    else
        echo ""
        echo -e "${YELLOW}  Recommended CPU-only pattern:${NC}"
        echo "    float get_value(...) const {"
        echo "        TORCH_CHECK(data_.is_cpu(),"
        echo "            \"Requires CPU tensor. Use bulk API for GPU.\");"
        echo "        if (data_.is_contiguous()) {"
        echo "            return data_.data_ptr<float>()[idx];"
        echo "        }"
        echo "        auto contig = data_.contiguous();"
        echo "        return contig.data_ptr<float>()[idx];"
        echo "    }"
    fi
    echo ""
fi

# -----------------------------------------------------------------------------
# SUMMARY
# -----------------------------------------------------------------------------
echo "============================================="

# JSON output mode
if [ "$JSON_MODE" = true ]; then
    # Determine status
    if [ $ISSUES_FOUND -eq 1 ]; then
        STATUS="fail"
    else
        STATUS="pass"
    fi

    # Build JSON arrays
    ISSUES_JSON="["
    for i in "${!JSON_ISSUES[@]}"; do
        if [ $i -gt 0 ]; then
            ISSUES_JSON+=","
        fi
        ISSUES_JSON+="${JSON_ISSUES[$i]}"
    done
    ISSUES_JSON+="]"

    WARNINGS_JSON="["
    for i in "${!JSON_WARNINGS[@]}"; do
        if [ $i -gt 0 ]; then
            WARNINGS_JSON+=","
        fi
        WARNINGS_JSON+="${JSON_WARNINGS[$i]}"
    done
    WARNINGS_JSON+="]"

    # Output JSON
    echo "{"
    echo "  \"status\": \"$STATUS\","
    echo "  \"issue_count\": ${#JSON_ISSUES[@]},"
    echo "  \"warning_count\": ${#JSON_WARNINGS[@]},"
    echo "  \"issues\": $ISSUES_JSON,"
    echo "  \"warnings\": $WARNINGS_JSON"
    echo "}"
fi

# Summary output mode (human-readable one-liner)
if [ "$SUMMARY_MODE" = true ]; then
    ISSUE_COUNT=${#JSON_ISSUES[@]}
    WARNING_COUNT=${#JSON_WARNINGS[@]}

    # Build file list for context
    FILES_WITH_ISSUES=""
    for entry in "${JSON_ISSUES[@]}"; do
        file=$(echo "$entry" | grep -o '"file":"[^"]*"' | cut -d'"' -f4)
        pattern=$(echo "$entry" | grep -o '"pattern":"[^"]*"' | cut -d'"' -f4)
        if [ -n "$file" ]; then
            FILES_WITH_ISSUES+="${file}, "
        elif [ -n "$pattern" ]; then
            FILES_WITH_ISSUES+="${pattern}, "
        fi
    done
    FILES_WITH_ISSUES="${FILES_WITH_ISSUES%, }"  # Remove trailing comma

    if [ $ISSUES_FOUND -eq 1 ]; then
        echo -e "${RED}FAIL: ${ISSUE_COUNT} issue(s), ${WARNING_COUNT} warning(s)${NC}"
        if [ -n "$FILES_WITH_ISSUES" ]; then
            echo -e "${RED}  → ${FILES_WITH_ISSUES}${NC}"
        fi
    else
        echo -e "${GREEN}PASS: ${ISSUE_COUNT} issue(s), ${WARNING_COUNT} warning(s)${NC}"
    fi
fi

# Final status message (suppress if summary or json mode is active for cleaner CI output)
if [ $ISSUES_FOUND -eq 1 ]; then
    if [ "$SUMMARY_MODE" != true ] && [ "$JSON_MODE" != true ]; then
        echo -e "${RED}ISSUES DETECTED - Please fix before commit${NC}"
    fi
    exit 1
else
    if [ "$SUMMARY_MODE" != true ] && [ "$JSON_MODE" != true ]; then
        echo -e "${GREEN}ALL CHECKS PASSED${NC}"
    fi
    exit 0
fi
