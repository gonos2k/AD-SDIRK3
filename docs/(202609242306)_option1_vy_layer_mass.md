# Option-1 Fortran V-Y layer-mass repair and remaining checklist

Timestamp: 2026-09-24 23:06:07 JST (+0900)

## Context and numerical decision

This candidate starts from PR #225 head `e5378f3af1157eb4ab99c6d968ae6fe96a6afc29`, whose exact-head CI run `36006871877` passed. In WRF's `diff_opt=1` Fortran `horizontal_diffusion`, U-X/U-Y, V-X and W horizontal flux coefficients include the hybrid dry layer factor `L_k=c1(k)*MUT+c2(k)`. V-Y alone multiplied only physical diffusivity `xkmhd` at its north/south mass faces. The same omission is present in the official WRF v4.7.0 source, so this is an inherited source defect, not a regression caused by SDIRK3.

`xkmhd` is passed unchanged as a physical diffusivity for all components; V-X in the same routine multiplies it by `L_k`. V-Y accumulates into the same **mass-coupled** `rv_tendf`, so omitting `L_k` gives different units and a direction-dependent operator. The corresponding normal flux coefficient should be `L_k*K_h` at the south/north mass point, just as U-X uses `L_k*K_h` at its west/east mass point. The authoritative `dyn_em/module_big_step_utilities_em.F` now includes this factor in both V-Y face coefficients. Generated `.f90` was not edited. This is an intentional physical-consistency correction of upstream behavior; source compatibility with unmodified WRF is tracked separately and must not be confused with the intended equation.

The focused Graphify corpus had exact file-hash parity with the target worktree before editing; its relationships were used to navigate the caller and downstream consumers. Mathematical units and compiled source-extracted comparisons, rather than graph edges, support the numerical decision.

## Independent operator test

`tools/test_option1_momentum_diffusion.py` extracts and compiles the actual Fortran `horizontal_diffusion` routine. A flat 8×6×4 unit-map fixture prescribes positive `K_h=2`, two different `MUT` values, level-varying `c1/c2`, and independent U-X and V-Y modes. U-X is a periodic sine; V-Y is a no-flux Neumann cosine with even-reflected Y ghost values. The exact **discrete** eigenvalues are `-4 sin²(pi/Nx)*rdx²` for U-X and `-4 sin²(pi/(2Ny))*rdy²` for V-Y. The independent expected coupled tendency is `L_k*K_h*lambda*q`; the test also checks finite values, resolved nonzero signal and negative discrete work.

The modified source passed default REAL and REAL64 at both `-O0`/`-O2` with `-fcheck=bounds`. Across the two mass cases, the largest FP32 U-X discrepancy was `1.22e-3` against a `2.25e-2` budget, and the largest FP32 V-Y discrepancy was `1.53e-4` against a `5.05e-3` budget. REAL64 maximum discrepancies were below `5.7e-13`. The saved transcript SHA-256 is `b49d85cba8d0ec6f0dde1c8277f58b7454a84a2ce41be36df4f80344b5385e8f`.

The prior source passes U-X but fails V-Y numerically by about `225.825` against a FP32 budget `0.00345`; each one-line reversion of `mkrdym` or `mkrdyp` also fails V-Y, with maximum discrepancy about `421.4`. These are compiled numerical negative controls, not syntax failures. Their logs are retained in `.validation/option1-vy-object/`.

## Same-setup WRF evidence and limits

GNU Fortran compiled the changed authoritative `.F` into an isolated object using the archived build's compiler family, optimization and module files. Its SHA-256 is `463af4d2618fa81bee76c62e886034689b1e97a751a577018c63be7a7a591680`. Replacing only that member in a **copied** archived `libwrflib.a` produced archive SHA-256 `e56f7045491b12c06e43024e5f1048a5c52888f00978ad0468402b6dcc498505`; relinking the unchanged #225 C++ archive produced executable SHA-256 `09dff4d68f2f76e6b98339f36599c2dca1b5ca105eabfe9fda43be053b0819d9`. This is an isolated affected-object rebuild and incremental WRF relink, **not** a clean complete WRF build. The generated `.f90` and Registry products were not regenerated in this local run. The checked-in 2011 Tapenade `wrftladj/module_big_step_utilities_em_tl.F` and `_ad.F` also retain the old V-Y formula and omit the new MU derivative/cotangent; they were not edited or qualified here. Consequently this forward correction is **not** Fortran TL/AD compatible until those paths are regenerated or consistently updated and independently tested.

With the archived `em_b_wave` input, one-rank/one-thread RK3 at `dt=60 s`, `diff_opt=1`, `khdif=1000`, `kvdif=0` completed one step for both old and corrected executables. The original test case has `V=0`, so the output files are byte-identical (SHA-256 `0243218d5fd0704e95f98d5231700d30bf2012a6bf9152fa5d191750a459f75b`). This is a useful negative control, not activation of V-Y diffusion.

A second same-setup diagnostic copies the same `wrfinput_d01` and adds a 1 m/s sine V mode across the V-staggered Y points, zero at both physical Y edges; both old and corrected RK3 runs completed, and all 174 output floating fields were finite. The modified input SHA-256 is `982f1c6ea151980be14fe855fa12454e4f70e12dd0a7d13b584ea5770569fc38` in both runs. Old/new output SHA-256 differs (`22c636e282ebbd4a6c47e906d4b35abe05879e1a7d5f8682f6e2cddda5e1f052` versus `c697ef3eca0cfab5efec2cc616391e5443f4f6bbb44f98ffba66295d2a9b783c`). Final-frame RMS differences in output units are U `6.90e-8`, V `1.73e-7`, W `4.76e-7`, PH `1.04e-4`, T `6.74e-7`, MU `3.34e-6`. This shows the changed Fortran object reaches a live forecast path, but the short difference is not a forecast-skill measure. The 100 km grid and one-step integration make its physical V diffusion effect small.

No same-source SDIRK3 option-1 forecast was produced: its existing stage-2 GMRES true residual stalls near `0.07` before the first completed step. Therefore RK3-versus-SDIRK3 field/runtime accuracy, active C++ U/V/W option-1 parity, temporal order, and whole-model adjoint remain open. The archived option-2 RK3 comparison is a different spatial operator and is not reused as a baseline for this option-1 correction.

## Checklist status

| ID | Status after this candidate | Remaining closure condition |
|---|---|---|
| I0 | Open | Integrate reviewed stacked PRs into the source used for qualification; then bind source, executable and input hashes. |
| H1/S1a, O1, S1b-θslow, F1-flat | Earlier candidate results | Integrate; do not extend them to whole-WRF budgets. |
| F1-hybrid-1 | Candidate verified in #223–#225 | Scalar layer mass, stagger maps and nonuniform base on tested complete tile; broader tile/BC scope remains. |
| F1-U/V/W-opt1 | **Open.** Fortran V-Y physical coefficient candidate repaired here. | Dispatch C++ U/V/W to the actual coordinate-surface operator, compare source-extracted U/V/W on the same state, and complete WRF active-case validation. The current C++ stress helpers remain different operators. |
| F1-opt2 | Open | Reproduce `horizontal_diffusion_s` rho/metric/map/terrain operator and its coefficient/halo input contract. |
| S1b-θall, S1b-mom | Open | Accepted fast/source and staggered momentum face budgets. |
| G1, K1 | Open | Partial-tile/MPI boundary equivalence and diagnosed-coefficient derivative policy. |
| T1 | Open | Diagnose stage-2 PH-dominated GMRES plateau; then active time refinement and same-accuracy RK3/PC0/PC2 cost. |
| A1 | Open | Update/validate the Fortran TL/AD V-Y dependence on MU and K, then whole-WRF active-diffusion/observation-path adjoint and objective Taylor/dot checks. |

The direct next development unit is the option-1 U/V/W C++ dispatch, preserving option-2 stress handling. For V-Y, the corrected Fortran equation is now the project reference; compatibility with the unmodified upstream formula should remain an explicit legacy comparison, not be silently treated as physically correct.
