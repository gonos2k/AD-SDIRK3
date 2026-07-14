#!/usr/bin/env bash
# check_action_pins.sh (PR 6): thin launcher for the STRUCTURAL supply-chain
# checker. The contract lives in check_action_pins.py, which validates parsed
# YAML values — the original grep version was trivially bypassable (a comment
# containing '@<40-hex>' exempted a tag pin, '- { uses: x@v6 }' flow style
# evaded the line anchor, and a comment 'uses: ./' forged the local-path
# exemption). Line greps over YAML are not a security boundary; a parser is.
# PyYAML absent = FAIL (the python side refuses to fall back to greps).
set -euo pipefail
exec python3 "$(dirname "$0")/check_action_pins.py" "$@"
