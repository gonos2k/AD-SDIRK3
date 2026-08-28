#!/usr/bin/env python3
"""Every rule in wrf_sdirk3_first_failure.h must have a consumer that is not a test.

Three times in three increments this repository shipped a pure rule with fixtures and no
production caller: the pins guarded a copy of the logic while the logic that runs went
unexamined, and in one case (`gmres_total_failures` -> KrylovStagnated) the unguarded copy was
the defect the rule had been written to fix.

A rule is satisfied by EITHER a call from production, OR a call from another rule in the header
(an internal helper is legitimately consumed). A rule with only fixtures is reported.

Exit 1 on any orphan not in the allowlist. Run: python3 .github/ci/lint_rule_consumers.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SDIR = ROOT / "external" / "libtorch_wrf" / "sdirk3"
HEADER = SDIR / "wrf_sdirk3_first_failure.h"
PRODUCTION = ["wrf_sdirk3_newton_solver.cpp", "wrf_sdirk3_tile_unified_impl.cpp",
              "wrf_sdirk3_config.cpp"]

# Rules deliberately without a production caller. Each needs a REASON, not just a name --
# an allowlist without reasons becomes a place to hide the next orphan.
ALLOWLIST = {
    # name: why it has no production caller
}


def main() -> int:
    hdr = HEADER.read_text()
    rules = sorted(set(re.findall(r"^inline\s+[\w:<>, ]+?\s+(\w+)\s*\(", hdr, re.M)))

    prod = "\n".join((SDIR / f).read_text() for f in PRODUCTION if (SDIR / f).exists())

    orphans = []
    for r in rules:
        if r in ALLOWLIST:
            continue
        if re.search(r"\b" + re.escape(r) + r"\s*\(", prod):
            continue
        # a call inside the header that is not the definition itself
        defn = re.search(r"^inline\s+[\w:<>, ]+?\s+" + re.escape(r) + r"\s*\(", hdr, re.M)
        calls = [m.start() for m in re.finditer(r"\b" + re.escape(r) + r"\s*\(", hdr)]
        internal = [c for c in calls if not (defn and abs(c - defn.start()) < 80)]
        if internal:
            continue
        orphans.append(r)

    print(f"rule-consumer lint: {len(rules)} rules, {len(ALLOWLIST)} allowlisted, "
          f"{len(orphans)} without any consumer")
    if orphans:
        print("\nFAIL: these rules have fixtures but nothing that runs calls them.")
        print("A rule with no consumer is a comment with a test: the pins guard a copy of the")
        print("logic while the logic that runs goes unexamined.\n")
        for o in orphans:
            print(f"  - {o}")
        print("\nFix by EITHER wiring the rule into the code that makes the decision, OR")
        print("deleting it if superseded, OR adding it to ALLOWLIST with the reason.")
        return 1
    print("OK: every rule has a production or header-internal consumer")
    return 0


if __name__ == "__main__":
    sys.exit(main())
