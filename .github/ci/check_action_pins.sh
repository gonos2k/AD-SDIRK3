#!/usr/bin/env bash
# check_action_pins.sh (PR 6): supply-chain contract for .github/workflows.
#  - every EXTERNAL `uses:` must be pinned to a full 40-hex commit SHA
#    (local `uses: ./...` is allowed); tag/branch pins are rejected
#  - `pull_request_target` is forbidden anywhere
#  - every workflow must declare an explicit top-level `permissions:`
#  - a workflow that targets self-hosted runners must not trigger on
#    pull_request (untrusted PR code must never reach persistent runners)
set -euo pipefail
cd "$(dirname "$0")/../.."
fail=0
for wf in .github/workflows/*.yml .github/workflows/*.yaml; do
  [ -e "$wf" ] || continue
  echo "── $wf"
  # NOTE: real workflow steps write `- uses:`; a previous `^\s*uses:` regex
  # matched NOTHING anywhere and the rule silently checked nothing (found by
  # canary PR: a tag-pinned @v6 sailed through). Dash-tolerant, POSIX classes.
  if grep -nE '^[[:space:]]*-?[[:space:]]*uses:[[:space:]]' "$wf" | grep -v 'uses: \./' | grep -vE '@[0-9a-f]{40}([[:space:]]|$|#)' | grep -q .; then
    echo "FAIL: external action not pinned to a 40-hex commit SHA:"
    grep -nE '^[[:space:]]*-?[[:space:]]*uses:[[:space:]]' "$wf" | grep -v 'uses: \./' | grep -vE '@[0-9a-f]{40}([[:space:]]|$|#)'
    fail=1
  fi
  if grep -qn 'pull_request_target' "$wf"; then
    echo "FAIL: pull_request_target is forbidden"; fail=1
  fi
  if ! grep -qE '^permissions:' "$wf"; then
    echo "FAIL: no explicit top-level permissions block"; fail=1
  fi
  if grep -q 'self-hosted' "$wf" && grep -qE '^\s*pull_request:' "$wf"; then
    echo "FAIL: self-hosted workflow must not trigger on pull_request"; fail=1
  fi
done
[ "$fail" -eq 0 ] && echo "OK: workflow supply-chain contract holds"
exit "$fail"
