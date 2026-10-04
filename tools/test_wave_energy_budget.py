#!/usr/bin/env python3
"""Standalone contract checks for the source-derived wave energy reference."""
from __future__ import annotations

import argparse
import hashlib
import json
import platform
import subprocess
import sys
from pathlib import Path

import numpy as np
import scipy
from scipy.linalg import expm

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import wave_energy_spatial_reference as ref  # noqa: E402


EXPECTED_OMEGA_N4 = 0.0021099603222840182


def _check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def _row_groups(n: int) -> dict[str, np.ndarray]:
    return {
        "U": np.arange(0, n),
        "W": np.arange(n, 2 * n),
        "p_E": np.arange(2 * n, 3 * n),
        "b": np.arange(3 * n, 4 * n),
        "surface": np.array([4 * n]),
    }


def explicit_pressure_buoyancy_commutators(column: dict) -> dict:
    """Rebuild Eulerian p_E and b tendencies from their continuum laws.

    The source side comes from the packed matrix through the explicit physical
    variable map.  The expected side below is independently assembled from
    the stated C-grid divergence/interpolation formulas; it never reuses the
    production comparator matrix as its expected p_E/b rows.
    """
    n = column["nz"]
    size = 4 * n + 1
    U, W, P, B = 0, n, 2 * n, 3 * n
    dz = column["height"]
    rho = column["rho"]
    gamma_p = column["gamma"] * column["p_mass"]

    # Incidence from active C-grid W faces to mass-cell vertical divergence.
    div_w = np.zeros((n, n), dtype=float)
    for k in range(n):
        div_w[k, k] = 1.0 / dz[k]
        if k > 0:
            div_w[k, k - 1] = -1.0 / dz[k]
    # Cell interpolation uses the fixed zero bottom W and averages active faces.
    interp_w = np.zeros((n, n), dtype=float)
    for k in range(n):
        interp_w[k, k] = 0.5
        if k > 0:
            interp_w[k, k - 1] = 0.5

    expected = np.zeros((size, size), dtype=np.complex128)
    expected[P:P + n, U:U + n] = -np.diag(gamma_p) @ (1j * column["kappa"] * np.eye(n))
    expected[P:P + n, W:W + n] = (
        -np.diag(gamma_p) @ div_w + np.diag(rho * column["g"]) @ interp_w
    )
    expected[B:B + n, W:W + n] = -np.diag(column["N2"]) @ interp_w

    source_physical = column["T"] @ column["A"] @ np.linalg.inv(column["T"])
    rp = source_physical[P:P + n, :] - expected[P:P + n, :]
    rb = source_physical[B:B + n, :] - expected[B:B + n, :]
    return {"Rp": rp, "Rb": rb, "expected": expected, "source_physical": source_physical}


def _conservative_closure(column: dict) -> dict:
    """Check natural and blockwise weighted-adjoint cancellation."""
    a = ref.conservative_eulerian_matrix(column, theta_gradient="legacy_fd")
    h = column["H_physical"]
    left, right = a.conj().T @ h, h @ a
    closure = left + right
    natural_denominator = np.linalg.norm(left) + np.linalg.norm(right)
    natural = np.linalg.norm(closure) / max(natural_denominator, np.finfo(float).tiny)
    groups = _row_groups(column["nz"])
    block_ratios = []
    for rows in groups.values():
        for cols in groups.values():
            idx = np.ix_(rows, cols)
            denominator = np.linalg.norm(left[idx]) + np.linalg.norm(right[idx])
            if denominator > 0.0:
                block_ratios.append(float(np.linalg.norm(closure[idx]) / denominator))

    # Mutation: remove the gravity-pressure stratification exchange from Wdot.
    n = column["nz"]
    rho, height = column["rho"], column["height"]
    gamma_p = column["gamma"] * column["p_mass"]
    w_dual = np.array([
        0.5 * (rho[r] * height[r] + rho[r + 1] * height[r + 1])
        if r < n - 1 else 0.5 * rho[-1] * height[-1]
        for r in range(n)
    ])
    interp = np.zeros((n, n), dtype=float)
    for k in range(n):
        interp[k, k] = 0.5
        if k > 0:
            interp[k, k - 1] = 0.5
    strat = np.diag(1.0 / w_dual) @ interp.T @ np.diag(
        height * rho * column["g"] / gamma_p
    )
    mutant = a.copy()
    mutant[n:2 * n, 2 * n:3 * n] += strat
    mleft, mright = mutant.conj().T @ h, h @ mutant
    mutant_ratio = np.linalg.norm(mleft + mright) / max(
        np.linalg.norm(mleft) + np.linalg.norm(mright), np.finfo(float).tiny
    )
    _check(natural < 1.0e-12, f"weighted-adjoint closure ratio {natural:.3g}")
    _check(max(block_ratios, default=0.0) < 1.0e-12,
           f"blockwise weighted-adjoint closure ratio {max(block_ratios):.3g}")
    _check(mutant_ratio > 1.0e-6,
           f"removing physical pressure-stratification exchange did not expose the defect: {mutant_ratio:.3g}")
    return {
        "natural_relative_frobenius": float(natural),
        "max_block_relative_frobenius": float(max(block_ratios)),
        "stratification_removed_mutant_relative": float(mutant_ratio),
        "max_elementwise_relative": float(np.max(np.divide(
            np.abs(closure), np.abs(left) + np.abs(right),
            out=np.zeros_like(closure.real), where=(np.abs(left) + np.abs(right)) > 0.0,
        ))),
    }


def _mode_q0(column: dict) -> tuple[np.ndarray, complex]:
    values, vectors = np.linalg.eig(column["A"])
    n = column["nz"]
    candidates = [i for i, value in enumerate(values)
                  if value.imag > 1.0e-7 and value.imag <= np.sqrt(np.max(column["N2"]))]
    _check(bool(candidates), f"N={n} has no oscillatory mode below N_max")
    candidates.sort(key=lambda i: values[i].imag, reverse=True)
    selected = candidates[0]
    plus = vectors[:, selected].copy()
    pivot = n + int(np.argmax(np.abs(plus[n:2 * n])))
    plus *= np.exp(-1j * np.angle(plus[pivot]))
    minus = np.array([-np.conj(plus[r]) if r < n else np.conj(plus[r])
                      for r in range(4 * n + 1)])
    q0 = plus + minus
    q0 *= 0.01 / np.max(np.abs(q0[n:2 * n]))
    return q0, complex(values[selected])


def _run_case(n: int) -> dict:
    c = ref.column(nz=n, nx=8, lx=40000.0, ly=30000.0, theta_gradient="legacy_fd")
    n2 = c["N2"]
    _check(np.all(np.isfinite(n2)) and np.all(n2 > 0.0), f"N={n} N² is not positive")
    weights = c["weights"]
    _check(np.all(np.isfinite(weights)) and np.all(weights > 0.0),
           f"N={n} physical energy weights are not strictly positive")
    _check(np.allclose(c["H_physical"], c["H_physical"].conj().T, rtol=0.0, atol=1.0e-5),
           f"N={n} physical H is not Hermitian")
    eig_h = np.linalg.eigvalsh(c["H_physical"])
    _check(float(np.min(eig_h)) > 0.0, f"N={n} physical H is not positive definite")

    commutators = explicit_pressure_buoyancy_commutators(c)
    n = c["nz"]
    p_rows = slice(2 * n, 3 * n)
    b_rows = slice(3 * n, 4 * n)
    _check(np.allclose(commutators["Rp"], c["physical_defect"][p_rows, :],
                       rtol=2.0e-12, atol=1.0e-10),
           f"N={n} independently derived pressure commutator disagrees with returned row difference")
    _check(np.allclose(commutators["Rb"], c["physical_defect"][b_rows, :],
                       rtol=2.0e-12, atol=1.0e-10),
           f"N={n} independently derived buoyancy commutator disagrees with returned row difference")

    closure = _conservative_closure(c)
    q0, eigenvalue = _mode_q0(c)
    # Exercise the top-work identity with independent prescribed physical
    # amplitudes so the test cannot pass merely because zeta or Wtop is zero.
    physical_probe = np.zeros(4 * n + 1, dtype=np.complex128)
    physical_probe[2 * n - 1] = 0.4 - 0.1j
    physical_probe[-1] = 2.0e-3 + 3.0e-3j
    q_probe = np.linalg.solve(c["T"], physical_probe)
    probe = ref.energy_budget(c, q_probe)
    expected_surface_power = 0.5 * c["area"] * c["rho_top"] * c["g"] * np.real(
        np.conj(physical_probe[-1]) * physical_probe[2 * n - 1]
    )
    _check(abs(probe["surface_rate"] - expected_surface_power) <
           1.0e-12 * max(abs(expected_surface_power), 1.0),
           f"N={n} moving-surface energy rate does not match the EOS boundary law")
    _check(abs(probe["boundary_flux_out"] + expected_surface_power) <
           1.0e-12 * max(abs(expected_surface_power), 1.0),
           f"N={n} physical outward top flux has the wrong sign or magnitude")
    _check(abs(probe["surface_energy_top_eos"] - 0.25 * c["area"] * c["rho_top"] * c["g"] *
               abs(physical_probe[-1]) ** 2) <
           1.0e-12 * max(probe["surface_energy_top_eos"], 1.0),
           f"N={n} EOS free-surface quadratic energy has the wrong coefficient")
    if n == 4:
        _check(abs(eigenvalue.imag - EXPECTED_OMEGA_N4) / EXPECTED_OMEGA_N4 < 1.0e-9,
               f"N=4 source frequency changed: {eigenvalue.imag:.15g}")
        period = 2.0 * np.pi / eigenvalue.imag
        exact_quarter = ref.integrate_energy_budget(c, q0, 0.25 * period)
        native_sample = ref.integrate_energy_budget(c, q0, 740.0)
        _check(abs(exact_quarter["bulk_start"] - 572094813.6290175) < 2.0,
               f"archived N=4 starting bulk energy changed: {exact_quarter['bulk_start']:.6f}")
        _check(abs(exact_quarter["bulk_end"] - 588778384.6729351) < 2.0,
               f"archived exact-quarter bulk energy changed: {exact_quarter['bulk_end']:.6f}")
        _check(abs(exact_quarter["bulk_delta"] / exact_quarter["bulk_start"]) > 1.0e-6,
               "bulk-energy diagnostic unexpectedly became constant")
        _check(abs(exact_quarter["surface_flux_closure_error"]) < 1.0e-6,
               "EOS surface storage does not cancel the physical top-work flux")
        _check(abs(exact_quarter["surface_delta"] - 1321437.1597614) < 2.0,
               "corrected EOS surface energy changed")
        _check(abs(exact_quarter["surface_end"] - exact_quarter["surface_start"] -
                   exact_quarter["surface_delta"]) < 1.0e-5,
               "surface energy was mixed into the reported bulk diagnostic")
        row_sum = sum(exact_quarter["source_vs_eulerian_row_power_integrals"].values())
        delta = exact_quarter["source_operator_delta_power_integral"]
        _check(abs(row_sum - delta) / max(abs(delta), 1.0) < 1.0e-12,
               "source row-power partition does not close to the independently weighted matrix delta")
        _check(abs(exact_quarter["matrix_budget_integral"] -
                   exact_quarter["interior_and_top_stencil_residual"]) /
               max(abs(delta), 1.0) < 1.0e-12,
               "metric plus source-row matrix powers do not close the bulk/top-flux residual")
        _check(abs(exact_quarter["rate_integral_closure_error"]) < 0.01 and
               abs(exact_quarter["surface_integral_closure_error"]) < 0.01,
               "Simpson integration does not close endpoint bulk/surface changes")
        _check(abs(native_sample["bulk_end"] - 588776902.4880365) < 2.0,
               "740 s native-sample energy changed")

        # Controlled same-q sensitivity: only the energy-map theta_z changes.
        analytic = ref.column(nz=4, nx=8, lx=40000.0, ly=30000.0,
                              theta_gradient="analytic")
        quarter_state = expm(0.25 * period * c["A"]) @ q0
        bulk_pct = lambda case: 100.0 * (
            0.5 * np.real(np.vdot(quarter_state, case["H_bulk"] @ quarter_state)) /
            (0.5 * np.real(np.vdot(q0, case["H_bulk"] @ q0))) - 1.0
        )
        legacy_pct, analytic_pct = bulk_pct(c), bulk_pct(analytic)
        _check(legacy_pct > analytic_pct and legacy_pct - analytic_pct > 0.01,
               "same-state analytic theta_z sensitivity was not resolved")
        _check(c["rho_top"] != c["rho_top_last_cell_candidate"],
               "top EOS density accidentally collapsed to the last mass-cell density")

        return {
            "nz": 4, "omega_rad_s": eigenvalue.imag, "period_s": period,
            "bulk_start": exact_quarter["bulk_start"], "bulk_exact_quarter": exact_quarter["bulk_end"],
            "bulk_change_pct": 100.0 * exact_quarter["bulk_delta"] / exact_quarter["bulk_start"],
            "surface_delta_eos": exact_quarter["surface_delta"],
            "top_flux_integral": exact_quarter["boundary_flux_out_integral"],
            "interior_top_stencil_residual": exact_quarter["interior_and_top_stencil_residual"],
            "row_power_integrals": exact_quarter["source_vs_eulerian_row_power_integrals"],
            "theta_z_legacy_bulk_change_pct_same_q": legacy_pct,
            "theta_z_analytic_bulk_change_pct_same_q": analytic_pct,
            "rho_top_eos": c["rho_top"], "rho_top_last_cell_candidate": c["rho_top_last_cell_candidate"],
            "closure": closure,
            "native_740s_bulk": native_sample["bulk_end"],
        }
    return {
        "nz": n, "physical_weights_min": float(np.min(weights)),
        "physical_H_min_eigenvalue": float(np.min(eig_h)),
        "selected_mode_omega_rad_s": eigenvalue.imag,
        "closure": closure,
        "pressure_commutator_max_abs": float(np.max(np.abs(commutators["Rp"]))),
        "buoyancy_commutator_max_abs": float(np.max(np.abs(commutators["Rb"]))),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, help="optional path for the JSON result")
    args = parser.parse_args()
    results = [_run_case(4), _run_case(8)]
    module_path = ROOT / "tools" / "wave_energy_spatial_reference.py"
    record = {
        "status": "passed",
        "source_revision": subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT,
                                           check=True, capture_output=True, text=True).stdout.strip(),
        "module_sha256": hashlib.sha256(module_path.read_bytes()).hexdigest(),
        "python": platform.python_version(), "numpy": np.__version__, "scipy": scipy.__version__,
        "cases": results,
    }
    payload = json.dumps(record, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(payload)
    print(payload, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
