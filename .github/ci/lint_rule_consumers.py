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

    # R13.25 SELF-REVIEW: the first version of this pattern required the return type to be a
    # word, so it silently skipped every `inline const char* foo(...)` -- nine real rules -- and
    # reported "31 rules, 0 orphans" as if that were the whole header. A gate that overstates its
    # own coverage is the defect it exists to catch, one level up. Count what is SKIPPED too, and
    # ratchet it, so a future declaration style cannot slip past unseen.
    decls = [(i + 1, ln) for i, ln in enumerate(hdr.split("\n"))
             if re.match(r"^\s*inline\s", ln)]
    rule_re = re.compile(r"^inline\s+(?:const\s+)?[\w:<>, ]+?[\s*&]+(\w+)\s*\(", re.M)
    rules = sorted(set(rule_re.findall(hdr)))

    # `inline constexpr <type> kName = ...;` is a CONSTANT, not a rule: it has no body to bypass,
    # so "is it called" is not the question for it.
    consts = [n for n, ln in decls if re.match(r"^\s*inline\s+constexpr\s", ln)]
    accounted = set()
    for _, ln in decls:
        m = re.search(r"\b(\w+)\s*\(", ln)
        if m and m.group(1) in rules:
            accounted.add(id(ln))
    unseen = [(n, ln.strip()) for n, ln in decls
              if not re.match(r"^\s*inline\s+constexpr\s", ln)
              and not (re.search(r"\b(\w+)\s*\(", ln)
                       and re.search(r"\b(\w+)\s*\(", ln).group(1) in rules)]
    if unseen:
        print(f"FAIL: {len(unseen)} inline declaration(s) this lint cannot classify.")
        print("An unrecognised rule is silently exempt -- the gate would overstate its coverage.")
        for n, ln in unseen:
            print(f"  line {n}: {ln[:95]}")
        print("\nFix the declaration pattern above, or make it a constant if that is what it is.")
        return 1
    # A forward declaration and its definition are two LINES for one rule, so the line count and
    # the name count differ legitimately. State both rather than a single number that has to be
    # reconciled by hand -- 48 = 40 + 6 does not add up until you know two names appear twice.
    rule_lines = len(decls) - len(consts)
    print(f"coverage: {len(decls)} inline declaration lines = {rule_lines} rule lines "
          f"({len(rules)} distinct rules, {rule_lines - len(rules)} forward-decl duplicates) "
          f"+ {len(consts)} constants; nothing unclassified")

    # R13.25 SELF-REVIEW: ratchet the COVERAGE, not just the orphan count. The first version
    # reported "31 rules, 0 orphans" while nine `const char*` rules were invisible to it -- a
    # clean bill of health over two thirds of the header. If the rule count drops without an
    # edit that removes rules, the pattern has stopped matching something again.
    expected_rules = 40
    if len(rules) < expected_rules:
        print(f"\nFAIL: rule count fell to {len(rules)} (expected >= {expected_rules}).")
        print("Either rules were deleted -- then lower this number in the same commit -- or the")
        print("declaration pattern stopped matching some, which silently exempts them.")
        return 1

    prod = "\n".join((SDIR / f).read_text() for f in PRODUCTION if (SDIR / f).exists())

    orphans = []
    for r in rules:
        if r in ALLOWLIST:
            continue
        if re.search(r"\b" + re.escape(r) + r"\s*\(", prod):
            continue
        # A call inside the header that is not a declaration of the rule itself.
        #
        # R13.25 SELF-REVIEW: this used the OLD pattern to locate the definition while `rules`
        # had already been widened to match `const char*`. For those nine rules the definition
        # was not found, so the definition line itself counted as an "internal call" and every
        # one of them was exempt automatically. Fixing the detection regex and leaving the
        # exclusion regex behind is the same producer/consumer split this lint exists to catch --
        # in the lint. Both now come from one pattern.
        # Work LINE BY LINE. Character-offset windows around a multi-line regex match were
        # subtly wrong -- the match began on the previous line, so the "same line" test compared
        # the wrong pair and every const-char* rule exempted itself. Lines are what a declaration
        # actually occupies here, and the test is then trivially checkable by reading it.
        internal = False
        for ln in hdr.split("\n"):
            if not re.search(r"\b" + re.escape(r) + r"\s*\(", ln):
                continue
            if re.match(r"^\s*inline\s", ln):
                continue          # this line DECLARES the rule; it is not a call of it
            internal = True
            break
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
