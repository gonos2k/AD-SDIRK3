#!/bin/bash

# Run script for em_b_wave test with SDIRK3

echo "=========================================="
echo "WRF em_b_wave test with unified SDIRK3"
echo "=========================================="

# Set environment
export OMP_NUM_THREADS=1
export WRF_EM_CORE=1

# Clean previous run
rm -f rsl.* wrfout_* wrfrst_*

# Link the executable
if [ -f ../../main/wrf.exe ]; then
    ln -sf ../../main/wrf.exe .
else
    echo "Error: wrf.exe not found in ../../main/"
    exit 1
fi

# Check namelist
echo "Checking namelist.input for SDIRK3 settings..."
grep -E "time_integration_scheme|rk_ord|time_step_sound|sdirk3" namelist.input

echo ""
echo "Starting WRF with unified SDIRK3..."
echo "L-stable SDIRK3 handles all dynamics without mode splitting"
echo ""

# Run WRF
time mpirun -np 1 ./wrf.exe

# Check output
if [ -f rsl.error.0000 ]; then
    echo ""
    echo "Checking for errors..."
    tail -20 rsl.error.0000
    
    if grep -q "wrf: SUCCESS COMPLETE" rsl.error.0000; then
        echo ""
        echo "WRF completed successfully!"
        echo "Output files:"
        ls -la wrfout_*
    else
        echo ""
        echo "WRF did not complete successfully. Check rsl.error.0000"
    fi
else
    echo "Error: rsl.error.0000 not found"
fi