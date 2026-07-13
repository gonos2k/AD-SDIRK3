#!/usr/bin/env python3
"""check_action_pins.py (PR 6): structural supply-chain contract for workflows.

Validates PARSED YAML values, not raw lines. The previous grep-based checker
was trivially bypassable: a comment containing '@<40-hex>' exempted a
tag-pinned action, flow-mapping steps ('- { uses: x@v6 }') evaded the line
anchor entirely, and a comment containing 'uses: ./' forged the local-path
exemption. Comments never reach a YAML parser and flow style parses
identically to block style, so all three classes die structurally.

Contract per workflow file:
  - every `uses:` value must be a local path (./...) or pinned to a full
    40-hex commit SHA; docker:// refs must be pinned by sha256 digest
  - pull_request_target must not appear among the triggers
  - an explicit top-level `permissions:` block is required
  - a workflow with any self-hosted runs-on label must not trigger on
    pull_request / pull_request_target

A negative+positive SELF-TEST runs before every real scan: a gate that
cannot demonstrably fire is treated as broken (a rule that never fired has
never run).
"""
import glob
import os
import re
import sys

try:
    import yaml
except ImportError:  # fail CLOSED, never skip
    print("FAIL: PyYAML unavailable - the structural checker cannot run "
          "(install pyyaml; refusing to fall back to line greps)")
    sys.exit(1)

SHA_RE = re.compile(r"^[0-9a-f]{40}$")
DIGEST_RE = re.compile(r"^sha256:[0-9a-f]{64}$")

# EXACT allowlist of the GitHub-hosted labels this repo's workflows may use.
# Anything else is treated as a persistent-runner target, fail closed:
#  - runner labels are CASE-INSENSITIVE ('Self-Hosted' routes like
#    'self-hosted'), so string blocklists are case-bypassable;
#  - a custom label alone ('sdirk3-wrf') targets a persistent runner without
#    the word self-hosted ever appearing;
#  - custom labels are ARBITRARY strings, so a family pattern (ubuntu-*) is
#    bypassable by a prefixed custom label ('ubuntu-sdirk3-wrf') — hence
#    exact matching against a minimal set, extended only by a reviewed diff.
# Residual risk no static check can resolve: registering a self-hosted runner
# whose label shadows an allowlisted name ('ubuntu-24.04'). That is runner-
# registration governance (distinct labels / runner groups), not lintable
# from the workflow text.
HOSTED_LABELS = {"ubuntu-24.04"}


def nonhosted_runner_refs(doc):
    """Every runs-on reference not exactly an allowlisted hosted label."""
    runs_on = []
    collect(doc, "runs-on", runs_on)
    bad = []
    for r in runs_on:
        if isinstance(r, dict):  # runner-group form: not statically resolvable
            bad.append(str(r))
            continue
        for e in (r if isinstance(r, list) else [r]):
            s = str(e).strip()
            if "${{" in s or s.lower() not in HOSTED_LABELS:
                bad.append(s)
    return bad


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


def check_uses_value(value):
    """Return an error string, or None if the ref is acceptably pinned."""
    if not isinstance(value, str):
        return f"non-string uses value: {value!r}"
    if value.startswith("./"):
        return None  # local composite action: same-repo code, no pin needed
    if value.startswith("docker://"):
        ref = value.split("@", 1)
        if len(ref) == 2 and DIGEST_RE.match(ref[1]):
            return None
        return f"docker action not pinned by sha256 digest: '{value}'"
    if "@" not in value:
        return f"external action has no ref at all: '{value}'"
    ref = value.rsplit("@", 1)[1]
    if SHA_RE.match(ref):
        return None
    return f"external action not pinned to a 40-hex commit SHA: '{value}'"


def check_doc(doc, label):
    """Validate one parsed workflow document. Returns a list of failures."""
    fails = []
    if not isinstance(doc, dict):
        fails.append(f"{label}: not a YAML mapping (unparseable workflow)")
        return fails

    trigs = triggers_of(doc)
    if "pull_request_target" in trigs:
        fails.append(f"{label}: pull_request_target is forbidden")

    if "permissions" not in doc:
        fails.append(f"{label}: no explicit top-level permissions block")

    uses = []
    collect(doc, "uses", uses)
    for u in uses:
        err = check_uses_value(u)
        if err:
            fails.append(f"{label}: {err}")

    nonhosted = nonhosted_runner_refs(doc)
    if nonhosted:
        for bad in ("pull_request", "pull_request_target"):
            if bad in trigs:
                fails.append(
                    f"{label}: non-GitHub-hosted runner target(s) {nonhosted} "
                    f"must not trigger on {bad} (labels are case-insensitive "
                    f"and custom labels route to persistent runners)")
    return fails


# ── self-test fixtures: the gate must PROVE it can fire before every scan ──
BAD_FIXTURE = """
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
GOOD_FIXTURE = """
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
      - uses: actions/checkout@0123456789abcdef0123456789abcdef01234567 # v6
      - uses: docker://alpine@sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef
"""


def self_test():
    bad = check_doc(yaml.safe_load(BAD_FIXTURE), "selftest-bad")
    expected_hits = [
        "pull_request_target is forbidden",
        "no explicit top-level permissions",
        "'actions/checkout@v6'",           # comment-SHA forgery must NOT exempt
        "'evil/flow-style@v1'",            # flow mapping must be seen
        "'evil/no-ref'",
        "docker action not pinned",
        "'evil/comment-forge@v2'",         # 'uses: ./' inside a comment is not local
        "Self-Hosted",                     # case-changed label must still be caught
        "sdirk3-custom-only",              # custom label alone targets the runner
        "matrix.runner",                   # expression runs-on: fail closed
        "ubuntu-24.04-forged",             # hosted-PREFIXED custom label must fire
        "must not trigger on pull_request_target",
    ]
    missing = [e for e in expected_hits if not any(e in f for f in bad)]
    good = check_doc(yaml.safe_load(GOOD_FIXTURE), "selftest-good")
    if missing or good:
        print("FAIL: checker self-test - the gate cannot demonstrate it fires")
        for m in missing:
            print(f"  bad fixture NOT flagged for: {m}")
        for g in good:
            print(f"  good fixture wrongly flagged: {g}")
        return False
    return True


def main():
    os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
    if not self_test():
        return 1
    print("self-test OK: all bypass classes fire, clean fixture passes")

    files = sorted(glob.glob(".github/workflows/*.yml")
                   + glob.glob(".github/workflows/*.yaml"))
    if not files:
        print("FAIL: no workflow files found (empty gate would pass vacuously)")
        return 1
    fails = []
    for wf in files:
        print(f"── {wf}")
        try:
            with open(wf, "r", encoding="utf-8") as fh:
                doc = yaml.safe_load(fh)
        except yaml.YAMLError as e:
            fails.append(f"{wf}: unparseable YAML: {e}")
            continue
        fails += check_doc(doc, wf)
    if fails:
        for f in fails:
            print(f"FAIL: {f}")
        return 1
    print("OK: workflow supply-chain contract holds")
    return 0


if __name__ == "__main__":
    sys.exit(main())
