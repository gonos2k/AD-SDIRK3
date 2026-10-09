# PR283 fresh-archive WRF regression

Local timestamp: 2026-10-10 05:56:28 JST (+0900)

## Context

This records the authorized fresh `test/em_b_wave` regression for the PR283
small-step worktree at HEAD `4615c2e06551e743b27ab3ccd7925a82e6f23c30`.
The C++ archive was freshly built with the changed Newton solver header. No
Fortran/C bridge source or ABI changed. Before relinking, seven SDIRK3 Fortran
integration sources were hash-compared with the PR282 source tree; every hash
matched. The previously qualified WRF object and support-library inputs were
reused, so this was a fresh C++ archive relink, not a clean full WRF build.

## Run and validation

The run used one MPI rank and one OpenMP thread:

```text
mpirun -np 1 ./wrf.exe
OMP_NUM_THREADS=1
```

It completed 240 seconds at a 15-second timestep, with 16 main steps and 48
converged implicit stages (16 each at stages 2, 3, and 4). All 174 floating
output variables were finite in every output file. Perturbation mass, full dry
column mass, and W-face thickness remained positive. The three new output files
are byte-identical to the PR282 outputs for the same setup.

The archived split-explicit RK3 reference is
`/Users/yhlee/Documents/AD-SDIRK3-worktree-archive/20260929/SDIRK3-pr239-cleanbuild/.validation/pr239-cleanbuild/t1_240s_20260926/rk3_dt15`.
Output times match. At 240 seconds, SDIRK3 versus RK3 RMS field differences
were MU 0.09955 Pa, P 0.06419 Pa, PH 1.30420, T 0.04142, U 0.002108, V
0.0009522, and W 0.0005988 in WRF output units. The candidate's 16
`Timing for main` entries sum to 7.11104 seconds (maximum 0.53658 s); the
archived RK3 entries sum to 0.26754 seconds. These are descriptive single-run
measurements, not an equal-accuracy or performance claim.

The model invocation exited 0 in 8.57 seconds and logged `SUCCESS COMPLETE
WRF`. Process peak RSS was not captured during this single authorized run; no
second model run was made to measure it.

## Reproduction evidence

The fresh archive is `/private/tmp/pr283-small-step-build/libwrf_sdirk3_libtorch.a`
(SHA-256 `5c93143f1af1846d12e9bfeb9d7b3662f0a510fe969e9a0d71f6c141c0e26e48`).
The relinked executable is `/private/tmp/pr283-wrf-regression-20261010/wrf.exe`
(SHA-256 `4e5f44ca63c531a78f0d64ca6284f208864afb41363ab2c81737add830477753`).
The run reused `namelist.input` SHA-256
`6258b997543f95b6cdde538aa37e14084f17862210e94c02dd23fb7fd91a8e06`,
`wrfinput_d01` SHA-256
`e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, and
`diagnostics.txt` SHA-256
`bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`.

The C++ build used Apple Clang 21.0.0. The WRF link/run stack used GNU Fortran
15.2.0 and Open MPI 5.0.9. Its link recipe explicitly selected Homebrew
netCDF 4.9.3_2 and netCDF-Fortran 4.6.2 libraries; `otool -L` confirms the
linked libraries resolve through those Homebrew prefixes. (`nc-config` on the
shell PATH points to `/opt/local`, so its version output does not describe the
libraries selected by this link recipe.)

`wrf_validation_summary.json` contains per-file finite/physical checks, stage
counts, output hashes, same-time RK3 field comparisons, and step timings.
`wrf_provenance.json`, `link_receipt.json`, `run_status.json`, `link.log`,
`run.log`, `rsl.error.0000`, and `rsl.out.0000` preserve the source, archive,
qualified object, input, toolchain, command, and raw execution evidence.
