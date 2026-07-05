# SDIRK3 Baseline (em_b_wave)

- Date: 2026-02-16
- Configure command: `printf '37\n1\n' | ./configure`
- Compile command: `./compile em_b_wave -j 8`
- Run command: `./ideal.exe` then `./wrf.exe` (single-process run in current environment)
- WRF completion: `d01 0001-01-01_01:00:00 wrf: SUCCESS COMPLETE WRF`
- Wallclock (`wrf_serial.log`): `real 203.08`, `user 169.33`, `sys 37.38`
- NaN/Inf checks (`rsl.error.0000`): `Has NaN: YES = 0`, `Has Inf: YES = 0`
- Solver behavior (`rsl.error.0000`): `GMRES NOT CONVERGED = 51`, `GMRES failed badly = 50` (run completed)

## RK3 baseline comparison
- Reference: `test/em_b_wave/rk3_baseline/wrfout_d01_0001-01-01_00:00:00`
- Initial output index (`Time=0`) matches exactly for `U,V,W,T,PH,P` (max abs diff = `0` for all).
- Common last index comparison (`Time=6`, since RK3 has 12 outputs and SDIRK3 has 7):
  - `U` max abs diff `1.527397e+02`
  - `V` max abs diff `2.824334e+00`
  - `W` max abs diff `8.341091e-03`
  - `T` max abs diff `9.952863e+01`
  - `PH` max abs diff `2.523377e+02`
  - `P` max abs diff `7.205987e+03`

## Stored artifacts
- `wrfout_d01_0001-01-01_00:00:00`
- `rsl.out.0000`
- `rsl.error.0000`
- `wrf_serial.log`
- `ideal_test.log`
