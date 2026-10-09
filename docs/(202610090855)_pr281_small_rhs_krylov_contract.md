# PR281 small-RHS Krylov contract correction

Local timestamp: 2026-10-09T08:55:14+09:00.

Baseline: PR281 `38c05f7542136ed035197f7a0a0335e71b6fda83`; candidate working diff SHA-256 `ef6b5bf385e37926c04ee16493326e03d44084a16b05f467381a33da260dd013`. The cold fine-grid inverse remains open. No merge.

## Mathematical cause and minimal change

The same linear system A x=b must have a scale-independent relative residual when b and x are scaled together. Both GMRES and FGMRES previously used a 1e-12 denominator floor for internal stopping and returned rho_S=1 whenever the clamped RHS norm was not above the floor. A separate x0 norm<1e-14 shortcut treated small nonzero guesses as exactly zero. These choices break homogeneity and can reject a valid small solve or corrupt its starting residual.

The candidate keeps true nonzero RHS norms, defines the exact-zero solve contract explicitly, and skips A(x0) only for an elementwise exact zero guess. A shared scalar ratio owns the zero/nonzero decision. Residual/RHS reductions cast to FP64 before the norm, avoiding Float32 squared-norm underflow; the operator, Arnoldi vectors and breakdown thresholds retain their existing dtype/settings. No forcing tolerance or Newton/optimizer gate is loosened. Actual raw RHS/residual norm fields are logged; reconstructed r/rho is not used as evidence.

Before the correction, the direct production-kernel test had 10 failures among 85 checks. The expanded FP64/FP32 test now has 117 passing checks, including RHS scales 1e-16 and Float32 1e-24, initial convergence and exact-zero cases. Existing FGMRES assertions pass. The three affected CTests (full tile adjoint, full tile internal FP64, Krylov coordinate contract) pass, 26.00 seconds total. Full 129-test Linux CI has not passed yet.

## Linux precision evidence still open

Run 37803412710 used the previous production solver. Its ZIP SHA-256 is `9d5057dffd148cee3729054738c4fb6ecde942925b869ab0ca51385f6df8f20f`. The N13/K10 correction point had gradient norm 1.734989558e-6. N14/K10 failed at timestep 2, stage 4 because the small-RHS contract returned rho_S=1; no N14 gradient exists. That failure is retained. It is not proof of an arithmetic floor, and this candidate fix must be rerun on that exact saved input in Linux before promotion.

## Required WRF regression

An isolated incremental relink used the candidate archive SHA-256 `e4142ca90f856e0ca8a72fbc14ae68d66f3c302bcfc8b4fbd315eea29875e1c1`. Eleven cached Fortran/WRF link objects and the input, namelist and diagnostics sidecar matched the archived PR279 receipt. Public ABI headers/signatures are unchanged; Fortran was not cleanly rebuilt.

At dt15/final240 seconds, MPI rank 1 and OMP thread 1, all 48 stages converged, all 174 float variables across five frames were finite, and dry mass/layer thickness were positive. Three output files are byte-identical to PR279. Same-input archived RK3 endpoint RMS U/V/W/T/PH/P/MU remains approximately 0.002108/0.0009522/0.0005988/0.04142/1.3042/0.06419/0.09955. Candidate main timing sum 6.2929 seconds versus RK3 0.26754 seconds; this is a descriptive comparison, not equal accuracy or a performance improvement claim. The earlier missing diagnostics sidecar setup failure is preserved and had zero integration steps.

WRF summary SHA-256 `1b988f5d961ed1491a0d5fd56fc0aebee17796242e81e816b57ab408e1e3e42f`. Deployment-target warning 26.7 library/26.0 link was recorded; the matched existing stack ran successfully.

## Remaining checklist

- [x] PR280 m3 operand-based FD floor fix, unchanged 0.002 gate.
- [x] Independently reproduce and repair small-RHS GMRES/FGMRES coordinate/homogeneity defects.
- [x] Local direct kernels, full tile forward/adjoint and same-setup WRF/RK3 regression.
- [ ] Exact Linux N13/K10 and N14/K10 fixed-state rerun with this production patch.
- [ ] Cold fine h5 inverse, FD and fixed-control time sensitivity on a single qualified precision policy.
- [ ] Required exact-source Linux CI and final Green/Red closure.

Graphify was refreshed on the affected ten-file corpus; 355 nodes/633 clustered simple-graph edges (1,434 raw extraction edges). No semantic LLM extraction was used. Fortran/CMake/runtime coordinate gaps remain explicit.
