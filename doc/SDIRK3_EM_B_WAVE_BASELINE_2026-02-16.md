# SDIRK3 em_b_wave Baseline (2026-02-16)

## Scope
- Case: `test/em_b_wave`
- Purpose: set current SDIRK3 run as regression baseline for follow-up tuning work
- Build config selection: `./configure` -> `37` (dmpar + SDIRK-3 + LibTorch), `1` (basic nesting)

## Commands Used
```bash
printf '37\n1\n' | ./configure
./compile em_b_wave -j 2
cd test/em_b_wave
./ideal.exe
mpirun -np 1 ./wrf.exe
```

## Build Result
- Executables built:
  - `main/wrf.exe`
  - `main/ideal.exe`
- Compile completed successfully (no link failure).

## Runtime Result
- Completion marker:
  - `test/em_b_wave/rsl.out.0000:156`
  - `d01 0001-01-01_01:00:00 wrf: SUCCESS COMPLETE WRF`
- SDIRK3 final stats:
  - `test/em_b_wave/rsl.out.0000:149`
  - `Tile   1: Steps=18, Failed=0, Last dt=600.0000`

## Timing Summary
- `Timing for main` samples (s):
  - 00:10 -> `34.40751`
  - 00:20 -> `2.68880`
  - 00:30 -> `0.79926`
  - 00:40 -> `0.81416`
  - 00:50 -> `0.71274`
  - 01:00 -> `0.79504`
- Aggregates:
  - `Timing for main` sum: `40.21751 s`
  - `Timing for Writing` sum: `1.05640 s`
  - Combined (main + writing): `41.27391 s`

## Health/Anomaly Check
- NaN/Inf fatal state:
  - No `NaN: YES` / `Inf: YES` found in `rsl.out.0000` and `rsl.error.0000`
- Fatal runtime signals:
  - No `FATAL`, `SIGSEGV`, or floating-point exception found
- Non-fatal warning:
  - `NEWTON SOLVER FAILED TO CONVERGE` observed 3 times in `rsl.error.0000`
  - Run still completed with `SUCCESS COMPLETE WRF` and `Failed=0` in final solver stats

## Baseline Policy
- This run is the baseline reference for immediate SDIRK3 regression checks on `em_b_wave`.
- Future candidate runs should be compared against this baseline using:
```bash
tools/diffwrf <baseline_wrfout> <candidate_wrfout>
```
