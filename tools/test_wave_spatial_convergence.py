#!/usr/bin/env python3
"""Track one source-derived gravity/acoustic mode under horizontal and vertical refinement.

The source operator is assembled in wave_energy_spatial_reference.py.  Mode identity
comes from phase-independent physical-energy profile overlap on a shared height grid;
the continuum comparison is a separate Eulerian free-surface BVP.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import platform
import subprocess
import sys
from datetime import datetime
from pathlib import Path

import numpy as np
from scipy.integrate import solve_bvp


sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from wave_energy_spatial_reference import column, source_matrix  # noqa: E402


LX = 40_000.0
LY = 30_000.0
KX = 2.0 * math.pi / LX
EXPECTED_ORIGINAL_OMEGA = 0.002109960322
COMMON_Z = np.linspace(0.0, 1.0, 257)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def git_revision() -> str:
    return subprocess.run(["git", "rev-parse", "HEAD"], cwd=ROOT, check=True,
                          capture_output=True, text=True).stdout.strip()


def field(profile: dict, key: str) -> np.ndarray:
    return np.asarray(profile[key], dtype=np.float64).reshape(-1)


def sorted_xy(x: np.ndarray, y: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    order = np.argsort(x)
    xx, yy = x[order], y[order]
    unique, indices = np.unique(xx, return_index=True)
    return unique, yy[indices]


def trapezoid_weights(z: np.ndarray) -> np.ndarray:
    """Trapezoid-rule weights for samples on a monotone, possibly descending height grid."""
    dz = np.abs(np.diff(np.asarray(z, dtype=np.float64)))
    if dz.size == 0:
        return np.zeros_like(z, dtype=np.float64)
    weights = np.empty(dz.size + 1, dtype=np.float64)
    weights[0], weights[-1] = 0.5 * dz[0], 0.5 * dz[-1]
    if dz.size > 1:
        weights[1:-1] = 0.5 * (dz[:-1] + dz[1:])
    return weights


def continuous_height(eta: np.ndarray, background: dict) -> np.ndarray:
    """Hydrostatic geometric height for the specified continuous pressure profile."""
    eta = np.asarray(eta, dtype=np.float64)
    rd, cp, g = (float(background[name]) for name in ("rd", "cp", "g"))
    p_top, mass, p0 = 20000.0, 84000.0, 100000.0
    ps = p_top + mass
    kappa = rd / cp
    a, b = 317.0 + 8.0 * p_top / mass, 8.0 / mass
    pressure = p_top + mass * eta
    geopotential = rd / p0**kappa * (
        a * (ps**kappa - pressure**kappa) / kappa
        - b * (ps**(kappa + 1.0) - pressure**(kappa + 1.0)) / (kappa + 1.0)
    )
    return geopotential / g


def energy_weights(background: dict, eta: np.ndarray) -> tuple[np.ndarray, ...]:
    """Return common-height energy coefficients for (u,w,p_E,b,zeta_top)."""
    eta_mass = field(background, "eta_mass")
    theta = field(background, "theta")
    rho = field(background, "rho")
    n2 = field(background, "N2")
    p = field(background, "p_mass")
    interp = lambda values: np.interp(eta, *sorted_xy(eta_mass, values))
    theta_c, rho_c, n2_c, p_c = map(interp, (theta, rho, n2, p))
    gamma = float(background["gamma"])
    # T's b component is buoyancy acceleration, so available-potential energy
    # density is rho*|b|^2/(2*N^2).
    b_weight = rho_c / n2_c
    return rho_c, rho_c, 1.0 / (gamma * p_c), b_weight


def physical_profile(case: dict, eigvec: np.ndarray) -> dict:
    physical = np.asarray(case["T"]) @ eigvec
    nz = (len(physical) - 1) // 4
    u, w, pressure, buoyancy, zeta = np.split(physical, [nz, 2 * nz, 3 * nz, 4 * nz])
    result = {"u": u, "w": w, "p": pressure, "b": buoyancy, "zeta": zeta[0]}
    for key in ("eta_mass", "eta_w", "z_mass", "z_w"):
        if key in case:
            result[key] = field(case, key)
    return result


def shared_profile(case: dict, eigvec: np.ndarray, eta: np.ndarray = COMMON_Z) -> tuple[np.ndarray, complex]:
    """Interpolate each staggered physical field onto a common normalized-height grid."""
    prof = physical_profile(case, eigvec)
    nz = len(prof["u"])
    zm = prof.get("eta_mass", np.asarray(case["eta_mass"], dtype=float))
    zw = prof.get("eta_w", np.asarray(case["eta_w"], dtype=float))
    mass_fields = []
    for name in ("u", "p", "b"):
        x, y = sorted_xy(zm, prof[name])
        mass_fields.append(np.interp(eta, x, y.real) + 1j * np.interp(eta, x, y.imag))
    w_values = (np.concatenate(([0.0 + 0.0j], prof["w"]))
                if len(zw) == nz + 1 else prof["w"])
    x, y = sorted_xy(zw, w_values)
    w = np.interp(eta, x, y.real) + 1j * np.interp(eta, x, y.imag)
    u, p, b = mass_fields
    weights = energy_weights(case, eta)
    z_common = continuous_height(eta, case)
    quadrature = trapezoid_weights(z_common)
    scaled = np.concatenate([v * np.sqrt(wt * quadrature) for v, wt in zip((u, w, p, b), weights)])
    top = np.asarray([prof["zeta"] * np.sqrt(float(case["rho_top"]) * float(case["g"]))])
    return np.concatenate([scaled, top]), prof["zeta"]


def overlap(a: np.ndarray, b: np.ndarray) -> float:
    denom = np.linalg.norm(a) * np.linalg.norm(b)
    if not np.isfinite(denom) or denom == 0.0:
        return 0.0
    return float(abs(np.vdot(a, b)) / denom)


def node_count(profile: np.ndarray) -> int:
    """Count robust zeros after removing arbitrary complex phase."""
    if profile.size == 0:
        return 0
    pivot = profile[int(np.argmax(np.abs(profile)))]
    values = (profile * np.exp(-1j * np.angle(pivot))).real
    cutoff = 1e-7 * max(float(np.max(np.abs(values))), np.finfo(float).tiny)
    signs = np.sign(values[np.abs(values) > cutoff])
    return int(np.count_nonzero(signs[1:] * signs[:-1] < 0))


def participation(profile: np.ndarray) -> float:
    power = np.abs(profile) ** 2
    if power.size == 0 or np.sum(power**2) == 0:
        return 0.0
    return float(np.sum(power) ** 2 / (power.size * np.sum(power**2)))


def eigensystem(case: dict) -> tuple[np.ndarray, np.ndarray]:
    values, vectors = np.linalg.eig(np.asarray(case["A"], dtype=np.complex128))
    # Keep one propagation direction.  The conjugate eigenpair is the same
    # real standing-wave mode and must not create a false zero ambiguity gap.
    keep = np.isfinite(values.real) & np.isfinite(values.imag) & (values.imag > 1e-8)
    return values[keep], vectors[:, keep]


def select_mode(case: dict, previous: np.ndarray | None) -> tuple[complex, np.ndarray, dict]:
    values, vectors = eigensystem(case)
    if not len(values):
        raise RuntimeError("source matrix has no finite oscillatory eigenvalues")
    profiles = [shared_profile(case, vectors[:, i])[0] for i in range(len(values))]
    w_nodes = [node_count(physical_profile(case, vectors[:, i])["w"])
               for i in range(len(values))]
    baseline_frequency_error = None
    if previous is None:
        # Anchor using the archived N=4 frequency and one-node vertical profile,
        # then measure candidate ambiguity using physical profile overlaps.
        eligible = [i for i, count in enumerate(w_nodes) if count == 1]
        if not eligible:
            raise RuntimeError("N=4 anchor search found no one-node W mode")
        anchor = min(eligible, key=lambda i: abs(abs(values[i].imag) - EXPECTED_ORIGINAL_OMEGA))
        baseline_frequency_error = abs(abs(values[anchor].imag) / EXPECTED_ORIGINAL_OMEGA - 1.0)
        scores = np.asarray([overlap(profiles[anchor], p) for p in profiles])
    else:
        scores = np.asarray([overlap(previous, p) for p in profiles])
    order = np.argsort(scores)[::-1]
    best = int(order[0])
    second = float(scores[order[1]]) if len(order) > 1 else 0.0
    details = {
        "overlap": float(scores[best]),
        "runner_up_overlap": second,
        "overlap_gap": float(scores[best] - second),
        "oscillatory_candidates": int(len(values)),
        "phase_rate": float(values[best].real),
        "omega": float(abs(values[best].imag)),
        "eigenvalue": [float(values[best].real), float(values[best].imag)],
        "profile_node_count_b": node_count(physical_profile(case, vectors[:, best])["b"]),
        "profile_node_count_w": w_nodes[best],
        "baseline_frequency_relative_error": baseline_frequency_error,
        "profile_participation_b": participation(profiles[best][3 * len(COMMON_Z):4 * len(COMMON_Z)]),
        "profile_participation_w": participation(profiles[best][len(COMMON_Z):2 * len(COMMON_Z)]),
    }
    return values[best], vectors[:, best], details


def continuum_bvp(reference: dict) -> dict:
    """Independent Eulerian compressible free-surface eigenproblem in height coordinates."""
    rd, cp, cv, g = (float(reference[name]) for name in ("rd", "cp", "cv", "g"))
    pt, mass = 20000.0, 84000.0
    ps = pt + mass
    kappa = rd / cp
    a, b = 317.0 + 8.0 * pt / mass, 8.0 / mass
    gamma = cp / cv
    p_desc = np.linspace(ps, pt, 6001)
    z_desc = rd / (100000.0**kappa * g) * (
        a * (ps**kappa - p_desc**kappa) / kappa
        - b * (ps**(kappa + 1) - p_desc**(kappa + 1)) / (kappa + 1)
    )
    height = float(z_desc[-1])
    eta_desc = (p_desc - pt) / mass
    theta_desc = a - b * p_desc
    rho_desc = p_desc / (rd * theta_desc * (p_desc / 100000.0) ** (rd / cp))
    n2_desc = b * rho_desc * g * g / theta_desc
    pressure = lambda z: np.interp(z, z_desc, p_desc)
    density = lambda z: np.interp(z, z_desc, rho_desc)
    buoyancy_freq2 = lambda z: np.interp(z, z_desc, n2_desc)
    sound2 = lambda z: gamma * pressure(z) / density(z)
    kt = float(reference["k_physical"])
    rho_top = float(rho_desc[-1])
    omega_guess = float(reference.get("omega_guess", EXPECTED_ORIGINAL_OMEGA))
    p_scale = rho_top * g / max(omega_guess, 1e-12)
    zz = np.linspace(0.0, height, 241)
    initial = np.zeros((2, zz.size), dtype=np.complex128)
    initial[1] = np.sin(0.5 * math.pi * zz / height)
    initial[0] = (rho_top * g / (1j * omega_guess) / p_scale) * initial[1]

    def fun(z: np.ndarray, y: np.ndarray, param: np.ndarray) -> np.ndarray:
        lam = param[0]
        p = p_scale * y[0]
        w = y[1]
        rho, n2, c2, pbase = density(z), buoyancy_freq2(z), sound2(z), pressure(z)
        dp = (-rho * (lam + n2 / lam) * w - g * p / c2) / p_scale
        dw = (-lam * p / (gamma * pbase) + rho * g * w / (gamma * pbase)
              - kt * kt * p / (rho * lam))
        return np.vstack((dp, dw))

    def bc(ya: np.ndarray, yb: np.ndarray, param: np.ndarray) -> np.ndarray:
        lam = param[0]
        return np.asarray([ya[1], yb[0] - (rho_top * g / (lam * p_scale)) * yb[1], yb[1] - 1.0])

    solution = solve_bvp(fun, bc, zz, initial, p=np.asarray([1j * omega_guess]),
                         tol=2e-6, max_nodes=50000, verbose=0)
    lam = complex(solution.p[0])
    zcheck = np.linspace(0.0, height, 1025)
    state = solution.sol(zcheck)
    eta_check = np.interp(zcheck, z_desc, eta_desc)
    pressure_e = p_scale * state[0]
    velocity = -1j * kt * pressure_e / (density(zcheck) * lam)
    buoyancy = -buoyancy_freq2(zcheck) * state[1] / lam
    return {
        "success": bool(solution.success), "message": solution.message,
        "iterations": int(solution.niter), "nodes": int(solution.x.size),
        "height_m": height, "eigenvalue": [lam.real, lam.imag],
        "omega": abs(lam.imag), "growth_rate": lam.real,
        "profile_node_count_w": node_count(state[1]),
        "boundary_residual_max": float(np.max(np.abs(bc(solution.y[:, 0], solution.y[:, -1], solution.p)))),
        "_eta_profile": eta_check,
        "_u_profile": velocity,
        "_w_profile": state[1],
        "_p_profile": pressure_e,
        "_b_profile": buoyancy,
        "_zeta_top": state[1, -1] / lam,
        "_background": {"p": pressure(zcheck), "rho": density(zcheck),
                        "N2": buoyancy_freq2(zcheck)},
        "k_physical": kt,
    }


def compare_continuum(case: dict, eigvec: np.ndarray, continuum: dict) -> dict:
    """Compare physical mode amplitude and phase after common-eta interpolation."""
    eta = COMMON_Z
    source = physical_profile(case, eigvec)
    eta_mass = field(case, "eta_mass")
    eta_w = field(case, "eta_w")
    cont_eta = np.asarray(continuum["_eta_profile"])
    def interp(values: np.ndarray, x: np.ndarray) -> np.ndarray:
        xx, yy = sorted_xy(x, values)
        return np.interp(eta, xx, yy.real) + 1j * np.interp(eta, xx, yy.imag)
    source_w_values = np.concatenate(([0.0 + 0.0j], source["w"]))
    source_fields = [interp(source[name], eta_mass) for name in ("u", "p", "b")]
    source_fields.insert(1, interp(source_w_values, eta_w))
    cont_fields = [interp(np.asarray(continuum[f"_{name}_profile"]), cont_eta)
                   for name in ("u", "w", "p", "b")]
    pbase = 20000.0 + 84000.0 * eta
    theta = 317.0 - 8.0 * eta
    rho = pbase / (float(case["rd"]) * theta * (pbase / 100000.0) ** (float(case["rd"]) / float(case["cp"])))
    n2 = (8.0 / 84000.0) * rho * float(case["g"]) ** 2 / theta
    weight = (rho, rho, 1.0 / (float(case["gamma"]) * pbase), rho / n2)
    quadrature = trapezoid_weights(continuous_height(eta, case))
    def weighted(fields: list[np.ndarray], zeta: complex) -> np.ndarray:
        parts = [f * np.sqrt(w * quadrature) for f, w in zip(fields, weight)]
        parts.append(np.asarray([zeta * np.sqrt(float(case["rho_top"]) * float(case["g"]))]))
        return np.concatenate(parts)
    source_lambda = np.vdot(eigvec, case["A"] @ eigvec) / np.vdot(eigvec, eigvec)
    source_scale = 1.0 / source_fields[1][0] if abs(source_fields[1][0]) > 0.0 else 1.0 + 0j
    source_fields = [value * source_scale for value in source_fields]
    s = weighted(source_fields, source["zeta"] * source_scale)
    c = weighted(cont_fields, complex(continuum["_zeta_top"]))
    denom = float(np.vdot(c, c).real)
    scale = np.vdot(c, s) / denom if denom > 0.0 else 0j
    residual = s - scale * c
    norm = float(np.linalg.norm(s))
    lam_source = source_lambda
    source_omega = float(abs(lam_source.imag))
    continuum_omega = float(continuum["omega"])
    return {
        "source_omega": source_omega,
        "continuum_omega": continuum_omega,
        "frequency_delta_fraction": source_omega / continuum_omega - 1.0,
        "continuum_overlap": overlap(s, c),
        "optimal_amplitude_ratio": float(abs(scale)),
        "optimal_phase_offset_rad": float(np.angle(scale)),
        "source_top_w_magnitude_after_normalization": float(abs(source_fields[1][0])),
        "source_top_zeta_magnitude": float(abs(source["zeta"] * source_scale)),
        "continuum_top_zeta_magnitude": float(abs(continuum["_zeta_top"])),
        "relative_profile_residual": float(np.linalg.norm(residual) / norm) if norm else math.inf,
    }


def source_residual_by_physical_row(case: dict, continuum: dict) -> dict:
    """Apply the source matrix to the sampled continuum eigenfunction, by physical row."""
    eta_m = field(case, "eta_mass")
    eta_w = field(case, "eta_w")[1:]
    eta = np.asarray(continuum["_eta_profile"])
    def at(values: np.ndarray, points: np.ndarray) -> np.ndarray:
        return np.interp(points, *sorted_xy(eta, values.real)) + 1j * np.interp(
            points, *sorted_xy(eta, values.imag)
        )
    x = np.concatenate([
        at(np.asarray(continuum["_u_profile"]), eta_m),
        at(np.asarray(continuum["_w_profile"]), eta_w),
        at(np.asarray(continuum["_p_profile"]), eta_m),
        at(np.asarray(continuum["_b_profile"]), eta_m),
        np.asarray([continuum["_zeta_top"]]),
    ])
    q = np.linalg.solve(case["T"], x)
    source_tendency = case["T"] @ (case["A"] @ q)
    lam = complex(*continuum["eigenvalue"])
    residual = source_tendency - lam * x
    n = int(case["nz"])
    spans = {"u": slice(0, n), "w": slice(n, 2*n),
             "pressure_eulerian": slice(2*n, 3*n),
             "buoyancy": slice(3*n, 4*n), "surface_displacement": slice(4*n, 4*n+1)}
    out = {}
    for name, span in spans.items():
        local = residual[span]
        local_index = int(np.argmax(np.abs(local)))
        if name == "w":
            eta_values = eta_w
        elif name == "surface_displacement":
            eta_values = np.asarray([0.0])
        else:
            eta_values = eta_m
        out[name] = {
            "relative_tendency_residual": float(np.linalg.norm(local) /
                max(np.linalg.norm((lam * x)[span]), np.finfo(float).tiny)),
            "max_abs_residual": float(np.abs(local[local_index])),
            "max_residual_eta": float(eta_values[local_index]),
            "max_abs_source_tendency": float(np.max(np.abs(source_tendency[span]))),
            "max_abs_continuum_tendency": float(np.max(np.abs((lam * x)[span]))),
        }
    # The physical pressure tendency is assembled from source geopotential,
    # theta, and column-mass tendencies by the transform. Expose their sizes.
    tqdot = case["A"] @ q
    ptransform = case["T"][2*n:3*n]
    out["pressure_tendency_source_blocks"] = {
        name: float(np.linalg.norm(ptransform[:, span] @ tqdot[span]))
        for name, span in {"U": slice(0,n), "W": slice(n,2*n),
                           "PHI": slice(2*n,3*n), "THETA": slice(3*n,4*n),
                           "MU": slice(4*n,4*n+1)}.items()
    }
    return out


def make_case(nz: int, nx: int, legacy_fp32: bool = False) -> dict:
    return column(nz=nz, nx=nx, lx=LX, mode_k=KX,
                  legacy_fp32=legacy_fp32, theta_gradient="analytic")


def check_contracts(evidence: dict) -> list[str]:
    """Return failures for the source baseline, branch identity, BVP and convergence contracts."""
    failures: list[str] = []
    h0 = evidence["horizontal"][0]
    fp32 = evidence["source_fp32_phb_diagnostic"]
    for label, relative_error in (
        ("analytic FP64 N=4 baseline", h0["baseline_frequency_relative_error"]),
        ("legacy FP32 N=4 baseline", fp32["baseline_frequency_relative_error"]),
    ):
        if not np.isfinite(relative_error) or relative_error > 1.0e-7:
            failures.append(f"{label} relative frequency error {relative_error!r} exceeds 1e-7")

    bvp_records = (evidence["continuum"], evidence["continuum_nx8_symbol"])
    for index, bvp in enumerate(bvp_records):
        if not bvp["success"]:
            failures.append(f"continuum BVP {index} failed: {bvp['message']}")
        residual = float(bvp["boundary_residual_max"])
        if not np.isfinite(residual) or residual > 1.0e-5:
            failures.append(f"continuum BVP {index} boundary residual {residual:.6g} exceeds 1e-5")

    records = evidence["horizontal"] + evidence["vertical"] + evidence["coupled"]
    records.append(evidence["horizontal_symbol_endpoint_nz4"])
    reference_nodes = int(evidence["continuum"]["profile_node_count_w"])
    for index, record in enumerate(records):
        overlap_value = float(record["overlap"])
        gap = float(record["overlap_gap"])
        nodes = int(record["profile_node_count_w"])
        if nodes != reference_nodes:
            failures.append(f"tracked mode {index} W-node count {nodes} differs from BVP/source anchor {reference_nodes}")
        if not np.isfinite(overlap_value) or overlap_value <= 0.0:
            failures.append(f"tracked mode {index} has non-positive or non-finite profile overlap {overlap_value!r}")
        if not np.isfinite(gap) or gap <= 0.0:
            failures.append(f"tracked mode {index} has non-positive or non-finite overlap gap {gap!r}")

    coupled_errors = np.abs(np.asarray(
        [record["continuum_physical_fractional_error"] for record in evidence["coupled"]],
        dtype=np.float64,
    ))
    if not np.all(np.isfinite(coupled_errors)):
        failures.append("coupled continuum frequency errors contain non-finite values")
    elif not np.all(np.diff(coupled_errors) < 0.0):
        failures.append(f"coupled continuum frequency errors are not strictly decreasing: {coupled_errors.tolist()}")
    else:
        orders = np.log2(coupled_errors[:-1] / coupled_errors[1:])
        evidence["coupled_observed_orders"] = orders.tolist()
        for order_index, order in enumerate(orders[-2:], start=len(orders) - 2):
            if not 1.7 <= float(order) <= 2.3:
                failures.append(f"coupled observed order {order_index} is {order:.6g}, outside [1.7, 2.3]")
    evidence["coupled_relative_frequency_errors"] = coupled_errors.tolist()
    final_error = float(coupled_errors[-1]) if coupled_errors.size else math.inf
    if not np.isfinite(final_error) or final_error > 1.0e-3:
        failures.append(f"final coupled relative frequency error {final_error:.6g} exceeds 1e-3")
    return failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, default=Path("wave_spatial_convergence.json"))
    args = parser.parse_args()
    fp32 = False
    evidence = {
        "schema": "sdirk3.wave-spatial-convergence.v1",
        "source_revision": git_revision(),
        "generated_at_local": datetime.now().astimezone().isoformat(timespec="seconds"),
        "setup": {"lx_m": LX, "ly_m": LY, "k_physical_m-1": KX,
                  "theta_profile": "317 - 8*eta K", "p_top_pa": 20000.0,
                  "mass_total_pa": 84000.0, "base_mass_pa": 80000.0,
                  "base_theta_k": 300.0, "legacy_fp32_coefficients": fp32,
                  "theta_gradient": "analytic"},
        "horizontal": [], "vertical": [], "coupled": [], "continuum": None,
    }
    # The exact source fixture is Nx=8, Ny=6, Nz=4, dx=dy=5 km. Ly enters the
    # physical area only; the y-uniform mode has no y derivative.
    previous = None
    anchor_case = None
    anchor_vec = None
    horizontal_anchor_profile = None
    horizontal_final_case = None
    horizontal_final_vec = None
    horizontal_final_profile = None
    for nx in (8, 16, 32, 64):
        case = make_case(4, nx, fp32)
        value, vec, detail = select_mode(case, previous)
        profile, _ = shared_profile(case, vec)
        previous = profile
        horizontal_final_case, horizontal_final_vec = case, vec
        horizontal_final_profile = profile
        if nx == 8:
            anchor_case, anchor_vec = case, vec
            horizontal_anchor_profile = profile
        evidence["horizontal"].append({"nx": nx, "nz": 4, "dx_m": float(case["dx"]),
                                        "k_physical": float(case["k_physical"]),
                                        "kappa_source": float(case["kappa"]), **detail})
    evidence["original_fixture"] = {
        "nx": 8, "ny": 6, "nz": 4, "dx_m": 5000.0,
        "omega_reference": EXPECTED_ORIGINAL_OMEGA,
        "frequency_delta_fraction": evidence["horizontal"][0]["omega"] / EXPECTED_ORIGINAL_OMEGA - 1.0,
        "note": "Historical four-level source reference; fixed-domain inputs are reproduced separately.",
    }
    previous = horizontal_anchor_profile
    vertical_final_case = None
    vertical_final_vec = None
    for nz in (4, 8, 16, 32, 64):
        case = make_case(nz, 8, fp32)
        value, vec, detail = select_mode(case, previous)
        previous = shared_profile(case, vec)[0]
        vertical_final_case, vertical_final_vec = case, vec
        evidence["vertical"].append({"nx": 8, "nz": nz, "dx_m": float(case["dx"]),
                                      "k_physical": float(case["k_physical"]),
                                      "kappa_source": float(case["kappa"]), **detail})
    ref = anchor_case
    horizontal_symbol_endpoint = make_case(4, 8, fp32)
    horizontal_symbol_endpoint["kappa"] = KX
    horizontal_symbol_endpoint["A"] = source_matrix(horizontal_symbol_endpoint)
    _, _, horizontal_endpoint_detail = select_mode(horizontal_symbol_endpoint,
                                                    horizontal_final_profile)
    evidence["horizontal_symbol_endpoint_nz4"] = {
        "kappa_source_endpoint": KX,
        "basis": "same source-transcribed vertical operator with exact continuous horizontal derivative symbol",
        **horizontal_endpoint_detail,
    }
    continuum_input = {key: ref[key] for key in ("g", "rd", "cp", "cv", "k_physical")}
    continuum_input["omega_guess"] = evidence["horizontal"][0]["omega"]
    continuum_physical = continuum_bvp(continuum_input)
    continuum_nx8_input = dict(continuum_input)
    continuum_nx8_input["k_physical"] = float(ref["kappa"])
    continuum_nx8 = continuum_bvp(continuum_nx8_input)
    evidence["continuum"] = continuum_physical
    evidence["continuum_nx8_symbol"] = {k: v for k, v in continuum_nx8.items()
                                        if not k.startswith("_")}
    evidence["continuum_comparison"] = {
        "horizontal_nx64_nz4": compare_continuum(horizontal_final_case, horizontal_final_vec,
                                                   continuum_physical),
        "vertical_nx8_nz64": compare_continuum(vertical_final_case, vertical_final_vec,
                                                 continuum_nx8),
    }
    evidence["sampled_continuum_row_residuals"] = {
        "horizontal_nx64_nz4": source_residual_by_physical_row(horizontal_final_case,
                                                                 continuum_physical),
        "vertical_nx8_nz64": source_residual_by_physical_row(vertical_final_case,
                                                               continuum_nx8),
    }
    previous = None
    for nx, nz in ((8, 4), (16, 8), (32, 16), (64, 32), (128, 64)):
        case = make_case(nz, nx, fp32)
        _, vec, detail = select_mode(case, previous)
        previous = shared_profile(case, vec)[0]
        evidence["coupled"].append({"nx": nx, "nz": nz, "dx_m": float(case["dx"]),
                                    "kappa_source": float(case["kappa"]), **detail,
                                    "continuum_physical_fractional_error":
                                        detail["omega"] / continuum_physical["omega"] - 1.0})
    fp32_case = make_case(4, 8, True)
    _, _, fp32_detail = select_mode(fp32_case, None)
    evidence["source_fp32_phb_diagnostic"] = {
        "nx": 8, "nz": 4, **fp32_detail,
        "omega_delta_from_analytic_fp64": fp32_detail["omega"] - evidence["horizontal"][0]["omega"],
    }
    evidence["provenance"] = {
        "runner_sha256": sha256(Path(__file__)),
        "reference_sha256": sha256(ROOT / "tools" / "wave_energy_spatial_reference.py"),
        "source_sha256": {
            "stable_wave_reference.h": sha256(ROOT / "external/libtorch_wrf/sdirk3/tests/stable_wave_reference.h"),
            "tile_test_fixture.h": sha256(ROOT / "external/libtorch_wrf/sdirk3/tests/tile_test_fixture.h"),
            "module_big_step_utilities_em.F": sha256(ROOT / "dyn_em/module_big_step_utilities_em.F"),
            "solve_em.F": sha256(ROOT / "dyn_em/solve_em.F"),
            "test_stable_wave_reference_rows.py": sha256(ROOT / "tools/test_stable_wave_reference_rows.py"),
        },
        "graphify_navigation": "Graph and source-binding provenance are maintained by the task receipt; not needed to run this numerical reference.",
        "python": sys.version.split()[0], "numpy": np.__version__,
        "platform": platform.platform(), "scipy": __import__("scipy").__version__,
    }
    evidence["continuum"] = {k: v for k, v in evidence["continuum"].items()
                              if not k.startswith("_")}
    contract_failures = check_contracts(evidence)
    evidence["contract"] = {
        "passed": not contract_failures,
        "failures": contract_failures,
        "source_baseline_relative_tolerance": 1.0e-7,
        "continuum_boundary_residual_max": 1.0e-5,
        "coupled_observed_order_window": [1.7, 2.3],
        "coupled_final_relative_frequency_error_max": 1.0e-3,
        "tracked_w_node_count": int(evidence["continuum"]["profile_node_count_w"]),
        "scope": "source-discrete modal reference; no WRF/native-grid forecast claim",
    }
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(evidence, indent=2, allow_nan=False) + "\n")
    print(json.dumps({"horizontal": evidence["horizontal"], "vertical": evidence["vertical"],
                      "coupled": evidence["coupled"],
                      "horizontal_symbol_endpoint_nz4": evidence["horizontal_symbol_endpoint_nz4"],
                      "continuum_nx8_symbol": evidence["continuum_nx8_symbol"],
                      "continuum": {k: v for k, v in evidence["continuum"].items() if not k.startswith("_")},
                      "continuum_comparison": evidence["continuum_comparison"],
                      "source_fp32_phb_diagnostic": evidence.get("source_fp32_phb_diagnostic"),
                      "contract": evidence["contract"],
                      "out": str(args.out)}, indent=2))
    return 0 if evidence["contract"]["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
