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

Exits 0 only when every block's combined norm agrees within tolerance AND both runs
partitioned the domain exactly. A count that does not match the domain's degrees of
freedom means halo cells were double-counted or interior cells dropped -- the comparison
is then void, not merely approximate, and it exits non-zero saying so.
"""
import argparse
import math
import sys

RECORD = "SDIRK3_GLOBAL_NORM"


def parse(paths):
    """Return {block: {"sumsq": float, "count": int}} plus the domain extents."""
    blocks, domain = {}, None
    for path in paths:
        with open(path, encoding="utf-8", errors="replace") as fh:
            for line in fh:
                start = line.find(RECORD)
                if start < 0:
                    continue
                f = {}
                for token in line[start + len(RECORD):].split():
                    name, sep, value = token.partition("=")
                    if sep:
                        f[name] = value
                name = f.get("block")
                if name is None:
                    continue
                acc = blocks.setdefault(name, {"sumsq": 0.0, "count": 0})
                acc["sumsq"] += float(f["sumsq"])
                acc["count"] += int(f["count"])
                extents = {k: int(f[k]) for k in ("ids", "ide", "jds", "jde", "nz", "nz_w")}
                if domain is None:
                    domain = extents
                elif domain != extents:
                    raise ValueError(f"{path}: ranks disagree on the domain extents")
    return blocks, domain


def expected_counts(d):
    """Degrees of freedom per block for the whole domain, from the staggering."""
    ni, nj = d["ide"] - d["ids"], d["jde"] - d["jds"]     # mass points
    nz, nzw = d["nz"], d["nz_w"]
    return {"u": (ni + 1) * nj * nz, "v": ni * (nj + 1) * nz,
            "w": ni * nj * nzw, "ph": ni * nj * nzw,
            "t": ni * nj * nz, "mu": ni * nj}


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
    args = ap.parse_args()

    a_paths, b_paths = args.a, args.b

    try:
        a, dom_a = parse(a_paths)
        b, dom_b = parse(b_paths)
    except ValueError as exc:
        print(str(exc), file=sys.stderr)
        return 2

    problems = []
    for label, blocks, dom, paths in (("A", a, dom_a, a_paths), ("B", b, dom_b, b_paths)):
        if not blocks:
            problems.append(f"run {label}: no {RECORD} record in {len(paths)} log(s)")
            continue
        # The partition check. A wrong count voids the comparison rather than perturbing it.
        for name, want in expected_counts(dom).items():
            got = blocks.get(name, {}).get("count")
            if got is None:
                problems.append(f"run {label}: block {name} missing")
            elif got != want:
                problems.append(f"run {label}: block {name} counted {got} cells, "
                                f"domain has {want} -- owned boxes do not partition it")
    if problems:
        print("PARTITION INVALID:", file=sys.stderr)
        print("\n".join("  " + p for p in problems), file=sys.stderr)
        return 2
    if dom_a != dom_b:
        print(f"runs used different domains: {dom_a} vs {dom_b}", file=sys.stderr)
        return 2

    worst, rows = 0.0, []
    for name in sorted(set(a) | set(b)):
        na, nb = math.sqrt(a[name]["sumsq"]), math.sqrt(b[name]["sumsq"])
        rel = abs(na - nb) / nb if nb > 0 else (0.0 if na == 0 else float("inf"))
        worst = max(worst, rel)
        rows.append(f"  {name:<3} A={na:.12g}  B={nb:.12g}  rel={rel:.3e}")

    print(f"domain ids..ide={dom_a['ids']}..{dom_a['ide']} "
          f"jds..jde={dom_a['jds']}..{dom_a['jde']}  (owned boxes partition it exactly)")
    print("\n".join(rows))
    if worst <= args.rtol:
        print(f"np-equivalent: worst relative difference {worst:.3e} <= {args.rtol:g}")
        return 0
    print(f"NOT np-equivalent: worst relative difference {worst:.3e} > {args.rtol:g}",
          file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
