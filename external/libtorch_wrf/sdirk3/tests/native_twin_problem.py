"""Offline problem construction and scoring for the native idealized twin.

This module defines the two-control physical templates and direct packed-slot
observation operator. It does not launch WRF or claim any model result.
"""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re
import subprocess
from typing import Any

import netCDF4
import numpy as np


PACKED_SIZE = 24531
STEPS = (128, 256)
OWNER_ORDINALS = (2, 6, 10, 14)
BLOCKS = {
    "U": (0, (17, 16, 18)),
    "V": (4896, (18, 16, 17)),
    "W": (9792, (17, 17, 17)),
    "PH": (14705, (17, 17, 17)),
    "T": (19618, (17, 16, 17)),
    "MU": (24242, (17, 17)),
}
FIELD_INFO = {
    "U": ("X face", "m s-1", 0.05),
    "V": ("Y face", "m s-1", 0.05),
    "theta": ("mass", "K", 0.01),
}
GRID_LAYOUT_ID = "wrf_em_b_wave_16cube_native_packed_v1"
TILE_ORIGIN_GLOBAL = (1, 1, 1)
CPP_BASE_COMMIT = "e111d2bea3a2c3fa34eb117b131cb03cf89eda5f"
CPP_BASE_SHA256 = "7a2b921f44a9356fd7e73efa7ca82499b7c816ab43822fae28dd23933eb72a86"
EXPECTED_ARRAYS = {
    "P_f64": ((2, PACKED_SIZE), np.dtype("<f8")),
    "Z_star_f64": ((PACKED_SIZE,), np.dtype("<f8")),
    "Zb_f64": ((PACKED_SIZE,), np.dtype("<f8")),
    "observation_slots_i32": ((384,), np.dtype("<i4")),
    "observation_values_f64": ((384,), np.dtype("<f8")),
    "sigma_f64": ((384,), np.dtype("<f8")),
    "mu_base_f64": ((16, 16), np.dtype("<f8")),
    "ph_base_f64": ((16, 17, 16), np.dtype("<f8")),
    "ph_perturbation_f64": ((16, 17, 16), np.dtype("<f8")),
}


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _packed_slot(field: str, i: int, j: int, k: int) -> int:
    offset, shape = BLOCKS[field]
    if field == "MU":
        raise ValueError("MU has no observation rows")
    nj, nk, ni = shape
    if not (0 <= i < ni and 0 <= j < nj and 0 <= k < nk):
        raise IndexError(f"{field} packed index {(i, j, k)} is outside {shape}")
    return offset + (j * nk + k) * ni + i


def validate_observations(
    rows: list[dict[str, Any]],
    slots: np.ndarray,
    values: np.ndarray,
    sigma: np.ndarray,
) -> None:
    """Reject malformed/reordered observation ledgers and NPZ/JSON disagreement."""
    slots = np.asarray(slots)
    values = np.asarray(values)
    sigma = np.asarray(sigma)
    if len(rows) != 384:
        raise ValueError("observation ledger must contain exactly 384 rows")
    if slots.shape != (384,) or slots.dtype != np.dtype("<i4"):
        raise ValueError("observation slots must be int32[384]")
    if values.shape != (384,) or values.dtype != np.dtype("<f8"):
        raise ValueError("observation values must be float64[384]")
    if sigma.shape != (384,) or sigma.dtype != np.dtype("<f8"):
        raise ValueError("observation sigma must be float64[384]")
    if not (
        np.isfinite(values).all() and np.isfinite(sigma).all() and np.all(sigma > 0)
    ):
        raise ValueError(
            "observation values and sigma must be finite; sigma must be positive"
        )

    expected: list[tuple[int, str, int, int, int, int]] = []
    for step in STEPS:
        for field in ("U", "V", "theta"):
            for ok in OWNER_ORDINALS:
                for oj in OWNER_ORDINALS:
                    for oi in OWNER_ORDINALS:
                        expected.append(
                            (
                                step,
                                field,
                                oi,
                                oj,
                                ok,
                                _packed_slot(
                                    "T" if field == "theta" else field,
                                    oi - 1,
                                    oj - 1,
                                    ok - 1,
                                ),
                            )
                        )
    ids: set[str] = set()
    expected_slots = np.empty(384, dtype=np.int32)
    expected_sigma = np.empty(384, dtype=np.float64)
    row_values = np.empty(384, dtype=np.float64)
    for n, (row, key) in enumerate(zip(rows, expected, strict=True)):
        step, field, oi, oj, ok, slot = key
        stagger, unit, weight = FIELD_INFO[field]
        uid = row.get("unique_id")
        if not isinstance(uid, str) or uid in ids:
            raise ValueError("observation unique_id values must be distinct strings")
        ids.add(uid)
        required = {
            "accepted_step": step,
            "time_seconds": step * 0.9375,
            "field": field,
            "stagger": stagger,
            "unit": unit,
            "owner_i": oi,
            "owner_j": oj,
            "owner_k": ok,
            "packed_local_i0": oi - 1,
            "packed_local_j0": oj - 1,
            "packed_local_k0": ok - 1,
            "global_i": TILE_ORIGIN_GLOBAL[0] + oi - 1,
            "global_j": TILE_ORIGIN_GLOBAL[1] + oj - 1,
            "global_k": TILE_ORIGIN_GLOBAL[2] + ok - 1,
            "global_index_base": 1,
            "tile_origin_global_indices": {"i": 1, "j": 1, "k": 1},
            "packed_slot": slot,
            "grid_layout_id": GRID_LAYOUT_ID,
        }
        if any(row.get(name) != value for name, value in required.items()):
            raise ValueError(
                f"observation row {n} violates its frozen role/order/layout"
            )
        if uid != f"twin-s{step}-{field}-oi{oi}-oj{oj}-ok{ok}":
            raise ValueError(f"observation row {n} has an unexpected unique_id")
        if row.get("sigma") != weight:
            raise ValueError(f"observation row {n} has the wrong field weight")
        coords = row.get("horizontal_physical_coordinates")
        vertical = row.get("native_vertical_coordinate")
        if not isinstance(coords, dict) or not isinstance(
            coords.get("model_grid"), dict
        ):
            raise ValueError(f"observation row {n} lacks native horizontal coordinates")
        coordinate_values = [
            coords.get("XLAT_degrees"),
            coords.get("XLONG_degrees"),
            coords["model_grid"].get("x_m"),
            coords["model_grid"].get("y_m"),
        ]
        if (
            not isinstance(vertical, dict)
            or not all(
                isinstance(v, (int, float)) and np.isfinite(v)
                for v in coordinate_values
            )
            or not all(
                isinstance(vertical.get(k), (int, float)) and np.isfinite(vertical[k])
                for k in ("ZNU", "zeta_1_minus_ZNU")
            )
        ):
            raise ValueError(f"observation row {n} has invalid coordinates")
        row_value = row.get("truth_value_f64")
        if not isinstance(row_value, (int, float)) or not np.isfinite(row_value):
            raise ValueError(f"observation row {n} has a non-finite truth value")
        expected_slots[n], expected_sigma[n], row_values[n] = slot, weight, row_value
    if (
        len(ids) != 384
        or len(np.unique(expected_slots[:192])) != 192
        or len(np.unique(expected_slots[192:])) != 192
    ):
        raise ValueError(
            "observation identifiers or within-time packed slots are duplicated"
        )
    if not np.array_equal(slots, expected_slots):
        raise ValueError("NPZ observation slots disagree with the frozen row ledger")
    if not np.array_equal(sigma, expected_sigma):
        raise ValueError("NPZ observation weights disagree with the frozen row ledger")
    if not np.array_equal(values, row_values):
        raise ValueError("NPZ observation values disagree with the frozen row ledger")


def _validate_artifact_pins(directory: Path, manifest: dict[str, Any]) -> None:
    """Verify setup artifacts and source pins before reading their numerical arrays."""
    if (
        manifest.get("schema") != "native_idealized_twin_artifact_manifest_v1"
        or manifest.get("status") != "generated_problem_setup_no_model_run"
    ):
        raise ValueError("unexpected artifact manifest schema or status")
    for key, filename in (
        ("problem_npz", "problem.npz"),
        ("observations_json", "observations.json"),
        ("background_seed_f64", "initial_state_a0_f64.bin"),
    ):
        pin = manifest.get(key)
        path = directory / filename
        if (
            not isinstance(pin, dict)
            or Path(pin.get("path", "")).resolve() != path.resolve()
        ):
            raise ValueError(f"manifest {key} path does not identify {filename}")
        if (
            not path.is_file()
            or path.stat().st_size != pin.get("bytes")
            or _sha256(path) != pin.get("sha256")
        ):
            raise ValueError(f"manifest pin mismatch for {filename}")

    root = Path(__file__).resolve().parents[4]
    plan_pin = manifest.get("plan_source", {})
    plan_path = root / "docs/plans/native_idealized_twin_v1.json"
    graph_pin = manifest.get("source_graph_receipt", {})
    for label, pin, expected_path in (
        ("plan", plan_pin, plan_path),
        ("source graph receipt", graph_pin, Path(graph_pin.get("path", ""))),
    ):
        if (
            not expected_path.is_file()
            or Path(pin.get("path", "")).resolve() != expected_path.resolve()
        ):
            raise ValueError(f"manifest {label} path is missing or unexpected")
        if _sha256(expected_path) != pin.get("sha256"):
            raise ValueError(f"manifest {label} source hash changed")

    source_files = manifest.get("source_files", {})
    for relpath in (
        "external/libtorch_wrf/sdirk3/tests/native_twin_problem.py",
        "external/libtorch_wrf/sdirk3/tests/native_terminal_adjoint_test.F",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_state_layout.h",
        "external/libtorch_wrf/sdirk3/wrf_sdirk3_owned_box.h",
    ):
        source = root / relpath
        if not source.is_file() or _sha256(source) != source_files.get(relpath):
            raise ValueError(f"manifest source hash mismatch for {relpath}")
    cpp_rel = "external/libtorch_wrf/sdirk3/tests/native_terminal_adjoint_test.cpp"
    if manifest.get("base_source_commit") != CPP_BASE_COMMIT:
        raise ValueError("manifest C++ source base commit changed")
    try:
        base_cpp = subprocess.run(
            ["git", "-C", str(root), "show", f"{CPP_BASE_COMMIT}:{cpp_rel}"],
            check=True,
            capture_output=True,
        ).stdout
    except (OSError, subprocess.CalledProcessError) as exc:
        raise ValueError("cannot verify pinned committed C++ source") from exc
    cpp_hash = hashlib.sha256(base_cpp).hexdigest()
    if (
        cpp_hash != CPP_BASE_SHA256
        or source_files.get(cpp_rel + "@" + CPP_BASE_COMMIT) != cpp_hash
    ):
        raise ValueError("manifest committed C++ base source hash changed")

    for label in ("truth_source", "wrfinput_source"):
        pin = manifest.get(label, {})
        path = Path(pin.get("path", ""))
        if (
            not path.is_file()
            or path.stat().st_size != pin.get("bytes")
            or _sha256(path) != pin.get("sha256")
        ):
            raise ValueError(f"manifest input source hash mismatch for {label}")
    truth_pin = manifest["truth_source"]
    if truth_pin.get("dtype") != "<f8" or truth_pin.get("shape") != [257, PACKED_SIZE]:
        raise ValueError("pinned truth ledger has an unexpected dtype or shape")
    input_pin = manifest["wrfinput_source"]
    if (
        input_pin.get("dimensions") != [16, 16, 16]
        or input_pin.get("DX_m") != 100000.0
        or input_pin.get("DY_m") != 100000.0
    ):
        raise ValueError("pinned WRFinput grid differs from the frozen twin setup")
    tile_pin = manifest.get("tile_bounds_source", {})
    tile_path = Path(tile_pin.get("path", ""))
    if not tile_path.is_file() or _sha256(tile_path) != tile_pin.get("sha256"):
        raise ValueError("archived registered tile-bounds log hash changed")
    current_tile = _verify_registered_tile_log(tile_path)
    if current_tile["tile_origin_global_indices_fortran_1based"] != tile_pin.get(
        "tile_origin_global_indices_fortran_1based"
    ):
        raise ValueError("registered tile origin disagrees with archived log bounds")


def _load_arrays(path: Path) -> dict[str, np.ndarray]:
    with np.load(path, allow_pickle=False) as archive:
        if set(archive.files) != set(EXPECTED_ARRAYS):
            raise ValueError("problem NPZ has an unexpected array schema")
        arrays = {name: archive[name] for name in EXPECTED_ARRAYS}
    for name, (shape, dtype) in EXPECTED_ARRAYS.items():
        array = arrays[name]
        if array.shape != shape or array.dtype != dtype:
            raise ValueError(f"problem NPZ array {name} has wrong shape or dtype")
        if not np.isfinite(array).all():
            raise ValueError(f"problem NPZ array {name} contains non-finite values")
    return arrays


def _verify_registered_tile_log(path: Path) -> dict[str, Any]:
    """Bind the registered 1-based physical tile origin to the archived run log."""
    text = path.read_text(errors="strict")
    patterns = {
        "domain": r"ids,ide,jds,jde\s+1\s+17\s+1\s+17",
        "single_tile": r"set_tiles3:\s+NUMBER OF TILES\s*=\s*1",
        "vertical_tile": r"kds/kde/kms/kme/kts/kte\s*=\s*1\s+17\s+1\s+17\s+1\s+16",
        "x_tile": r"periodic_x=T, ids/ide/ims/ime/its/ite=\s*1\s+17\s+-4\s+22\s+1\s+17",
        "north_tile_edge": r"jte=\s*17, jde=\s*17, jte==jde=T",
    }
    found: dict[str, str] = {}
    lines = text.splitlines()
    for key, pattern in patterns.items():
        line = next((line.strip() for line in lines if re.search(pattern, line)), None)
        if line is None:
            raise ValueError(f"archived tile log lacks registered {key} evidence")
        found[key] = line
    return {
        "path": str(path),
        "sha256": _sha256(path),
        "physical_domain_bounds_fortran_1based": [1, 17, 1, 17, 1, 17],
        "registered_tile_count": 1,
        "tile_origin_global_indices_fortran_1based": {"i": 1, "j": 1, "k": 1},
        "derivation": "single tile spans the domain; its=ids=1, jts=jds=1, kts=kds=1",
        "matched_log_lines": found,
    }


class NativeTwinProblem:
    """Frozen arrays and native-stagger observation/objective operations."""

    def __init__(
        self,
        artifact_dir: Path,
        arrays: dict[str, np.ndarray],
        manifest: dict[str, Any],
        rows: list[dict[str, Any]],
    ):
        self.artifact_dir = artifact_dir
        self.P = arrays["P_f64"]
        self.Z_star = arrays["Z_star_f64"]
        self.Zb = arrays["Zb_f64"]
        self.observation_slots = arrays["observation_slots_i32"]
        self.observation_values = arrays["observation_values_f64"]
        self.sigma = arrays["sigma_f64"]
        self.mu_base = arrays["mu_base_f64"]
        self.ph_base = arrays["ph_base_f64"]
        self.ph_perturbation = arrays["ph_perturbation_f64"]
        self.rows = rows
        self.manifest = manifest
        truth_path = Path(manifest["truth_source"]["path"])
        if _sha256(truth_path) != manifest["truth_source"]["sha256"]:
            raise ValueError("pinned truth checkpoint ledger hash changed")
        if truth_path.stat().st_size != 257 * PACKED_SIZE * 8:
            raise ValueError("pinned truth checkpoint ledger has the wrong byte count")
        self.truth = np.memmap(
            truth_path, dtype="<f8", mode="r", shape=(257, PACKED_SIZE)
        )
        if not np.array_equal(self.truth[0], self.Z_star):
            raise ValueError(
                "NPZ solver-entry state differs from pinned truth row zero"
            )
        expected_zb = self.Z_star + 0.5 * self.P[0] - 0.5 * self.P[1]
        if not np.allclose(self.Zb, expected_zb, rtol=0.0, atol=2e-14):
            raise ValueError(
                "NPZ background does not match its frozen control templates"
            )

    def initial_state(self, control: np.ndarray | list[float]) -> np.ndarray:
        """Return Zb + P a in float64, preserving the generated template values."""
        a = np.asarray(control, dtype=np.float64)
        if a.shape != (2,) or not np.isfinite(a).all():
            raise ValueError("control must be a finite two-vector")
        return self.Zb + a @ self.P

    def write_initial_state(
        self, path: str | Path, control: np.ndarray | list[float]
    ) -> dict[str, Any]:
        """Write the exact generated F64 packed seed expected by the native harness."""
        target = Path(path)
        state = np.asarray(self.initial_state(control), dtype="<f8")
        state.tofile(target)
        return {
            "path": str(target),
            "dtype": "<f8",
            "shape": [PACKED_SIZE],
            "bytes": target.stat().st_size,
            "sha256": _sha256(target),
        }

    def truth_state(self, step: int) -> np.ndarray:
        if not (0 <= step <= 256):
            raise ValueError("truth step must be in [0, 256]")
        return np.asarray(self.truth[step]).copy()

    def observe(self, step: int, packed_state: np.ndarray) -> np.ndarray:
        if step not in STEPS:
            raise ValueError("observations are defined only at steps 128 and 256")
        state = np.asarray(packed_state, dtype=np.float64)
        if state.shape != (PACKED_SIZE,) or not np.isfinite(state).all():
            raise ValueError("packed state must be a finite float64-compatible vector")
        rows = [r for r in self.rows if r["accepted_step"] == step]
        slots = np.fromiter((r["packed_slot"] for r in rows), dtype=np.int64)
        values = state[slots].copy()
        for n, row in enumerate(rows):
            if row["field"] == "theta":
                values[n] += 300.0
        return values

    def objective(self, z128: np.ndarray, z256: np.ndarray) -> dict[str, Any]:
        """Evaluate both endpoint terms and their packed-state cotangents."""
        states = {
            128: np.asarray(z128, dtype=np.float64),
            256: np.asarray(z256, dtype=np.float64),
        }
        if any(
            z.shape != (PACKED_SIZE,) or not np.isfinite(z).all()
            for z in states.values()
        ):
            raise ValueError("endpoint states must be finite packed vectors")
        q = {step: np.zeros(PACKED_SIZE, dtype=np.float64) for step in STEPS}
        per_time_cost: dict[int, float] = {}
        residuals: dict[int, np.ndarray] = {}
        for step in STEPS:
            mask = np.fromiter(
                (r["accepted_step"] == step for r in self.rows),
                dtype=bool,
                count=len(self.rows),
            )
            slots = self.observation_slots[mask]
            predicted = self.observe(step, states[step])
            residual = predicted - self.observation_values[mask]
            inv_variance = 1.0 / np.square(self.sigma[mask])
            weighted = residual * inv_variance
            per_time_cost[step] = float(0.5 * np.dot(residual, weighted))
            residuals[step] = residual
            np.add.at(q[step], slots, weighted)
        return {
            "J": float(sum(per_time_cost.values())),
            "q": q,
            "per_time_cost": per_time_cost,
            "residuals": residuals,
        }

    def gradient_control(self, lambda0: np.ndarray) -> np.ndarray:
        """Apply the transpose of the exact generated float64 template matrix."""
        lam = np.asarray(lambda0, dtype=np.float64)
        if lam.shape != (PACKED_SIZE,) or not np.isfinite(lam).all():
            raise ValueError("incoming cotangent must be a finite packed vector")
        return self.P @ lam

    def physical_metrics(self, packed_state: np.ndarray) -> dict[str, float | bool]:
        """Report basic physical-field ranges using pinned WRFinput base fields."""
        z = np.asarray(packed_state, dtype=np.float64)
        if z.shape != (PACKED_SIZE,) or not np.isfinite(z).all():
            raise ValueError("packed state must be finite and have the native size")
        fields: dict[str, np.ndarray] = {}
        for name, (offset, shape) in BLOCKS.items():
            fields[name] = z[offset : offset + int(np.prod(shape))].reshape(shape)
        mu_total = self.mu_base + fields["MU"][:16, :16]
        ph_total = self.ph_base + fields["PH"][:16, :17, :16]
        theta = fields["T"][:16, :16, :16] + 300.0
        vertical_difference = np.diff(ph_total, axis=1)
        vertical_increasing = bool(np.all(vertical_difference > 0.0))
        return {
            "finite": True,
            "min_total_column_mass_pa": float(np.min(mu_total)),
            "min_potential_temperature_k": float(np.min(theta)),
            "min_geopotential_height_difference": float(np.min(vertical_difference)),
            "geopotential_monotone_with_level": vertical_increasing,
            "max_abs_U_m_s": float(np.max(np.abs(fields["U"]))),
            "max_abs_V_m_s": float(np.max(np.abs(fields["V"]))),
        }


def load_problem(artifact_dir: str | Path) -> NativeTwinProblem:
    directory = Path(artifact_dir)
    manifest = json.loads((directory / "artifact_manifest.json").read_text())
    _validate_artifact_pins(directory, manifest)
    rows_document = json.loads((directory / "observations.json").read_text())
    if rows_document.get("schema") != "native_twin_observations_v1":
        raise ValueError("unexpected observations schema")
    rows = rows_document.get("rows")
    if not isinstance(rows, list):
        raise ValueError("observation rows must be a list")
    arrays = _load_arrays(directory / "problem.npz")
    validate_observations(
        rows,
        arrays["observation_slots_i32"],
        arrays["observation_values_f64"],
        arrays["sigma_f64"],
    )
    seed = np.fromfile(directory / "initial_state_a0_f64.bin", dtype="<f8")
    if seed.shape != (PACKED_SIZE,) or not np.array_equal(seed, arrays["Zb_f64"]):
        raise ValueError("raw FP64 background seed differs from the NPZ background")
    return NativeTwinProblem(directory, arrays, manifest, rows)


def generate_artifacts(
    artifact_dir: str | Path, *, truth_path: str | Path, wrfinput_path: str | Path
) -> dict[str, Any]:
    """Create the pinned P/Zb and noiseless two-time observation artifacts."""
    out = Path(artifact_dir)
    out.mkdir(parents=True, exist_ok=True)
    truth_file, input_file = Path(truth_path), Path(wrfinput_path)
    if truth_file.stat().st_size != 257 * PACKED_SIZE * 8:
        raise ValueError("truth ledger must contain 257 little-endian float64 states")
    truth = np.memmap(truth_file, dtype="<f8", mode="r", shape=(257, PACKED_SIZE))
    zstar = np.asarray(truth[0]).copy()
    if not np.isfinite(zstar).all():
        raise ValueError("truth solver-entry state contains non-finite values")
    tile_info = _verify_registered_tile_log(input_file.parent / "rsl.out.0000")

    with netCDF4.Dataset(input_file, "r") as ds:
        nx = len(ds.dimensions["west_east"])
        ny = len(ds.dimensions["south_north"])
        nz = len(ds.dimensions["bottom_top"])
        dx, dy = float(ds.getncattr("DX")), float(ds.getncattr("DY"))
        znu = np.asarray(ds.variables["ZNU"][0], dtype=np.float64)
        coords = {
            "mass_lat": np.asarray(ds.variables["XLAT"][0], dtype=np.float64),
            "mass_lon": np.asarray(ds.variables["XLONG"][0], dtype=np.float64),
            "u_lat": np.asarray(ds.variables["XLAT_U"][0], dtype=np.float64),
            "u_lon": np.asarray(ds.variables["XLONG_U"][0], dtype=np.float64),
            "v_lat": np.asarray(ds.variables["XLAT_V"][0], dtype=np.float64),
            "v_lon": np.asarray(ds.variables["XLONG_V"][0], dtype=np.float64),
        }
        mu_base = np.asarray(ds.variables["MUB"][0], dtype=np.float64)
        # NetCDF stores [k,j,i], while the packed layout indexes [j,k,i].
        ph_base = np.transpose(
            np.asarray(ds.variables["PHB"][0], dtype=np.float64), (1, 0, 2)
        )
        ph_perturbation = np.transpose(
            np.asarray(ds.variables["PH"][0], dtype=np.float64), (1, 0, 2)
        )
    if (nx, ny, nz) != (16, 16, 16) or not (dx > 0 and dy > 0):
        raise ValueError("WRFinput grid differs from the frozen 16x16x16 setup")
    if (
        znu.shape != (16,)
        or ph_base.shape != (16, 17, 16)
        or not all(
            np.isfinite(x).all() for x in [znu, mu_base, ph_base, ph_perturbation]
        )
    ):
        raise ValueError("WRFinput vertical/base fields are malformed or non-finite")

    p = np.zeros((2, PACKED_SIZE), dtype=np.float64)
    wind, theta = p
    # Corner streamfunction on a 17x17 periodic-x / closed-y corner grid.
    lx, ly = nx * dx, ny * dy
    x_corner = np.arange(nx + 1, dtype=np.float64) * dx
    y_corner = np.arange(ny + 1, dtype=np.float64) * dy
    psi = np.empty((ny + 1, nx + 1, nz), dtype=np.float64)
    for k in range(nz):
        zeta = 1.0 - znu[k]
        psi[:, :, k] = (
            np.sin(2.0 * np.pi * x_corner[None, :] / lx)
            * np.sin(np.pi * y_corner[:, None] / ly)
            * np.sin(np.pi * zeta)
        )
    u_off = BLOCKS["U"][0]
    v_off = BLOCKS["V"][0]
    u = wind[u_off : u_off + 17 * 16 * 18].reshape(17, 16, 18)
    v = wind[v_off : v_off + 18 * 16 * 17].reshape(18, 16, 17)
    u_owner = -np.diff(psi[:17, :17, :], axis=0)[:16, :16, :] / dy
    # U owner slots are (j,k,i), while the derivative above is (j,i,k).
    u_owner = np.transpose(u_owner, (0, 2, 1))
    v_owner = np.diff(psi[:17, :17, :], axis=1)[:17, :16, :] / dx
    v_owner = np.transpose(v_owner, (0, 2, 1))
    v_owner[[0, 16], :, :] = 0.0
    wind_max = max(float(np.max(np.abs(u_owner))), float(np.max(np.abs(v_owner))))
    if not np.isfinite(wind_max) or wind_max <= 0:
        raise ValueError("wind template has zero/non-finite physical-owner amplitude")
    u_owner *= 0.1 / wind_max
    v_owner *= 0.1 / wind_max
    u[:16, :, :16] = u_owner
    u[:16, :, 16] = u[:16, :, 0]
    u[:16, :, 17] = u[:16, :, 1]
    u[16, :, :] = u[15, :, :]
    v[:17, :, :16] = v_owner
    v[:17, :, 16] = v[:17, :, 0]
    v[17, :, :] = -v[15, :, :]

    # Temperature perturbation on mass owners, normalized by its owner maximum.
    t_off, t_shape = BLOCKS["T"]
    t = theta[t_off : t_off + int(np.prod(t_shape))].reshape(t_shape)
    x_mass = (np.arange(nx, dtype=np.float64) + 0.5) * dx
    y_mass = (np.arange(ny, dtype=np.float64) + 0.5) * dy
    raw_t = np.empty((ny, nx, nz), dtype=np.float64)
    for k in range(nz):
        raw_t[:, :, k] = (
            np.sin(2.0 * np.pi * x_mass[None, :] / lx)
            * np.cos(np.pi * y_mass[:, None] / ly)
            * np.sin(np.pi * (1.0 - znu[k]))
        )
    theta_owner = np.transpose(raw_t, (0, 2, 1))
    theta_max = float(np.max(np.abs(theta_owner)))
    if not np.isfinite(theta_max) or theta_max <= 0:
        raise ValueError("theta template has zero/non-finite physical-owner amplitude")
    theta_owner *= 0.01 / theta_max
    t[:16, :, :16] = theta_owner
    t[:16, :, 16] = t[:16, :, 0]
    t[16, :, :] = t[15, :, :]

    zb = zstar + 0.5 * wind - 0.5 * theta
    if not (np.isfinite(p).all() and np.isfinite(zb).all()):
        raise ValueError("generated templates or background contain non-finite values")

    rows: list[dict[str, Any]] = []
    for step in STEPS:
        cp = np.asarray(truth[step])
        for field in ("U", "V", "theta"):
            stagger, unit, sigma = FIELD_INFO[field]
            ni = 16
            nj = 17 if field == "V" else 16
            for ok in OWNER_ORDINALS:
                k = ok - 1
                if k >= nz:
                    continue
                for oj in OWNER_ORDINALS:
                    j = oj - 1
                    if j >= nj:
                        continue
                    for oi in OWNER_ORDINALS:
                        i = oi - 1
                        if i >= ni:
                            continue
                        packed_field = "T" if field == "theta" else field
                        slot = _packed_slot(packed_field, i, j, k)
                        y_value = float(cp[slot]) + (300.0 if field == "theta" else 0.0)
                        if field == "U":
                            lat, lon = coords["u_lat"][j, i], coords["u_lon"][j, i]
                            xy = {"x_m": float(i * dx), "y_m": float((j + 0.5) * dy)}
                        elif field == "V":
                            lat, lon = coords["v_lat"][j, i], coords["v_lon"][j, i]
                            xy = {"x_m": float((i + 0.5) * dx), "y_m": float(j * dy)}
                        else:
                            lat, lon = (
                                coords["mass_lat"][j, i],
                                coords["mass_lon"][j, i],
                            )
                            xy = {
                                "x_m": float((i + 0.5) * dx),
                                "y_m": float((j + 0.5) * dy),
                            }
                        rows.append(
                            {
                                "unique_id": f"twin-s{step}-{field}-oi{oi}-oj{oj}-ok{ok}",
                                "accepted_step": step,
                                "time_seconds": step * 0.9375,
                                "field": field,
                                "stagger": stagger,
                                "unit": unit,
                                "owner_i": oi,
                                "owner_j": oj,
                                "owner_k": ok,
                                "packed_local_i0": i,
                                "packed_local_j0": j,
                                "packed_local_k0": k,
                                "global_i": i + TILE_ORIGIN_GLOBAL[0],
                                "global_j": j + TILE_ORIGIN_GLOBAL[1],
                                "global_k": k + TILE_ORIGIN_GLOBAL[2],
                                "global_index_base": 1,
                                "tile_origin_global_indices": {"i": 1, "j": 1, "k": 1},
                                "packed_slot": slot,
                                "horizontal_physical_coordinates": {
                                    "model_grid": xy,
                                    "XLAT_degrees": float(lat),
                                    "XLONG_degrees": float(lon),
                                },
                                "native_vertical_coordinate": {
                                    "ZNU": float(znu[k]),
                                    "zeta_1_minus_ZNU": float(1.0 - znu[k]),
                                },
                                "grid_layout_id": GRID_LAYOUT_ID,
                                "truth_value_f64": y_value,
                                "sigma": sigma,
                            }
                        )
    if len(rows) != 384:
        raise AssertionError(f"expected 384 observation rows; generated {len(rows)}")
    slots = np.asarray([r["packed_slot"] for r in rows], dtype=np.int32)
    y = np.asarray([r["truth_value_f64"] for r in rows], dtype=np.float64)
    sigmas = np.asarray([r["sigma"] for r in rows], dtype=np.float64)
    if len(np.unique(slots[:192])) != 192 or len(np.unique(slots[192:])) != 192:
        raise AssertionError("observation slots are not unique within an endpoint")
    if not (np.isfinite(y).all() and np.all(sigmas > 0)):
        raise ValueError("observation values/weights are invalid")

    npz_path = out / "problem.npz"
    np.savez(
        npz_path,
        P_f64=p,
        Z_star_f64=zstar,
        Zb_f64=zb,
        observation_slots_i32=slots,
        observation_values_f64=y,
        sigma_f64=sigmas,
        mu_base_f64=mu_base,
        ph_base_f64=ph_base,
        ph_perturbation_f64=ph_perturbation,
    )
    (out / "observations.json").write_text(
        json.dumps({"schema": "native_twin_observations_v1", "rows": rows}, indent=2)
        + "\n"
    )
    seed_path = out / "initial_state_a0_f64.bin"
    np.asarray(zb, dtype="<f8").tofile(seed_path)
    module_path = Path(__file__).resolve()
    plan_path = module_path.parents[4] / "docs/plans/native_idealized_twin_v1.json"
    manifest = {
        "schema": "native_idealized_twin_artifact_manifest_v1",
        "status": "generated_problem_setup_no_model_run",
        "base_source_commit": CPP_BASE_COMMIT,
        "plan": "docs/plans/native_idealized_twin_v1.json",
        "plan_source": {"path": str(plan_path), "sha256": _sha256(plan_path)},
        "truth_source": {
            "path": str(truth_file),
            "sha256": _sha256(truth_file),
            "bytes": truth_file.stat().st_size,
            "dtype": "<f8",
            "shape": [257, PACKED_SIZE],
        },
        "wrfinput_source": {
            "path": str(input_file),
            "sha256": _sha256(input_file),
            "bytes": input_file.stat().st_size,
            "DX_m": dx,
            "DY_m": dy,
            "dimensions": [nx, ny, nz],
        },
        "tile_bounds_source": tile_info,
        "source_graph_receipt": {
            "path": "/Users/yhlee/AD-SDIRK3-review-evidence/native-idealized-twin-20261022/green_graph/pre_edit_binding.json",
            "sha256": "e0c3438d218b9d1cd6178a9e96d89db7d058f29d82c8eaf9df67c28786bf8b79",
        },
        "source_files": {
            "external/libtorch_wrf/sdirk3/tests/native_twin_problem.py": _sha256(
                module_path
            ),
            "external/libtorch_wrf/sdirk3/wrf_sdirk3_state_layout.h": _sha256(
                module_path.parents[1] / "wrf_sdirk3_state_layout.h"
            ),
            "external/libtorch_wrf/sdirk3/tests/native_terminal_adjoint_test.F": _sha256(
                module_path.parent / "native_terminal_adjoint_test.F"
            ),
            "external/libtorch_wrf/sdirk3/wrf_sdirk3_owned_box.h": _sha256(
                module_path.parents[1] / "wrf_sdirk3_owned_box.h"
            ),
            "external/libtorch_wrf/sdirk3/tests/native_terminal_adjoint_test.cpp@e111d2bea3a2c3fa34eb117b131cb03cf89eda5f": CPP_BASE_SHA256,
        },
        "packed_layout": {
            "order": list(BLOCKS),
            "offsets": [v[0] for v in BLOCKS.values()],
            "shapes": [list(v[1]) for v in BLOCKS.values()],
            "packed_size": PACKED_SIZE,
        },
        "template": {
            "dtype": "float64",
            "control_order": ["wind", "theta"],
            "wind_common_owner_max_abs": 0.1,
            "theta_owner_max_abs": 0.01,
            "background_coefficients": [0.5, -0.5],
            "truth_control": [-0.5, 0.5],
            "wind_owner_max_abs_actual": wind_max * (0.1 / wind_max),
            "theta_owner_max_abs_actual": theta_max * (0.01 / theta_max),
            "boundary_policy": "U/T periodic x alias and north copy; V periodic x alias, zero physical walls, antisymmetric north ghost; W/PH/MU P entries zero",
        },
        "objective": {
            "steps": list(STEPS),
            "times_seconds": [120.0, 240.0],
            "sigma_by_field": {k: v[2] for k, v in FIELD_INFO.items()},
            "cost": "0.5 * sum((H(z)-y)^2 / sigma^2)",
            "state_dependent_residual_recomputed": True,
        },
        "claims": {
            "model_run_performed": False,
            "native_control_application_verified": False,
            "global_exact_fp64_gradient_claimed": False,
        },
        "problem_npz": {
            "path": str(npz_path),
            "sha256": _sha256(npz_path),
            "bytes": npz_path.stat().st_size,
        },
        "background_seed_f64": {
            "path": str(seed_path),
            "sha256": _sha256(seed_path),
            "bytes": seed_path.stat().st_size,
            "dtype": "<f8",
            "shape": [PACKED_SIZE],
        },
        "observations_json": {
            "path": str(out / "observations.json"),
            "sha256": _sha256(out / "observations.json"),
            "bytes": (out / "observations.json").stat().st_size,
        },
    }
    (out / "artifact_manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


if __name__ == "__main__":
    import argparse

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact_dir", type=Path)
    parser.add_argument("--truth", required=True, type=Path)
    parser.add_argument("--wrfinput", required=True, type=Path)
    args = parser.parse_args()
    print(
        json.dumps(
            generate_artifacts(
                args.artifact_dir, truth_path=args.truth, wrfinput_path=args.wrfinput
            ),
            indent=2,
        )
    )
