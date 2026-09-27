# V mass-basis candidate WRF receipt

Recorded: 2026-09-27T22:44:53+09:00 JST

## Source and candidate-first relink

The candidate source base is merged PR #248 HEAD `fb025cd40dd7f3b288cce6a7d6a3249e8faabbc8`. At WRF link/run start and finish, the working source snapshot had `git diff HEAD --binary` SHA-256 `3076d4f9a8bb777c08077728dcc0de7d28ad8bd8e2cb94124dcf3c627fe6d872`. The production `wrf_sdirk3_tile_unified_impl.cpp` SHA-256 is `a34900fc4377edce9daa310a90afb638a5abf529ce005d7cd399de3d006550dd`. Subsequent post-run cleanup changed only documentation/test files; production implementation, archive, and WRF executable hashes remained unchanged.

The candidate archive is `external/libtorch_wrf/sdirk3/libwrf_sdirk3_libtorch.a`, SHA-256 `596970bb4cc497279bcbcaaa5b9ebedf2c694a40b735e9fff92e354d3b151fd9` (24 object members plus macOS archive index). The author reports it was rebuilt with `LIBTORCH_ABI=0`; the raw Make compile/configure command log was not retained. The final link/runtime compatibility was exercised by the successful one-step WRF cases below. It was candidate-first relinked against the preserved #246 clean WRF objects under `/private/tmp/sdirk3-u-fullrhs-on-pr246/.validation/integrated-u/wrf_objects/`. The copied `wrf.o`, `module_wrf_top.o`, `libwrflib.a`, `module_internal_header_util.o`, and `pack_utils.o` match their #246 clean-worktree counterparts byte-for-byte.

The link command selects `-L/private/tmp/sdirk3-v-step10-massbasis/external/libtorch_wrf/sdirk3 -lwrf_sdirk3_libtorch` first and explicitly includes that same candidate archive after the reused WRF objects. It does not select the previous #247 archive. Candidate executable SHA-256 is `693dd198ad7a0ec91f21fb774c8fde853a31ff539b876fa5034ae7ca49c687ee`. Link command/argv/log SHA-256 values are `d055a02585408651d1a1775eb9050437e12ac587fcbce85669a5b0b7be696011`, `3ad42fd5fdae4be04f7c3f9ea5f0786e68b2d9ea30c27607030831a1f0d5f1ca`, and `62033386ca4e9ccaad66eb633423326620df6fdfe3478187c88fd34053f9f277`. The post-cleanup full CTest passed 107/107; final log SHA-256 is `88cf4aac7fedb338c4c4acd6dc884127100701d5f6739c408015561134c9e16d`. The post-cleanup test-source hash reported for `test_scalar_diffusion_contract.cpp` is `e822b0245fbe5a0376d5181a88643de917b9947a87759c019c31dfd822d1437f`; the WRF implementation/archive/executable are unchanged.

This is an incremental WRF relink against the verified #246 object set, not a new full WRF build. The prior #246 executable is `1ed66f384917cb22ea60d8decdb4bbe9e756da423d75ad12e9bf6f7a6ca042af`; the prior #247 candidate executable is `1d9f6c3fea9c5908005442b13024115451d3a5c5db91456bbbeba6c212fb7771`.

## Same-setup WRF controls

The K>0 pair uses archived `wrfinput_d01` SHA-256 `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9`, `diagnostics.txt` SHA-256 `bc5c198d7f033fa8cd5d5b9bcbe2a46a2eae7d69ea8dd414d18f0cc5dc62b224`, and `input_jet` SHA-256 `9f4cdc9e55c316df8164d6cde4dc65c91e55927e43339d0addcc1a938966aba6`. The one-rank setup is dt=60 s / 60-second duration, 17×17×17, `diff_opt=2`, `km_opt=1`, `damp_opt=0`, `khdif=1000`, `kvdif=10`. The exact archived PC2/RK3 namelist SHA-256 values are `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850` and `86d1ee65708470aa4c1683a34131f6e249d03a574678f5c7f6ca03ef55bca8f2`.

The K0 control is PC2 with both `khdif=0` and `kvdif=0`; its namelist SHA-256 is `505e6f98de36beb7c990ba9a64f10429e69e5e38a5ed80e83fd5abbfccc1e0d3`. Input and diagnostics hashes match the K>0 pair.

| Case | Candidate output SHA-256 | Clean #246 / #247 reference | Candidate `Timing for main` |
|---|---|---|---:|
| PC2, Kh=1000 Kv=10 | `1a4d48818597764938777d70290bd9a847a56d8003ece43b1fd07eec5649b144` | byte-identical to #246 and #247 | 0.51866 s |
| RK3, Kh=1000 Kv=10 | `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336` | byte-identical to #246 and #247 | 0.07971 s |
| PC2, K0 | `359737a47eac4b0fb7c4031dc216d32e165fc0a16f2438492a7ee064f3c0cd77` | byte-identical to clean #246 and #247 | 0.48974 s |

Additional comparison recorded 2026-09-27T22:47:55+09:00 JST. For the candidate K>0 endpoints, PC2-minus-RK3 float64 RMS / maximum absolute differences are:

| Field | RMS | Maximum absolute |
|---|---:|---:|
| U | 0.00209011943778 | 0.0106077194214 |
| V | 2.21139003568e-5 | 9.93933063e-5 |
| W | 0.000665487492008 | 0.00167457456701 |
| PH | 0.306797021925 | 0.94140625 |
| T | 0.0103997918347 | 0.0304565429688 |
| P | 0.039813248637 | 0.131130218506 |
| MU | 0.0415027193922 | 0.144721031189 |

These are same-input, one-step scheme differences, not errors against truth or a convergence-order estimate.

All three runs reached 00:01:00 and logged `SUCCESS COMPLETE WRF`; each output has 174 floating variables, 378,352 values, and all values finite. Exact byte identity means zero field differences from the corresponding references. Main-step timings are descriptive only. No RK3 K0 run was requested for this U/V momentum-diffusion control.

Raw executable, link receipts, namelists, inputs, logs, and outputs are preserved under `.validation/v-step10/wrf_candidate_final/`. This evidence covers only the tested one-rank 60-second case and does not establish long-run forecast quality, convergence order, MPI scaling, or adjoint behavior.
