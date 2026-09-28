#!/usr/bin/env python3
"""Check actual option-2 W RHS ON-OFF against extracted WRF stresses."""
from __future__ import annotations

import argparse
import importlib.util
import math
import os
from pathlib import Path
import subprocess
import tempfile


def load_oracle(path: Path):
    spec = importlib.util.spec_from_file_location("option2_momentum_geometry", path)
    if spec is None or spec.loader is None:
        raise RuntimeError("cannot load source-extracted WRF oracle")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def parse_cpp(binary: Path, mode: str):
    run = subprocess.run([str(binary), mode], check=True, text=True, capture_output=True)
    values: dict[tuple[str, int, int, int], float] = {}
    meta: dict[str, str] = {}
    for line in run.stdout.splitlines():
        fields = line.split()
        if fields and fields[0] == "WACT_META":
            meta.update(item.split("=", 1) for item in fields[1:])
        elif fields and fields[0] in {"WACT_RATE", "WACT_H_RATE", "WACT_V_RATE"}:
            _, j, k, i, value = fields
            values[(fields[0], int(j), int(k), int(i))] = float(value)
    return values, meta


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, type=Path)
    parser.add_argument("--fortran-compiler", default=os.environ.get("FC", "gfortran"))
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[4]
    oracle = load_oracle(repo / "external/libtorch_wrf/sdirk3/tests/test_option2_momentum_geometry.py")
    cases = (("physical", "--option2-momentum-w-actual-rhs-physical",
              "actual-w-rhs", False),
             ("packed", "--option2-momentum-w-actual-rhs-packed",
              "actual-w-rhs-packed", True))
    eps32 = 2.0**-23
    all_ok = True
    with tempfile.TemporaryDirectory(prefix="sdirk3-option2-w-actual-") as temp_dir:
        for name, mode, profile, packed in cases:
            compiled, routine_hash = oracle.compiled_fortran_oracle(
                repo, args.fortran_compiler, ["-O0"], Path(temp_dir) / name,
                profile=profile)
            cpp, meta = parse_cpp(args.binary, mode)
            core_nx = oracle.NX - 1 if packed else oracle.NX
            core_ny = oracle.NY - 1 if packed else oracle.NY
            # W is mass located; exclude lateral physical edges and the two
            # vertical boundary levels. These are locally owned interior cells.
            owned = {(j, k, i) for j in range(1, core_ny - 1)
                     for k in range(1, oracle.NZ) for i in range(1, core_nx - 1)}
            for label in ("HW_RAW", "VW_RAW"):
                if not owned.issubset(compiled.get(label, {})):
                    raise RuntimeError(f"{name}: Fortran {label} lacks owned W cells")
            rate = {q: cpp.get(("WACT_RATE", *q), float("nan")) for q in owned}
            h_rate = {q: cpp.get(("WACT_H_RATE", *q), float("nan")) for q in owned}
            v_rate = {q: cpp.get(("WACT_V_RATE", *q), float("nan")) for q in owned}
            if not all(math.isfinite(value) for value in rate.values()):
                raise RuntimeError(f"{name}: C++ actual RHS lacks owned W cells")
            velocity_mass_w = float(meta.get("velocity_mass_w", "nan"))
            prescribed_velocity_mass_w = 80000.0
            map_value = float(meta.get("map", "nan"))
            if (meta.get("packed") != ("1" if packed else "0") or
                    not math.isfinite(velocity_mass_w) or velocity_mass_w <= 0 or
                    abs(velocity_mass_w-prescribed_velocity_mass_w) > 1e-6 or
                    abs(map_value-1.25) > 1e-7 or
                    float(meta.get("kh", "0")) == 0 or float(meta.get("kv", "0")) == 0):
                raise RuntimeError(f"{name}: fixture gates or W mass metadata are invalid")
            expected_h = {q: compiled["HW_RAW"][q] / velocity_mass_w for q in owned}
            expected_v = {q: compiled["VW_RAW"][q] / velocity_mass_w for q in owned}
            expected = {q: compiled["RHS_W"][q] * map_value / velocity_mass_w
                        for q in owned}
            signal_h = max(abs(value) for value in expected_h.values())
            signal_v = max(abs(value) for value in expected_v.values())
            signal = max(abs(value) for value in expected.values())
            error = max(abs(rate[q] - expected[q]) for q in owned)
            h_error = max(abs(h_rate[q]-expected_h[q]) for q in owned)
            v_error = max(abs(v_rate[q]-expected_v[q]) for q in owned)
            linearity_error = max(abs(rate[q]-h_rate[q]-v_rate[q]) for q in owned)
            source_normalization_error = max(
                abs((compiled["HW_RAW"][q]+compiled["VW_RAW"][q])/map_value-
                    compiled["RHS_W"][q]) for q in owned)
            source_signal = max(abs(compiled["RHS_W"][q]) for q in owned)
            source_budget = 2e-6*max(1e-12,source_signal)
            onoff_floor = 4.0*eps32*max(
                float(meta.get("on_w_max", "nan")),
                float(meta.get("off_w_max", "nan")), signal)
            tolerance = onoff_floor + source_budget*map_value/velocity_mass_w
            bottom = max(abs(cpp[("WACT_RATE", j, 0, i)]) for j in range(oracle.NY)
                         for i in range(oracle.NX))
            top = max(abs(cpp[("WACT_RATE", j, oracle.NZ, i)]) for j in range(oracle.NY)
                      for i in range(oracle.NX))
            passed = (signal_h > 1e-8 and signal_v > 1e-8 and signal > 1e-8 and
                      error <= tolerance and h_error <= tolerance and
                      v_error <= tolerance and linearity_error <= tolerance and
                      source_normalization_error <= 2e-6*max(1e-12,source_signal) and
                      bottom == 0.0 and top == 0.0)
            print(f"WACT_SOURCE {name} owned={len(owned)} H/V signals="
                  f"{signal_h:.9g}/{signal_v:.9g} errors total/H/V="
                  f"{error:.9g}/{h_error:.9g}/{v_error:.9g} "
                  f"linearity={linearity_error:.9g} "
                  f"budget={tolerance:.3g} source_norm={source_normalization_error:.9g} "
                  f"K0/top={bottom:.9g}/{top:.9g} "
                  f"routine_sha={routine_hash}")
            all_ok = passed and all_ok
            if packed:
                old_period, old_hash = oracle.compiled_fortran_oracle(
                    repo, args.fortran_compiler, ["-O0"], Path(temp_dir) / "packed-old-period",
                    profile="actual-w-rhs-packed-old-period")
                if old_hash != routine_hash:
                    raise RuntimeError("packed old-period control used different Fortran routines")
                old_period_error = max(
                    abs(rate[q]-old_period["RHS_W"][q]*map_value/velocity_mass_w)
                    for q in owned)
                mutation_pass = old_period_error > 10.0*tolerance
                print(f"WACT_MUTATION packed old-nx W period error={old_period_error:.9g} "
                      f"separation_budget={10.0*tolerance:.3g} pass={mutation_pass}")
                all_ok = mutation_pass and all_ok
    if not all_ok:
        return 1
    print("PASS option-2 W actual RHS physical/packed source contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
