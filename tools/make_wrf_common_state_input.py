#!/usr/bin/env python3
"""Create a new time-zero WRF input from one timestamp in a compatible WRFOUT."""
from __future__ import annotations
import argparse, hashlib, json, os, shutil, tempfile
from pathlib import Path
import numpy as np
from netCDF4 import Dataset

STATE_FIELDS = ("U", "V", "W", "PH", "T", "THM", "MU", "P")
BASE_FIELDS = ("PB", "PHB", "MUB")
THETA_CONTEXT_FIELDS = ("QVAPOR",)
TIME_ZERO = "0001-01-01_00:00:00"

def _sha_array(value): return hashlib.sha256(np.ascontiguousarray(value).tobytes()).hexdigest()
def _file_sha(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""): digest.update(block)
    return digest.hexdigest()
def _times(ds):
    if "Times" not in ds.variables: raise ValueError(f"{ds.filepath()}: missing Times")
    return [b"".join(row.tolist()).decode("ascii").rstrip("\x00 ") for row in ds["Times"][:]]
def _array(base, source, name, index, source_time_count):
    if name not in base.variables or name not in source.variables:
        raise ValueError(f"base and source files must both contain {name}")
    left, right = base[name], source[name]
    if (not left.dimensions or not right.dimensions or left.dimensions[0] != "Time" or
            right.dimensions[0] != "Time" or left.shape[0] != 1 or right.shape[0] != source_time_count):
        raise ValueError(f"{name}: leading Time dimension does not match file Times records")
    if left.dtype != right.dtype or left.shape[1:] != right.shape[1:] or left.dimensions[1:] != right.dimensions[1:]:
        raise ValueError(f"{name}: source and base dtype/stagger shape differ")
    if getattr(left, "units", None) != getattr(right, "units", None):
        raise ValueError(f"{name}: source and base units differ")
    return np.asarray(right[index]), getattr(right, "units", None)

def make_input(base_path, source_path, timestamp, output_path):
    base_path, source_path, output_path = map(Path, (base_path, source_path, output_path))
    if not base_path.is_file() or not source_path.is_file(): raise FileNotFoundError("base WRF input/source WRFOUT missing")
    if base_path.resolve() == source_path.resolve() or output_path.resolve() in (base_path.resolve(), source_path.resolve()):
        raise ValueError("output, base input, and source output must be distinct")
    if os.path.lexists(output_path): raise FileExistsError(output_path)
    fields, units, base_hashes, context_hashes = {}, {}, {}, {}
    with Dataset(base_path) as base, Dataset(source_path) as source:
        base_times, source_times = _times(base), _times(source)
        if base_times != [TIME_ZERO]: raise ValueError(f"base must be one time-zero record; got {base_times}")
        if source_times.count(timestamp) != 1: raise ValueError(f"source timestamp must occur exactly once: {timestamp}")
        index = source_times.index(timestamp)
        for name in BASE_FIELDS:
            value, _ = _array(base, source, name, index, len(source_times)); reference = np.asarray(base[name][0])
            if value.dtype != reference.dtype or not np.array_equal(value, reference):
                raise ValueError(f"{name}: source base state differs from time-zero input")
            base_hashes[name] = _sha_array(reference)
        for name in THETA_CONTEXT_FIELDS:
            in_base, in_source = name in base.variables, name in source.variables
            if in_base != in_source: raise ValueError(f"{name}: must be present in both files or neither")
            if in_base:
                value, _ = _array(base, source, name, index, len(source_times))
                reference = np.asarray(base[name][0])
                if value.dtype != reference.dtype or not np.array_equal(value, reference):
                    raise ValueError(f"{name}: source thermodynamic context differs from time-zero input")
                context_hashes[name] = _sha_array(reference)
        for name in STATE_FIELDS: fields[name], units[name] = _array(base, source, name, index, len(source_times))
        replaced = set(STATE_FIELDS)
        preserved = {n: _sha_array(np.asarray(v[:])) for n, v in base.variables.items() if n not in replaced}

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp_name = tempfile.mkstemp(prefix=f".{output_path.name}.", suffix=".tmp", dir=output_path.parent); os.close(fd)
    temp_path = Path(temp_name)
    try:
        shutil.copy2(base_path, temp_path)
        with Dataset(temp_path, "r+") as output:
            for name, value in fields.items(): output[name][0] = value
            for name, expected in preserved.items():
                if _sha_array(np.asarray(output[name][:])) != expected: raise ValueError(f"preserved variable changed: {name}")
            for name, value in fields.items():
                if not np.array_equal(np.asarray(output[name][0]), value): raise ValueError(f"source copy failed: {name}")
            if _times(output) != [TIME_ZERO]: raise ValueError("time-zero output record changed")
        os.link(temp_path, output_path)  # Atomic no-overwrite commit on the same filesystem.
    finally: temp_path.unlink(missing_ok=True)
    return {"schema":"wrf-common-state-input-v1","base":str(base_path),"base_sha256":_file_sha(base_path),
        "source_wrfout":str(source_path),"source_sha256":_file_sha(source_path),"source_timestamp":timestamp,
        "source_time_index":index,"output":str(output_path),"output_sha256":_file_sha(output_path),
        "output_time":TIME_ZERO,"copied_fields":{n:{"shape":list(a.shape),"dtype":str(a.dtype),"units":units[n],"sha256":_sha_array(a)} for n,a in fields.items()},
        "base_fields_exact":base_hashes,"theta_context_exact":context_hashes,
        "preserved_variable_count":len(preserved),"preserved_variables_exact":True}

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("base_wrfinput",type=Path);p.add_argument("source_wrfout",type=Path)
    p.add_argument("timestamp",help="unique WRF Times value, e.g. 0001-01-01_00:01:00");p.add_argument("output_wrfinput",type=Path)
    a=p.parse_args();print(json.dumps(make_input(a.base_wrfinput,a.source_wrfout,a.timestamp,a.output_wrfinput),indent=2))

if __name__ == "__main__": main()
