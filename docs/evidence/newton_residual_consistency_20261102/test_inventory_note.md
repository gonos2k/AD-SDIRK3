# Test inventory and retained evidence

The first Trust execution ran 100 checks but failed the fixture's stale expected count of 96. The failure is retained in `initial_test_trust_model_contract_run.log`; the count was corrected to 100 and the next execution passed 100/100. A later test-only source update added one multi-tile halo-direction check and updated the expected count to 101; `trust_halo_run.log` reports 101/101. The count changes account for added checks and do not relax their assertions. The latest Trust test receipt identifies its source, executable and linked archive independently of the earlier core build receipt.

The separate returned-residual test passed both adaptive settings against the same linked archive. Its retained log reports returned and expected residual 0.215316 versus initial 0.507358 for each. These are manufactured single-stage checks, not evidence of broad forecast convergence.

The h=600 native execution and numerical validation are limited to one research-feature configuration. The runner's original pending result is retained, and the separate validation records the subsequent comparisons. This does not establish default-configuration behavior, long-forecast stability, or a performance claim. No model run or compilation was performed as part of this identity audit.
