#!/bin/bash
#
# WRF Implicit vs Explicit Scheme Comparison Test
# Date: 2025-06-14
#

# Set paths
WRF_DIR="/Users/yhlee/WRF/WRFV4.7.0/WRF_IMPLICIT_SCHEME_20250613"
TEST_DIR="/Users/yhlee/WRF/WRFV4.7.0/test/em_quarter_ss"

echo "=========================================="
echo "WRF Implicit vs Explicit Scheme Test"
echo "=========================================="
echo ""

# Function to run test
run_test() {
    local test_name=$1
    local namelist_file=$2
    
    echo "Running $test_name test..."
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
        echo "Timing information:"
        grep "Timing for main" rsl.out.0000 | tail -5
        
        # Check for implicit solver info if applicable
        if [ "$test_name" == "implicit" ]; then
            echo ""
            echo "Implicit solver statistics:"
            grep -i "implicit\|newton\|solver" rsl.out.0000 | tail -10
        fi
    else
        echo "ERROR: $test_name test failed"
        echo "Last 20 lines of rsl.error.0000:"
        tail -20 rsl.error.0000
    fi
    
    echo ""
    cd ${TEST_DIR}
}

# Run explicit test first (baseline)
echo "1. EXPLICIT SCHEME TEST (Baseline)"
echo "=========================================="
run_test "explicit" "namelist.input.explicit_test"

# Run implicit test
echo ""
echo "2. IMPLICIT SCHEME TEST"
echo "=========================================="
run_test "implicit" "namelist.input.implicit_test"

# Compare results
echo ""
echo "3. COMPARISON SUMMARY"
echo "=========================================="

if [ -f test_explicit/wrfout_d01_0001-01-01_00:30:00 ] && [ -f test_implicit/wrfout_d01_0001-01-01_00:30:00 ]; then
    echo "Both tests produced output files"
    
    # Compare file sizes
    echo ""
    echo "Output file sizes:"
    ls -lh test_explicit/wrfout_d01* | awk '{print "Explicit: " $9 " - " $5}'
    ls -lh test_implicit/wrfout_d01* | awk '{print "Implicit: " $9 " - " $5}'
    
    # Extract and compare total run times
    echo ""
    echo "Total simulation times:"
    explicit_time=$(grep "Timing for main" test_explicit/rsl.out.0000 | tail -1 | awk '{print $9}')
    implicit_time=$(grep "Timing for main" test_implicit/rsl.out.0000 | tail -1 | awk '{print $9}')
    
    if [ ! -z "$explicit_time" ] && [ ! -z "$implicit_time" ]; then
        echo "Explicit scheme: ${explicit_time} seconds"
        echo "Implicit scheme: ${implicit_time} seconds"
        
        # Calculate speedup (requires bc)
        if command -v bc &> /dev/null; then
            speedup=$(echo "scale=2; $explicit_time / $implicit_time" | bc)
            echo "Speedup factor: ${speedup}x"
        fi
    fi
else
    echo "ERROR: One or both tests failed to produce output files"
fi

echo ""
echo "Test completed at: $(date)"
echo "=========================================="