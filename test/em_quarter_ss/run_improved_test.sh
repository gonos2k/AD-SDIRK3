#!/bin/bash
#
# WRF Implicit Scheme Improved Test
# Date: 2025-06-14
#

# Set paths
WRF_DIR="/Users/yhlee/WRF/WRFV4.7.0/WRF_IMPLICIT_SCHEME_20250613"
TEST_DIR="/Users/yhlee/WRF/WRFV4.7.0/test/em_quarter_ss"

echo "=========================================="
echo "WRF Implicit Scheme Improved Test"
echo "=========================================="
echo ""

# Function to run test
run_test() {
    local test_name=$1
    local namelist_file=$2
    local description=$3
    
    echo "Running $test_name test: $description"
    echo "----------------------------------------"
    
    # Create test directory
    mkdir -p ${TEST_DIR}/test_${test_name}
    cd ${TEST_DIR}/test_${test_name}
    
    # Copy necessary files
    cp ${TEST_DIR}/${namelist_file} namelist.input
    cp ${TEST_DIR}/input_sounding .
    
    # Link executables
    ln -sf ${WRF_DIR}/main/ideal.exe .
    ln -sf ${WRF_DIR}/main/wrf.exe .
    
    # Run ideal.exe
    echo "Running ideal.exe..."
    ./ideal.exe > ideal.log 2>&1
    
    if [ ! -f wrfinput_d01 ]; then
        echo "ERROR: ideal.exe failed to create wrfinput_d01"
        tail -20 ideal.log
        return 1
    fi
    
    # Run wrf.exe
    echo "Running wrf.exe..."
    time ./wrf.exe > wrf.log 2>&1
    
    # Check if run completed
    if grep -q "SUCCESS COMPLETE WRF" rsl.out.0000 2>/dev/null; then
        echo "SUCCESS: $test_name test completed successfully"
        
        # Extract timing information
        echo ""
        echo "Average time per step:"
        grep "Timing for main" rsl.out.0000 | tail -20 | awk '{sum+=$9; count++} END {if(count>0) printf "%.5f seconds\n", sum/count}'
        
        # Check for implicit solver convergence
        echo ""
        echo "Solver convergence stats:"
        total_steps=$(grep -c "Timing for main" rsl.out.0000)
        converged=$(grep -c "converged in" rsl.out.0000 2>/dev/null || echo "0")
        not_converged=$(grep -c "did not converge" rsl.out.0000 2>/dev/null || echo "0")
        echo "Total time steps: $total_steps"
        echo "Converged: $converged"
        echo "Not converged: $not_converged"
        
        # Show some solver iteration info
        echo ""
        echo "Sample solver iterations:"
        grep "Iteration.*residual" rsl.out.0000 | tail -10
        
    else
        echo "ERROR: $test_name test failed"
        echo "Last 20 lines of rsl.error.0000:"
        tail -20 rsl.error.0000
    fi
    
    echo ""
    cd ${TEST_DIR}
}

# Clean previous tests
echo "Cleaning previous test directories..."
rm -rf test_implicit_*

# Run tests with different configurations
echo "1. IMPLICIT IMPROVED TEST (larger tolerance, more iterations)"
echo "=========================================="
run_test "implicit_improved" "namelist.input.implicit_improved" "tol=1e-3, max_iter=50, theta=0.55"

echo ""
echo "2. IMPLICIT CRANK-NICOLSON TEST (theta=0.5)"
echo "=========================================="
run_test "implicit_cn" "namelist.input.implicit_cn" "tol=1e-4, max_iter=100, theta=0.5"

# Summary
echo ""
echo "3. SUMMARY"
echo "=========================================="

for test in implicit_improved implicit_cn; do
    if [ -f test_${test}/rsl.out.0000 ]; then
        echo ""
        echo "${test}:"
        time=$(grep "Timing for main" test_${test}/rsl.out.0000 | tail -20 | awk '{sum+=$9; count++} END {if(count>0) printf "%.5f", sum/count}')
        echo "  Average step time: ${time} seconds"
        
        # Count convergence
        not_conv=$(grep -c "did not converge" test_${test}/rsl.out.0000 2>/dev/null || echo "0")
        echo "  Failed convergence: ${not_conv} times"
    fi
done

# Compare with baseline explicit result
if [ -f test_explicit/rsl.out.0000 ]; then
    echo ""
    echo "Baseline (explicit):"
    time=$(grep "Timing for main" test_explicit/rsl.out.0000 | tail -20 | awk '{sum+=$9; count++} END {if(count>0) printf "%.5f", sum/count}')
    echo "  Average step time: ${time} seconds"
fi

echo ""
echo "Test completed at: $(date)"
echo "==========================================

## Recommendations:
# If convergence issues persist:
# 1. Increase implicit_solver_max_iter (try 200)
# 2. Relax implicit_solver_tol (try 1e-2)
# 3. Try theta_implicit = 0.51 (slightly implicit)
# 4. Check initial guess quality in the solver"