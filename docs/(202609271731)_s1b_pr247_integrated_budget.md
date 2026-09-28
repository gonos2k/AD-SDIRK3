# S1b factored endpoint budget on the PR #247 stack

Local timestamp: 2026-09-27 17:31:17 JST (+0900)

This test-only candidate is branch `agent/s1b_factored_endpoint_budget_on_pr247`, commit `f7d802d3952a6c83fae0488d6a76f12a69b25297`, on exact draft PR #247 HEAD `702471e7aec5a27056ef8029326a179043145c2c`. The only changed code is `external/libtorch_wrf/sdirk3/tests/test_full_tile_step.cpp`. The authoritative ARK update source and production tile implementation remain byte-identical to #247. The original S1b report, `(202609270622)_S1b_factored_endpoint_budget.md`, records the derivation and predeclared FP32/FP64 arithmetic bounds; this addendum records the result after stacking it on the U-rate correction.

The existing closed, nonuniform hybrid/map fixture and its timestep/state were retained. Accepted-stage mass and theta face/source receipts, boundary-flux checks, raw ARK update replay, and final boundary projection still run before the new assertion. The assertion separates the observed sequential FP32 linear update residual from the bilinear endpoint product term. It requires the factored identity to meet its baseline-free FP64 gamma bound and a bilinear-omission mutant to exceed that same bound by 100 times. This establishes that the test can distinguish the omitted product term; the algebraic identity does not by itself prove the physical stage RHS budget.

On the exact stacked source, `Full_Tile_Step_Adjoint` passed 1/1. Its values were `D=-9711.85`, expanded `D=-9711.85`, identity error `2.36832e-9`, bound `9.55995e-5`, linear FP32 contribution `-9711.89`, and bilinear contribution `0.0420821`. Omitting the bilinear term gave error `0.0420821`, or `440.192` times the predeclared bound. The signed aggregate FP32 update residual met the summed per-component gamma bound; the test reported maxima `8.38139e-7` for observed residual and `1.90736e-6` for the bound. The older unstacked report's `D=-9712.04` belongs to its earlier production source, not this PR #247 stack.
The existing whole-field `hybrid_budget` subtraction gives an endpoint diagnostic about `0.02` different from this factored `D`, because it subtracts large baseline products. The tight gamma bound applies only to the two factored forms and is not an error bound for that global subtraction. This result does not tighten the accepted whole-step theta drift tolerance or establish physical conservation.

| Artifact | SHA-256 |
|---|---|
| Stacked test source | `88762e231137de95325cf18c4d5f1789c8f3ad9bfa4e7e3ce356dc2e7d9d3db5` |
| Unchanged production tile implementation | `cbf9dcbbbcb4ae7be00b0da5680d96020df141fd56613e3b3b053c3ec75f22f3` |
| Unchanged ARK composition source | `8ebf4d680c79a0103522b68c278f5145e2c2dd1c5007fd17e684014411a2a907` |
| Homebrew Torch test executable | `02e81238a519793dd149c50be73de6a373d2031799c554eab43b6c5821c2f147` |
| Targeted CTest log | `1e69bf90cc0b36c79f5b13325879e1b123a0f859339cbd35f8737c58b99c39a9` |
| Refreshed code-only Graphify graph | `190e9be87f1bb34898ef584e0d1e5b32af86ac7caea8f9715a9cbf9d52e276c2` |

Graphify was checked before the cherry-pick and refreshed after it. Its code graph links `check_governing_step_budget()` to the full-tile test; extracted call direction is a navigation aid, not numerical proof. The first generic Graphify `--update` attempted semantic extraction of dated docs without an LLM key, so `graphify update .` was used for a code-only refresh. The source formula and ARK update order were verified directly in C++.

No WRF build, `em_b_wave` run, MPI run, or RK3 field/runtime comparison was repeated for this test-only change. The last same-setup PC2/RK3 one-step comparison is the PR #247 validation at the unchanged production source: both candidate outputs were byte-identical to the clean #246 controls, with finite fields; this does not establish time order or forecast acceptance. An independent Red audit confirmed the algebra, replay order, stage receipts, and the stated scope. Full CTest and exact-head CI on this stacked branch remain the next gates before a PR.

Checklist status after this change: the S1b endpoint product *identity and omission-detection check* is closed for the declared closed tile fixture. General physical conservation with varying area/eta, boundary fluxes and sources, full V/W/scalar RHS parity, G1 decomposition, K1 coefficient derivatives, T1 time accuracy/stability, and A1 complete active-step adjoint remain open.
