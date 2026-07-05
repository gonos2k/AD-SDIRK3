#!/bin/bash

# Script to run implicit scheme tests after compilation
# Created: 2025-06-15

echo "==============================================="
echo "WRF Implicit Scheme Test Suite"
echo "==============================================="

# Set paths
WRF_DIR="/Users/yhlee/WRF/WRFV4.7.0/WRF_IMPLICIT_SCHEME_20250613"
TEST_DIR="${WRF_DIR}/test/em_quarter_ss"
MAIN_DIR="${WRF_DIR}/main"

# Check if executables exist
if [ ! -f "${MAIN_DIR}/ideal.exe" ] || [ ! -f "${MAIN_DIR}/wrf.exe" ]; then
    echo "ERROR: WRF executables not found. Please wait for compilation to complete."
    echo "Looking for:"
    echo "  ${MAIN_DIR}/ideal.exe"
    echo "  ${MAIN_DIR}/wrf.exe"
    exit 1
fi

# Create symbolic links
echo "Creating symbolic links to executables..."
ln -sf ${MAIN_DIR}/ideal.exe ${TEST_DIR}/ideal.exe
ln -sf ${MAIN_DIR}/wrf.exe ${TEST_DIR}/wrf.exe

# Function to run a test
run_test() {
    local test_name=$1
    local namelist=$2
    local test_dir="${TEST_DIR}/${test_name}"
    
    echo ""
    echo "==============================================="
    echo "Running test: ${test_name}"
    echo "Namelist: ${namelist}"
    echo "==============================================="
    
    # Create test directory
    mkdir -p ${test_dir}
    cd ${test_dir}
    
    # Link executables
    ln -sf ../ideal.exe .
    ln -sf ../wrf.exe .
    
    # Copy namelist
    cp ../${namelist} namelist.input
    
    # Copy input files
    cp ../input_sounding .
    
    # Run ideal.exe
    echo "Running ideal.exe..."
    ./ideal.exe > ideal.log 2>&1
    
    if [ ! -f wrfinput_d01 ]; then
        echo "ERROR: ideal.exe failed to create wrfinput_d01"
        cat ideal.log
        return 1
    fi
    
    # Run wrf.exe
    echo "Running wrf.exe..."
    ./wrf.exe > wrf.log 2>&1
    
    # Check for completion
    if grep -q "wrf: SUCCESS COMPLETE WRF" wrf.log; then
        echo "SUCCESS: ${test_name} completed successfully"
        
        # Extract convergence info if available
        echo "Checking for implicit solver convergence..."
        grep -E "Implicit solver|Newton iteration|converged|residual" rsl.out.0000 | tail -20 || true
    else
        echo "ERROR: ${test_name} failed"
        tail -50 rsl.error.0000
    fi
    
    cd ${TEST_DIR}
}

# Run tests
echo "Starting test suite..."

# 1. Explicit scheme (baseline)
run_test "test_explicit_baseline" "namelist.input.explicit_test"

# 2. Implicit scheme - simple
run_test "test_implicit_simple" "namelist.input.implicit_simple"

# 3. Implicit scheme - improved tolerances
run_test "test_implicit_improved" "namelist.input.implicit_improved"

# 4. Implicit scheme - Crank-Nicolson
run_test "test_implicit_cn" "namelist.input.implicit_cn"

echo ""
echo "==============================================="
echo "Test suite completed"
echo "==============================================="
echo ""
echo "Results summary:"
for test_dir in test_explicit_baseline test_implicit_simple test_implicit_improved test_implicit_cn; do
    if [ -d ${test_dir} ]; then
        echo -n "${test_dir}: "
        if [ -f ${test_dir}/wrf.log ] && grep -q "SUCCESS COMPLETE" ${test_dir}/wrf.log; then
            echo "SUCCESS"
        else
            echo "FAILED"
        fi
    fi
done