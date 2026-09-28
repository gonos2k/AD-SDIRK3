# Native option-2 W RHS derivative contract

Local timestamp: 2026-09-29 03:00:58 JST (+0900)

## Scope

This is a test-only check of the supported native option-2 scalar-K `ExplicitOnly` RHS increment. It does not claim a fixed-trajectory or whole-step adjoint: `validateFixedTrajectoryProfile` explicitly requires `diffusion_option=0` and zero K (`wrf_sdirk3_tile_unified_impl.cpp:42003-42013`). No production source changed.

The WACT physical fixture is reused on a single complete tile with `mass_coordinate_mode=1`, dry QV, `smagorinsky_opt=1`, no setter-supplied K, `damp_opt=0`, WRF `fnm/fnp`, nonflat `PHB`, and periodic-X/symmetric-Y Wdamp policies. The deprecated `wrf_omega_ww_cp=false` value does not disable the native path: `effective_wrf_omega_ww_cp()` is true in mass-coordinate mode 1. The test asserts those gates. It compares `G_K(U)=F_K(U,ExplicitOnly)-F_0(U,ExplicitOnly)` on the same solver/state/base/maps/eta; K is a fixed scalar for each operator. Each returned FP32 RHS is promoted to FP64 before ON/OFF subtraction. Only un-packed owned W cells (j/i interior, k=1..nz−1) enter the derivative checks.

The W derivative fixture sets U/V to zero and uses a small nonzero W profile `0.01*sin(pi*k/nz)*cos(2*pi*i/nx)`. It tests horizontal W stress with `khdif=0,kvdif=1` and vertical W stress with `khdif=1,kvdif=0`. The two state directions are the existing manufactured variable PH′ profile and a 4× variable-MU profile. Centered FP32 steps are fixed at `h={2^-5, 2^-6, 2^-7}`; each nonzero direction entry must remain representable in both `U+h d` and `U-h d`.

## Results

`compute_jvp_fwad_or_fd` used FWAD without fallback for each case. Nonzero derivatives matched centered finite differences and passed VJP transpose, FD plateau, and Taylor checks:

| Operator / direction | JVP projection | centered FD at h=1/32, 1/64, 1/128 | VJP dot relative error |
| --- | ---: | --- | ---: |
| W horizontal (`Kv`) / PH | 1.04781e-9 | 1.04781e-9, 1.04782e-9, 1.04779e-9 | 5.36e-8 |
| W horizontal (`Kv`) / MU | 2.86070e-10 | 2.86080e-10, 2.86033e-10, 2.86051e-10 | 1.96e-8 |
| W vertical (`Kh`) / PH | 3.21420e-9 | 3.21417e-9, 3.21426e-9, 3.21398e-9 | 1.16e-8 |

W vertical (`Kh`) / MU is a source-derived zero-response invariant for this fixture, not a nonzero AD parity case. With `c1h=1,c2h=0`, WRF `alt=(alb*mub-rdnw_signed*DeltaPH′)/mut`, so `rho/mut` is independent of MU; W vertical stress is `rho*K*D33` and the public W conversion divides by `mut` (up to the small velocity-mass epsilon). The base K increment is resolved (`||G_K||=2.78522e-8`, base output-quantization estimate 3.32024e-15), while the MU derivative is at the FP32 floor: `||Jv||=5.68e-17`; the smallest-step centered difference norm is 2.53017e-13 against its output-quantization estimate 4.24991e-13. `G_K(U+h d)-G_K(U)` and the negative-step counterpart are 3.71e-15 and 3.54e-15. The test uses an absolute zero-response check here and skips relative VJP comparison because that derivative signal is not resolvable.

The predeclared output-quantization estimate uses unit roundoff `u32=2^-24` and a factor of two over the four ON/OFF projected output scales, divided by `2h` for derivative estimates. It is labeled an output-quantization estimate, not a rigorous bound on all internal FP32 arithmetic. Nonzero branches require signal above 100× that estimate; the MU invariant branch requires JVP, finite-difference, and state-response values below the absolute estimate. No tolerance or K/W amplitude was changed after seeing results.

Affected CTests passed:

- `Option2_Momentum_Geometry_Source_Parity` — 32.48 s
- `Option2_W_Actual_Rhs_Contract` — 8.66 s
- `Option2_W_Actual_Rhs_AD_Derivative_Contract` — 0.47 s

`git diff --check` passed. The pinned CTest inventory and current README count references were updated to 109. No WRF integration/model run or RK3 forecast comparison was performed; these are operator-level CTests and make no forecast-quality claim.

## Source and build identity

Target base: PR #263, commit `f3e40b524d539efbd702782bd0196c5581a3dda2`. Worktree: `/private/tmp/sdirk3-k1-option2-ad-derivative-20260929`. Final validation build: `/private/tmp/sdirk3-k1-option2-ad-homogeneous-build-20260929`, Unix Makefiles, AppleClang 21.0.0.21000334 with Apple libc++, `CMAKE_PREFIX_PATH=/opt/homebrew`, `Torch_DIR=/opt/homebrew/share/cmake/Torch`, and Python 3.12.13 as the CTest driver. Both TorchConfig and `opt/pytorch` resolve to Homebrew Cellar PyTorch 2.10.0. The compiler include roots resolve under that same 2.10.0 Cellar tree, and every Torch dylib reported by `otool -L` resolves to 2.10.0. CMake reports `_GLIBCXX_USE_CXX11_ABI` as N/A under Apple libc++; no libstdc++ ABI assertion is made. This standalone Apple-libc++ CMake test does not validate the WRF production archive’s `_GLIBCXX_USE_CXX11_ABI=0` linkage or a stock-WRF linked run. The earlier mixed Python3.10/PyTorch2.13 build was not used for final validation.

| Artifact | SHA-256 |
| --- | --- |
| derivative fixture `test_scalar_diffusion_contract.cpp` | `813b8b246e52b4551856a1a1f68992057e907ec8e702c165eb259d93e58dd363` |
| production RHS `wrf_sdirk3_tile_unified_impl.cpp` | `5bdcddeea7c8e6bb7ce52adba66f55f8c391738e54e538964b4d5644ae81f832` |
| density EOS `wrf_hydrostatic_pressure.h` | `f926092be30df9a21b2e9967f7f4bf7d18b1fe2790cc8d2145d316aa9f73591b` |
| stage solver `wrf_sdirk3_newton_solver.cpp` | `5fe5daf5dc8704896de54fa0109bf64d8a7c78ca2f1b83f77b30a4bbefc59893` |
| converged-stage pullback `wrf_sdirk3_implicit_autograd.h` | `fa9d052b93de4952cd5e032569116889a8a18e4e25af87eb3127b120b9e9c15e` |
| CTest registration `CMakeLists.txt` | `ba8ca8f286aa19732a639e02613d754a3e20afeb2bc9031ad5ffef4817a1fafa` |
| pinned CTest inventory | `696b3a9ef599ad2e6aa9a3f1c8a5952c3d3bdf12d200b2f71518d9c836a25f0b` |
| built `test_scalar_diffusion_contract` | `f32b5e6cfa62182f31b93d67e64799e618361e7cc05647ab8c6ff876c16c3e2c` |
| CMake build configuration (homogeneous build) | `9ed2d44d64f10cd53275f3083b66a25b21a531f676c200223c2fb3468dd1f5f2` |
| standalone CMake SDIRK3 static archive | `d60055cbe1f4a2fa9d6dab2cb214fa9ce0a391238a01c97cd7ddc6ac4650e5f1` |
| exact test link command | `0323d3fa5bbd0d0271398038b7f89ac5569d11cd8450accf7be229430026d8df` |
| exact `otool -L` output | `c79986fce4f0c5d7db2ffd01651deb47a1da1e8e873617d971a6d1571c32be18` |
| Homebrew `TorchConfig.cmake` | `3023a4d04799d0f7842dc6f3551bbea916f2a61bfc5deb07212b6dd77538f1d7` |
| Homebrew C++ API header `torch/torch.h` | `3e5eb79dca5c605bb17c76659953db8f9fdd27813aa6744063c1bd41b05dfabc` |
| Homebrew core header `ATen/ATen.h` | `0470bc509c3e2a482c7ba4680a714fb092f5653a1db61d5561b16cb5b2220727` |
| Homebrew `libtorch.dylib` | `3cb51bdf67ca14c93a99c06d7b80f002ddeeb5f1212c8e9f00ce85c7aaea0899` |
| Homebrew `libtorch_cpu.dylib` | `f8f2c1b648fafcac93cd01f47a5bfcb2bf8d816cec1f83f0eb0297299c66ff6d` |
| Homebrew `libc10.dylib` | `cb56ee731b2cbddf4e58dc42f4b8422735c414c6466d9902fc73e9633af767e6` |
| Fortran/C++ WACT runner | `8953aa27f501a5e98fe2a0e7f70eeca5c15dd0d4f174ddc6b24cf440bcfda036` |

The full dependency listing is preserved at `/private/tmp/sdirk3-k1-option2-ad-homogeneous-build-20260929/otool-L-test_scalar_diffusion_contract.txt`; its Torch entries use `/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/lib/{libtorch,libtorch_cpu,libc10}.dylib`, each resolving to `/opt/homebrew/Cellar/pytorch/2.10.0/libexec/lib/python3.14/site-packages/torch/lib/`. The exact link command is at `/private/tmp/sdirk3-k1-option2-ad-homogeneous-build-20260929/link-test_scalar_diffusion_contract.txt`; CXX includes are `/opt/homebrew/include` and `/opt/homebrew/include/torch/csrc/api/include`, also resolving under the same 2.10.0 Cellar tree.


Graphify was built before and refreshed after the source edit from copies of these exact f3e40b worktree files: `dyn_em/module_implicit_sdirk3.F`, `dyn_em/solve_em.F`, `external/libtorch_wrf/sdirk3/wrf_sdirk3_tile_unified_impl.cpp`, `wrf_hydrostatic_pressure.h`, `wrf_sdirk3_newton_solver.cpp`, `wrf_sdirk3_implicit_autograd.h`, `tests/test_scalar_diffusion_contract.cpp`, and `tests/test_option2_w_actual_rhs.py`. The post-edit code-only graph contains 651 nodes / 2,629 edges at `/private/tmp/sdirk3-k1-option2-ad-graph-post-20260929/graphify-out/graph.json` (SHA-256 `ab7e7b2f590acdee78747bfa9d03297ff775083f5ced51d3bef40e1825b5bae3`). The graph includes the changed test and production RHS/pullback sources. CMakeLists is omitted because Graphify classifies it as documentation; the test registration is verified directly. The graph source snapshot is the f3e40b base plus this test-only working-tree edit.

Red reviewed the final test source and invariant as no-blocker. This evidence is limited to the `ExplicitOnly` native option-2 RHS increment; it does not expand the fixed-trajectory adjoint contract.
