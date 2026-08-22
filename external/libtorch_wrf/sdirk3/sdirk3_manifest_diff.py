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
record missing from one arm, or a run with no manifest at all -- each of which voids the
sweep rather than merely annotating it.

RECORDS ARE KEYED ON THEIR FULL IDENTITY, NOT ON `stage`. The manifest is emitted once per
stage per Newton solve, so a run with several timesteps, a retry, several tiles or several
ranks holds several records for the same stage. Keying on the stage alone meant the last
one won -- and "the last one" need not be the same occurrence in both arms, so the diff
could compare arm A's third emission against arm B's first and report agreement. A
duplicate key is now an error rather than an overwrite: if two records really are
indistinguishable, the identity is not identifying and no comparison built on it is sound.
"""
import argparse
import sys

RECORD = "SDIRK3_POLICY_MANIFEST"

# Present on the record, never part of the key. Process-local values that legitimately differ
# between two runs of the same forecast.
NON_KEY_DEBUG_FIELDS = ("solver_id", "step_seq")


# What makes one manifest record distinct from another. Every one of these can vary within a
# single run, and two records that agree on all of them are genuinely the same measurement.
# PHYSICAL identity, in the order a reader should think about it. `solver_id` is deliberately
# NOT here: it is a process-wide ordinal assigned in construction order, so two separately
# launched arms can give the same id to different tiles and different ids to the same one --
# and comparing two separately launched arms is the entire job. It stays on the record as a
# debug field, and is not an authority for keying across runs.
KEY_FIELDS = ("global_timestep", "retry_generation", "rank", "stage", "newton_iter")


def parse(path):
    """Return {identity-tuple: {field: value}} for every manifest record in a run log.

    Raises on a duplicate identity: an overwrite here is how two arms end up compared at
    different occurrences of the same stage while the diff still reports agreement.
    """
    records = {}
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
            missing = [k for k in KEY_FIELDS if k not in fields]
            if missing:
                raise ValueError(
                    f"{path}: a {RECORD} record is missing {missing}. A log without the full "
                    f"identity predates R13.1 and cannot be keyed on physical identity -- "
                    f"comparing it would key on a process-local ordinal, which does not "
                    f"survive the change of decomposition the comparison exists to make.")
            key = tuple(fields.pop(k) for k in KEY_FIELDS)
            # Dropped from the comparison, not just from the key: two runs differing only in
            # a process ordinal are not differing.
            for debug_field in NON_KEY_DEBUG_FIELDS:
                fields.pop(debug_field, None)
            if key in records:
                raise ValueError(
                    f"{path}: duplicate manifest identity {dict(zip(KEY_FIELDS, key))}. Two "
                    f"records that cannot be told apart mean the identity is not identifying.")
            records[key] = fields
    return records


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("baseline")
    ap.add_argument("arm")
    ap.add_argument("--expect", action="append", default=[], metavar="FIELD",
                    help="field allowed to differ (the knob under sweep); repeatable")
    args = ap.parse_args()

    try:
        base, arm = parse(args.baseline), parse(args.arm)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    expected = set(args.expect)
    problems = []

    for path, records in ((args.baseline, base), (args.arm, arm)):
        if not records:
            problems.append(f"{path}: no {RECORD} record "
                            f"(was WRF_SDIRK3_POLICY_MANIFEST=1 set?)")
    if problems:
        print("\n".join(problems), file=sys.stderr)
        return 2

    def name(key):
        return " ".join(f"{k}={v}" for k, v in zip(KEY_FIELDS, key))

    for key in sorted(set(base) | set(arm)):
        if key not in base or key not in arm:
            present = args.baseline if key in base else args.arm
            problems.append(f"[{name(key)}]: record only in {present}")
            continue
        b, a = base[key], arm[key]
        for field in sorted(set(b) | set(a)):
            if field not in b or field not in a:
                where = args.baseline if field in b else args.arm
                problems.append(f"[{name(key)}]: field {field!r} only in {where}")
            elif b[field] != a[field] and field not in expected:
                problems.append(
                    f"[{name(key)}]: {field} {b[field]} -> {a[field]} (not declared)")

    # A declared field that never actually moved means the sweep did not do what it said.
    shared = set(base) & set(arm)
    for field in sorted(expected):
        if all(field in base[k] and field in arm[k] and base[k][field] == arm[k][field]
               for k in shared):
            problems.append(f"--expect {field}: identical in both arms (knob never moved)")

    if problems:
        print(f"MANIFEST DIFF FAILED ({len(problems)}):", file=sys.stderr)
        print("\n".join("  " + p for p in problems), file=sys.stderr)
        return 2

    print(f"manifest diff OK: {len(shared)} record(s) agree except "
          f"{sorted(expected) or 'nothing'}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
