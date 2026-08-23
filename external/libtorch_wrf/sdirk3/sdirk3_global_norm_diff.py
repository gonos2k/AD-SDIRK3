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

THE CLAIM IS SPLIT, because the record's position in the step decides what it can support.
A norm taken at solver ENTRY compares the state going IN: that is a decomposition and
ownership check, and it is the one currently reachable. A norm taken after the step
PUBLISHES compares what SDIRK3 produced, which is the np-equivalence claim -- and at a dt
where no step completes there are no such records, so --phase output fails closed saying so
rather than borrowing the entry data to make the stronger statement.

Exits 0 only when both runs partition the domain exactly at every compared step and every
block's combined norm agrees within tolerance.
"""
import argparse
import math
import sys
from collections import defaultdict

RECORD = "SDIRK3_GLOBAL_NORM"
# wrf_sdirk3_tile_unified.h: only OK_ADVANCED corresponds to a step the driver accepts. Every
# other code -- HARD_STAGE_ABORT included -- means U_{n+1} = U_n was published back.
OK_ADVANCED = 0
INVALID = "SDIRK3_GLOBAL_NORM_INVALID"

BOX_KEYS = ("i0", "i1", "j0", "j1", "k0", "k1")
# What the record must carry before it can be compared at all. `phase` is the one that
# decides WHICH claim the comparison supports.
REQUIRED = BOX_KEYS + ("step_seq", "stagger", "sumsq", "count", "phase",
                       "state_published", "outcome", "global_timestep", "rk_step")
DOMAIN_KEYS = ("ids", "ide", "jds", "jde", "nz", "nz_w")


def _fields(line, marker):
    out = {}
    for token in line[line.find(marker) + len(marker):].split():
        name, sep, value = token.partition("=")
        if sep:
            out[name] = value
    return out


def parse(paths, phase):
    """Return ({step_seq: {block: [record, ...]}}, domain-extents) for one phase."""
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
                missing = [k for k in REQUIRED if k not in f]
                if missing:
                    problems.append(
                        f"{path}: {RECORD} block={block} is missing {missing} -- this log "
                        f"predates the phase-tagged owned-box record and cannot support "
                        f"either claim")
                    continue
                if f["phase"] != phase:
                    continue
                rec = {k: int(f[k]) for k in BOX_KEYS}
                sumsq = float(f["sumsq"])
                # A blow-up must not read as agreement. `max(0.0, nan)` is 0.0 in Python --
                # every comparison against NaN is false, so max returns its first argument --
                # and two Inf norms give rel = nan, leaving worst at 0.0 and printing
                # "np-equivalent". Two runs that both exploded looked identical.
                if not math.isfinite(sumsq):
                    problems.append(f"{path}: non-finite sumsq for block {block} "
                                    f"({f['sumsq']}) -- a blow-up is not an agreement")
                    continue
                if sumsq < 0.0:
                    problems.append(f"{path}: negative sumsq for block {block} ({sumsq})")
                    continue
                rec.update(block=block, stagger=f["stagger"], sumsq=sumsq,
                           count=int(f["count"]), phase=f["phase"],
                           outcome=f["outcome"], published=f["state_published"])
                # An output record from a step that did not publish is not an output.
                #
                # R13.14 (red team round 5, R5-14): this guard could never fire, because the
                # emitter passed `published` as a compile-time LITERAL `true` at the only
                # phase=output call site -- and `outcome`, the one field that distinguishes an
                # advanced step from a reverted one, was declared REQUIRED, parsed, stored, and
                # read ONLY inside this unreachable branch. Meanwhile HARD_STAGE_ABORT publishes
                # U_n back to itself and falls through to that same emit. On em_b_wave, which
                # completes zero steps at every dt tried, ranks would then agree bit-for-bit --
                # by construction, since nothing moved -- and this comparator would call that
                # np-equivalence.
                #
                # The emitter now passes the measured outcome. This predicate reads BOTH fields,
                # so a record whose outcome is not OK_ADVANCED is refused whatever `published`
                # says, and neither field can go stale behind the other.
                if phase == "output" and (f["state_published"] != "1"
                                          or f["outcome"] != str(OK_ADVANCED)):
                    problems.append(f"{path}: {RECORD} block={block} is tagged phase=output "
                                    f"with state_published={f['state_published']} "
                                    f"outcome={f['outcome']} -- an output record from a step "
                                    f"that did not advance compares two copies of the input")
                    continue
                # GROUP ON THE FORECAST, not on a process counter.
                #
                # step_seq is a per-process call counter. Under MPI each rank is its own
                # process, so all ranks happen to start at 1 and grouping by it appeared to
                # work -- but it is not a property of the forecast, and any two ranks that
                # made a different number of calls (a retry, a probe arm) land in different
                # groups and the partition check then sees half a domain. The live
                # emitter/parser loop found exactly that: two ranks of one decomposition
                # arrived as step_seq 1 and 2.
                #
                # global_timestep = -1 means the host never stamped these records. They can
                # still be partition-checked -- that is a statement about one instant -- but
                # they cannot be ordered into a trajectory, and the verdict says so.
                steps[(int(f["global_timestep"]), int(f["rk_step"]))][block].append(rec)
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
                    help="global_timestep to compare (default: every step, and the step SETS "
                         "must match)")
    # WHICH claim the run is being asked to support. These are different statements and the
    # script used to make the stronger one from the weaker one's data.
    #
    #   input  -- the ranks partition the domain and see the same state ENTERING the solve.
    #             Real, and currently the only one reachable: the solve does not complete.
    #   output -- SDIRK3 produced the same answer. Requires a published state, so at a dt
    #             where no step completes there are no records and this fails closed.
    ap.add_argument("--phase", choices=("input", "output"), default="output",
                    help="input: the state ENTERING the solve (a decomposition claim). "
                         "output: the published state (the SDIRK3-equivalence claim). "
                         "Default output.")
    args = ap.parse_args()

    a_steps, dom_a, prob_a = parse(args.a, args.phase)
    b_steps, dom_b, prob_b = parse(args.b, args.phase)
    verdict_name = ("input-partition" if args.phase == "input" else "SDIRK3-output")
    problems = prob_a + prob_b

    for label, steps, paths in (("A", a_steps, args.a), ("B", b_steps, args.b)):
        if not steps:
            problems.append(
                f"run {label}: no {RECORD} record with phase={args.phase} in "
                f"{len(paths)} log(s)" +
                ("  (a phase=output record is emitted only after a step PUBLISHES its state;"
                 " if no step completes there is nothing to compare, which is the correct"
                 " answer rather than a missing feature)" if args.phase == "output" else ""))
    if problems:
        print("RECORDS INVALID:", file=sys.stderr)
        print("\n".join("  " + p for p in problems), file=sys.stderr)
        return 2

    # EVERY step by default. Comparing only the first common one lets step 1 agree while
    # step 2 diverges and still prints "np-equivalent" -- and a trajectory that agrees for
    # one step is not an equivalent trajectory. --step narrows it deliberately.
    if args.step is not None:
        wanted = [k for k in set(a_steps) & set(b_steps) if k[0] == args.step]
        if not wanted:
            print(f"global_timestep {args.step} is not present in both runs "
                  f"(A has {sorted(a_steps)}, B has {sorted(b_steps)})", file=sys.stderr)
            return 2
        steps_to_compare = sorted(wanted)
    else:
        if set(a_steps) != set(b_steps):
            print(f"the runs recorded DIFFERENT steps, so there is no trajectory to compare: "
                  f"A has {sorted(a_steps)}, B has {sorted(b_steps)}", file=sys.stderr)
            return 2
        steps_to_compare = sorted(a_steps)
    if dom_a != dom_b:
        print(f"runs used different domains: {dom_a} vs {dom_b}", file=sys.stderr)
        return 2

    for step in steps_to_compare:
        for label, blocks, dom in (("A", a_steps[step], dom_a), ("B", b_steps[step], dom_b)):
            for name in ("u", "v", "w", "ph", "t", "mu"):
                if name not in blocks:
                    problems.append(f"run {label}: block {name} missing at "
                                    f"global_timestep {step[0]} rk_step {step[1]}")
                else:
                    problems += check_partition(label, name, blocks[name], dom)
    if problems:
        print("PARTITION INVALID:", file=sys.stderr)
        print("\n".join("  " + p for p in problems), file=sys.stderr)
        return 2

    worst, worst_where, rows = 0.0, None, []
    for step in steps_to_compare:
        a, b = a_steps[step], b_steps[step]
        for name in sorted(set(a) | set(b)):
            # fsum, not sum: a different rank count changes the accumulation ORDER, and a
            # difference produced by summation order is not a difference in the physics.
            na = math.sqrt(math.fsum(r["sumsq"] for r in a[name]))
            nb = math.sqrt(math.fsum(r["sumsq"] for r in b[name]))
            if not (math.isfinite(na) and math.isfinite(nb)):
                print(f"step {step} block {name}: combined norm is not finite "
                      f"(A={na}, B={nb})", file=sys.stderr)
                return 2
            rel = abs(na - nb) / nb if nb > 0 else (0.0 if na == 0 else float("inf"))
            # Explicit, because `max(worst, nan)` silently keeps `worst`.
            if not math.isfinite(rel):
                print(f"step {step} block {name}: relative difference is not finite",
                      file=sys.stderr)
                return 2
            if rel > worst:
                worst, worst_where = rel, f"global_timestep {step[0]} rk_step {step[1]} block {name}"
            rows.append(f"  ts={step[0]} rk={step[1]}  {name:<3} "
                        f"A={na:.12g}  B={nb:.12g}  rel={rel:.3e}")

    unstamped = any(k[0] < 0 for k in steps_to_compare)
    if unstamped:
        print("NOTE: these records carry global_timestep=-1 -- the host never stamped them, "
              "so this is a statement about ONE instant and not about a trajectory")
    print(f"steps compared (global_timestep, rk_step): {steps_to_compare} -- domain "
          f"ids..ide={dom_a['ids']}..{dom_a['ide']} jds..jde={dom_a['jds']}..{dom_a['jde']}; "
          f"owned boxes cover it exactly at every step, with no overlap and no gap")
    print("\n".join(rows))
    if worst <= args.rtol:
        print(f"{verdict_name} equivalent over {len(steps_to_compare)} step(s): "
              f"worst relative difference {worst:.3e} <= {args.rtol:g}")
        return 0
    print(f"NOT {verdict_name} equivalent: worst relative difference {worst:.3e} > "
          f"{args.rtol:g} at {worst_where}", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
