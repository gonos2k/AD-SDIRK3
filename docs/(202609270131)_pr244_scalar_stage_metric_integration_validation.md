# PR #244 plus option-2 scalar stage-metric integration validation

Local timestamp: 2026-09-27 01:31:02 JST (+0900)

## Integrated source and graph

This validation is on `agent/option2_scalar_stage_metric_sign` at `c6c6655ad78a5ff8d728f0aac249aa4b84f54ab6`, based on exact PR #244 HEAD `17b505cda24fd232979518140d58757eebefb4d1`. The isolated scalar source, test, and evidence commits were cherry-picked without conflict. Graphify was regenerated on this integrated worktree with the no-cluster AST pass; it produced 42,280 nodes and 150,781 links. The graph is a navigation aid, not numerical proof.

## Build and CTest

The CMake build and test run use Homebrew LibTorch 2.10.0 at `/opt/homebrew/opt/pytorch/libexec/lib/python3.14/site-packages/torch/share/cmake`, matching the Make archive and WRF link stack. The initial CMake configuration accidentally selected user Python 3.10 LibTorch 2.13; its 103/104 run failed only `Stage2_Rejection_Snapshot_Archive` while reading an empty optional tensor (`tensor does not have a device`). I preserved that log, reconfigured in a fresh build directory with Torch 2.10.0, rebuilt all targets, and confirmed the snapshot test passed in isolation and in the full suite.

The matching Homebrew build passed the focused set 4/4: `Scalar_Diffusion_Contract`, `Option2_Momentum_Geometry_Source_Parity`, `WRF_Dynamics_Config`, and `Stage2_Rejection_Snapshot_Archive`. Full CTest passed 104/104 with `-j2`. The production Make archive was rebuilt first; manifest validation and exact 24-member archive parity passed.

## Candidate-first WRF validation

I linked the integrated candidate C++ archive first in both library resolution positions into a new executable. The link reused unchanged #244 clean WRF objects and `libwrflib.a` from clean worktree HEAD `1b99eb7a6b8aa8b5b21d2fe6d6527acd81a203c7`; it did not modify that worktree. `nm` confirms the executable contains both scalar helper overloads with the stage-metric parameter. This is a candidate-first relink with preserved Fortran objects, not a clean full WRF build of the integrated source.

Fresh 60-second PC2 and RK3 runs use copies of the #244 clean `namelist.input`, `wrfinput_d01`, and diagnostics file. Both report `SUCCESS COMPLETE`, with matching finite counts in U/V/W/PH/T/MU. RK3 outputs are bitwise identical to the clean reference. PC2 has material one-step differences: max absolute deltas are U `3.34e-6`, V `1.84e-5`, W `3.082e-3`, PH `1.4693`, T `6.433e-2`, and MU `1.812e-5`. These results diagnose the changed scalar operator; they do not establish forecast acceptance. No longer-duration run was performed.

Candidate timings were 2.33 s wall / 0.80 s user for PC2 and 0.56 s wall / 0.46 s user for RK3. The clean reference was 0.97 s wall / 0.83 s user for PC2 and 0.56 s wall / 0.47 s user for RK3. These one-step measurements do not support a performance claim.

The scalar regression itself compares actual RHS ON−OFF output with a hand-coded source-equation oracle transcribed from Fortran `vertical_diffusion_s` and `rk_addtend_dry`; it does not call a compiled Fortran oracle. Option-1 actual-RHS coverage is a bounded signed-diffusion smoke, not full option-1 Fortran parity. These scope limits carry through the integration.

## Provenance hashes

| Artifact | SHA-256 |
|---|---|
| Integrated HEAD | `c6c6655ad78a5ff8d728f0aac249aa4b84f54ab6` |
| Integrated C++ implementation | `000251c70e2bab9c0ccb109433c6943c289793b9a854c76e05beee28653749e3` |
| Integrated header | `534b59b3fc5ef66c5d696c006c698d26abb95936321ecf15c9c70eafd9ad8867` |
| Integrated scalar contract test | `b5e8e800569f39ae5117de3440ebdfa18ca3f63ebb93ee5e77283f2ca456fd48` |
| `module_diffusion_em.F` | `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea` |
| `module_em.F` | `fb424486dbf9c903f77da6a15baf35e6772847e786b18d0297139eb48f0d586e` |
| Integrated Make archive | `0b6e5f735eebbeec3897c17d5e6f170f15eba0601d5a9cd17c54dc24968e49b7` |
| Graphify AST graph | `8b435bf6b833418134aa40f92d255529b10c503f0ebd03a18ea2dcf7b5f4bd87` |
| Homebrew full CTest log (`.validation/integrated-scalar/ctest_homebrew_final.log`) | `2c800571f73890ec6d1d4611db290f69b0eae1482556a2544bbc86d92a7afeb5` |
| Initial Torch 2.13 CTest log (`.validation/integrated-scalar/ctest_final.log`) | `1feac62cf38bdce46ac338eb1334776903b929fce0978eebebdc6d1c3cc89fc2` |
| Focused Homebrew tests (`.validation/integrated-scalar/targeted_homebrew.log`) | `b6b9ead492d69ecea8b05be8736b53f8227541bab58bd20074beaeb3e1a75bf0` |
| Make archive build log | `91e3d7eaa622ff661ee6986dffc73598d55d048168d32a4bb7689300abe05101` |
| Candidate WRF executable | `7cd81308091f7b517c595f150d162d648f6fd4f2bf6b43614978dc7a4368e634` |
| Candidate link command | `2700bd82a943e1c6e2c34c22c41d83557736d7b282469e0f1dfd6c4c9dad5b88` |
| Candidate link output log | `62033386ca4e9ccaad66eb633423326620df6fdfe3478187c88fd34053f9f277` |
| Clean #244 `libwrflib.a` | `bff3b8ca0f214028ac00a3b5d72b172915de02a4ec86ab1872d837803ee9217b` |
| Clean #244 C++ archive replaced during link | `01c4faf1dbd58951032d855d8661128352a16d2adae8a72aa7079ef1fe61eade` |
| PC2 input namelist | `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850` |
| PC2 `wrfinput_d01` | `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9` |
| PC2 output | `8d9e012fa0062c9696df904b72d5add4f05c7b2a73cdb59a81a0be0df5e482e9` |
| RK3 output | `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336` |
| PC2 `rsl.error.0000` | `c4fa4855443e8af92675eb9b07525cb94f0db5c930438b9b97fbed373e54d5b7` |
| RK3 `rsl.error.0000` | `75e1f3795d0b9ac1d9749132c674f310b81ba92bbea6b1e693720af08f344e32` |

## Remaining validation

The integrated candidate is ready for Red review. The exact-head CI result is still pending. The PC2 field differences need Fortran full-RHS and longer-run assessment before any forecast-quality claim. A clean full WRF build against the exact integrated source remains open, as do S1b physical-budget closure, G1 decomposition, K1 coefficient derivatives, T1 time accuracy/stability/cost, and A1 complete active-step adjoint evidence.
