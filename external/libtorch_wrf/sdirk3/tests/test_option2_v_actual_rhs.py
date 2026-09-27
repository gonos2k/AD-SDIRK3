#!/usr/bin/env python3
"""Actual option-2 V RHS against source-extracted WRF H/Z operators."""
from __future__ import annotations

import argparse
import importlib.util
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def load_geometry_module(path: Path):
    spec = importlib.util.spec_from_file_location("option2_momentum_geometry", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load source-extracted WRF oracle")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_cpp(binary: Path, mode: str):
    result = subprocess.run([str(binary), mode], check=True, text=True,
                            capture_output=True)
    fields_by_name: dict[str, dict[tuple[int, int, int], float]] = {}
    meta: dict[str, str] = {}
    for line in result.stdout.splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "VACT_META":
            meta.update(item.split("=", 1) for item in fields[1:])
        elif fields[0] in {"VACT_RAW_H", "VACT_RAW_Z", "VACT_H_RATE",
                           "VACT_Z_RATE", "VACT_BOTH_RATE", "VACT_M_V",
                           "VACT_MSFVX", "VACT_ALPHA_V", "VACT_OFF_V",
                           "VACT_ON_H", "VACT_ON_Z", "VACT_ON_BOTH"}:
            name, j, k, i, value = fields
            fields_by_name.setdefault(name, {})[(int(j), int(k), int(i))] = float(value)
    return fields_by_name, meta, result.stdout


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--fortran-compiler", default=os.environ.get("FC", "gfortran"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    oracle = load_geometry_module(repo / "external/libtorch_wrf/sdirk3/tests/test_option2_momentum_geometry.py")
    cases = (
        ("physical", "--option2-momentum-v-actual-rhs-physical", "actual-v-rhs", False, oracle.MAP),
        ("packed", "--option2-momentum-v-actual-rhs-packed", "actual-v-rhs-packed", True, oracle.MAP),
        ("map1", "--option2-momentum-v-actual-rhs-map1", "actual-v-rhs-map1", False, 1.0),
    )
    all_ok = True
    receipts = repo / ".validation/v-step10"
    receipts.mkdir(parents=True, exist_ok=True)
    eps32 = 2.0**-23
    for name, mode, profile, packed, map_value in cases:
        work = receipts / f"fortran-{name}"
        fortran, routine_hash = oracle.compiled_fortran_oracle(
            repo, args.fortran_compiler, ["-O0"], work, profile=profile)
        cpp, meta, stdout = parse_cpp(args.binary, mode)
        (receipts / f"cpp-{name}-source-test.stdout").write_text(stdout)
        owned_i = range(oracle.NX - 1) if packed else range(oracle.NX)
        owned_j = range(1, oracle.NY - 1) if packed else range(1, oracle.NY)
        owned = {(j, k, i) for j in owned_j for k in range(oracle.NZ) for i in owned_i}
        for label in ("VACT_RAW_H", "VACT_RAW_Z", "VACT_H_RATE", "VACT_Z_RATE",
                      "VACT_BOTH_RATE", "VACT_OFF_V", "VACT_ON_H", "VACT_ON_Z",
                      "VACT_ON_BOTH"):
            if not owned.issubset(cpp.get(label, {})):
                raise RuntimeError(f"{name}: missing C++ {label} owned cells")
        for label in ("V_H_RAW", "V_Z_RAW", "RHS_V"):
            if not owned.issubset(fortran.get(label, {})):
                raise RuntimeError(f"{name}: missing Fortran {label} owned cells")

        h_error = max(abs(cpp["VACT_RAW_H"][q] - fortran["V_H_RAW"][q]) for q in owned)
        z_error = max(abs(cpp["VACT_RAW_Z"][q] - fortran["V_Z_RAW"][q]) for q in owned)
        h_signal = max(abs(fortran["V_H_RAW"][q]) for q in owned)
        z_signal = max(abs(fortran["V_Z_RAW"][q]) for q in owned)
        h_budget = 2e-6 * max(1e-12, h_signal)
        z_budget = 2e-6 * max(1e-12, z_signal)
        layer_mass = 92000.0
        mass = 80000.0
        alpha = layer_mass / map_value
        source_rate = {q: fortran["RHS_V"][q] * map_value / layer_mass for q in owned}
        source_h = {q: fortran["V_H_RAW"][q] / layer_mass for q in owned}
        source_z = {q: fortran["V_Z_RAW"][q] / layer_mass for q in owned}
        source_signal = max(abs(value) for value in source_rate.values())
        source_normalization_error = max(
            abs((fortran["V_H_RAW"][q] + fortran["V_Z_RAW"][q]) / map_value -
                fortran["RHS_V"][q]) for q in owned)
        source_budget = 2e-6 * max(1e-12, source_signal)
        h_rate_error = max(abs(cpp["VACT_H_RATE"][q] - source_h[q]) for q in owned)
        z_rate_error = max(abs(cpp["VACT_Z_RATE"][q] - source_z[q]) for q in owned)
        both_rate_error = max(abs(cpp["VACT_BOTH_RATE"][q] - source_rate[q]) for q in owned)
        linearity_error = max(abs(cpp["VACT_BOTH_RATE"][q] - cpp["VACT_H_RATE"][q] -
                                  cpp["VACT_Z_RATE"][q]) for q in owned)
        off_signal = max(abs(cpp["VACT_OFF_V"][q]) for q in owned)
        on_signal = max(abs(cpp["VACT_ON_BOTH"][q]) for q in owned)
        onoff_floor = max(
            4.0 * eps32 * max(abs(cpp["VACT_ON_BOTH"][q]), abs(cpp["VACT_OFF_V"][q])) +
            source_budget + (abs(cpp["VACT_RAW_H"][q] - fortran["V_H_RAW"][q]) +
                             abs(cpp["VACT_RAW_Z"][q] - fortran["V_Z_RAW"][q])) / layer_mass
            for q in owned)
        old_mutant = {q: fortran["V_H_RAW"][q] / alpha +
                         fortran["V_Z_RAW"][q] / mass for q in owned}
        mutant_error = max(abs(old_mutant[q] - source_rate[q]) for q in owned)
        old_scale_separation = max(0.0, mutant_error / max(1e-30, onoff_floor))
        state_ok = (meta.get("packed") == ("1" if packed else "0") and
                    abs(float(meta.get("M_v", "nan")) - mass) <= 1e-4 and
                    abs(float(meta.get("alpha_v", "nan")) - alpha) <= 1e-3 and
                    float(meta.get("rho_error", "inf")) <= 2e-6)
        same_solver_order_error = float(meta.get("same_solver_order_error", "inf"))
        mode_ok = (h_error <= h_budget and z_error <= z_budget and
                   source_normalization_error <= 2e-6 * max(1e-12, max(abs(v) for v in fortran["RHS_V"].values())) and
                   h_rate_error <= onoff_floor and z_rate_error <= onoff_floor and
                   both_rate_error <= onoff_floor and linearity_error <= onoff_floor and
                   same_solver_order_error <= onoff_floor and
                   mutant_error > 10.0 * onoff_floor and state_ok)
        print(f"VACT_SOURCE {name} owned={len(owned)} raw_H/Z={h_error:.9g}/{z_error:.9g} "
              f"raw_budget={h_budget:.3g}/{z_budget:.3g} source_H/Z/total={h_rate_error:.9g}/"
              f"{z_rate_error:.9g}/{both_rate_error:.9g} ONOFF_floor={onoff_floor:.9g} "
              f"on/off_max={on_signal:.9g}/{off_signal:.9g} source_normalization="
              f"{source_normalization_error:.9g} same_solver_order={same_solver_order_error:.9g} "
              f"old_M_alpha_mutant={mutant_error:.9g} "
              f"separation={old_scale_separation:.6g}x state_ok={state_ok} "
              f"routine_sha={routine_hash}")
        (receipts / f"fortran-{name}-source-test.txt").write_text(
            f"profile={profile}\nroutine_sha={routine_hash}\nowned={len(owned)}\n"
            f"raw_H_error={h_error:.17g}\nraw_Z_error={z_error:.17g}\n"
            f"source_H_rate_error={h_rate_error:.17g}\nsource_Z_rate_error={z_rate_error:.17g}\n"
            f"source_total_rate_error={both_rate_error:.17g}\nONOFF_floor={onoff_floor:.17g}\n"
            f"on_signal={on_signal:.17g}\noff_signal={off_signal:.17g}\n"
            f"mutant_error={mutant_error:.17g}\nmutant_floor_separation={old_scale_separation:.17g}\n"
            f"state_ok={state_ok}\nmode_ok={mode_ok}\n")
        all_ok = all_ok and mode_ok
    if not all_ok:
        return 1
    print("PASS option-2 V actual RHS physical/packed/map1 source contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
