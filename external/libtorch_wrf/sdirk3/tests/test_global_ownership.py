#!/usr/bin/env python3
"""R13 D2: the global-norm combiner must reject an inexact partition, not just a wrong total.

WHY THIS FILE EXISTS AT ALL. R12 C7 and R12 R2 report that the two offline comparators were
verified against synthetic cases -- and nothing in CI ran them. A gate that no job invokes is
a script, and this repository has already learned once that an unexercised claim drifts. Both
comparators are now ctest targets.

WHAT IT PINS. The previous combiner summed counts and compared them to the domain's degrees of
freedom. That catches a rank whose box is the wrong SIZE. It cannot catch the failure that
actually threatens an np-equivalence claim: boxes that overlap on m cells and drop m others.
The total is then exactly right and the norm double-counts one region while missing another.
The overlap case below is constructed to have a perfect count sum for that reason -- if the
count check were still the whole test, it would pass.
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "sdirk3_global_norm_diff.py")

# em_b_wave: 40x80 mass points, 64 mass levels, 65 w levels.
IDS, IDE, JDS, JDE, KDS, NZ, NZW = 1, 41, 1, 81, 1, 64, 65

failures = 0
checks = 0


def check(ok, what):
    global failures, checks
    checks += 1
    print(("  ok   " if ok else "  FAIL ") + what)
    if not ok:
        failures += 1


def box(stagger, i0, i1, j0, j1):
    k0 = KDS
    k1 = KDS if stagger == "column" else (KDS + NZW - 1 if stagger == "z" else KDS + NZ - 1)
    return i0, i1, j0, j1, k0, k1


def record(block, stagger, i0, i1, j0, j1, sumsq, step_seq=1):
    i0, i1, j0, j1, k0, k1 = box(stagger, i0, i1, j0, j1)
    count = (i1 - i0 + 1) * (j1 - j0 + 1) * (k1 - k0 + 1)
    return (f"SDIRK3_GLOBAL_NORM block={block} stagger={stagger} step_seq={step_seq} "
            f"i0={i0} i1={i1} j0={j0} j1={j1} k0={k0} k1={k1} "
            f"sumsq={sumsq!r} count={count} "
            f"its={i0} ite={i1} jts={j0} jte={j1} "
            f"ids={IDS} ide={IDE} jds={JDS} jde={JDE} "
            f"nx=41 ny=81 nz={NZ} nx_u=42 ny_v=82 nz_w={NZW}")


BLOCKS = (("u", "x"), ("v", "y"), ("w", "z"), ("ph", "z"), ("t", "mass"), ("mu", "column"))


def i_hi(stagger):
    return IDE if stagger == "x" else IDE - 1


def j_hi(stagger):
    return JDE if stagger == "y" else JDE - 1


def single_rank(scale=1.0, step_seq=1):
    return [record(b, s, IDS, i_hi(s), JDS, j_hi(s), 100.0 * scale, step_seq)
            for b, s in BLOCKS]


def two_rank(jsplit=40, overlap=0, scale=1.0, step_seq=1):
    """Split in j. overlap>0 makes the halves share cells AND drop as many, so the total
    count stays exactly right -- the case a count check cannot see."""
    out = []
    for b, s in BLOCKS:
        hi = j_hi(s)
        out.append(record(b, s, IDS, i_hi(s), JDS, jsplit, 50.0 * scale, step_seq))
        out.append(record(b, s, IDS, i_hi(s), jsplit + 1 - overlap, hi - overlap,
                          50.0 * scale, step_seq))
    return out


def run(a_lines, b_lines, extra=()):
    with tempfile.TemporaryDirectory() as d:
        pa, pb = os.path.join(d, "a.log"), os.path.join(d, "b.log")
        open(pa, "w").write("\n".join(a_lines) + "\n")
        open(pb, "w").write("\n".join(b_lines) + "\n")
        r = subprocess.run([sys.executable, SCRIPT, "--a", pa, "--b", pb, *extra],
                           capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr


rc, out = run(single_rank(), single_rank())
check(rc == 0, "np=1 against itself: PASS (a gate that rejects everything is not a gate)")

rc, out = run(single_rank(), two_rank())
check(rc == 0, "an exact 2-way j-split reproduces the 1-rank norm")

# The case the count sum cannot see: one shared row, one dropped row, total unchanged.
rc, out = run(single_rank(), two_rank(overlap=1))
check(rc == 2 and "owned by more than one rank" in out and "owned by no rank" in out,
      "boxes that overlap on m cells and drop m others FAIL -- and the count sum is exactly "
      "right, which is why counting could not catch it")

# A short second half: this one a count check would also catch, so it must still fail.
short = [r for r in two_rank()]
short[1] = record("u", "x", IDS, i_hi("x"), 41, j_hi("x") - 2, 50.0)
rc, out = run(single_rank(), short)
check(rc == 2 and "owned by no rank" in out, "a gap with no overlap FAILS")

# Two timesteps in one log. Previously the partials were summed across them and the count
# came out at 2x the domain -- fail-closed, but reported as a decomposition error.
rc, out = run(single_rank(step_seq=1) + single_rank(step_seq=2), single_rank(step_seq=1))
check(rc == 0 and "step_seq 1 (of [1])" in out,
      "a log holding two timesteps compares ONE of them, not their sum")

rc, out = run(single_rank(step_seq=1) + single_rank(scale=4.0, step_seq=2),
              single_rank(step_seq=2), ["--step", "2"])
check(rc == 2 and "NOT np-equivalent" in out,
      "--step selects the timestep, and a real difference at that step still fails")

rc, out = run(single_rank(), single_rank(scale=1.0 + 1e-3))
check(rc == 2 and "NOT np-equivalent" in out, "a norm drift above tolerance FAILS")

missing = [r for r in single_rank() if not r.startswith("SDIRK3_GLOBAL_NORM block=mu")]
rc, out = run(single_rank(), missing)
check(rc == 2 and "block mu missing" in out, "a missing block FAILS")

rc, out = run(single_rank(), ["nothing here"])
check(rc == 2 and "no SDIRK3_GLOBAL_NORM record" in out, "a run with no records FAILS")

# The solver's own self-check reaching the combiner.
rc, out = run(single_rank(),
              single_rank() + ["SDIRK3_GLOBAL_NORM_INVALID block=t looped=1 box_count=2"])
check(rc == 2 and "GLOBAL_NORM_INVALID" in out,
      "a partial the solver already declared void is not silently skipped")

# An older log, from before the box was emitted, must not be treated as checkable.
legacy = ["SDIRK3_GLOBAL_NORM block=t sumsq=100.0 count=204800 "
          f"its=1 ite=41 jts=1 jte=81 ids={IDS} ide={IDE} jds={JDS} jde={JDE} "
          f"nx=41 ny=81 nz={NZ} nx_u=42 ny_v=82 nz_w={NZW}"]
rc, out = run(single_rank(), legacy)
check(rc == 2 and "predates the owned-box record" in out,
      "a log without the owned box is REFUSED, not read as a passing partition")

EXPECTED = 11
ok = checks == EXPECTED
print(("  ok   " if ok else "  FAIL ") + f"case-count ratchet ({checks}/{EXPECTED})")
if not ok:
    failures += 1

print("GLOBAL_OWNERSHIP_OVERLAP_GAP_CONTRACT: " +
      ("PASS" if failures == 0 else f"FAIL ({failures})"))
sys.exit(0 if failures == 0 else 1)
