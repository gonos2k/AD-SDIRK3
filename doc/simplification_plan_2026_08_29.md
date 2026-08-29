# Simplification plan — reviewed three times before touching anything

**Trigger** the user's question "is the pipeline over-complicated?" · **Verdict** yes, measured.
**Independent review: NOT RUN.**

## The measurements

| | value |
|---|---|
| `first_failure.h` | 160 lines (2026-08-22) → **1,746** (7 days, 11×) |
| comment / code | **905 / 758** |
| comment blocks carrying campaign narrative | **95 blocks, 744 lines** |
| session-era mechanisms firing on the shipped dt=600 run | **2 of 11**; the other nine are opt-in or on paths this configuration never reaches |
| classification fixtures | 235 checks / 2,247 lines |
| dt=600 status | `newton_budget_exhausted` — **unchanged by the last five PRs** |

The pattern: review → real defect → fix → self-review → defect *inside the fix* → gate → defect
in the gate → ratchet on the gate. Every step locally justified; in sum, more machinery guards
code that does not run than code that does. The user named it exactly.

## Three reviews

**1 — narrative comments.** Of 744 lines in 95 blocks: 112 read as current contract, 134 as
history, ~500 connective prose, and **55 blocks mix the two**. A regex delete was measured:
it strands **483 lines mid-sentence**. Hand-edit only. Principle, verified on the four largest
blocks (132 → 51 lines): keep decision tables, reachability lists and measured numbers; drop
"R13.N missed it, R13.M fixed it".

**2 — production monitors on unreached paths.**

| | verdict |
|---|---|
| `SHORTCUT_MODEL_MISMATCH` | **removed** — a runtime re-check of a three-boolean identity the fixtures pin |
| `RETURN_PAIRING_VIOLATION` | **removed** — guards a four-line site that sets both values together |
| `LIFECYCLE_FLAG_MISMATCH` | **kept** — the one class that recurred five times in a week |

**3 — fixtures, lint, ratchet.** Two enum-identity checks dropped; the rest pin real rules. The
lint's coverage ratchet earned its place (it caught my own `[[nodiscard]]` edit) — kept, but four
per-header counts became one aggregate. Checklist docs stay as the audit trail; no more
per-round self-review appendices.

## Rule for the next review cycle

A finding on a path the shipped dt=600 configuration does not execute gets **the fix and one
fixture**. Contracts, receipts, monitors and ratchets are reserved for the default path.

## Execution log

- **Step 1** — monitors, rules, struct, 11 fixtures removed; ratchet consolidated. −184 lines.
  ctest 62/62, telemetry byte-identical.
- **Step 2** — header narrative rewritten block by block (in progress; the four largest first).
