# Option 2 scalar stage metric and signed-eta closure

Local timestamp: 2026-09-27 00:58:18 JST

## Context and change

This isolated branch starts at PR #243 source HEAD `a1d5f6a3c8c64e66510c81d5fbe98ce568ed180f`. Step 9 of `computeUnifiedRHS` builds `option2_stage_rdz` from the current RHS geopotential. The option-2 vertical scalar consumers previously omitted that tensor and read cached `rdz_3d_` instead. WRF `vertical_diffusion_s` uses the current RHS `rdz` in its H3 flux. WRF eta decreases with k, so Fortran `dnw` is negative; this solver stores `|rdnw|`, which requires a negative sign in the flux divergence.

Both scalar helper overloads now accept an optional stage `rdz`. An explicitly supplied metric must match the scalar/W-level shape or the helper fails closed. The tensor remains attached to the current state graph; the solver cache is not modified. Option-2 stress and simple scalar branches pass the stage metric to theta and all active scalar species. For native option-2 with `km_opt=1`, both branches use `xkhv=3*kvdif` when no scalar K was supplied. The helpers restore the Fortran eta orientation locally with `-g*ΔH3*|rdnw|`. Option-1 retains the no-stage metric fallback and its existing coefficient fallback.

## Numerical contract

The test fixture uses the actual RHS ON−OFF difference, `khdif=1`, `kvdif=10`, nonzero vertical theta signal, and a quadratic-in-eta PH perturbation that changes current layer widths while cached base `rdz` stays at `2.0e-4`. It runs physical and packed periodic extents and exercises both option-2 stress and simple scalar branches. The expected source-equation operator transcribes the H3 and tendency equations in `dyn_em/module_diffusion_em.F:4865–4892`; its raw result is converted by the fixture's layer mass after the `rk_addtend_dry` scalar map conversion (unit map factors). This is a source-equation oracle in C++, not a call to a compiled Fortran routine.

The predeclared FP32 budget is `128*epsilon*signal`, with no absolute floor. Across the four option-2 cases, patched current-stage errors are `4.55e-13` to `9.09e-13`, below budgets `1.14e-10` to `1.21e-10`. The preserved cached-metric control misses by `6.30e-8` to `9.73e-8`. The stress path raw signal is about `0.4813`, with normalized signal `7.95e-6`; its current stage metric interior differs from the cached metric by up to `4.60e-6`. The fixture's unweighted theta-work sign is included only as a supporting sign discriminator on this prescribed profile.

An additional actual option-1 RHS smoke case sets `khdif=0`, `kvdif=10`, flat unit maps, zero velocity, and monotone vertical theta. It produces a finite signal `4.44052e-6` with negative fixture `q·Lq=-7.97391e-4`. A direct shared-helper source-equation case gives signal `0.15696`, error `1.49e-8`, and a wrong-sign mutant separation of `0.31392`. These checks cover option-1 sign behavior, not full option-1 Fortran parity.

The first provisional comparison used positive `dnw` and compared opposite signs. Its apparent stage mismatch (`expected=-7.95e-6` versus observed `-2.47e-6` in the simple branch) is retracted: it used the wrong eta sign and that branch's then-existing `Kv` fallback instead of canonical `xkhv=3*kvdif`. The final comparison uses WRF's signed negative `dnw`; both option-2 branches now use `xkhv=3*kvdif`. The stale metric and old-sign controls remain explicit negative controls.

## Validation

- `Scalar_Diffusion_Contract` passes after the final test-only option-1 RHS addition.
- Full CTest passed `103/103` immediately before that test-only addition, with final production source and archive. The focused contract was rebuilt and passed after the addition.
- Production Make checks passed after final production edits: `check-core-manifest` (24 sources) and `check-core-archive` (exact 24-member parity).
- A fresh one-step 60 s PC2 and RK3 `test/em_b_wave` run used the archived #244 input and namelists. Both runs report `SUCCESS COMPLETE` with finite counts unchanged. RK3 outputs are bitwise identical across U/V/W/PH/T/MU. PC2 has material one-step field changes versus the clean #244 reference: max absolute deltas are U `3.34e-6`, V `1.84e-5`, W `3.082e-3`, PH `1.4693`, T `6.433e-2`, and MU `1.812e-5`. The change is therefore not presented as accepted forecast parity; no longer-duration run was performed.
- Candidate PC2 runtime was 1.66 s wall / 0.89 s user; clean reference was 0.97 s wall / 0.83 s user. RK3 was 0.56 s wall in both. These one-step times are variable and do not support a performance claim.

The WRF executable is a mixed validation artifact: this branch's candidate C++ archive was linked into a new executable using unchanged Fortran objects and `libwrflib.a` from clean worktree HEAD `1b99eb7a6b8aa8b5b21d2fe6d6527acd81a203c7` (#244 clean build). It is not a clean full WRF rebuild of this branch. The final #244-plus-scalar stack still needs an exact-head rebuild/relink and Red review before integration.

Red's final review found no P0/P1 blocker in this bounded change. It confirms the option-1 active-RHS sign smoke and scopes the oracle as a hand-coded source-equation extraction rather than a compiled Fortran call; full option-1 Fortran parity remains open. The intended integration target is #244 HEAD `17b505c`, which still requires its own exact-head build and checks.

## Provenance hashes

| Artifact | SHA-256 |
|---|---|
| Candidate C++ implementation | `cc4f4f955be6afd921c4391d974cf5f2a796388a726d5f3f27aeea3232ee191b` |
| Candidate header | `534b59b3fc5ef66c5d696c006c698d26abb95936321ecf15c9c70eafd9ad8867` |
| Candidate scalar contract test | `b5e8e800569f39ae5117de3440ebdfa18ca3f63ebb93ee5e77283f2ca456fd48` |
| Fortran `module_diffusion_em.F` | `c044c533e5e1e9d168418f2b72feba62964bee6a0f55e211c2522ffceaa6b7ea` |
| Fortran `module_em.F` | `fb424486dbf9c903f77da6a15baf35e6772847e786b18d0297139eb48f0d586e` |
| Candidate Make archive | `1f798cdbf43738be75ea3a87e986af2583d1f4e54f10b363114447713dfb36f4` |
| Clean #244 `libwrflib.a` from unchanged Fortran objects | `bff3b8ca0f214028ac00a3b5d72b172915de02a4ec86ab1872d837803ee9217b` |
| Clean #244 C++ archive replaced during link | `01c4faf1dbd58951032d855d8661128352a16d2adae8a72aa7079ef1fe61eade` |
| Final full CTest log (`.validation/scalar-stage-metric/ctest_final.log`) | `a8ceeaa5a86535d80f0972c68e44af63e003800d618c5fa5846a6d92fd2bda39` |
| Final focused run transcript (`/tmp/stage-scalar-run-option1.log`) | `cf8b6afbb34ffb91e7533dc2194b2d3a2da7410db25b2c349f39b32238dc886e` |
| Candidate WRF executable | `979a420be38ec691858b00c3f5eb24368ece03158fd50ed8b4dbbf43aae1c7fd` |
| Candidate WRF link command | `d30a40efeabadcbaeba5f021ccab9737d28cb74998c616c40753fa999cd71e3b` |
| Candidate WRF link output log | `62033386ca4e9ccaad66eb633423326620df6fdfe3478187c88fd34053f9f277` |
| Candidate PC2 input `namelist.input` | `9a5d8c0d2972c096100a6878cb41d4825ff26313707da3ed7fd26f553ccba850` |
| Candidate PC2 input `wrfinput_d01` | `e71b730a12e5a16d181f404f6314e7904e0165eefa622ffd0c508bf8c00dd2a9` |
| Candidate PC2 `wrfout_d01_0001-01-01_00:00:00` | `8d9e012fa0062c9696df904b72d5add4f05c7b2a73cdb59a81a0be0df5e482e9` |
| Candidate RK3 `wrfout_d01_0001-01-01_00:00:00` | `bcb32fdddb10fa2756e7bc88cac2c9da05664ed34cebac05ae68d55b00f5c336` |
| Candidate PC2 `rsl.error.0000` | `3e1fa2d2605ebbb42f4142c5e8c5a81f3beb73984253ed4d24d9299fa9db9038` |
| Candidate RK3 `rsl.error.0000` | `2ae4936cb4ac1c961fb157f1ded5913d2de048d28d8be047921e0a110d5bcab1` |
| Candidate PC2 `run.log` | `7421519cd8f4262d1ae5c40797971465539a7bd4263fcd5e45d5b2d9c65257e5` |
| Candidate RK3 `run.log` | `d5678c800a44f1c888ffd2f46b91038893b45770acdf190c8a5aa94681f1e202` |
| Refreshed Graphify AST graph | `fe78ac062faacbfbfc688e730ec107b14f7eaa52691dfcb22c78ad951fe70d7d` |

## Remaining scope

The PC2 field deltas need an independent Red assessment and a longer run before any forecast-quality claim. This work closes only the current-stage metric and eta-sign path for dry option-2 scalar diffusion; it does not establish complete RHS parity for U/V/W or all coupled scalar species. Option-1 full Fortran parity also remains open beyond the actual-RHS sign smoke and shared-helper amplitude contract.
