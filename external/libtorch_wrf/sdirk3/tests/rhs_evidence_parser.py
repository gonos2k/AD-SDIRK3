#!/usr/bin/env python3
"""Strict schema validator for SDIRK3 RHS evidence records.

This replaces an awk reduction that could DROP records instead of rejecting them.
The awk version keyed its table on sweep_seq and only entered a seq into the
iteration order when it saw a valid `begin`; a record with an invalid phase set a
flag on a seq that the END block then never visited, so the malformed line vanished
and an otherwise-clean log passed. It also matched the marker anywhere in the line,
so `WARNING: ... SDIRK3_RHS_CALLS phase=begin ...` was consumed as a real record.

The governing rule here is that every line carrying a marker is CONSUMED EXACTLY
ONCE and must satisfy the schema, or the whole log is rejected. Silence is never an
outcome: a line that cannot be classified is a violation, not a skip.

Usage:
    rhs_evidence_parser.py sweeps <log>    -> per-sweep table + verdict
    rhs_evidence_parser.py run <log>       -> whole-run verdict
Exit status: 0 if the log satisfies the schema, 1 otherwise, 2 on usage error.
"""

import re
import sys

SWEEP_MARKER = "SDIRK3_RHS_CALLS"
RUN_MARKER = "SDIRK3_RHS_RUN_TOTAL"
# PR 9F.7 P1-1: control-flow VIOLATION markers. Their mere presence fails the log --
# each means the RHS was evaluated outside an open lifecycle (after close, or with no
# begin). A healthy run emits neither.
VIOLATION_MARKERS = ("SDIRK3_RHS_POST_CLOSE_TICK", "SDIRK3_RHS_BEGIN_MISSING")

# A record line must START with the marker followed by a single space. Anything
# that merely CONTAINS the marker is a violation rather than a record, because a
# diagnostic line quoting the marker must never be mistaken for evidence.
SWEEP_LINE = re.compile(r"^" + SWEEP_MARKER + r" (.*)$")
RUN_LINE = re.compile(r"^" + RUN_MARKER + r" (.*)$")
UINT = re.compile(r"^(0|[1-9][0-9]*)$")

SWEEP_KEYS = {"phase", "sweep_seq", "total", "delta", "concurrent"}
RUN_KEYS = {"phase", "total", "exit", "authority", "reason", "generation", "post_close_tick"}


def parse_kv(rest, allowed):
    """Split `k=v k=v` with no repeats and no unknown keys. Returns (dict, error)."""
    out = {}
    for tok in rest.split():
        if "=" not in tok:
            return None, "token_without_equals:" + tok
        k, v = tok.split("=", 1)
        if k not in allowed:
            return None, "unknown_key:" + k
        if k in out:
            return None, "duplicate_key:" + k
        out[k] = v
    return out, None


def uint_or_none(s):
    return int(s) if s is not None and UINT.match(s) else None


def scan(path, marker, line_re, allowed):
    """Return (records, violations). A record is (lineno, fields)."""
    records, violations = [], []
    try:
        with open(path, "r", errors="replace") as fh:
            lines = fh.readlines()
    except OSError as exc:
        return None, ["unreadable:" + str(exc)]

    for n, raw in enumerate(lines, 1):
        line = raw.rstrip("\n")
        if marker not in line:
            continue
        m = line_re.match(line)
        if not m:
            # Carries the marker but is not a well-formed record line. Counting it
            # is the whole point: the previous parser silently ignored these.
            violations.append("line %d: marker_not_at_line_start" % n)
            continue
        fields, err = parse_kv(m.group(1), allowed)
        if err:
            violations.append("line %d: %s" % (n, err))
            continue
        records.append((n, fields))
    return records, violations


def check_sweeps(path):
    records, violations = scan(path, SWEEP_MARKER, SWEEP_LINE, SWEEP_KEYS)
    if records is None:
        return violations, []

    begins, ends = {}, {}
    order = []
    for n, f in records:
        phase = f.get("phase")
        if phase not in ("begin", "end"):
            violations.append("line %d: invalid_phase:%s" % (n, phase))
            continue
        seq = uint_or_none(f.get("sweep_seq"))
        total = uint_or_none(f.get("total"))
        if seq is None:
            violations.append("line %d: bad_or_missing_sweep_seq" % n)
            continue
        if total is None:
            violations.append("line %d: bad_or_missing_total" % n)
            continue
        conc = f.get("concurrent")
        if conc is not None and conc != "1":
            violations.append("line %d: bad_concurrent:%s" % (n, conc))
            continue

        if phase == "begin":
            if "delta" in f:
                violations.append("line %d: delta_on_begin" % n)
                continue
            if seq in begins:
                violations.append("line %d: duplicate_begin_seq:%d" % (n, seq))
                continue
            begins[seq] = (n, total, conc == "1")
            order.append(seq)
        else:
            delta = uint_or_none(f.get("delta"))
            if delta is None:
                violations.append("line %d: bad_or_missing_delta_on_end" % n)
                continue
            if seq in ends:
                violations.append("line %d: duplicate_end_seq:%d" % (n, seq))
                continue
            ends[seq] = (n, total, delta, conc == "1")

    rows = []
    for seq in order:
        bn, btot, bconc = begins[seq]
        if seq not in ends:
            violations.append("seq %d: missing_end" % seq)
            rows.append((seq, btot, None, None, "missing_end"))
            continue
        en, etot, delta, econc = ends[seq]
        if en < bn:
            violations.append("seq %d: record_order_invalid" % seq)
        if etot < btot:
            violations.append("seq %d: total_decreased" % seq)
        if delta != etot - btot:
            violations.append("seq %d: delta_mismatch" % seq)
        if bconc or econc:
            violations.append("seq %d: concurrent_sweeps" % seq)
        rows.append((seq, btot, etot, delta, "ok"))

    for seq in ends:
        if seq not in begins:
            violations.append("seq %d: missing_begin" % seq)

    if not rows:
        violations.append("no_sweep_records")
    elif not any(r[3] for r in rows if r[3] is not None):
        # Every delta zero means the compared interval is empty; equality across
        # such a table proves nothing.
        violations.append("no_sweep_moved")
    return violations, rows


def scan_violation_markers(path):
    """Any line starting with a VIOLATION marker fails the log (PR 9F.7 P1-1)."""
    out = []
    try:
        with open(path, "r", errors="replace") as fh:
            for n, raw in enumerate(fh, 1):
                line = raw.rstrip("\r\n")
                for mk in VIOLATION_MARKERS:
                    if line.startswith(mk + " ") or line == mk:
                        out.append("line %d: control_flow_violation:%s" % (n, mk))
    except OSError as exc:
        out.append("unreadable:" + str(exc))
    return out


def check_run(path):
    records, violations = scan(path, RUN_MARKER, RUN_LINE, RUN_KEYS)
    if records is None:
        return violations, {}
    # A control-flow violation marker anywhere in the log is a hard failure,
    # independent of the whole-run begin/end schema below.
    violations.extend(scan_violation_markers(path))

    begins, ends = [], []
    for n, f in records:
        phase = f.get("phase")
        total = uint_or_none(f.get("total"))
        gen = uint_or_none(f.get("generation"))
        if phase not in ("begin", "end"):
            violations.append("line %d: invalid_phase:%s" % (n, phase))
            continue
        if total is None:
            violations.append("line %d: bad_or_missing_total" % n)
            continue
        if gen is None:
            violations.append("line %d: bad_or_missing_generation" % n)
            continue
        if phase == "begin":
            # A begin carries neither an exit NOR an authority (P2). Both belong to
            # the close alone; either on a begin is a malformed or forged record.
            if "exit" in f:
                violations.append("line %d: exit_on_begin" % n)
                continue
            if "authority" in f:
                violations.append("line %d: authority_on_begin" % n)
                continue
            if "post_close_tick" in f:
                violations.append("line %d: post_close_tick_on_begin" % n)
                continue
            if "reason" in f:
                violations.append("line %d: reason_on_begin" % n)
                continue
            begins.append((n, total, gen))
        else:
            kind = f.get("exit")
            if kind not in ("clean", "fatal"):
                violations.append("line %d: bad_or_missing_exit:%s" % (n, kind))
                continue
            # Which layer closed the run. Without it, a FALLBACK firing where the
            # authority path should have is invisible -- the record's presence alone
            # was once mistaken for the production path working.
            auth = f.get("authority")
            if auth not in ("fortran_outcome", "fortran_finalize",
                            "cpp_preabort", "cpp_destructor"):
                violations.append("line %d: bad_or_missing_authority:%s" % (n, auth))
                continue
            # PR 9F.7 P1-1: post_close_tick is NOT benign telemetry. A tick after
            # the close means the RHS was evaluated past the fatal/finalize decision
            # -- a control-flow defect. Any post_close_tick on an end record (any
            # value) fails the log; the value included in the record's identity below
            # so a differing duplicate never launders as "agreeing".
            pct = f.get("post_close_tick")
            if pct is not None:
                violations.append("line %d: post_close_tick_on_end:%s" % (n, pct))
                continue
            reason = f.get("reason")
            if reason not in ("step_outcome", "outcome_query", "finalize", "halo",
                              "solver_state", "topology", "cpp_fallback", "other",
                              "none"):
                violations.append("line %d: bad_or_missing_reason:%s" % (n, reason))
                continue
            ends.append((n, total, kind, auth, gen, reason))

    info = {}
    if len(begins) != 1:
        violations.append("begin_count:%d (must be exactly 1)" % len(begins))
    if not ends:
        violations.append("missing_end")
    # The end record is written on the controlled-fatal path, where losing it is
    # worse than repeating it, so identical repeats are legitimate. Disagreement is
    # not: it means two different observations of the same quantity.
    if len({(t, k, a, g, r) for _, t, k, a, g, r in ends}) > 1:
        violations.append("disagreeing_end_records")

    if begins and ends:
        bn, btot, bgen = begins[0]
        if btot != 0:
            violations.append("begin_total_not_zero:%d" % btot)
        first_end_line = min(n for n, _, _, _, _, _ in ends)
        if first_end_line < bn:
            violations.append("record_order_invalid")
        etot, kind, auth, egen, ereason = (ends[0][1], ends[0][2],
                                            ends[0][3], ends[0][4], ends[0][5])
        if etot < btot:
            violations.append("end_total_below_begin")
        # A single run is a single generation: the close must belong to the begin it
        # closes. A generation mismatch means two lifecycles were spliced together.
        if bgen != egen:
            violations.append("generation_mismatch:begin=%d,end=%d" % (bgen, egen))
        info = {"begin": btot, "end": etot, "exit": kind, "authority": auth,
                "generation": bgen, "reason": ereason}
    return violations, info


def main(argv):
    if len(argv) != 3 or argv[1] not in ("sweeps", "run"):
        sys.stderr.write(__doc__)
        return 2
    mode, path = argv[1], argv[2]
    if mode == "sweeps":
        violations, rows = check_sweeps(path)
        for seq, b, e, d, st in rows:
            print("SWEEP seq=%d begin=%s end=%s delta=%s status=%s"
                  % (seq, b, "-" if e is None else e,
                     "-" if d is None else d, st))
    else:
        violations, info = check_run(path)
        if info:
            print("RUN generation=%d begin=%d end=%d exit=%s authority=%s reason=%s"
                  % (info["generation"], info["begin"], info["end"],
                     info["exit"], info["authority"], info["reason"]))
    for v in violations:
        print("VIOLATION %s" % v)
    print("VERDICT %s violations=%d"
          % ("ok" if not violations else "rejected", len(violations)))
    return 0 if not violations else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
