# T1 dt=1 Stage-2 Picard retry prototype

Local timestamp: 2026-09-29 06:12:25 JST (+0900).

## Scope

This disposable, default-off experiment tests one additional Stage-2 attempt after the three normal trust candidates reject. It is restricted to `dt == 1`, Stage 2, frozen per-stage omega reference, and `WRF_SDIRK3_STAGE2_DT1_PICARD_RETRY=1`. The ordinary attempt limit remains three; their clipping, prediction, force-accept boundary, gates, and common accepted-state update are unchanged. Attempt 3 uses `dK=-R`, clipped to half the current `S_inv` effective limit. The candidate goes through the existing canonical `K_trial`, `U_trial`, `compute_rhs`, actual/rho, and block-aware acceptance path. Its prediction uses one FWAD JVP on realized `U_trial-U_eval` and realized `K_trial-K`; finite-difference fallback fails closed. The shared per-Newton RHS budget charges one candidate RHS and one JVP RHS.

No tolerance or acceptance threshold changed. The prototype is not a production change and is not in a PR.

## Result

OFF and ON used one executable, one MPI rank, one OpenMP thread, and identical dt=1 namelist, `wrfinput_d01`, and diagnostics. OFF matches the original fresh PR #265 dt=1 terminal JSON/PT and initial `wrfout` bitwise. Both runs fail closed in Stage 2 before a forecast frame is written.

| Arm | Picard candidates | Picard accepted | Picard rejected | Total Newton accepted / rejected | Terminal `R_last` | Outcome |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| OFF | 0 | 0 | 0 | 9 / 3 | `1.205e-7` | ZeroStepStall; 12 Newton iterations |
| ON | 6 | 3 | 3 | 14 / 3 | `1.065e-7` | ZeroStepStall; 17 Newton iterations |

The six Picard candidates each used half the `1e-6` effective limit (`5e-7` in `S_inv` norm). The first three passed the unchanged actual/rho gate: actual/predicted merit reductions were approximately `1.862e-11/1.862e-11`, `7.838e-12/7.837e-12`, and `6.227e-12/6.228e-12`, with rho `1.0`, `1.0`, and `0.9998`. The last three rejected with actual/predicted reductions about `-2.203e-13/-2.191e-13`, giving an undefined rho. The budget log records one candidate RHS and one JVP RHS per candidate, leaving zero tokens. No JVP failure or finite-difference fallback occurred.

The terminal residual remains above the unchanged `1e-7` tolerance, so the run produces only its initial `wrfout` frame. This one dt=1 trajectory does not establish convergence, a completed forecast, or an attainable precision floor. No new RK3 run or comparison was performed.

## Provenance

- Source HEAD: `14e1dbd051ffb438c3f4a69c35141fa4e4114e88`; the only tracked local change is `external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp`, SHA-256 `3fd6d55139110d978b1944ae36823f4636eaed994fc4d768a4b023412a45ca40`.
- Detached worktree: `/private/tmp/sdirk3-t1-dt1-picard-fallback-20260929`. The build reused only artifacts from the fresh PR #265 full build: `configure.wrf` SHA-256 `e14c0ff55cee9df6b879b4c70948c6f281c9263f0d97ceca7053d3e1851d10ab`; WRF `libwrflib.a` `29e7a1ccadc7847448a95257bc3018727f7aa03e8599d45044b9f5a242ca7537`; `wrf.o` `e57e22fb3b28cd1ce9001ee7afbf11f4b78ad688f41994b84a2729ea37770f15`; `module_wrf_top.o` `c30437abc83a59ccbf1c70155ee981862d4ac5b134ffb6f69d4e70e1b9f5d179`.
- The exact WRF `g++ -O3`, C++17, ABI0 one-TU command and recorded fresh-build link command both succeeded. Compile command/log hashes: `b1634c104b3dc02935ca9b792100b7f6e06f34b7e438bdcaeb94e223bc107b7c` / `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855`. Link command/log hashes: `6a61265fb29fefb947bfc9485e3facde682144f757377efc689e6d6e0d355ad4` / `62033386ca4e9ccaad66eb633423326620df6fdfe3478187c88fd34053f9f277`.
- Original freshly built C++ archive SHA-256: `f6b4f216f17ba343693e1e736ae139d02d180844a6e8bcfa6005e880a072b682`. The experiment archive is `e5857d20c912813347cde1cb74a3abde4d00a4067348c06f8a7a9543cf7feaa7`; its 25 member names match the original, all non-Newton member payloads are unchanged, and only the Newton member plus archive symbol index differ. Original/replacement Newton member hashes: `3d43011e7f9a62c43e1e62851dff4d05b5cae37e019d431c7b1886a172b3dba7` / `075332612ed11e71c5bcb4896874508001a7db0b95c18c1eaa5ddd0da4cbc278`.
- Paired executable SHA-256: `a4d35c2fb0791481b73d58f691aca1538274ca9a0533f4caec24acbbfd083f2d`. Both input arms share namelist `7c55d64c7854fa70f31332baf68f574d75fc2ed22aa3b1588166d8031559b80a`, `wrfinput_d01` `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, and diagnostics `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`.
- OFF / ON `rsl.error.0000` SHA-256: `5fe0495c384166b38b8717960057aece1b408da64b2ebf38d4ae266aff84450e` / `f973c1184b45a2b75cdb29798e58f693b0adc92cdb1df02945b4a82cd83f3a27`. OFF terminal JSON/PT: `6b444bef27e4268a44a916df02bca46722bcf7ca3c639479bf0b21b85ba39647` / `0d2a39d1386c5a70050eacae9c66193325560dbe4e80ae04c6f032b589cc1615`, both identical to the original fresh dt=1 baseline. ON terminal iter16 JSON/PT: `05c1ab3a65716b2bec7ee03f9f7e7f7d1321ed2a171a715b675fa44816adce07` / `308f527fe4600c161254ca25b927341b9d54587330347ee66ec7aea34efbd20d`. OFF and ON initial-only `wrfout` files are byte-identical, SHA-256 `656183b864d523a3cf8223f9113979bf33252e558d5786bd9b43581666571b11`.
- Graphify focused pre/post used exact-source copies of the Newton solver, snapshot/layout/trust/config files. Graph counts were 208 nodes / 516 edges before and 209 / 1,406 after; post graph SHA-256 `1a612b223067a9a807c63422539777ad01ee4d5787f54dbd123a5f19e4b0d77b`. Graph relationships were used for navigation only; source and run receipts establish the claims above.

No production code outside this detached worktree was modified, and no commit or PR was created.
