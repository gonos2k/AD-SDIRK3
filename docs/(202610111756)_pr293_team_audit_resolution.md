# PR #293 fresh Green/Red audit and resolution

Local timestamp: 2026-10-11 17:56:35 JST (+0900)

## Context and findings

The audit reviewed PR #293 commit `229cb923cf9ff79b3e44351d8eaf8d7426fc59e6`
against clean main `35037bd2093e17beb23d27857a6d9c81ce08aed0`. Green independently
checked the ARK stage/root derivative, supported-path limits, rotating source/state/
control dimensions, observations and boundary claims. Red checked the completion
logic, scope, evidence lineage, links and actual changed files.

**P2 found and corrected:** the opening and next-action paragraphs withheld
completion until a performance criterion was fixed, although the closing paragraph
made efficiency conditional. The [canonical definition](PROBLEM_DEFINITION.md)
and root README now consistently require a declared forecast window and field-error
budgets. Efficiency is an optional target not adopted by this document; its
same-accuracy cost/timestep criterion becomes a gate only if explicitly selected.

The root README also now says the Newton matvec `v - h*gamma*Jv` **uses** an RHS
JVP `Jv`, avoiding the implication that the full residual matvec is the RHS JVP.
The production equations and their implementation are unchanged.

## Validation and remaining boundary

Before this documentation correction, all required GitHub checks on `229cb923`
passed: `fast-contracts`, `core-linux` and `required`; the diagnostic-only WRF
accuracy job was intentionally skipped. These results do not certify the updated
commit until that commit's checks finish. Local checks verify the new documentation
links and whitespace, and only Markdown files are changed.

Final Green and Red re-reviews both accepted the corrected wording and found no
remaining blocker within this documentation scope.
The forecast window and physical error budgets remain deliberately unselected,
not accidentally omitted. The completed two-control rotating inverse/forecast
remains closed. General integrated WRF accuracy, full observation-window adjoint
and an equal-accuracy cost advantage remain unverified at project level.

No source/configuration or test was changed. No model build, numerical integration,
or new same-setup WRF/RK3 field/runtime comparison was performed in this audit;
existing reports and source contracts were reused. The next action is to select
the mandatory window/error instance, then address its first missing evidence gate.
