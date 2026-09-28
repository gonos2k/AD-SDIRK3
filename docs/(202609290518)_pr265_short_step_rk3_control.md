# Fresh-WRF short-step SDIRK3 and RK3 control

Local timestamp: 2026-09-29 05:18:50 JST (Asia/Tokyo).

## Context and setup

The exact PR #265 source `14e1dbd051ffb438c3f4a69c35141fa4e4114e88` was built from a fresh detached worktree with pinned MYNN; the executable is `main/wrf.exe` SHA-256 `8f7d66c12a124bde6e9098e7b9933d436b5dcd47ea62b28fb2291b7c9f9af3da`. Its full build and strict 15 s diagnostic ON/OFF evidence are in `docs/(202609290455)_pr265_fresh_wrf_build_stage2.md`.

Four single-rank, single-thread runs used that identical executable, archived `wrfinput_d01` SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, and `diagnostics.txt` SHA-256 `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`. Physics, `diff_opt=2`, `km_opt=1`, `khdif=1000`, `kvdif=10`, initial condition, rank count, and thread count were unchanged. The 15 s SDIRK3 arm has `run_seconds=240` and `history_interval_s=60`; the short arms set timestep, requested duration, and history interval to one equal value (5 or 1 s). The 5 s RK3 namelist differs from the 5 s SDIRK3 namelist only in `time_integration_scheme=0` versus `6`.

| Arm | Namelist SHA-256 | First-step result | Terminal Stage-2 `R_last` | Output frames |
| --- | --- | --- | ---: | ---: |
| SDIRK3 15 s | `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f` | `ZeroStepStall`, iter9 | `1.274e-6` | 1, initial only |
| SDIRK3 5 s | `fc29e1d697ed36383155b57630899b30afc36c0c3c7951f69e6591e5ba796c79` | `ZeroStepStall`, iter10 | `1.102e-6` | 1, initial only |
| SDIRK3 1 s | `7c55d64c7854fa70f31332baf68f574d75fc2ed22aa3b1588166d8031559b80a` | `ZeroStepStall`, iter11 | `1.205e-7` | 1, initial only |
| RK3 5 s | `e6cf7b39c9b7ff73e9b95fe98400fa1fff0d355d1ba78b72dc7bffdcf8f2e3a4` | `SUCCESS COMPLETE WRF` at 5 s | n/a | 2, initial and 5 s |

All SDIRK3 residuals exceed the unchanged `1e-7` Newton tolerance and each run fails closed before publishing a forecast step. Decreasing timestep changes the observed terminal residual, but these three failed cases establish neither an asymptotic time order nor an unavoidable precision floor. The RK3 arm is a positive control for the same initial input and active diffusion setting, not a matched final-field comparison.

Every run's initial `wrfout` frame has all 174 common floating fields **bitwise equal** to the others. The three SDIRK3 files contain 174 floating fields / 189,176 values each; RK3 contains 174 fields / 378,352 values across two frames. All scanned values are finite. RK3's second frame differs from its initial U/V/W/PH/T/MU fields (the fieldwise RMS changes were respectively `1.78968e-3`, `2.68414e-4`, `4.43013e-5`, `4.62174e-3`, `7.32939e-5`, `1.04200e-3`, each in that variable's output units). No SDIRK3 final frame exists, so forecast accuracy, numerical parity with RK3, equal-accuracy runtime, and whole-call adjoint remain unassessed.

Output SHA-256 values are `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a` (SDIRK3 15 s initial), `6cbb25654f1b4d908be573d9dc3102d6680d1d2735f9bbf9298b219160e3510` (SDIRK3 5 s), `656183b864d523a3cf8223f9113979bf33252e558d5786bd9b43581666571b11` (SDIRK3 1 s), and `025c8e10742359c269be3d6331cde9e4bb3a29564a1406050170926adc374731` (RK3 5 s). The run directories are under `/private/tmp/sdirk3-pr265-fresh-20260929/.validation/` with matching arm names. No solver code, tolerance, or acceptance gate was changed for this diagnostic.
