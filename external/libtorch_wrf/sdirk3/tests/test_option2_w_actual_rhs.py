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
        elif fields and fields[0] in {"WACT_RATE", "WACT_H_RATE", "WACT_V_RATE", "WACT_MASS",
                                      "WACT_RHO", "WACT_H_RAW", "WACT_V_RAW"}:
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

        # Keep native scalar K and vary only the current column dry mass. The
        # state geopotential perturbation is zero, so total nonflat PH comes
        # from PHB and remains identical to the constant-mass geometry.
        compiled, routine_hash = oracle.compiled_fortran_oracle(
            repo, args.fortran_compiler, ["-O0"], Path(temp_dir) / "variable-mu",
            profile="actual-w-rhs-variable-mu")
        cpp, meta = parse_cpp(args.binary,
                              "--option2-momentum-w-actual-rhs-variable-mu-physical")
        owned = {(j, k, i) for j in range(1, oracle.NY - 1)
                 for k in range(1, oracle.NZ) for i in range(1, oracle.NX - 1)}
        labels = ("WACT_RATE", "WACT_H_RATE", "WACT_V_RATE", "WACT_MASS")
        missing = [label for label in labels
                   if any((label, *q) not in cpp for q in owned)]
        if missing or not all(q in compiled.get("W_MASS", {}) for q in owned):
            raise RuntimeError(f"variable-mu fixture misses owned values: {missing}")
        mass_cpp = {q: cpp[("WACT_MASS", *q)] for q in owned}
        mass_src = {q: compiled["W_MASS"][q] for q in owned}
        mass_error = max(abs(mass_cpp[q] - mass_src[q]) for q in owned)
        rate = {q: cpp[("WACT_RATE", *q)] for q in owned}
        h_rate = {q: cpp[("WACT_H_RATE", *q)] for q in owned}
        v_rate = {q: cpp[("WACT_V_RATE", *q)] for q in owned}
        raw_h = {q: h_rate[q] * mass_cpp[q] for q in owned}
        raw_v = {q: v_rate[q] * mass_cpp[q] for q in owned}
        expected_h = {q: compiled["HW_RAW"][q] for q in owned}
        expected_v = {q: compiled["VW_RAW"][q] for q in owned}
        expected_rate = {q: (expected_h[q] + expected_v[q]) / mass_src[q]
                         for q in owned}
        raw_h_error = max(abs(raw_h[q] - expected_h[q]) for q in owned)
        raw_v_error = max(abs(raw_v[q] - expected_v[q]) for q in owned)
        direct_h = {q: cpp[("WACT_H_RAW", *q)] for q in owned}
        direct_v = {q: cpp[("WACT_V_RAW", *q)] for q in owned}
        direct_h_error = max(abs(direct_h[q] - expected_h[q]) for q in owned)
        direct_v_error = max(abs(direct_v[q] - expected_v[q]) for q in owned)
        rho_owned = {(j, k, i) for j in range(1, oracle.NY - 1)
                     for k in range(oracle.NZ) for i in range(1, oracle.NX - 1)}
        if (not rho_owned.issubset(compiled.get("RHO_MASS", {})) or
                any(("WACT_RHO", *q) not in cpp for q in rho_owned)):
            raise RuntimeError("variable-mu raw probe lacks current-state density values")
        rho_src = {q: compiled["RHO_MASS"][q] for q in rho_owned}
        rho_cpp = {q: cpp[("WACT_RHO", *q)] for q in rho_owned}
        rho_error = max(abs(rho_cpp[q] - rho_src[q]) for q in rho_owned)
        rate_error = max(abs(rate[q] - expected_rate[q]) for q in owned)
        linearity_error = max(abs(rate[q] - h_rate[q] - v_rate[q]) for q in owned)
        h_signal = max(abs(value) for value in expected_h.values())
        v_signal = max(abs(value) for value in expected_v.values())
        signal = max(abs(value) for value in expected_rate.values())
        h_raw_budget = 4e-5 * max(h_signal, 1e-12)
        v_raw_budget = 4e-5 * max(v_signal, 1e-12)
        rho_budget = 4e-6 * max(max(abs(value) for value in rho_src.values()), 1e-12)
        mass_min = min(mass_src.values())
        mass_max = max(mass_src.values())
        # The independent C++ and Fortran FP32 sin/cos evaluations can differ
        # by one input ULP; adding that perturbation to ~80 kPa rounds at the
        # FP32 ULP of M_w. Budget two M_w ULPs before comparing the masses.
        mass_ulp = math.ldexp(1.0, math.floor(math.log2(mass_max)) - 23)
        mass_budget = 2.0 * mass_ulp
        map_value = float(meta.get("map", "nan"))
        source_signal = max(abs(compiled["RHS_W"][q] * map_value) for q in owned)
        source_budget = 2e-6 * max(1e-12, source_signal)
        onoff_roundoff = 4.0 * eps32 * max(
            float(meta.get("on_w_max", "nan")),
            float(meta.get("off_w_max", "nan")), signal) * mass_max
        raw_budget = onoff_roundoff + source_budget
        rate_budget = raw_budget / mass_min
        source_normalization_error = max(
            abs((compiled["HW_RAW"][q] + compiled["VW_RAW"][q]) / map_value -
                compiled["RHS_W"][q]) for q in owned)
        bottom = max(abs(cpp[("WACT_RATE", j, 0, i)])
                     for j in range(oracle.NY) for i in range(oracle.NX))
        top = max(abs(cpp[("WACT_RATE", j, oracle.NZ, i)])
                  for j in range(oracle.NY) for i in range(oracle.NX))
        variable_mu_ok = (
            meta.get("variable_mu") == "1" and abs(map_value - 1.25) < 1e-7 and
            min(mass_src.values()) > 0 and mass_error <= mass_budget and
            h_signal > 1e-8 and v_signal > 1e-8 and
            direct_h_error <= h_raw_budget and direct_v_error <= v_raw_budget and
            rho_error <= rho_budget and
            rate_error <= rate_budget and linearity_error <= rate_budget and
            source_normalization_error <= 2e-6 * max(1e-12, source_signal) and
            bottom == 0.0 and top == 0.0)
        print(f"WACT_VARIABLE_MU owned={len(owned)} mass-range="
              f"[{mass_min:.9g},{mass_max:.9g}] H/V raw signals="
              f"{h_signal:.9g}/{v_signal:.9g} raw errors="
              f"direct={direct_h_error:.9g}/{direct_v_error:.9g} "
              f"onoff_recovery={raw_h_error:.9g}/{raw_v_error:.9g} "
              f"rho_error={rho_error:.9g}/{rho_budget:.3g} rate error={rate_error:.9g} "
              f"mass error={mass_error:.9g}/{mass_budget:.9g} raw/rate budgets="
              f"{h_raw_budget:.3g}/{v_raw_budget:.3g}/{rate_budget:.3g} source_norm="
              f"{source_normalization_error:.9g} bottom/top={bottom}/{top} "
              f"routine_sha={routine_hash} pass={variable_mu_ok}")
        all_ok = variable_mu_ok and all_ok

        # Kernel-level dynamic-K contract only: the native actual-RHS path
        # rejects supplied coefficient tensors, so exercise the existing W
        # helpers directly and compare them with source-extracted Fortran.
        compiled, routine_hash = oracle.compiled_fortran_oracle(
            repo, args.fortran_compiler, ["-O0"], Path(temp_dir) / "variable-k",
            profile="w-variable-k-rho")
        run = subprocess.run([str(args.binary), "--option2-w-variable-k-rho"],
                             check=True, text=True, capture_output=True)
        labels = {"WH_RAW", "WV_RAW", "WH_KH_WRONG", "WV_KV_WRONG",
                  "WH_SCALAR_FALLBACK", "WV_SCALAR_FALLBACK"}
        kernel: dict[tuple[str, int, int, int], float] = {}
        for line in run.stdout.splitlines():
            fields = line.split()
            if fields and fields[0] in labels:
                label, j, k, i, value = fields
                kernel[(label, int(j), int(k), int(i))] = float(value)
        owned = {(j, k, i) for j in range(1, oracle.NY - 1)
                 for k in range(1, oracle.NZ) for i in range(1, oracle.NX - 1)}
        expected_h = {q: compiled["HW_RAW"][q] for q in owned}
        expected_v = {q: compiled["VW_RAW"][q] for q in owned}
        cpp_h = {q: kernel.get(("WH_RAW", *q), float("nan")) for q in owned}
        cpp_v = {q: kernel.get(("WV_RAW", *q), float("nan")) for q in owned}
        scale_h = max(abs(v) for v in expected_h.values())
        scale_v = max(abs(v) for v in expected_v.values())
        tol_h = 4e-5 * max(scale_h, 1e-12)
        tol_v = 4e-5 * max(scale_v, 1e-12)
        h_error = max(abs(cpp_h[q] - expected_h[q]) for q in owned)
        v_error = max(abs(cpp_v[q] - expected_v[q]) for q in owned)
        preproduct, mutant_hash = oracle.compiled_fortran_oracle(
            repo, args.fortran_compiler, ["-O0"], Path(temp_dir) / "preproduct-mutant",
            profile="w-variable-k-rho", mutation="preproduct")
        mutation_h = max(abs(preproduct["HW_RAW"][q] - expected_h[q]) for q in owned)
        wrong_h = max(abs(kernel[("WH_KH_WRONG", *q)] - expected_h[q]) for q in owned)
        wrong_v = max(abs(kernel[("WV_KV_WRONG", *q)] - expected_v[q]) for q in owned)
        scalar_h = max(abs(kernel[("WH_SCALAR_FALLBACK", *q)] - expected_h[q]) for q in owned)
        scalar_v = max(abs(kernel[("WV_SCALAR_FALLBACK", *q)] - expected_v[q]) for q in owned)
        variable_ok = (scale_h > 1e-8 and scale_v > 1e-8 and
                       h_error <= tol_h and v_error <= tol_v and
                       mutation_h > 10.0 * tol_h and
                       wrong_h > 10.0 * tol_h and wrong_v > 10.0 * tol_v and
                       scalar_h > 10.0 * tol_h and scalar_v > 10.0 * tol_v and
                       mutant_hash != routine_hash)
        print(f"W_KERNEL_VARIABLE_K owned={len(owned)} H/V signals="
              f"{scale_h:.9g}/{scale_v:.9g} errors={h_error:.9g}/{v_error:.9g} "
              f"preproduct_separation={mutation_h:.9g} "
              f"wrong_K_separation={wrong_h:.9g}/{wrong_v:.9g} "
              f"scalar_fallback_separation={scalar_h:.9g}/{scalar_v:.9g} "
              f"budgets={tol_h:.3g}/{tol_v:.3g} "
              f"routine_sha={routine_hash} mutant_sha={mutant_hash} pass={variable_ok}")
        all_ok = variable_ok and all_ok
    if not all_ok:
        return 1
    print("PASS option-2 W actual RHS physical/packed source contract")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
