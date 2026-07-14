#!/usr/bin/env python3
"""check_action_pins.py (PR 6, round 4): structural supply-chain contract.

Validates PARSED YAML values, never raw lines (round 2: comments and flow
style defeat line greps), with an EXACT hosted-label allowlist (round 3:
family patterns are prefix-bypassable), and — round 4 — a REUSABLE-WORKFLOW
CALL GRAPH: a PR-triggered workflow with no runs-on of its own can reach a
persistent runner through `jobs.<id>.uses: ./.github/workflows/x.yml`, whose
callee triggers only on workflow_call, so per-file checks pass both files
while the COMBINATION routes untrusted PR code to the persistent runner.

Contract:
  per file
    - explicit top-level `permissions:` required
    - pull_request_target forbidden
    - step-level `uses` must be ./local, docker@sha256-digest, or @40-hex
    - job-level `uses` (reusable workflow) must be a local
      ./.github/workflows/*.yml reference or an external ref pinned @40-hex
    - any `uses` in a position this checker does not model is a FAILURE
  call graph (roots: pull_request / pull_request_target / merge_group)
    - every job reachable through local reusable-workflow calls must run on
      an exactly-allowlisted hosted label; violations report the full
      root -> callee -> job path
    - external reusable workflows are forbidden from PR roots outright
      (their runner placement cannot be verified from this repository)
    - missing local targets, call cycles, and depth > 10 fail closed

A multi-file SELF-TEST runs the real scanner against fixture trees in a
temp directory before every real scan: every violation class must fire and
the clean fixtures must pass, else the checker fails itself (a gate that
cannot demonstrate it fires is treated as broken).

Residual risk no static check can resolve: a self-hosted runner registered
with a label that SHADOWS an allowlisted hosted name. That is runner-
registration governance (distinct labels / runner groups), stated here
rather than silently ignored.
"""
import glob
import os
import re
import sys
import tempfile

try:
    import yaml
except ImportError:  # fail CLOSED, never skip
    print("FAIL: PyYAML unavailable - the structural checker cannot run "
          "(install pyyaml; refusing to fall back to line greps)")
    sys.exit(1)

SHA_RE = re.compile(r"^[0-9a-f]{40}$")
DIGEST_RE = re.compile(r"^sha256:[0-9a-f]{64}$")
LOCAL_WF_RE = re.compile(r"^\./\.github/workflows/([^/@]+\.ya?ml)$")

# EXACT allowlist of the GitHub-hosted labels this repo's workflows may use
# (round 3: custom labels are arbitrary strings, so ubuntu-* style family
# patterns are bypassable by 'ubuntu-sdirk3-wrf'; labels compare
# case-insensitively). Extending this set is a reviewed one-line diff.
HOSTED_LABELS = {"ubuntu-24.04"}

# Graph roots. merge_group refs execute post-approval, but constraining them
# costs nothing here and keeps the policy uniformly fail-closed.
PR_ROOT_TRIGGERS = {"pull_request", "pull_request_target", "merge_group"}

MAX_CALL_DEPTH = 10  # GitHub's reusable-workflow nesting ceiling


def collect(node, key, out):
    """All values of `key` in any mapping, at any depth, any YAML style."""
    if isinstance(node, dict):
        for k, v in node.items():
            if k == key:
                out.append(v)
            collect(v, key, out)
    elif isinstance(node, list):
        for item in node:
            collect(item, key, out)


def triggers_of(doc):
    # YAML 1.1 parses a bare `on:` key as boolean True.
    on = doc.get("on", doc.get(True))
    if on is None:
        return []
    if isinstance(on, str):
        return [on]
    if isinstance(on, list):
        return [str(t) for t in on]
    if isinstance(on, dict):
        return [str(k) for k in on.keys()]
    return [str(on)]


def check_step_uses(value):
    """Error string for a step-level uses ref, or None if acceptably pinned."""
    if not isinstance(value, str):
        return f"non-string uses value: {value!r}"
    if value.startswith("./"):
        return None  # local composite action: same-commit code on the
        # already-validated runner of the enclosing job
    if value.startswith("docker://"):
        ref = value.split("@", 1)
        if len(ref) == 2 and DIGEST_RE.match(ref[1]):
            return None
        return f"docker action not pinned by sha256 digest: '{value}'"
    if "@" not in value:
        return f"external action has no ref at all: '{value}'"
    if SHA_RE.match(value.rsplit("@", 1)[1]):
        return None
    return f"external action not pinned to a 40-hex commit SHA: '{value}'"


def local_wf_target(ref):
    """Basename of a local reusable-workflow ref, or None if not local."""
    if not isinstance(ref, str):
        return None
    m = LOCAL_WF_RE.match(ref)
    return m.group(1) if m else None


def nonhosted_labels_of_job(job):
    """runs-on entries of one job that are not exactly-allowlisted hosted."""
    r = job.get("runs-on")
    if r is None:
        return []
    if isinstance(r, dict):  # runner-group form: not statically resolvable
        return [str(r)]
    bad = []
    for e in (r if isinstance(r, list) else [r]):
        s = str(e).strip()
        if "${{" in s or s.lower() not in HOSTED_LABELS:
            bad.append(s)
    return bad


def classify_jobs(doc):
    """(reusable_calls, step_uses, nonstruct) with completeness guard.

    reusable_calls: list of (job_id, ref) from jobs.<id>.uses
    step_uses:      list of (job_id, ref) from jobs.<id>.steps[*].uses
    nonstruct:      count of uses values OUTSIDE the modeled positions
    """
    reusable, steps_u = [], []
    jobs = doc.get("jobs") or {}
    if isinstance(jobs, dict):
        for job_id, job in jobs.items():
            if not isinstance(job, dict):
                continue
            if "uses" in job:
                reusable.append((str(job_id), job["uses"]))
            for st in (job.get("steps") or []):
                if isinstance(st, dict) and "uses" in st:
                    steps_u.append((str(job_id), st["uses"]))
    flat = []
    collect(doc, "uses", flat)
    nonstruct = len(flat) - len(reusable) - len(steps_u)
    return reusable, steps_u, nonstruct


def check_file(doc, label):
    """Per-file rules. Graph rules live in graph_failures()."""
    fails = []
    if not isinstance(doc, dict):
        return [f"{label}: not a YAML mapping (unparseable workflow)"]

    if "pull_request_target" in triggers_of(doc):
        fails.append(f"{label}: pull_request_target is forbidden")

    if "permissions" not in doc:
        fails.append(f"{label}: no explicit top-level permissions block")

    reusable, steps_u, nonstruct = classify_jobs(doc)
    if nonstruct != 0:
        fails.append(f"{label}: {nonstruct} `uses` value(s) in a position "
                     f"this checker does not model - failing closed")
    for job_id, ref in steps_u:
        err = check_step_uses(ref)
        if err:
            fails.append(f"{label}: jobs.{job_id}: {err}")
    for job_id, ref in reusable:
        if local_wf_target(ref):
            continue  # same-commit local workflow; graph rules apply
        if not isinstance(ref, str):
            fails.append(f"{label}: jobs.{job_id}: non-string reusable "
                         f"workflow ref: {ref!r}")
        elif "@" in ref and SHA_RE.match(ref.rsplit("@", 1)[1]):
            continue  # pinned external reusable; PR-root ban is graph-level
        else:
            fails.append(f"{label}: jobs.{job_id}: external reusable "
                         f"workflow not pinned to a 40-hex commit SHA: "
                         f"'{ref}'")
    return fails


def graph_failures(docs):
    """Persistent-runner reachability from every PR-triggered root."""
    fails = []

    def dfs(root, fname, path):
        doc = docs.get(fname)
        if doc is None or not isinstance(doc, dict):
            fails.append(f"PR-triggered workflow {root} calls missing/"
                         f"unparseable local workflow through: "
                         f"{' -> '.join(path)} (failing closed)")
            return
        if len(path) > MAX_CALL_DEPTH:
            fails.append(f"PR-triggered workflow {root} exceeds reusable-"
                         f"workflow call depth {MAX_CALL_DEPTH}: "
                         f"{' -> '.join(path)} (failing closed)")
            return
        jobs = doc.get("jobs") or {}
        if not isinstance(jobs, dict):
            return
        for job_id, job in jobs.items():
            if not isinstance(job, dict):
                continue
            if "uses" in job:
                ref = job["uses"]
                target = local_wf_target(ref)
                if target is None:
                    fails.append(
                        f"PR-triggered workflow {root} reaches an EXTERNAL "
                        f"reusable workflow through: "
                        f"{' -> '.join(path)} -> jobs.{job_id} uses "
                        f"'{ref}' (runner placement unverifiable from this "
                        f"repository; forbidden from PR roots)")
                elif target in path:
                    fails.append(
                        f"PR-triggered workflow {root} has a reusable-"
                        f"workflow call CYCLE: "
                        f"{' -> '.join(path)} -> {target} (failing closed)")
                else:
                    dfs(root, target, path + [target])
            else:
                bad = nonhosted_labels_of_job(job)
                if bad:
                    fails.append(
                        f"PR-triggered workflow {root} reaches persistent "
                        f"runner through: {' -> '.join(path)} -> "
                        f"jobs.{job_id} runs-on: {bad} (labels are case-"
                        f"insensitive and custom labels route to persistent "
                        f"runners)")

    for fname, doc in docs.items():
        if not isinstance(doc, dict):
            continue
        if set(triggers_of(doc)) & PR_ROOT_TRIGGERS:
            dfs(fname, fname, [fname])
    return fails


def scan(wf_dir):
    """Parse every workflow under wf_dir; per-file + graph failures."""
    files = sorted(glob.glob(os.path.join(wf_dir, "*.yml"))
                   + glob.glob(os.path.join(wf_dir, "*.yaml")))
    if not files:
        return None, ["no workflow files found (an empty gate would pass "
                      "vacuously)"]
    docs, fails = {}, []
    for wf in files:
        base = os.path.basename(wf)
        try:
            with open(wf, "r", encoding="utf-8") as fh:
                doc = yaml.safe_load(fh)
        except yaml.YAMLError as e:
            fails.append(f"{base}: unparseable YAML: {e}")
            docs[base] = None
            continue
        docs[base] = doc
        fails += check_file(doc, base)
    fails += graph_failures(docs)
    return files, fails


# ─────────────────────────── self-test fixtures ───────────────────────────
# Multi-file scenarios executed through the REAL scanner in a temp tree.
# Every violation class must fire; every clean scenario must be silent.

_CHECKOUT = "actions/checkout@0123456789abcdef0123456789abcdef01234567"

_PERFILE_BAD = """
name: bad
on:
  pull_request_target:
    branches: [main]
jobs:
  a:
    runs-on: [Self-Hosted, forged]
    steps:
      - uses: actions/checkout@v6   # @0123456789abcdef0123456789abcdef01234567
      - { uses: evil/flow-style@v1 }
      - uses: evil/no-ref
      - uses: docker://alpine:3
  b:
    runs-on: ubuntu-24.04
    steps:
      - uses: evil/comment-forge@v2  # uses: ./not-really-local
  c:
    runs-on: sdirk3-custom-only
    steps:
      - uses: ./ok
  d:
    runs-on: ${{ matrix.runner }}
    steps:
      - uses: ./ok
  e:
    runs-on: ubuntu-24.04-forged
    steps:
      - uses: ./ok
"""

_PERFILE_GOOD = f"""
name: good
on:
  push:
    branches: [main]
permissions:
  contents: read
jobs:
  a:
    runs-on: ubuntu-24.04
    steps:
      - uses: ./local/composite
      - uses: {_CHECKOUT} # v6
      - uses: docker://alpine@sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
  standalone-persistent:
    runs-on: [self-hosted, sdirk3-wrf]
    steps:
      - uses: ./local/composite
"""

_PR_ENTRY = """
name: pr entry
on:
  pull_request:
    branches: [main]
permissions:
  contents: read
jobs:
  call-persistent:
    uses: ./.github/workflows/persistent.yml
"""

_PERSISTENT = f"""
name: persistent
on:
  workflow_call:
permissions:
  contents: read
jobs:
  execute:
    runs-on: [self-hosted, sdirk3-wrf]
    steps:
      - uses: {_CHECKOUT}
"""

_NEST_ROOT = """
name: nest root
on:
  pull_request:
permissions:
  contents: read
jobs:
  go:
    uses: ./.github/workflows/mid.yml
"""

_NEST_MID = """
name: mid
on:
  workflow_call:
permissions:
  contents: read
jobs:
  deeper:
    uses: ./.github/workflows/persistent.yml
"""

_EXT_ROOT = """
name: ext root
on:
  pull_request:
permissions:
  contents: read
jobs:
  call:
    uses: other/repo/.github/workflows/build.yml@0123456789abcdef0123456789abcdef01234567
"""

_HOSTED_ROOT = """
name: hosted root
on:
  pull_request:
permissions:
  contents: read
jobs:
  call:
    uses: ./.github/workflows/hosted-callee.yml
"""

_HOSTED_CALLEE = f"""
name: hosted callee
on:
  workflow_call:
permissions:
  contents: read
jobs:
  build:
    runs-on: ubuntu-24.04
    steps:
      - uses: {_CHECKOUT}
"""

_MISSING_ROOT = """
name: missing root
on:
  pull_request:
permissions:
  contents: read
jobs:
  call:
    uses: ./.github/workflows/does-not-exist.yml
"""

_CYCLE_A = """
name: cycle a
on:
  pull_request:
permissions:
  contents: read
jobs:
  call:
    uses: ./.github/workflows/cycle-b.yml
"""

_CYCLE_B = """
name: cycle b
on:
  workflow_call:
permissions:
  contents: read
jobs:
  call:
    uses: ./.github/workflows/cycle-a.yml
"""

# (name, {filename: content}, expected substrings in failures, expect_clean)
_SCENARIOS = [
    ("per-file violations", {"bad.yml": _PERFILE_BAD}, [
        "pull_request_target is forbidden",
        "no explicit top-level permissions",
        "'actions/checkout@v6'",       # comment-SHA forgery must NOT exempt
        "'evil/flow-style@v1'",        # flow mapping must be seen
        "'evil/no-ref'",
        "docker action not pinned",
        "'evil/comment-forge@v2'",     # 'uses: ./' inside a comment not local
        "Self-Hosted",                 # case-changed label
        "sdirk3-custom-only",          # bare custom label
        "matrix.runner",               # expression runs-on
        "ubuntu-24.04-forged",         # hosted-PREFIXED custom label
    ], False),
    ("clean per-file + standalone persistent on push", {
        "good.yml": _PERFILE_GOOD}, [], True),
    ("one-hop local call to persistent runner", {
        "pr-entry.yml": _PR_ENTRY, "persistent.yml": _PERSISTENT}, [
        "pr-entry.yml -> persistent.yml -> jobs.execute",
        "self-hosted",
    ], False),
    ("nested local call to persistent runner", {
        "nest-root.yml": _NEST_ROOT, "mid.yml": _NEST_MID,
        "persistent.yml": _PERSISTENT}, [
        "nest-root.yml -> mid.yml -> persistent.yml -> jobs.execute",
    ], False),
    ("external reusable workflow from PR root", {
        "ext-root.yml": _EXT_ROOT}, [
        "EXTERNAL reusable workflow",
        "other/repo/.github/workflows/build.yml",
    ], False),
    ("hosted-only local chain passes", {
        "hosted-root.yml": _HOSTED_ROOT,
        "hosted-callee.yml": _HOSTED_CALLEE}, [], True),
    ("missing local target fails closed", {
        "missing-root.yml": _MISSING_ROOT}, [
        "missing/unparseable local workflow",
    ], False),
    ("call cycle fails closed", {
        "cycle-a.yml": _CYCLE_A, "cycle-b.yml": _CYCLE_B}, [
        "CYCLE",
    ], False),
]


def self_test():
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        for name, files, expected, expect_clean in _SCENARIOS:
            d = os.path.join(tmp, re.sub(r"\W+", "-", name),
                             ".github", "workflows")
            os.makedirs(d)
            for fname, content in files.items():
                with open(os.path.join(d, fname), "w",
                          encoding="utf-8") as fh:
                    fh.write(content)
            _, fails = scan(d)
            if expect_clean:
                if fails:
                    ok = False
                    print(f"FAIL: self-test '{name}' should be clean but got:")
                    for f in fails:
                        print(f"    {f}")
            else:
                missing = [e for e in expected
                           if not any(e in f for f in fails)]
                if missing:
                    ok = False
                    print(f"FAIL: self-test '{name}' did not fire on:")
                    for m in missing:
                        print(f"    {m}")
    if not ok:
        print("FAIL: checker self-test - the gate cannot demonstrate it fires")
    return ok


def main():
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                          "..", ".."))
    if not self_test():
        return 1
    print(f"self-test OK: {len(_SCENARIOS)} scenarios "
          f"(per-file + call-graph) fire/pass as designed")

    files, fails = scan(".github/workflows")
    for wf in (files or []):
        print(f"── {wf}")
    if fails:
        for f in fails:
            print(f"FAIL: {f}")
        return 1
    print("OK: workflow supply-chain contract holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
