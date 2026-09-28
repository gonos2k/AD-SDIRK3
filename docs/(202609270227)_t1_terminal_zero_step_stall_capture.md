# T1 terminal zero-step stall snapshot extension

Recorded: 2026-09-27 02:27:59 JST (+0900)

## Scope and behavior

This isolated continuation starts from exact PR #245 source HEAD `9147bbdca8e34059847c908d6e633d9053dd13a4` on branch `agent/t1-terminal-stall-snapshot`. It extends the existing default-off `sdirk3_stage2_rejection_snapshot_diag` path. The first common Stage-2 trust-decision rejection still writes its original schema-2 artifact once per solver. Each later rejected candidate that reaches the same common trust-decision site replaces a function-local pending snapshot with a detached deep clone of its tensors. This keeps the latest candidate values independent of later tensor-buffer reuse.

At the existing `ZeroStepStall` branch, the solver writes one additional schema-3 artifact with kind `stage2_terminal_zero_step_stall`. Its metadata separates the candidate Newton iteration/attempt from the terminal stall iteration and stagnation count. The terminal writer requires the pending candidate to be Stage 2 and to have the same Newton iteration as the terminal event. If no common-path candidate exists or the iteration differs, it logs an unavailable reason and writes no terminal artifact. Earlier GMRES quality-gate continues and recovery-fallback trials remain outside both capture scopes. The change adds tensor cloning and serialization only under the diagnostic gate; it adds no RHS, JVP, or preconditioner call and does not enter the solver decision predicates.

The round-trip contract tests both schemas. Its negative aliasing control mutates the original K and R_trial after retaining a pending clone and checks the terminal archive still has the frozen operands. A stale-iteration control attempts to publish a candidate from iteration 6 at terminal iteration 7; the writer rejects it and leaves neither output file. No new configuration option or CTest name was added.

## CMake and Graphify validation

The Homebrew LibTorch 2.10.0 / ABI 0 CMake build completed all 217 steps on the source worktree. After the final iteration-match guard, the Newton solver recompiled and the CMake tree completed its incremental rebuild. `Stage2_Rejection_Snapshot_Archive` passed after the stale-candidate control was added. The exact 104-test inventory then passed **104/104** with `--parallel 2` in 172.28 seconds, including `Core_Archive_MakeParity`. Full output: `.validation/t1-terminal-stall-ctest-104-final.log`, SHA-256 `e88e776d387bd3a3bc8ee52abbc434041ae409cf43e55f1acc6fd56833f95977`.

The initial `make clean` / `make -j2 LIBTORCH_ABI=0` built the 24-object Make archive. After the terminal-iteration guard changed the Newton source, Make recompiled that object and rebuilt the archive; the final manifest and exact-member checks again passed for all 24 objects. Final Make archive SHA-256 is `5a918d09a13ce5980e5cec80595ee25c59311b515be25849c90371538b81fa5c`; the corresponding CMake archive is `dd3e7be82668f1c7cbaf2ceeb233e25afead22d280776bab08070884134d1aec`.

Graphify AST extraction was invoked on the same 12-file input list before and after the edits. The resulting graphs contain 9 distinct `source_file` paths, all under `external/libtorch_wrf/sdirk3`; Registry, Fortran bridge, and CMake inputs were not represented in the graph, so those edges were checked directly against source. At base HEAD `9147bbd`, the graph had 257 nodes / 727 edges and SHA-256 `3700cd44b0e54a1e8e28498afc923af807e6b3f7d2fb8097d411ff57ed86e94b`. After the edit it had 257 nodes / 727 edges and SHA-256 `9e508b203dfae18bf1846c7ce8fb67fa4db8481ed1249d16196d7e571b36b37c`. These graphs are navigation evidence, not numerical evidence.

## Strict dt=15 OFF/ON control

I copied the archived strict PC2 `dt=15 s`, 240-second setup into this worktree’s `.validation/t1-terminal-stall/run_off` and `run_on` directories. The only namelist difference is `sdirk3_stage2_rejection_snapshot_diag` (`.false.` OFF / `.true.` ON). Both use `wrfinput_d01` SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9` and diagnostics SHA-256 `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`.

Both runs exited 1 at Stage 2 with the same strict failure: final scaled RMS `1.3614e-6` against `1e-7`, 7 Newton iterations, 301 Krylov iterations, and a `ZeroStepStall` at Newton iteration 6 after three zero-update iterations. OFF emitted no snapshot files. ON emitted the original first-rejection pair and exactly one terminal-stall pair. The first artifact remains at Newton iteration 1 / trust attempt 0 (`rho=-0.0418917127`); the terminal artifact carries candidate iteration 6 / trust attempt 1 and terminal iteration 6 (`rho=-0.0497578526`, actual reduction `-2.57741961e-10`, predicted reduction `5.17992533e-9`, trust radius about `1e-6`, stagnation count 3). Both metadata sidecars state `rhs_replay_performed: false` and `observed_fp32_operands_only`.

The OFF and ON partial `wrfout` files are byte-identical, SHA-256 `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`. After removing only opt-in snapshot log lines and elapsed-time fields, 188 selected Newton, trust, Krylov, telemetry, and failure lines match exactly; the normalized line-set SHA-256 is `abf13e18082568e465c078bdd943e23c82150d81b4cf5f51bee75499d69c9809`. Stage-2 JVP telemetry and total Krylov count (301) also match. The capture path has no solver-evaluation calls. There is no direct RHS-call counter in this run, so the evidence is source inspection plus identical solver event/JVP/Krylov records and byte-identical output, not a standalone RHS-call count.

## Archive-first link against clean #244 WRF objects

The #244 clean WRF base was left untouched. Its worktree is `/Users/yhlee/SDIRK3-pr244-cleanbuild`, source HEAD `1b99eb7a6b8aa8b5b21d2fe6d6527acd81a203c7`, with clean-built `main/wrf.exe` SHA-256 `ec9ab9c9cbd7f8e75753e45185d0ef1ad6e5ca62874ecc0717187ba1646c8934`, `main/libwrflib.a` SHA-256 `bff3b8ca0f214028ac00a3b5d72b172915de02a4ec86ab1872d837803ee9217b`, and `configure.wrf` SHA-256 `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab`. The saved clean build used LibTorch ABI 0. The #245 source difference from the clean #244 source is C++/test/documentation; no Fortran or Registry ABI input changed. I copied the needed #244 link objects and archive into `.validation/t1-terminal-stall/link/main`, then replayed the saved WRF link argv with the final #245 Make archive as both the first `-L/-l` lookup and the later explicit archive argument. Link exited 0. Candidate executable SHA-256: `484201e490abe80d94a38b4251c45f5914704ac9e0fb1d36f485160df502472d`. This is an archive-first incremental relink, not a clean full WRF build of final #245 source.

Input namelist hashes are OFF `246a148486b8f5bc8d3e22fe03ca7a1ff7090fdf5d89a471ebf8e35642e84d54` and ON `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`. OFF/ON `rsl.error.0000` hashes are `0b42571ca30228548956aa0f6cf34f5d3eeebb7bf06f66f8d094acb2b0af27df` / `9e9692a9962666b6d1edda5fce01b7f896bd1136f6c0cae5c219dfbeeca0448f`.

## Operand-only residual comparison

I read the terminal archive’s FP32 K/F/R, K_trial/F_trial/R_trial, S_inv, and halo mask. The halo mask is empty for this single-tile run. For each packed block, the stored FP32 residual equals an FP32 subtraction of its stored operands exactly. I then converted those same FP32 operands to FP64 and subtracted K−F (and K_trial−F_trial); both norm reductions used FP64 and the same captured FP32 S_inv converted to FP64. This isolates subtraction of the captured operands. It does not call the RHS, reproduce FP64 RHS formation, include FP64 stage inputs/forcing, or identify a precision-floor cause.

| Block | Base scaled L2: FP32 / FP64 subtract | Base scaled-vector relative difference | Rejected-trial scaled L2: FP32 / FP64 subtract | Trial scaled-vector relative difference |
|---|---:|---:|---:|---:|
| ru | `1.67402826e-6 / 1.67402826e-6` | `9.38e-11` | `1.64716075e-6 / 1.64716075e-6` | `0` |
| rv | `1.62340086e-6 / 1.62340086e-6` | `0` | `1.62794338e-6 / 1.62794338e-6` | `0` |
| rw | `2.13222143e-4 / 2.13222143e-4` | `7.30e-9` | `2.13825885e-4 / 2.13825885e-4` | `6.68e-9` |
| ph | `8.91942316e-9 / 8.91942316e-9` | `0` | `8.24708347e-9 / 8.24708347e-9` | `0` |
| t | `1.15184234e-10 / 1.15184234e-10` | `0` | `1.09210984e-10 / 1.09210984e-10` | `0` |
| mu | `3.18924738e-7 / 3.18924738e-7` | `0` | `3.02431922e-7 / 3.02431922e-7` | `0` |

The terminal base W-block L2 contribution divided by `sqrt(24531)` is `1.3613653043e-6` using FP32 subtraction and `1.3613653046e-6` using FP64 subtraction of the captured operands (difference `3.15e-16`). The rejected-trial W contribution is `1.3652200303e-6` / `1.3652200305e-6` (difference `1.59e-16`). These agree in scale with the logged global scaled RMS, but they do not establish why the Newton residual stalls.

The complete machine-readable per-block receipt is `.validation/t1-terminal-stall/terminal_operand_precision.json`, SHA-256 `e2b18a48572db2980bae1b4e3d0cc64e39a5f25561d7dbf0a3647e4a27c1f9c6`. OFF/ON and artifact comparisons are in `.validation/t1-terminal-stall/run_comparison.json`, SHA-256 `f6510551f341add029744574e743de504c8a4d1c4e1e83009959f7ef3ee7450b`.

## Artifact and source hashes

- Exact PR #245 base HEAD: `9147bbdca8e34059847c908d6e633d9053dd13a4`.
- `wrf_sdirk3_newton_solver.cpp`: `e6bd103eecda067a3b4fd08a6308b286fc7289b7491a7af3ed7452a62ef274c5`.
- `wrf_sdirk3_stage2_rejection_snapshot.h`: `168569ab28519a99cd030dcdaf5691ca51ff716db9394ba8921748093d216f90`.
- `test_stage2_rejection_snapshot.cpp`: `64cedcf9c101d4cc600563dc45c8583d7f90325d7f1d4955e06afd755851e4c4`.
- Final Make archive: `5a918d09a13ce5980e5cec80595ee25c59311b515be25849c90371538b81fa5c`; CMake archive: `dd3e7be82668f1c7cbaf2ceeb233e25afead22d280776bab08070884134d1aec`.
- Candidate WRF executable: `484201e490abe80d94a38b4251c45f5914704ac9e0fb1d36f485160df502472d`.
- Candidate link argv JSON / output log: `7079b50cc96542e2720d00d0e0428a271c066f1fea1e43568f367441fdcf6e12` / `62033386ca4e9ccaad66eb633423326620df6fdfe3478187c88fd34053f9f277`.
- First snapshot archive / metadata: `85785c8cc28c9b0e0463ef26d228533e51c858b71923460638b0b26c8ce092a8` / `46fb66225e9cfd0fa1a6f3f915f584133a098896441c647923701e41ff0469c4`.
- Terminal snapshot archive / metadata: `93d06c31af75707e2d788713bcd297b5447dbf7417f91cc8d01580b2c1485fb4` / `5364e9d44312c289f8a1fa933b4a8359e823e5cf4773007c29f52917ab9fdbd4`.
- OFF/ON `wrfout`: both `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`.

No clean WRF rebuild, successful forecast run, alpha ladder, faithful offline RHS replay, negative model-mismatch control, FP64 RHS evaluation, or precision-floor attribution was performed. The strict case remains a fail-closed Stage-2 failure; these artifacts preserve its terminal trust candidate and provide operand-subtraction sensitivity only.
