#!/usr/bin/env python3
"""Combine per-rank SDIRK3 global-norm partials and compare two rank counts.

Every other probe in this solver reports a tile-local number, and two runs at different
rank counts have different tiles -- so a difference between their records says nothing
about whether the physics agreed. Owned regions, expressed in global indices, partition
the domain, so their partials add to one domain quantity no matter how many ranks
produced it. That sum is comparable; a tile-local norm is not.

    WRF_SDIRK3_GLOBAL_NORMS=1 ./wrf.exe                       # np=1
    WRF_SDIRK3_GLOBAL_NORMS=1 mpirun -np 4 ./wrf.exe          # np=4
    sdirk3_global_norm_diff.py --a np1/rsl.error.* --b np4/rsl.error.*

TWO THINGS THIS CHECKS THAT A COUNT SUM DOES NOT.

1. AN EXACT PARTITION, not a matching total. If the ranks' boxes overlap on m cells and
   drop m others, the counts add up perfectly and the norm is wrong. So every owned box
   is marked onto the domain footprint and the marks are checked for double-cover and
   for holes. The C++ emits the box; this script does not re-derive WRF's ownership rule,
   because a second copy of a convention hides a disagreement instead of failing on it.

2. ONE TIMESTEP AT A TIME. Records are grouped by step_seq. Summing across timesteps
   produces a number that is not a domain quantity at any time -- previously that showed
   up only as a count that came out at 2x the domain, which fails closed but reads as a
   decomposition error rather than as what it is.

Exits 0 only when both runs partition the domain exactly at the compared step and every
block's combined norm agrees within tolerance.
"""
import argparse
import math
import sys
from collections import defaultdict

RECORD = "SDIRK3_GLOBAL_NORM"
INVALID = "SDIRK3_GLOBAL_NORM_INVALID"

BOX_KEYS = ("i0", "i1", "j0", "j1", "k0", "k1")
DOMAIN_KEYS = ("ids", "ide", "jds", "jde", "nz", "nz_w")


def _fields(line, marker):
    out = {}
    for token in line[line.find(marker) + len(marker):].split():
        name, sep, value = token.partition("=")
        if sep:
            out[name] = value
    return out


def parse(paths):
    """Return ({step_seq: {block: [record, ...]}}, domain-extents)."""
    steps, domain, problems = defaultdict(lambda: defaultdict(list)), None, []
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                # The solver's own self-check. A partial whose loop extent disagreed with
                # the box it declared is void, and must not be silently skipped here.
                if INVALID in line:
                    problems.append(f"{path}: {line.strip()}")
                    continue
                if RECORD not in line:
                    continue
                f = _fields(line, RECORD)
                block = f.get("block")
                if block is None:
                    continue
                missing = [k for k in BOX_KEYS + ("step_seq", "stagger", "sumsq", "count")
                           if k not in f]
                if missing:
                    problems.append(
                        f"{path}: {RECORD} block={block} is missing {missing} -- this log "
                        f"predates the owned-box record and cannot be checked for overlap")
                    continue
                rec = {k: int(f[k]) for k in BOX_KEYS}
                rec.update(block=block, stagger=f["stagger"],
                           sumsq=float(f["sumsq"]), count=int(f["count"]))
                steps[int(f["step_seq"])][block].append(rec)
                extents = {k: int(f[k]) for k in DOMAIN_KEYS}
                if domain is None:
                    domain = extents
                elif domain != extents:
                    problems.append(f"{path}: ranks disagree on the domain extents")
    return steps, domain, problems


def domain_footprint(domain, stagger):
    """The (i, j) cells the whole domain owns for this staggering, and its k extent."""
    i_hi = domain["ide"] if stagger == "x" else domain["ide"] - 1
    j_hi = domain["jde"] if stagger == "y" else domain["jde"] - 1
    nk = 1 if stagger == "column" else (domain["nz_w"] if stagger == "z" else domain["nz"])
    return domain["ids"], i_hi, domain["jds"], j_hi, nk


def check_partition(label, block, records, domain):
    """Exact partition test: every domain cell covered once, and only once."""
    problems = []
    staggers = {r["stagger"] for r in records}
    if len(staggers) != 1:
        return [f"run {label}: block {block} reports several staggerings {sorted(staggers)}"]
    stagger = staggers.pop()

    # k is not decomposed today. Rather than assume it, require the ranks to agree -- if that
    # ever changes, this fails closed instead of marking a 2-D footprint for a 3-D split.
    kranges = {(r["k0"], r["k1"]) for r in records}
    if len(kranges) != 1:
        return [f"run {label}: block {block} ranks disagree on the k extent {sorted(kranges)} "
                f"-- the footprint test assumes k is not decomposed"]
    k0, k1 = kranges.pop()

    i_lo, i_hi, j_lo, j_hi, nk_want = domain_footprint(domain, stagger)
    if (k1 - k0 + 1) != nk_want:
        problems.append(f"run {label}: block {block} spans {k1 - k0 + 1} levels, "
                        f"the {stagger}-staggered domain has {nk_want}")

    cover = defaultdict(int)
    for r in records:
        if r["count"] != (r["i1"] - r["i0"] + 1) * (r["j1"] - r["j0"] + 1) * (k1 - k0 + 1):
            problems.append(f"run {label}: block {block} box "
                            f"[{r['i0']}..{r['i1']}]x[{r['j0']}..{r['j1']}] does not hold "
                            f"{r['count']} cells")
        for i in range(r["i0"], r["i1"] + 1):
            for j in range(r["j0"], r["j1"] + 1):
                cover[(i, j)] += 1

    overlap = sorted(c for c, n in cover.items() if n > 1)
    gap = [(i, j)
           for i in range(i_lo, i_hi + 1)
           for j in range(j_lo, j_hi + 1)
           if (i, j) not in cover]
    stray = sorted(c for c in cover
                   if not (i_lo <= c[0] <= i_hi and j_lo <= c[1] <= j_hi))
    if overlap:
        problems.append(f"run {label}: block {block} -- {len(overlap)} cell(s) owned by more "
                        f"than one rank, e.g. {overlap[:3]}")
    if gap:
        problems.append(f"run {label}: block {block} -- {len(gap)} domain cell(s) owned by no "
                        f"rank, e.g. {gap[:3]}")
    if stray:
        problems.append(f"run {label}: block {block} -- {len(stray)} owned cell(s) outside the "
                        f"domain, e.g. {stray[:3]}")
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    # Named groups rather than a bare "--" separator: argparse consumes a lone "--" as its
    # end-of-options marker, so a separator that looks obvious on the command line silently
    # never reaches the parser.
    ap.add_argument("--a", nargs="+", required=True, metavar="LOG",
                    help="rsl.error.* from the first run (e.g. np=1)")
    ap.add_argument("--b", nargs="+", required=True, metavar="LOG",
                    help="rsl.error.* from the second run (e.g. np=4)")
    ap.add_argument("--rtol", type=float, default=1e-6,
                    help="relative tolerance on the combined norm (default 1e-6)")
    ap.add_argument("--step", type=int, default=None,
                    help="step_seq to compare (default: the first present in both runs)")
    args = ap.parse_args()

    a_steps, dom_a, prob_a = parse(args.a)
    b_steps, dom_b, prob_b = parse(args.b)
    problems = prob_a + prob_b

    for label, steps, paths in (("A", a_steps, args.a), ("B", b_steps, args.b)):
        if not steps:
            problems.append(f"run {label}: no {RECORD} record in {len(paths)} log(s)")
    if problems:
        print("RECORDS INVALID:", file=sys.stderr)
        print("\n".join("  " + p for p in problems), file=sys.stderr)
        return 2

    common = sorted(set(a_steps) & set(b_steps))
    if not common:
        print(f"no step_seq is present in both runs (A has {sorted(a_steps)}, "
              f"B has {sorted(b_steps)})", file=sys.stderr)
        return 2
    step = args.step if args.step is not None else common[0]
    if step not in common:
        print(f"step_seq {step} is not present in both runs (common: {common})",
              file=sys.stderr)
        return 2
    if dom_a != dom_b:
        print(f"runs used different domains: {dom_a} vs {dom_b}", file=sys.stderr)
        return 2

    a, b = a_steps[step], b_steps[step]
    for label, blocks, dom in (("A", a, dom_a), ("B", b, dom_b)):
        for name in ("u", "v", "w", "ph", "t", "mu"):
            if name not in blocks:
                problems.append(f"run {label}: block {name} missing at step_seq {step}")
            else:
                problems += check_partition(label, name, blocks[name], dom)
    if problems:
        print("PARTITION INVALID:", file=sys.stderr)
        print("\n".join("  " + p for p in problems), file=sys.stderr)
        return 2

    worst, rows = 0.0, []
    for name in sorted(set(a) | set(b)):
        na = math.sqrt(sum(r["sumsq"] for r in a[name]))
        nb = math.sqrt(sum(r["sumsq"] for r in b[name]))
        rel = abs(na - nb) / nb if nb > 0 else (0.0 if na == 0 else float("inf"))
        worst = max(worst, rel)
        rows.append(f"  {name:<3} A={na:.12g}  B={nb:.12g}  rel={rel:.3e}")

    print(f"step_seq {step} (of {common}) -- domain "
          f"ids..ide={dom_a['ids']}..{dom_a['ide']} jds..jde={dom_a['jds']}..{dom_a['jde']}; "
          f"owned boxes cover it exactly, with no overlap and no gap")
    print("\n".join(rows))
    if worst <= args.rtol:
        print(f"np-equivalent: worst relative difference {worst:.3e} <= {args.rtol:g}")
        return 0
    print(f"NOT np-equivalent: worst relative difference {worst:.3e} > {args.rtol:g}",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
