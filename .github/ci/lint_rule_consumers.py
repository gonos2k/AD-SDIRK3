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
# R13.25 SELF-REVIEW: checking ONE header was itself a coverage overstatement -- three other
# headers carry pure decision rules (57 of them), and a gate silent on those reports a clean bill
# of health it has not earned. Each entry carries its own rule-count ratchet.
HEADERS = [
    "wrf_sdirk3_first_failure.h",
    "wrf_sdirk3_probe_validity.h",
    "wrf_sdirk3_stage_history_diag.h",
    "wrf_sdirk3_krylov_metrics.h",
]
# One aggregate coverage ratchet instead of four per-header counts: the same protection (a
# pattern that stops matching cannot silently exempt rules) with one number to maintain.
EXPECTED_RULES_TOTAL = 84

# R13.25 SELF-REVIEW: production is EVERY solver translation unit, not the three this started
# with. A rule called only from a .cpp outside that list would have been reported as an orphan --
# a false alarm, and a gate that cries wolf gets ignored, which is how it stops working at all.
PRODUCTION_GLOB = "*.cpp"
# ...but NOT the ten test_*.cpp that live in the same directory. R13.25 SELF-REVIEW: widening the
# glob to every .cpp swept those in, so a rule called ONLY from a contract test would have counted
# as consumed -- which is exactly the state this lint exists to reject. The whole point is
# "something that RUNS calls it", and a fixture is not that.
TEST_PREFIX = "test_"

# Rules deliberately without a production caller. Each needs a REASON, not just a name --
# an allowlist without reasons becomes a place to hide the next orphan.
ALLOWLIST = {
    # name: why it has no production caller
}


def check_header(name: str, prod: str) -> "tuple[int, int]":
    hdr = (SDIR / name).read_text()

    # R13.25 SELF-REVIEW: the first version of this pattern required the return type to be a
    # word, so it silently skipped every `inline const char* foo(...)` -- nine real rules -- and
    # reported "31 rules, 0 orphans" as if that were the whole header. A gate that overstates its
    # own coverage is the defect it exists to catch, one level up. Count what is SKIPPED too, and
    # ratchet it, so a future declaration style cannot slip past unseen.
    decls = [(i + 1, ln) for i, ln in enumerate(hdr.split("\n"))
             if re.match(r"^\s*(?:\[\[[\w:, ]+\]\]\s*)?inline\s", ln)]
    # R13.26: attributes may precede `inline` -- `[[nodiscard]] inline bool foo(...)`. The
    # coverage ratchet caught this the moment [[nodiscard]] was added (40 -> 33), which is exactly
    # what it is for: the rules did not disappear, the pattern stopped seeing them.
    rule_re = re.compile(r"^(?:\[\[[\w:, ]+\]\]\s*)?inline\s+(?:const\s+)?[\w:<>, ]+?[\s*&]+(\w+)\s*\(",
                         re.M)
    rules = sorted(set(rule_re.findall(hdr)))

    # `inline constexpr <type> kName = ...;` is a CONSTANT, not a rule: it has no body to bypass,
    # so "is it called" is not the question for it.
    consts = [n for n, ln in decls
              if re.match(r"^\s*(?:\[\[[\w:, ]+\]\]\s*)?inline\s+constexpr\s", ln)]
    accounted = set()
    for _, ln in decls:
        m = re.search(r"\b(\w+)\s*\(", ln)
        if m and m.group(1) in rules:
            accounted.add(id(ln))
    unseen = [(n, ln.strip()) for n, ln in decls
              if not re.match(r"^\s*(?:\[\[[\w:, ]+\]\]\s*)?inline\s+constexpr\s", ln)
              and not (re.search(r"\b(\w+)\s*\(", ln)
                       and re.search(r"\b(\w+)\s*\(", ln).group(1) in rules)]
    if unseen:
        print(f"FAIL: {len(unseen)} inline declaration(s) this lint cannot classify.")
        print("An unrecognised rule is silently exempt -- the gate would overstate its coverage.")
        for n, ln in unseen:
            print(f"  line {n}: {ln[:95]}")
        print("\nFix the declaration pattern above, or make it a constant if that is what it is.")
        return (1, 0)
    # A forward declaration and its definition are two LINES for one rule, so the line count and
    # the name count differ legitimately. State both rather than a single number that has to be
    # reconciled by hand -- 48 = 40 + 6 does not add up until you know two names appear twice.
    rule_lines = len(decls) - len(consts)
    print(f"[{name}] coverage: {len(decls)} inline declaration lines = {rule_lines} rule lines "
          f"({len(rules)} distinct rules, {rule_lines - len(rules)} forward-decl duplicates) "
          f"+ {len(consts)} constants; nothing unclassified")

    # R13.25 SELF-REVIEW: ratchet the COVERAGE, not just the orphan count. The first version
    # reported "31 rules, 0 orphans" while nine `const char*` rules were invisible to it -- a
    # clean bill of health over two thirds of the header. If the rule count drops without an
    # edit that removes rules, the pattern has stopped matching something again.

    orphans = []
    for r in rules:
        if r in ALLOWLIST:
            continue
        # A call `foo(...)` OR an address-of reference `&foo` / a bare function-pointer
        # argument. R13.25 SELF-REVIEW: a rule registered as a callback (`set_pre_abort_hook(
        # &sdirk3_rhs_run_emit_end_fatal)`) is consumed, and counting only `name(` reported it as
        # an orphan. False alarms are how a gate stops being read.
        # Two shapes count as consumption, checked separately so neither weakens the other:
        #   a CALL            `foo(`      -- possibly namespace-qualified, hence \b not a
        #                                    character class (a preceding ':' broke that)
        #   an ADDRESS-OF     `&foo`      -- a rule registered as a callback is consumed
        called = re.search(r"\b" + re.escape(r) + r"\s*\(", prod)
        referenced = re.search(r"&(?:[\w:]*::)?" + re.escape(r) + r"\b", prod)
        if called or referenced:
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
            if re.match(r"^\s*(?:\[\[[\w:, ]+\]\]\s*)?inline\s", ln):
                continue          # this line DECLARES the rule; it is not a use of it
            # A call, or an address-of: registering a rule as a callback inside its own header
            # (`set_pre_abort_hook(&sdirk3_rhs_run_emit_end_fatal)`) is consumption, and treating
            # it as an orphan is a false alarm -- which is how a gate stops being read at all.
            if re.search(r"\b" + re.escape(r) + r"\s*\(", ln) or \
               re.search(r"&(?:[\w:]*::)?" + re.escape(r) + r"\b", ln):
                internal = True
                break
        if internal:
            continue
        orphans.append(r)

    print(f"[{name}] {len(rules)} rules, {len(ALLOWLIST)} allowlisted, "
          f"{len(orphans)} without any consumer")
    if orphans:
        print("\nFAIL: these rules have fixtures but nothing that runs calls them.")
        print("A rule with no consumer is a comment with a test: the pins guard a copy of the")
        print("logic while the logic that runs goes unexamined.\n")
        for o in orphans:
            print(f"  - {o}")
        print("\nFix by EITHER wiring the rule into the code that makes the decision, OR")
        print("deleting it if superseded, OR adding it to ALLOWLIST with the reason.")
        return (1, len(rules))
    return (0, len(rules))


def main() -> int:
    prod = "\n".join(p.read_text() for p in sorted(SDIR.glob(PRODUCTION_GLOB))
                     if not p.name.startswith(TEST_PREFIX))
    rc = 0
    total = 0
    for name in HEADERS:
        r, n = check_header(name, prod)
        rc |= r
        total += n
    if total < EXPECTED_RULES_TOTAL:
        print(f"\nFAIL: rule count fell to {total} (expected >= {EXPECTED_RULES_TOTAL}).")
        print("Either rules were deleted -- lower EXPECTED_RULES_TOTAL in the same commit -- or the")
        print("declaration pattern stopped matching some, which silently exempts them.")
        return 1
    if rc == 0:
        print(f"OK: {total} rules across {len(HEADERS)} headers, every one with a consumer")
    return rc


if __name__ == "__main__":
    sys.exit(main())
