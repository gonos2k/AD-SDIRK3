#!/usr/bin/env python3
"""R13 E1: the manifest comparator must key on a record's full identity, not on its stage.

WHAT WAS WRONG. The manifest record carried `stage=` and nothing else identifying, and the
parser did `stages[stage] = fields` with the comment "last step wins". A run emits one record
per stage per Newton solve, so several timesteps -- or a retry, another tile, another rank --
produce several records for the same stage. "Last wins" then compares arm A's last occurrence
against arm B's last occurrence, and those need not be the same occurrence. The diff reports
agreement over two different measurements.

WHAT PINS IT NOW. The emitter carries (step_seq, solver_id, rank, stage, newton_iter); the
parser keys on that tuple and RAISES on a duplicate rather than overwriting -- because two
records that cannot be told apart mean the identity is not identifying, and nothing built on
it is sound. This file also covers the case that made the old behaviour invisible: two arms
with different record COUNTS, which previously collapsed to one comparison each and passed.

And, as with the ownership combiner: R12 C7 reported this script verified against six
synthetic cases, and no CI job ran it. It is a ctest target now.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "sdirk3_manifest_diff.py")

failures = 0
checks = 0


def check(ok, what):
    global failures, checks
    checks += 1
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures += 1


def rec(stage=2, step_seq=1, newton_iter=0, solver_id=7, rank=0, **fields):
    base = dict(restart=7, tol=0.2, trust_radius=1.0, precond_type=1)
    base.update(fields)
    body = " ".join(f"{k}={v}" for k, v in base.items())
    return (f"SDIRK3_POLICY_MANIFEST stage={stage} step_seq={step_seq} "
            f"newton_iter={newton_iter} solver_id={solver_id} rank={rank} {body}")


def run(a_lines, b_lines, extra=()):
    with tempfile.TemporaryDirectory() as d:
        pa, pb = os.path.join(d, "a.log"), os.path.join(d, "b.log")
        open(pa, "w").write("\n".join(a_lines) + "\n")
        open(pb, "w").write("\n".join(b_lines) + "\n")
        r = subprocess.run([sys.executable, SCRIPT, pa, pb, *extra],
                           capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr


both = [rec(stage=2, step_seq=1), rec(stage=3, step_seq=1)]
rc, out = run(both, both)
check(rc == 0, "two identical arms agree (a gate that rejects everything is not a gate)")

# THE CASE THE OLD KEY HID. Arm A ran two timesteps and arm B one. Under "last wins" both
# collapsed to one record per stage and the comparison passed while measuring step 2 against
# step 1. Keyed properly, the extra records are visible as records present in only one arm.
a_two_steps = both + [rec(stage=2, step_seq=2, restart=51), rec(stage=3, step_seq=2)]
rc, out = run(a_two_steps, both)
check(rc == 2 and "step_seq=2" in out and "record only in" in out,
      "arms with different record COUNTS fail -- previously both collapsed to one record "
      "per stage and step 2 was silently compared against step 1")

# Multiple Newton iterations of the same stage are distinct records, not a duplicate.
rc, out = run([rec(newton_iter=0), rec(newton_iter=1)],
              [rec(newton_iter=0), rec(newton_iter=1)])
check(rc == 0, "several Newton iterations of one stage are distinct records, not duplicates")

# Two tiles / two solver instances in one process.
rc, out = run([rec(solver_id=7), rec(solver_id=8)], [rec(solver_id=7), rec(solver_id=8)])
check(rc == 0, "two solver instances in one process are distinct records")

# Two ranks.
rc, out = run([rec(rank=0), rec(rank=1)], [rec(rank=0), rec(rank=1)])
check(rc == 0, "two ranks are distinct records")

# A genuine duplicate: same identity twice. Not resolvable, so refuse.
rc, out = run([rec(), rec()], both)
check(rc == 2 and "duplicate manifest identity" in out,
      "a truly duplicated identity is an ERROR, not an overwrite")

# The ordinary job still works.
rc, out = run(both, [rec(stage=2, step_seq=1, restart=51), rec(stage=3, step_seq=1)])
check(rc == 2 and "restart 7 -> 51" in out, "an undeclared difference fails")

rc, out = run(both, [rec(stage=2, step_seq=1, restart=51), rec(stage=3, step_seq=1)],
              ["--expect", "restart"])
check(rc == 0, "the declared knob is allowed to move")

rc, out = run(both, both, ["--expect", "restart"])
check(rc == 2 and "knob never moved" in out,
      "a declared knob that never moved fails -- an arm that reran the baseline is void")

rc, out = run(both, [rec(stage=2, step_seq=1), rec(stage=3, step_seq=1, extra_field=1)])
check(rc == 2 and "only in" in out, "a field present in one arm only fails")

rc, out = run(both, ["nothing here"])
check(rc == 2 and "no SDIRK3_POLICY_MANIFEST record" in out, "a run with no manifest fails")

# A log from before the identity was emitted must be refused, not keyed on the stage alone.
legacy = ["SDIRK3_POLICY_MANIFEST stage=2 restart=7 tol=0.2"]
rc, out = run(both, legacy)
check(rc == 2 and "predates R13" in out,
      "a log without the full identity is REFUSED rather than keyed on the stage alone")

EXPECTED = 12
ok = checks == EXPECTED
print(("  ok   " if ok else "  FAIL ") + f"case-count ratchet ({checks}/{EXPECTED})")
if not ok:
    failures += 1

print("MANIFEST_DUPLICATE_FAIL_CONTRACT: " +
      ("PASS" if failures == 0 else f"FAIL ({failures})"))
sys.exit(0 if failures == 0 else 1)
