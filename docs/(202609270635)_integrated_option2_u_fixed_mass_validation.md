# PR #246 integration validation: fixed-mass option-2 U

Local timestamp: 2026-09-27 06:35:13 JST (+0900)

## Integrated source and Graphify

The integrated worktree is `/private/tmp/sdirk3-u-fullrhs-on-pr246` on
`agent/option2_u_full_rhs_normalization`. Its exact base is PR #246 HEAD
`427b1cf` and the validation tree is HEAD `abc4db9e2a70eec0ed5299f3577cea4698395062`.
The source-equation correction and physical U implementation are unchanged from
the reviewed development candidate. The #246 stage-history diagnostic header is
restored byte-for-byte; it is not part of this U change. The CTest oracle now
runs each extracted Fortran compile and executable in its own temporary working
directory so parallel map/profile tests cannot race on generated `.mod` files.

Graphify was refreshed on this exact integrated tree. It extracted 4,478 source
files and produced 42,276 nodes / 68,758 edges / 4,404 communities. No topology
change was needed after the final oracle-cwd edit; Graphify reports the updated
source manifest.

| Source/receipt | SHA-256 |
|---|---|
| Integrated HEAD | `abc4db9e2a70eec0ed5299f3577cea4698395062` |
| U implementation | `cbf9dcbbbcb4ae7be00b0da5680d96020df141fd56613e3b3b053c3ec75f22f3` |
| Fortran oracle and per-call scratch isolation | `689c48d7bc9e7da3c1b67ca05d723d00c1f4e4292f51fe89f6d45c968d60379e` |
| Stage-history diagnostic header (unchanged from #246) | `92355d49f094f304289486d2d93962146bf9b96381398e92575f527680e84758` |
| C++ full-RHS fixture / CMake test registration | `a32e10ef25ebaa3dd0ab5d56acc54ed1f8f827b7ed7618bb28e5ae6ac5d20bcd` / `141731242dca834abd5ccef7633e26f9139f79adaa734274507cac8ac1108323` |
| Pinned CTest names / Graphify graph / manifest | `c09178acbd39a03cd1c6785fbf6420b057c085271dbf02500d899c22d2949ef9` / `9cef5c517424479091c69ebc1c127a3d5e599affa0121b615f2d915569f91c98` / `e0983de04e084fc325255efeb41250992a953cd71cba2e1879d8094d35873e37` |

## Build and tests

CMake configured in `/tmp/sdirk3-u-fullrhs-on-pr246-build` with Homebrew
LibTorch 2.10 and the matching ABI (`LIBTORCH_ABI=0`). All configured CMake
targets built. Make rebuilt the production archive and passed manifest and exact
24-member archive checks. The sorted `ctest -N` inventory exactly matches the
106 pinned names.

The first parallel full-suite run had two Fortran oracle compile failures: map
probes wrote generated `.mod` files in the shared caller directory. The harness
now sets the unique oracle scratch directory as `cwd` for both compiler and
executable. After this change, focused CTest passed 5/5 (`Scalar_Diffusion_Contract`,
`Option2_Momentum_Geometry_Source_Parity`, map-1 and map-1.25 U physical contracts,
and `Full_Tile_Step_Adjoint`), and full CTest passed 106/106 with `-j2`.

| Validation log/artifact | SHA-256 |
|---|---|
| Homebrew Torch 2.10 CMake configure | `88e6a7be6ca712f64996db9dfb86197f0e1a60d4982b99288dc2c529662c1e81` |
| Full CMake build, after restoring #246 header | `eccf56dee78c4487d05c59eb7fc2dca7a184aae5f5b8bd8112c9f8fe7b25eb3a` |
| Initial incomplete Fortran-module-race full CTest (superseded) | `82717db3cf5a6c44ff1a2e7b7c5a8b0fb0aeb5745efa2370b51d20dc311e84be` |
| Final focused 5/5 CTest | `5df8ed2be7d519c77e0a749d045997c8d7d0cba74a89c2f3cf4966eee5e515dd` |
| Final full 106/106 CTest | `8fd8b911d969cfb9e0033e8e54f8316896817c104239d52e2ae1a3748edda0c3` |
| Final Make manifest/archive checks | `514697bdb4da4a3ad6b0f41c3de0fa033e7ee4ea73aa1eb1f25b4c9837eacd60` |
| Final candidate archive | `e29b4e6f2d64e4b2854163423608e069fa899d302dcf3f840fc86b324c4aec6a` |

## Candidate-first WRF relink and run pair

The candidate-first relink and the one-rank 60-second PC2/RK3/K=0 comparisons
are documented in the companion [WRF validation receipt](docs/%28202609270631%29_option2_u_fullrhs_pr246_wrf_receipt.md). It records the copied #246 object tree, exact link argv, inputs, output hashes, finite-field counts, per-field RMS/max comparisons, and timings. The runs reached `SUCCESS COMPLETE WRF`; candidate PC2 and RK3 outputs are byte-identical to clean #246, and K=0 PC2 matches its preserved clean control.

This verifies regression neutrality for the archived `em_b_wave` setup. It is a
candidate-first relink, not a clean full WRF build of the integrated source; the
standard case does not demonstrate material activation of nonunit map/layer-mass
corrections.

## Scope and remaining limits

This validation integrates the corrected fixed-mass source-equivalent U
contract. The Fortran operator driver compiles the deformation, U H/V diffusion,
and `rk_addtend_dry` extracts; it computes the small-step physical transform
from the source equation and fixed `SMALLSTEP_LU` receipts. It does not invoke
`small_step_prep`, `advance_uv`, or `small_step_finish`. Its halos are analytic
and it pins `rho=1`; it is not complete production physical-boundary parity.

The candidate currently uses the RHS `mu_full` layer mass for `L`; later-stage
`L_s/L_t` behavior when small-step masses differ remains unverified. The standard
`em_b_wave` run is byte-identical to clean #246, so it is a regression check,
not evidence of a material response to nonunit map/layer offsets. Full clean WRF
build of the integrated source, V/W full-RHS parity, MPI/decomposition coverage,
variable-K, time accuracy/stability, and complete active-step adjoint evidence
remain open. At the time this addendum was recorded, no push or PR had been made
from this validation worktree.
