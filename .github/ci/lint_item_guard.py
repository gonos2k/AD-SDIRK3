#!/usr/bin/env python3
"""Count `.item()` extractions that are neither inside a NoGradGuard nor on a detached operand.

WHY THIS EXISTS. CLAUDE.md states the rule unconditionally -- every `.item()` inside a
`NoGradGuard` scope -- because it breaks the autograd graph and forces a GPU->CPU sync, and the
project has recorded it as a repeated regression. Two things make it hard to hold by review:

  * scope. `torch::NoGradGuard` is RAII, so one declared inside a loop stops protecting at the end
    of each iteration. A later block in the same function is unguarded while the guard is still
    visible a few dozen lines up. That is exactly how the SDIRK3_NUMRANGE block scan came to call
    `.item<double>()` twice with no guard, in a probe a review had just checked for this class.
  * volume. A brace-depth audit found 129 sites; 118 in one 29k-line file.

So it is counted, not reviewed. Ratcheted like the from_blob lint: fixing sites REQUIRES lowering
the baseline, and adding one fails.

WHAT COUNTS. A `.item<T>()` or `.item()` with no enclosing NoGradGuard at or above its brace depth,
whose receiver is not reachable from a `.detach()`. `guarded_item<T>()` is the sanctioned helper and
is not counted. Comments and string literals are stripped first, so prose about `.item()` is not a
violation. Test sources are excluded: they run no autograd graph and no hot path.

THE DETACH RULE, and why it is data-flow and not proximity. The first version excluded a site when
`.detach()` appeared in the four preceding lines. Audited against this tree, all 12 exclusions were
legitimate -- the idiom here is `auto x_cpu = y.detach().to(kCPU);` followed by reductions -- so it
found no false negative. But it would have missed one BY LUCK: an `.item()` on an undetached tensor
that happens to sit near an unrelated `.detach()` slips through, and a lint whose soundness depends
on local style is a lint that fails the day the style changes. So the exclusion now follows the
DATA FLOW, but LOCAL -- and the reason is a measurement, not a preference. A file-wide transitive
closure returned ZERO violations where there are 100: in 29k lines almost every name is reachable
from some detach, so the "more principled" rule degenerated into excluding everything. The
proximity-only rule it replaced was audited by reading all 12 of its exclusions here, and each was a
real data-flow link -- but that is soundness by local style. The shipped rule requires BOTH: the
link must be NAMED (an identifier in the receiver expression comes from a `.detach()` assignment)
and NEAR (within eight lines). See `detached_locals`.

WHAT IT DOES NOT PROVE. A guarded `.item()` can still be a GPU sync on a hot path, and a detached
operand says nothing about whether the sync belongs there. Nor does it see `.data`, `.cpu()` or
`.numpy()` in a graph region. This bounds one failure mode.
"""
import os
import re
import sys


def strip_comments_and_strings(src: str) -> str:
    """Blank out comments and string literals, preserving line structure."""
    out, i, n, state = [], 0, len(src), None
    while i < n:
        c = src[i]
        if state is None:
            if src.startswith('/*', i):
                state = 'block'; i += 2; continue
            if src.startswith('//', i):
                j = src.find('\n', i)
                j = n if j < 0 else j
                out.append(' ' * (j - i)); i = j; continue
            if c == '"':
                state = 'str'; out.append(' '); i += 1; continue
            out.append(c); i += 1
        elif state == 'block':
            if src.startswith('*/', i):
                state = None; i += 2; out.append('  '); continue
            out.append('\n' if c == '\n' else ' '); i += 1
        else:
            if c == '\\':
                out.append('  '); i += 2; continue
            if c == '"':
                state = None
            out.append('\n' if c == '\n' else ' '); i += 1
    return ''.join(out)


ITEM = re.compile(r'\.item<|\.item\(\)')
GUARD = re.compile(r'NoGradGuard\s+\w+\s*;')
ASSIGN = re.compile(r'\b(?:auto|torch::Tensor|const\s+auto)\s+&?\s*(\w+)\s*=\s*(.+)$')
IDENT = re.compile(r'\b([A-Za-z_]\w*)\b')


def detached_locals(code, lineno, window=8):
    """Names assigned from a `.detach()` in the `window` lines above `lineno`, plus names
    derived from them within that same window.

    WHY A WINDOW AND NOT THE WHOLE FILE. The first attempt took the transitive closure over every
    assignment in the translation unit. Measured: it returned ZERO violations on a file with 100 --
    in 29k lines almost every name is reachable from some detach, so the "more principled" rule
    degenerated to excluding everything. A rule that cannot fire is not a rule. The second attempt
    (proximity alone: any `.detach()` within four lines) was audited by reading all 12 of its
    exclusions in this tree and every one was a real data-flow link -- but it is sound only by
    local style. This keeps both halves: the link must be NAMED, and it must be NEAR.
    """
    seeded = set()
    lo = max(0, lineno - 1 - window)
    for line in code[lo:lineno - 1]:
        m = ASSIGN.search(line)
        if not m:
            continue
        name, rhs = m.group(1), m.group(2)
        if '.detach()' in rhs or (seeded & set(IDENT.findall(rhs))):
            seeded.add(name)
    return seeded


def violations(path: str):
    src = open(path, errors='replace').read()
    code = strip_comments_and_strings(src).split('\n')
    depth, guards, hits = 0, [], []
    for lineno, line in enumerate(code, 1):
        for ch in line:
            if ch == '{':
                depth += 1
            elif ch == '}':
                depth -= 1
                while guards and guards[-1] > depth:
                    guards.pop()
        if GUARD.search(line):
            guards.append(depth)
        if ITEM.search(line) and 'guarded_item' not in line:
            if guards:
                continue
            if '.detach()' in line:
                continue
            # The receiver expression is everything before the FIRST `.item` on the line. If any
            # identifier in it is reachable from a detach, the extraction is off-graph. Naming the
            # link is the point: proximity to an unrelated `.detach()` is not evidence.
            head = line[:line.index('.item')]
            if set(IDENT.findall(head)) & detached_locals(code, lineno):
                continue
            hits.append(lineno)
    return hits


def main() -> int:
    root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')
    d = os.path.join(root, 'external', 'libtorch_wrf', 'sdirk3')
    total, detail = 0, []
    for name in sorted(os.listdir(d)):
        if not name.endswith(('.cpp', '.h')) or name.startswith('test_'):
            continue
        hits = violations(os.path.join(d, name))
        if hits:
            total += len(hits)
            detail.append((name, hits))
    if '--list' in sys.argv:
        for name, hits in detail:
            print(f"{name}: {len(hits)}  lines {hits}")
    print(total)
    return 0


if __name__ == '__main__':
    sys.exit(main())
