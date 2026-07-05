# AD-SDIRK3

A **differentiable SDIRK3** (3rd-order singly-diagonally-implicit Runge–Kutta) implicit time
integrator built into **WRF v4.7.0**, using **PyTorch / libtorch** with a zero-copy Fortran↔C++
interface and full autodiff (JVP / VJP / HVP). The goal is a differentiable dynamical core for
**4D-Var adjoint** modeling.

- **IMEX split** (mode 3 = ARK324L2SA): horizontal/slow terms explicit, vertical acoustic implicit.
- **Matrix-free Newton–Krylov** implicit solve: GMRES (Givens) with Eisenstat–Walker adaptive
  forcing, a vertical preconditioner, and a trust-region fallback. `A·v = v − dt·γ·J·v` is a JVP;
  the 4D-Var gradient is a VJP.
- **Zero-copy interface:** Fortran `(i,k,j)` column-major maps to C++ `(j,k,i)` row-major with no
  data copy — layout `{nj,nk,ni}`, strides `{ni*nk, ni, 1}`.
- Cross-platform CPU / CUDA / MPS.

## Status

The model **builds and runs** (`main/wrf.exe`, `main/ideal.exe`,
`external/libtorch_wrf/sdirk3/libwrf_sdirk3_libtorch.a`). The stock-RK3 baseline is validated. The
differentiable implicit solve converges at small timesteps; making it converge and remain stable at
the **operational timestep dt=600** on `em_b_wave` is the active investigation.

Two obstacles are measured (see `doc/` and below):

- **Wall-1 — implicit indefiniteness at large dt (≥~80, incl. 600).** The fast acoustic operator
  `A = I − dt·γ·J_fast` is *intrinsically indefinite* — its Ritz/numerical-range spectrum straddles
  the origin, and it is ~linear, so no smooth linearization background removes the indefiniteness. By
  Sylvester's law no SPD preconditioner reaches dt=600; the preconditioner track is measured-dead.
  This is the same stiffness that motivates WRF's split-explicit acoustic sub-stepping. The
  large-step adjoint is therefore deferred to a WRFPLUS-style TL/AD coupling.
- **Wall-2 — explicit advection cascade at small dt (≤~70).** The explicit **u-momentum** slow
  tendency blows up in a super-exponential stage cascade (measured ≥99.99% in the `ru` block; a
  **bilinear/quadratic** runaway confirmed by amplitude-scaling; HEVI-independent). Root cause: the
  ARK324 explicit tableau over-extrapolates the physical advective tendency of the sheared baroclinic
  jet at large dt. The fix under development is to **sub-cycle the explicit slow tendency** (WRF-native
  split-explicit style), keeping the SDIRK implicit machinery intact.

## Build & run

Build **only from the repo root** — never `make` in an individual directory.

```bash
printf '37\n1\n' | ./configure                 # 37 = this system, 1 = nesting (read from STDIN)
nohup ./compile -j 4 em_b_wave > compile.log 2>&1 &   # then: tail -f compile.log
```

`./clean -a` is required **only after a Registry change** (it regenerates `frame/` / `inc/`); skip it
otherwise. Run the test case:

```bash
cd test/em_b_wave && ./ideal.exe && ./wrf.exe
```

## Configuration

The IMEX split is selected by `sdirk3_imex_split_mode` (`0` full-implicit → `2` post-SDIRK3 →
`3` ARK324, the operational mode). HEVI (horizontally-explicit / vertically-implicit) is opt-in via
`sdirk3_hevi_split` (default off → baseline byte-identical). New solver knobs are **opt-in**
(default = no behavior change) and fully wired through Registry + Fortran `set_config` + C++.

## Key files (`external/libtorch_wrf/sdirk3/`)

| File | Role |
|---|---|
| `wrf_sdirk3_newton_solver.cpp` | Newton–Krylov + GMRES + solver diagnostics |
| `wrf_sdirk3_tile_unified_impl.cpp` | Unified RHS, tile parallelization, ARK324 stage loop, HEVI split |
| `wrf_sdirk3_unified_preconditioner.cpp` | Vertical preconditioner (M) |
| `wrf_sdirk3_imex_ark324_coeffs.h` | ARK324L2SA Butcher tableau |
| `wrf_sdirk3_jvp_autograd.{cpp,h}`, `wrf_sdirk3_jvp_fwad_or_fd.h` | JVP (forward-mode dual + FD fallback) |
| `wrf_sdirk3_config.h` | Config knobs, `effective_imex_split_mode()` |
| `jvp_bridge.F90` | Fortran↔C++ AD bridge |

## Documentation

- `doc/SDIRK3_EM_B_WAVE_BASELINE_2026-02-16.md` — baseline validation
- `doc/sdirk3_hevi_preconditioner_findings_2026-06-21.md` — HEVI + preconditioner findings
- `doc/sdirk3_mode3_stage3_rootcause_2026-06-20.md` — Stage-3 root-cause analysis
- `external/sdirk3_lib/docs/` — design spec, critical/runtime fixes, JVP / `.item()` guidance

## Constraints (for contributors)

- The `(j,k,i)` memory layout is correct — never change it.
- Every `.item()` must be inside a `NoGradGuard` scope (it breaks the autograd graph and forces a
  GPU→CPU sync). Prefer AD-safe tensor ops; keep `.item()` off the hot path.
- Protect the computational graph: no stray `.detach()` / `.data` / CPU sync in graph regions; use
  `index_put_` for tensor assignment; no global dtype changes (scoped autocast only).
- Defensive Fortran interface: null checks and dimension validation at every boundary.
