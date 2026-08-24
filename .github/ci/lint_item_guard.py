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

WHAT COUNTS. A `.item<T>()` or `.item()` with no enclosing NoGradGuard at or above its brace depth
and no `.detach()` in the four lines leading up to it. `guarded_item<T>()` is the sanctioned helper
and is not counted. Comments and string literals are stripped first, so prose about `.item()` is
not a violation. Test sources are excluded: they run no autograd graph and no hot path.

WHAT IT DOES NOT PROVE. A guarded `.item()` can still be a GPU sync on a hot path, and a detached
operand says nothing about whether the sync belongs there. This bounds one failure mode.
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
            if '.detach()' in ' '.join(code[max(0, lineno - 4):lineno]):
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
