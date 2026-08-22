#!/usr/bin/env python3
"""Compare the SDIRK3 policy manifests of two runs and fail on any undeclared difference.

A single-variable sweep is only single-variable if nothing else moved. Two arms are
separate processes, so there is no in-process baseline to fail closed against; what makes
the sweep verifiable is that every behaviour-bearing value is on the record in both arms.
This comparator turns that record into a gate.

    WRF_SDIRK3_POLICY_MANIFEST=1 ./wrf.exe   # in each arm
    sdirk3_manifest_diff.py base/rsl.error.0000 arm/rsl.error.0000 --expect restart

Exits 0 only when the arms agree on every field except the ones named by --expect.
Exits 2 on an undeclared difference, a field present in one arm and not the other, a
stage missing from one arm, or a run with no manifest at all -- each of which voids the
sweep rather than merely annotating it.
"""
import argparse
import sys

RECORD = "SDIRK3_POLICY_MANIFEST"


def parse(path):
    """Return {stage: {field: value}} for every manifest record in a run log."""
    stages = {}
    with open(path, encoding="utf-8", errors="replace") as fh:
        for line in fh:
            start = line.find(RECORD)
            if start < 0:
                continue
            fields = {}
            for token in line[start + len(RECORD):].split():
                name, sep, value = token.partition("=")
                if sep:
                    fields[name] = value
            stage = fields.pop("stage", None)
            if stage is not None:
                # A later record for a stage supersedes an earlier one (last step wins).
                stages[stage] = fields
    return stages


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("baseline")
    ap.add_argument("arm")
    ap.add_argument("--expect", action="append", default=[], metavar="FIELD",
                    help="field allowed to differ (the knob under sweep); repeatable")
    args = ap.parse_args()

    base, arm = parse(args.baseline), parse(args.arm)
    expected = set(args.expect)
    problems = []

    for path, stages in ((args.baseline, base), (args.arm, arm)):
        if not stages:
            problems.append(f"{path}: no {RECORD} record "
                            f"(was WRF_SDIRK3_POLICY_MANIFEST=1 set?)")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 2

    for stage in sorted(set(base) | set(arm)):
        if stage not in base or stage not in arm:
            present = args.baseline if stage in base else args.arm
            problems.append(f"stage {stage}: manifest only in {present}")
            continue
        b, a = base[stage], arm[stage]
        for field in sorted(set(b) | set(a)):
            if field not in b or field not in a:
                where = args.baseline if field in b else args.arm
                problems.append(f"stage {stage}: field {field!r} only in {where}")
            elif b[field] != a[field] and field not in expected:
                problems.append(
                    f"stage {stage}: {field} {b[field]} -> {a[field]} (not declared)")

    # A declared field that never actually moved means the sweep did not do what it said.
    for field in sorted(expected):
        if all(field in base.get(s, {}) and field in arm.get(s, {})
               and base[s][field] == arm[s][field] for s in set(base) & set(arm)):
            problems.append(f"--expect {field}: identical in both arms (knob never moved)")

    if problems:
        print(f"MANIFEST DIFF FAILED ({len(problems)}):", file=sys.stderr)
        print("\n".join("  " + p for p in problems), file=sys.stderr)
        return 2

    stages = ", ".join(sorted(set(base) & set(arm)))
    print(f"manifest diff OK: stages [{stages}] agree except {sorted(expected) or 'nothing'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
