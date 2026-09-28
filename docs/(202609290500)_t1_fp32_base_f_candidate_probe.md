# T1 strict-15 base-F reachable-candidate probe

Local timestamp: 2026-09-29 04:56:17 JST (+0900)

## Scope and result

This disposable, output-only experiment is isolated from PR #265, based on source HEAD `14e1dbd051ffb438c3f4a69c35141fa4e4114e88`. Its one env-gated probe runs after Stage-2 `ZeroStepStall` is decided and the terminal snapshot is written. It does not alter K/R, trust acceptance, tolerance, or stage output. OFF and ON used the same `main/wrf.exe`; the only probe control was `WRF_SDIRK3_STAGE2_FP32_FIXED_POINT_PROBE` unset / set to `1`. Both used `OMP_NUM_THREADS=1`, the same strict input files, and `sdirk3_stage2_rejection_snapshot_diag=.true.`; `WRF_SDIRK3_STAGE_OPERAND_DIAG` was unset in both.

The saved terminal archive has coherent base `U_stage`, `K`, `U_eval`, `F`, `R`, plus separate rejected-trial fields. The probe uses only the base fields: `K′=F_base` with the production halo mask, `U′=fl32(U_stage+dt*gamma*K′)` in production scalar order, and exactly one captured `compute_rhs(U′)` call. It logs the scaled RMS of `K′−F(U′)`, bitwise map closures and input digests. The probe is guarded against `omega_update_ref_per_newton=true`; this strict fixture uses the fixed per-stage reference.

The base map closes bitwise. The candidate changes the RHS input digest from `0x5441e872da2706fa` to `0xcd834ef808dcf847`, so `candidate_map_closed=0` and `candidate_rhs_equals_saved_base=0`. The latter does not imply nondeterminism: `F(U′)` and `F(U_eval)` have different inputs, so no fixed-state repeatability claim is made. The measured candidate residual is `4.417670709e-7`, below the base `1.273917405e-6` but above tolerance `1e-7`. Its scaled displacement `||S⁻¹(K′−K)||₂=1.995257189e-4` exceeds the saved effective trust limit/radius `9.999999975e-7` by `199.5257194×`. This one out-of-trust candidate is not a lower-bound or convergence proof and was never accepted.

OFF and ON both terminate in Stage-2 `ZeroStepStall`, iter 9 (`R_last≈1.2739174e-6`); neither completes a physical forecast step. The terminal JSON/PT snapshots and partial `wrfout` are byte-identical. No same-setup RK3 comparison was performed.

## Build receipt

The validation tree is a copy-on-write clone of the previously isolated PR #246 WRF object set, not a clean whole-tree build. The authoritative WRF Fortran source/object remain the integrated PR #265 pair. The integrated ABI0 Make archive `798fd2dc36f1594fdd288cbcbdcae883f0cd7ac3b0b35b42cfc66fc9eee4d7ed` was changed only by replacing `wrf_sdirk3_newton_solver.o` and updating the normal archive symbol index. Member-set audit found the other 23 objects byte-identical.

The one-TU compile used the exact experimental worktree source and its source-directory headers, Homebrew clang, C++17 `-O2`, Homebrew Torch 2.10 headers, and `_GLIBCXX_USE_CXX11_ABI=0`. This diagnostic TU used standalone Homebrew `clang++ -O2`; the WRF Make build uses `/usr/bin/g++ -O3` with WRF macros. Both use ABI0 and matching Torch headers/libraries; Red review found no relevant macro-dependent branch in this TU. The copied PR #246 C++ headers are stale and were not used for this compilation. The dependency file names 44 project headers resolved from the exact 14e1 worktree. The Newton source was also copied byte-identically into the validation tree for inspection. The link used the recorded PR #265 `g++ -o wrf.exe` command with only the validation-root prefix substituted. Link exit was 0; the executable hash remained identical to the binary used for OFF/ON.

- Experimental worktree: `agent/t1-base-f-reachability-probe-20260929`, base HEAD `14e1dbd051ffb438c3f4a69c35141fa4e4114e88`.
- Newton source SHA-256 (worktree and validation copy): `64ce84d612846f60f48ccb3c41b7fed7f70ad2e0313a6feb24e4a4ab6d541436`.
- One-TU command receipt: `.validation/fp32_fixedpoint_one_tu_compile_command.txt`, SHA-256 `d3c5f04f70f77ebc151762f42cd9857c9f311307b44ca1f6381d6fd86aed105b`; compiler log `.validation/fp32_fixedpoint_one_tu_compile.log`, SHA-256 `fdc2d71d94e011de7bb96bb89238ea76f9d8fa6ac2041320c418de49a3d398e4`.
- The 44-header manifest is `.validation/fp32_fixedpoint_one_tu_headers.sha256`, SHA-256 `acbe76acc078cc6d84ae5b7ed6d84eb8efa634ffaf9ea9f28f967012cc05b5f7`. Key exact-source header hashes: stage snapshot `a006ee94739964646b9a6c4c5c0440bac1932ef4b92c8cfb2b9eb9ea29e28155`; RW capture `afc06ead0a51ca93157a22966f571d01ae16043221a19fbebe73597adbc9eb58`; Newton header `2e89d381708c99c48e58e33d738bfc7c029790a2b36e40163966545839844db8`.
- Newton object/archive member SHA-256: `4899e8b2fb432fb03aa7fee3311fcc6a406b9f265495c7cf9d9f8b214fcf2179`.
- Modified Make archive SHA-256: `7989c8aaed4f740ce819a950d2080557a59eb044c98f2975c7ca0273b63269cd`.
- Link command receipt `.validation/fp32_fixedpoint_link_command.txt`, SHA-256 `cc2d6ba9b77df93f61e33f427f0b8f868a33197a0c7d22ea66c461a2f5b8871f`; link log `.validation/fp32_fixedpoint_link.log`, SHA-256 `62033386ca4e9ccaad66eb633423326620df6fdfe3478187c88fd34053f9f277`.
- Relinked executable SHA-256: `c9f8a8a24d1eb56f8ae28b8ccd36e57af2a86576fbc7013c0ce6964f6a0efef0`.
- Fortran source/object SHA-256: `4f6903102b53f0671de6d87458c5c20ef34b39688e267276b9296c0f4b1183d7` / `45d81fb6038fdfe5e986cb93fde0398e9b4cd98e727f8a2c7cb7374633c47ad0`.
- `configure.wrf` SHA-256: `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab`.
- Shared `wrfinput_d01`, namelist and diagnostics hashes: `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, `51e49cff681926a4a5d645c943ad6d264c862a7aada4c3a80159d5605896263f`, `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`.
- OFF / ON `rsl.error.0000` hashes: `0d8ccd845fd632cb44b2f54a555783d6f81df611b04541469ebc624d2575ff53` / `565241ff8d9760303b6062824ad12ff7678448602b5b473d8225ca4e895417a7`.
- Shared terminal JSON / PT hashes: `78f555ef994840bf4ea081630b3a8866df77f8e94b53b92416af9d93cc69a2f0` / `f99fd53b18fc859c5648ade3a654358c45b10b5ae99d0d286cbdc879c2f3d053`; shared partial `wrfout` SHA-256 `ce58a0741fc7b4ae278b00a5767f8454bd1d9459ca85c53a87fff5e65b9f369a`.
- Focused Graphify corpus `/private/tmp/sdirk3-t1-reachability-graph-pre-20260929`: 411 nodes, 1024 edges, 26 communities; final Newton source re-extracted; graph metadata base `14e1dbd0`.

The prototype remains local to this isolated worktree: no PR, no production solver option or acceptance change, no completed forecast.
