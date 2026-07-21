#!/usr/bin/env python3
"""Parse SDIRK3 advance_w geopotential-decomposition records.

The producer is opt-in through WRF_SDIRK3_ADVANCE_W_PH_DIAG=1. This parser
keeps the raw fields, derives closure ratios and a dominant RMS component, and
fails closed on malformed or explicit INVALID records unless requested
otherwise.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator, Sequence, TextIO

MARKER = "SDIRK3_ADVANCE_W_PH_DIAG"
INVALID_MARKER = "SDIRK3_ADVANCE_W_PH_DIAG_INVALID"
COMPONENTS = ("frozen", "old_w", "vertical_adv", "new_w")
REQUIRED_FLOATS = (
    "dts",
    "coupled_max", "coupled_rms",
    "physical_max", "physical_rms",
    "previous_max", "previous_rms",
    "frozen_max", "frozen_rms",
    "old_w_max", "old_w_rms",
    "vertical_adv_max", "vertical_adv_rms",
    "new_w_max", "new_w_rms",
    "actual_delta_max", "actual_delta_rms",
    "sum_delta_max", "sum_delta_rms",
    "closure_max", "closure_rms",
    "mass_old_min", "mass_old_max",
    "mass_new_min", "mass_new_max",
)


class ParseError(ValueError):
    """A diagnostic record is present but violates the output contract."""


@dataclass(frozen=True)
class SourceLine:
    source: str
    line_number: int
    text: str


@dataclass(frozen=True)
class InvalidRecord:
    source: str
    line_number: int
    sequence: str
    small_step: str
    reason: str


def _finite_float(value: str, *, key: str, where: SourceLine) -> float:
    try:
        parsed = float(value)
    except ValueError as exc:
        raise ParseError(f"{where.source}:{where.line_number}: {key} is not numeric: {value!r}") from exc
    if not math.isfinite(parsed):
        raise ParseError(f"{where.source}:{where.line_number}: {key} is non-finite: {value!r}")
    return parsed


def _integer(value: str, *, key: str, where: SourceLine) -> int:
    try:
        return int(value)
    except ValueError as exc:
        raise ParseError(f"{where.source}:{where.line_number}: {key} is not an integer: {value!r}") from exc


def _tokens_after_marker(where: SourceLine, marker: str) -> dict[str, str]:
    start = where.text.find(marker)
    if start < 0:
        return {}
    text_to_parse = where.text[start + len(marker):]
    if " reason=" in text_to_parse:
        text_to_parse = text_to_parse.split(" reason=", 1)[0]
    fields: dict[str, str] = {}
    for token in text_to_parse.strip().split():
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        if key in fields:
            raise ParseError(f"{where.source}:{where.line_number}: duplicate field {key!r}")
        fields[key] = value
    return fields


def parse_record(where: SourceLine) -> dict[str, object]:
    fields = _tokens_after_marker(where, MARKER)
    for key in ("sequence", "small_step"):
        if key not in fields:
            raise ParseError(f"{where.source}:{where.line_number}: missing {key}")
    missing = [key for key in REQUIRED_FLOATS if key not in fields]
    if missing:
        raise ParseError(
            f"{where.source}:{where.line_number}: missing fields: {', '.join(missing)}"
        )

    record: dict[str, object] = {
        "source": where.source,
        "line": where.line_number,
        "sequence": _integer(fields["sequence"], key="sequence", where=where),
        "small_step": _integer(fields["small_step"], key="small_step", where=where),
    }
    for key in REQUIRED_FLOATS:
        record[key] = _finite_float(fields[key], key=key, where=where)

    actual_rms = float(record["actual_delta_rms"])
    actual_max = float(record["actual_delta_max"])
    rms_floor = max(actual_rms, 1.0e-300)
    max_floor = max(actual_max, 1.0e-300)
    component_rms = {name: float(record[f"{name}_rms"]) for name in COMPONENTS}
    dominant = max(COMPONENTS, key=lambda name: component_rms[name])

    record.update(
        dominant_component=dominant,
        dominant_rms=component_rms[dominant],
        dominant_to_actual_rms=component_rms[dominant] / rms_floor,
        closure_rel_rms=float(record["closure_rms"]) / rms_floor,
        closure_rel_max=float(record["closure_max"]) / max_floor,
        component_l1_to_actual_rms=sum(component_rms.values()) / rms_floor,
        mass_old_span=float(record["mass_old_max"]) - float(record["mass_old_min"]),
        mass_new_span=float(record["mass_new_max"]) - float(record["mass_new_min"]),
    )
    return record


def parse_invalid(where: SourceLine) -> InvalidRecord:
    fields = _tokens_after_marker(where, INVALID_MARKER)
    reason_delimiter = " reason="
    reason = "unknown"
    marker_start = where.text.find(INVALID_MARKER)
    if marker_start >= 0:
        tail = where.text[marker_start + len(INVALID_MARKER):]
        if reason_delimiter in tail:
            reason = tail.split(reason_delimiter, 1)[1].strip() or "unknown"
    return InvalidRecord(
        source=where.source,
        line_number=where.line_number,
        sequence=fields.get("sequence", "?"),
        small_step=fields.get("small_step", "?"),
        reason=reason,
    )


def iter_source_lines(paths: Sequence[Path]) -> Iterator[SourceLine]:
    if not paths:
        for number, text in enumerate(sys.stdin, start=1):
            yield SourceLine("<stdin>", number, text.rstrip("\n"))
        return
    for path in paths:
        with path.open("r", encoding="utf-8", errors="replace") as handle:
            for number, text in enumerate(handle, start=1):
                yield SourceLine(str(path), number, text.rstrip("\n"))


def collect(paths: Sequence[Path]) -> tuple[list[dict[str, object]], list[InvalidRecord]]:
    records: list[dict[str, object]] = []
    invalid: list[InvalidRecord] = []
    for where in iter_source_lines(paths):
        if INVALID_MARKER in where.text:
            invalid.append(parse_invalid(where))
        elif MARKER in where.text:
            records.append(parse_record(where))
    return records, invalid


def write_tsv(records: Iterable[dict[str, object]], handle: TextIO) -> None:
    columns = (
        "source", "line", "sequence", "small_step", "dts",
        "physical_rms", "frozen_rms", "old_w_rms", "vertical_adv_rms", "new_w_rms",
        "actual_delta_rms", "closure_rms", "closure_rel_rms",
        "dominant_component", "dominant_rms", "dominant_to_actual_rms",
        "component_l1_to_actual_rms", "mass_old_min", "mass_old_max",
        "mass_new_min", "mass_new_max",
    )
    handle.write("\t".join(columns) + "\n")
    for record in records:
        handle.write("\t".join(str(record[column]) for column in columns) + "\n")


def self_test() -> None:
    floats = {key: 0.0 for key in REQUIRED_FLOATS}
    floats.update(
        dts=200.0,
        physical_max=0.02,
        physical_rms=0.01,
        frozen_max=4.0,
        frozen_rms=3.0,
        old_w_max=1.0,
        old_w_rms=0.5,
        vertical_adv_max=0.4,
        vertical_adv_rms=0.2,
        new_w_max=0.2,
        new_w_rms=0.1,
        actual_delta_max=4.4,
        actual_delta_rms=3.2,
        sum_delta_max=4.4,
        sum_delta_rms=3.2,
        closure_max=2.0e-7,
        closure_rms=1.0e-7,
        mass_old_min=8.0e4,
        mass_old_max=9.0e4,
        mass_new_min=8.1e4,
        mass_new_max=9.1e4,
    )
    payload = " ".join(["sequence=7", "small_step=2"] + [f"{k}={v}" for k, v in floats.items()])
    record = parse_record(SourceLine("self-test", 1, f"prefix {MARKER} {payload}"))
    assert record["dominant_component"] == "frozen"
    assert math.isclose(float(record["closure_rel_rms"]), 1.0e-7 / 3.2)
    assert math.isclose(float(record["component_l1_to_actual_rms"]), 3.8 / 3.2)

    malformed = SourceLine("self-test", 2, f"{MARKER} sequence=1 small_step=1 dts=1")
    try:
        parse_record(malformed)
    except ParseError:
        pass
    else:
        raise AssertionError("malformed record did not fail closed")

    invalid_reason = "shape mismatch because sequence=3 at k=4"
    invalid = parse_invalid(
        SourceLine(
            "self-test",
            3,
            f"{INVALID_MARKER} sequence=4 small_step=1 dts=200 reason={invalid_reason}",
        )
    )
    assert invalid.sequence == "4"
    assert invalid.reason == invalid_reason
    print("advance_w ph diagnostic parser self-test: PASS")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="*", type=Path, help="WRF/RSL logs; stdin when omitted")
    parser.add_argument("--json-out", type=Path, help="write complete records as JSON")
    parser.add_argument("--tsv-out", type=Path, help="write the compact summary as TSV")
    parser.add_argument("--allow-invalid", action="store_true", help="do not fail on INVALID records")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args(argv)

    if args.self_test:
        self_test()
        return 0

    records, invalid = collect(args.logs)
    if invalid and not args.allow_invalid:
        details = "; ".join(
            f"{item.source}:{item.line_number} seq={item.sequence} step={item.small_step} {item.reason}"
            for item in invalid
        )
        raise ParseError(f"diagnostic emitted INVALID records: {details}")
    if not records:
        raise ParseError("no SDIRK3_ADVANCE_W_PH_DIAG records found")

    if args.json_out:
        args.json_out.write_text(json.dumps(records, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    if args.tsv_out:
        with args.tsv_out.open("w", encoding="utf-8", newline="") as handle:
            write_tsv(records, handle)
    if not args.tsv_out:
        write_tsv(records, sys.stdout)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ParseError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(2)
