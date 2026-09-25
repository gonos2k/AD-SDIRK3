# Active dry option-2 scalar RHS increment derivative

Timestamp: 2026-09-25 13:30:37 JST (+0900)

## Context and change

This test-only candidate is stacked on PR #230 head `2769fd08927b95a3ea242914b508cdb2abc39363`. The parent connects the source-matched option-2 scalar metric helper to a bounded production Step 9 path. A whole-step dot product can be dominated by other dynamics when the diffusion increment is small. The existing scalar contract therefore now differentiates `H(q)=RHS_K(q)-RHS_0(q)` itself, using the supported canonical, dry, `km_opt=1`, `damp_opt=0`, one-tile `ExplicitOnly` RHS.

Theta, PH and MU are perturbed separately with physical units; the packed directions are lifted through the same periodic-X/symmetric-Y copy map as the production state. Every ON/OFF and central-difference evaluation constructs an identically configured fresh solver. K, coordinate coefficients and maps are held fixed. The PH direction changes `rdzw`, `zx` and `zy` individually, so the active geometry path participates in the derivative; the MU direction exercises current-state density and the final layer-mass conversion. The objective contracts the physical theta increment with the same cotangent for Torch VJP and finite differences.

## Validation

After rebasing this test onto the final #230 source, Apple Clang/libtorch rebuilt the affected test target and `Scalar_Diffusion_Contract` passed 1/1. The direct final transcript is `.validation/option2-vjp/vjp_final.log`, SHA-256 `775a02d8a66fda6660589f00c9572a06380a40ef1006d3a4b4518f380e6c71ee`. Both central steps `h=0.02,0.01` and a Richardson estimate are checked against the VJP with an FP32 roundoff floor and a 2% relative signal allowance.

| Direction | Physical VJP | Physical Richardson FD | Packed VJP | Packed Richardson FD |
|---|---:|---:|---:|---:|
| Theta | `−1.03748e-3` | `−1.03751e-3` | `−1.42756e-3` | `−1.42759e-3` |
| PH | `−2.32541e-4` | `−2.32532e-4` | `−3.19362e-4` | `−3.19331e-4` |
| MU | `3.56533e-4` | `3.56555e-4` | `4.56579e-4` | `4.56572e-4` |

The largest reported Richardson–VJP difference is `3.07e-8`, below the specified combined budget. Physical PH perturbation gives nonzero `d_rdzw=1.77e-5`, `d_zx=0.935` and `d_zy=0.414`; packed PH gives nonzero responses in all three metrics too. Green/Red review found no blocking defect in this RHS-local fixed-K/context test. The focused Graphify source corpus matched this worktree and was refreshed after the change to 730 nodes, 1,527 edges and 26 communities.

No numerical production code or WRF executable changed in this PR. The parent #230 provides the same-source `em_b_wave` PC2/RK3 one-step field and runtime comparison. Additional same-executable time-step refinement at fixed 60 s and 240 s final times is documented in the companion `(202609251326)_option2_temporal_refinement_observation.md`; those exploratory output-field ratios do not resolve third-order accuracy. This test closes only the **active option-2 scalar RHS increment directional VJP subcheck**. The converged ARK step, stage-reference context, diagnostic coefficient derivatives, boundary/packing transpose across tiles, MPI communication and observation-path adjoint remain open. A future attribution test can separately expose MU response through rho and through L; their combined forward/RHS derivative is the quantity checked here.
