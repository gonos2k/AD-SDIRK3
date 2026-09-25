# Option-1 active momentum diffusion increment derivative

Timestamp: 2026-09-25 10:21:30 JST (+0900)

## Context and change

This candidate is stacked on PR #227 head `ccd692d9b2ffc64ddb043288b6f759889bfa855d`. That PR establishes raw Fortran/C++ option-1 U/V/W operator parity and an integrated hybrid/nonunit-map RHS basis check. Its one-step `em_b_wave` execution is a sigma/unit-map case and cannot qualify derivatives through nontrivial hybrid layer mass. The remaining A1 item requires the actual active-diffusion increment to be differentiated, rather than accepting a whole-step dot product dominated by other terms.

The existing `Scalar_Diffusion_Contract` now tests `H(q)=RHS_K(q)-RHS_0(q)` at `K=1000 m2/s` with fresh, identically prepared solver contexts. One shared configuration is set before each fixture construction; only K changes after identical preparation. The test compares a Torch VJP directional product with paired central differences of H at both perturbed states. The directions separately excite U, V, W, MU-X and MU-Y; a uniform-MU base and perturbation check the expected layer-mass cancellation. Hybrid `c1h/f<1`, positive `c2h/f`, varying base MU and nonunit staggered maps make both axis-specific MU derivatives nonzero. K and the stage/reference context are fixed by this test. Cotangents select the matching momentum block for each wind direction and all three momentum blocks for MU.

## Validation

Apple Clang 22.1.4 and Homebrew libtorch built the affected core and test from this worktree. The targeted `Scalar_Diffusion_Contract` passed 1/1. Direct binary output from this revision is `.validation/option1-vjp/option1_momentum_ad_contract.log`, SHA-256 `66260fc10556e5826c9cc67d4956ab40e9fa5185f4cf8ace26b9e6d07cc81809`.

| Input direction | FD step sizes | AD signal | Maximum FD–VJP error |
|---|---:|---:|---:|
| U | 0.2, 0.1 | 0.616961 | `2.94e-8` |
| V | 0.2, 0.1 | 0.125362 | `3.44e-8` |
| W | 0.2, 0.1 | 0.517111 | `1.82e-8` |
| MU-X | 1000, 500 Pa | `1.80108e-7` | `8.03e-10` |
| MU-Y | 1000, 500 Pa | `−1.83440e-7` | `1.99e-10` |
| MU-uniform | 1000, 500 Pa | `−1.09e-10` | `1.11e-10` |

The MU-X and MU-Y finite-difference estimates resolve nonzero signals above the computed FP32 roundoff floor, and agree with the VJP. For nonzero directions the budget is that floor plus 8% of the AD signal; it catches a detached MU path or an order-one wrong sensitivity, but is not a strict bound on small relative derivative errors. On the uniform-MU case the expected derivative is zero; both FD estimates are near zero, and the AD value is below 1% of the smaller resolved axis signal. That relative cancellation budget avoids treating FP32 reduction noise as a structural error. No production numerical formula changed, so the same-setup RK3 field/runtime comparison remains the #227 one-step comparison; no WRF model run or new RK3 comparison was performed for this test-only change.

The focused Graphify corpus was copied from the verified #227 source snapshot, updated for this test change, and now contains 693 nodes, 1,474 edges and 23 communities. The graph is a call-path aid, not evidence for the VJP result.

## Scope and next actions

This closes only the **supported one-tile option-1 momentum RHS increment VJP/FD subcheck**. The exact historical Fortran full step, stage-context derivatives, active full-step/observation-path adjoint, coefficient diagnostics, other physical boundary conditions, and MPI/partial-tile transpose remain open. The Fortran V-Y source correction in parent #226 still has stale checked-in Tapenade TL/AD counterparts. Next compare the actual converged active step and its boundary/packing transforms, then address the separate option-2 scalar terrain operator and whole-domain budgets.
