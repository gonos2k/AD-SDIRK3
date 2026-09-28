# Native option-2 W whole-step derivative probe

Local timestamp: 2026-09-29 03:35:27 JST (+0900)

## Context and scope

This test-only change is based on integration revision `0ef8e3b65616db6584fef7b28796962facb73b73` (tree `d65487c`). The older `check_governing_step_budget` whole-step probe sets `diffusion_option=2` but leaves `WRFGridInfoExtended::smagorinsky_opt` at its default 0, so it does not establish native option-2 coverage. The added check in `external/libtorch_wrf/sdirk3/tests/test_full_tile_step.cpp` sets `smagorinsky_opt=1` and exercises one complete `unifiedStep` with `kvdif=100000`, `khdif=0`, `dt=0.01`, and `dx=1000 m`.

The native preconditions are asserted: mode-1 WRF parity, ordinary mode-3 ARK, dry state, damping off, complete single tile, `km_opt=1`, and no supplied coefficient setter. `TileCase::step` supplies WRF `fnm/fnp` pointers. The nonhydrostatic canonical-horizontal check in `wrf_sdirk3_tile_unified_impl.cpp:14344-14350` is fail-closed; with `diffusion_option=2` and `smagorinsky_opt>0`, the option-2 native gate at `21688-21706` is declared and the supplied-K/eta/ownership conditions are required. The passing native whole-step calls therefore demonstrate the gate and metric snapshot path.

The base PH′ and MU perturbations are zero; PH and MU are nonzero only in their explicit derivative directions. This does not claim a spatially variable base PH′ or MU state. The test compares `G_K(x)=Step_{Kv=100000}(x)-Step_{Kv=0}(x)` on owned interior W output cells. It subtracts ON/OFF outputs and VJPs after converting to FP64. Input directions cover W, PH, and MU; the W cotangent has the same owned W support. Both predeclared centered-FD widths `h={0.5,0.25}` are checked for representability. The unchanged per-width acceptance is `|AD|>3*floor` and `|FD-AD| <= floor + 0.1*|AD|`, where `floor=3*epsilon_float*weighted_L2(projected ON/OFF outputs)/(2h)`. This is an output-quantization estimate, not a rigorous bound on internal FP32 arithmetic. The selected `Kv` gives `Kv*dt/dx^2=1e-3`.

## Results

W direction resolves at both widths:

- `h=0.5`: FD `-3.12705e-2`, VJP `-3.12703e-2`, floor `1.63333e-6`, error `1.52627e-7`.
- `h=0.25`: FD `-3.12699e-2`, VJP `-3.12703e-2`, floor `1.94208e-6`, error `4.33503e-7`.

PH remains open: at `h=0.5`, FD `-2.16111e-6` vs VJP `-2.07968e-6` passes; at `h=0.25`, VJP magnitude `2.07968e-6` is below `3*floor=3.63963e-6`. MU remains open and under the signal floor at both widths (`|VJP|=4.98324e-7`; floors `6.06605e-7` and `1.21321e-6`). These PH/MU readings are diagnostics only and do not close their whole-step derivative paths. The earlier #264 direct RHS derivative test covers its own explicit-RHS scope.

Homogeneous standalone build used AppleClang 21, Homebrew Torch 2.10.0 (`CMAKE_PREFIX_PATH=/opt/homebrew`, `Torch_DIR=/opt/homebrew/share/cmake/Torch`), Apple libc++, and Python 3.12.13. `otool -L` resolves Torch libraries under `/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/lib`; this standalone test does not validate WRF production ABI0 linkage.

Target CTests passed: `Full_Tile_Step_Adjoint`, `Option2_W_Actual_Rhs_Contract`, and `Option2_W_Actual_Rhs_AD_Derivative_Contract` (3/3; 30.58 s total). The direct binary run also completed with `Full tile step contracts passed`. No WRF `test/em_b_wave` forecast or RK3 comparison was run. No production C++ or Fortran source changed.

## Provenance

- Worktree branch: `agent/k1-option2-whole-step-20260929`; base HEAD `0ef8e3b65616db6584fef7b28796962facb73b73`.
- Test source SHA-256: `f7e5453cb5b936e9fcda4bcbe566e99a1befac4887c9ee347109a31fdc046ef6`.
- Unchanged production implementation SHA-256: `5bdcddeea7c8e6bb7ce52adba66f55f8c391738e54e538964b4d5644ae81f832`.
- Test executable SHA-256: `5b6f4a0d74152d53aa9ef1039d845f110c9cefca1f8308d89754f0eae2cea419`.
- Static archive SHA-256: `88a9d7669370b1d483cbaed027eaf3f1c18161ff46f16c6e2fe1bfa399b156be`.
- CMakeCache SHA-256: `1b47998ffd4053dbfab4209167f1c3dca2fcf136625b27f68024cc5438948333`.
- TorchConfig SHA-256: `3023a4d04799d0f7842dc6f3551bbea916f2a61bfc5deb07212b6dd77538f1d7`.
- Graphify refreshed after the test edit from the seven-file focused corpus at `/private/tmp/sdirk3-k1-wholestep-graph-pre-20260929`; 565 nodes, 1239 edges, 24 communities. Graph report commit metadata is base `0ef8e3b6`; the changed test was re-extracted from the edited worktree. Graph artifacts remain outside the repository.
- No WRF `test/em_b_wave` whole-call or fixed-trajectory adjoint claim.

## Next actions

Red review the test-only change and evidence. Keep PH/MU whole-step directions open unless a separately predeclared signal becomes distinguishable.
