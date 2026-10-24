"""Synthetic NetCDF contracts for the offline WRF accuracy/cost analyzer."""
from __future__ import annotations

import hashlib
import json
import tempfile
import unittest
from datetime import datetime, timedelta
from pathlib import Path
from unittest import mock

import benchmark_wrf_accuracy_cost as benchmark
import numpy as np
from netCDF4 import Dataset

START = datetime(1, 1, 1)


def timestamp(seconds: int) -> str:
    return (START + timedelta(seconds=seconds)).isoformat(sep="_", timespec="seconds")


def write_run(root: Path, label: str, grid: dict[str, int], times: list[int], *,
              initial_bytes: bytes = b"shared-wrfinput", initial_t_delta: float = 0.0,
              initial_p_delta: float = 0.0,
              start_offset_seconds: int = 0,
              growth: float = 0.0, negative_zero_u: bool = False,
              qvapor: float = 0.0, pressure_base: float = 100000.0,
              mapfac: float = 1.0, duplicate_time: bool = False,
              h_seconds: float = 600.0) -> dict:
    run_dir = root / label
    run_dir.mkdir(parents=True)
    input_path = run_dir / "wrfinput_d01"
    nx, ny, nz = grid["nx"], grid["ny"], grid["nz"]
    path = run_dir / f"wrfout_d01_{timestamp(0)}"
    times_text = [timestamp(s + start_offset_seconds) for s in times]
    if duplicate_time and len(times_text) > 1:
        times_text[1] = times_text[0]
    dims = {
        "U": ("Time", "bottom_top", "south_north", "west_east_stag"),
        "V": ("Time", "bottom_top", "south_north_stag", "west_east"),
        "W": ("Time", "bottom_top_stag", "south_north", "west_east"),
        "PH": ("Time", "bottom_top_stag", "south_north", "west_east"),
        "PHB": ("Time", "bottom_top_stag", "south_north", "west_east"),
        "T": ("Time", "bottom_top", "south_north", "west_east"),
        "MU": ("Time", "south_north", "west_east"),
        "MUB": ("Time", "south_north", "west_east"),
        "P": ("Time", "bottom_top", "south_north", "west_east"),
        "PB": ("Time", "bottom_top", "south_north", "west_east"),
        "QVAPOR": ("Time", "bottom_top", "south_north", "west_east"),
        "DNW": ("Time", "bottom_top"), "DN": ("Time", "bottom_top"),
        "ZNW": ("Time", "bottom_top_stag"), "ZNU": ("Time", "bottom_top"),
        "MAPFAC_M": ("Time", "south_north", "west_east"),
        "MAPFAC_U": ("Time", "south_north", "west_east_stag"),
        "MAPFAC_V": ("Time", "south_north_stag", "west_east"),
        "MAPFAC_MX": ("Time", "south_north", "west_east"),
        "MAPFAC_MY": ("Time", "south_north", "west_east"),
        "F": ("Time", "south_north", "west_east"),
    }
    shape = {
        "U": (len(times), nz, ny, nx + 1), "V": (len(times), nz, ny + 1, nx),
        "W": (len(times), nz + 1, ny, nx), "PH": (len(times), nz + 1, ny, nx),
        "T": (len(times), nz, ny, nx), "MU": (len(times), ny, nx),
        "P": (len(times), nz, ny, nx), "MUB": (len(times), ny, nx),
    }
    arrays = {
        name: np.zeros(shape[name], dtype=np.float32)
        for name in ("U", "V", "W", "PH", "T", "MU", "P")
    }
    arrays["MUB"] = np.full(shape["MUB"], 100000.0, dtype=np.float32)
    arrays["PHB"] = np.broadcast_to((np.arange(nz + 1) * 9.81 * 100.0)[None, :, None, None],
                                     (len(times), nz + 1, ny, nx)).astype(np.float32).copy()
    arrays["PB"] = np.full(shape["P"], pressure_base, dtype=np.float32)
    arrays["QVAPOR"] = np.full(shape["P"], qvapor, dtype=np.float32)
    arrays["DNW"] = np.full((len(times), nz), -1.0 / nz, dtype=np.float32)
    arrays["DN"] = -arrays["DNW"].copy()
    arrays["ZNW"] = np.broadcast_to(np.linspace(1.0, 0.0, nz + 1)[None, :],
                                     (len(times), nz + 1)).astype(np.float32).copy()
    arrays["ZNU"] = np.broadcast_to((1.0 - (np.arange(nz) + 0.5) / nz)[None, :],
                                     (len(times), nz)).astype(np.float32).copy()
    arrays["MAPFAC_M"] = np.full((len(times), ny, nx), mapfac, dtype=np.float32)
    arrays["MAPFAC_U"] = np.full((len(times), ny, nx + 1), mapfac, dtype=np.float32)
    arrays["MAPFAC_V"] = np.full((len(times), ny + 1, nx), mapfac, dtype=np.float32)
    arrays["MAPFAC_MX"] = arrays["MAPFAC_M"].copy()
    arrays["MAPFAC_MY"] = arrays["MAPFAC_M"].copy()
    arrays["F"] = np.full((len(times), ny, nx), 1.0e-4, dtype=np.float32)
    if negative_zero_u:
        arrays["U"].fill(-0.0)
    if growth:
        x = np.cos(2 * np.pi * np.arange(nx) / nx).astype(np.float32)
        arrays["T"][:] = np.asarray([growth * i * x[None, None, :] for i in range(len(times))])
        arrays["W"][:, 1:, :, :] = np.asarray([growth * i * x[None, None, :] for i in range(len(times))])
        arrays["P"][:] = np.asarray([0.5 * growth * i * x[None, None, :] for i in range(len(times))])
    arrays["T"][0] += initial_t_delta
    arrays["P"][0] += initial_p_delta

    with Dataset(path, "w") as ds:
        for name, size in {
            "Time": len(times), "DateStrLen": 19, "bottom_top": nz,
            "bottom_top_stag": nz + 1, "south_north": ny,
            "south_north_stag": ny + 1, "west_east": nx,
            "west_east_stag": nx + 1,
        }.items():
            ds.createDimension(name, size)
        for name, value in {
            "DX": 100000.0, "DY": 100000.0, "MAP_PROJ": 0,
            "CEN_LAT": 0.0, "CEN_LON": 0.0, "TRUELAT1": 0.0,
            "TRUELAT2": 0.0, "STAND_LON": 0.0,
        }.items():
            ds.setncattr(name, value)
        ds.createVariable("Times", "S1", ("Time", "DateStrLen"))[:] = np.asarray(
            [np.frombuffer(t.encode("ascii"), dtype="S1") for t in times_text])
        for name, dimensions in dims.items():
            ds.createVariable(name, "f4", dimensions)[:] = arrays[name]
    with Dataset(input_path, "w") as ds:
        for name, size in {
            "Time": 1, "DateStrLen": 19, "bottom_top": nz,
            "bottom_top_stag": nz + 1, "south_north": ny,
            "south_north_stag": ny + 1, "west_east": nx,
            "west_east_stag": nx + 1,
        }.items():
            ds.createDimension(name, size)
        for name, value in {
            "DX": 100000.0, "DY": 100000.0, "MAP_PROJ": 0,
            "CEN_LAT": 0.0, "CEN_LON": 0.0, "TRUELAT1": 0.0,
            "TRUELAT2": 0.0, "STAND_LON": 0.0,
            "TEST_INITIAL_INPUT_MANIFEST": initial_bytes.hex(),
        }.items():
            ds.setncattr(name, value)
        ds.createVariable("Times", "S1", ("Time", "DateStrLen"))[:] = np.asarray(
            [np.frombuffer(times_text[0].encode("ascii"), dtype="S1")])
        for name, dimensions in dims.items():
            ds.createVariable(name, "f4", dimensions)[:] = arrays[name][0:1]
    return {
        "label": label, "path": str(run_dir), "h_seconds": h_seconds,
        "method": "RK3" if label.startswith("rk3") else "ARK324L2SA",
        "input_sha256": hashlib.sha256(input_path.read_bytes()).hexdigest(),
        "wall_seconds": 1.0, "peak_rss_bytes": 1024,
    }


def write_plan(root: Path, runs: list[dict], **options) -> Path:
    path = root / "runs.json"
    path.write_text(json.dumps({"runs": runs, **options}, indent=2))
    return path


def share_input(runs: list[dict]) -> None:
    source = Path(runs[0]["path"]) / "wrfinput_d01"
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    for run in runs[1:]:
        target = Path(run["path"]) / "wrfinput_d01"
        target.unlink()
        target.symlink_to(source)
        run["input_sha256"] = digest


class BenchmarkToolTests(unittest.TestCase):
    def test_legacy_default_16cube_240s_metrics_are_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            grid = {"nx": 16, "ny": 16, "nz": 16}
            runs = [write_run(root, "rk3_ref", grid, [0, 240]),
                    write_run(root, "ark", grid, [0, 240], growth=0.01)]
            plan = write_plan(root, runs)
            report = benchmark.analyze(plan, "rk3_ref", root / "analysis.json")
            metrics = report["comparisons"]["ark"][timestamp(240)]["T"]
            self.assertEqual(report["endpoint_seconds"], 240)
            self.assertEqual(report["expected_grid"], grid)
            self.assertEqual(set(metrics), {
                "rms", "max_abs", "reference_signal_rms_from_initial",
                "relative_rms_to_reference_signal", "relative_rms_status"})
            self.assertAlmostEqual(metrics["rms"], 0.01 / np.sqrt(2.0))
            self.assertNotIn("P_total", report["comparisons"]["ark"][timestamp(240)])
            self.assertEqual(report["shared_initial_state_gate"]["status"], "not_requested")

    def test_owned_masks_preserve_v_walls_and_top_and_drop_duplicate_faces(self):
        grid = {"nx": 2, "ny": 2, "nz": 2}
        u = np.arange(2 * 2 * 3).reshape(2, 2, 3)
        v = np.arange(2 * 3 * 2).reshape(2, 3, 2)
        w = np.arange(3 * 2 * 2).reshape(3, 2, 2)
        fields = {"U": u, "V": v, "W": w, "PH": w.copy(),
                  "T": np.zeros((2, 2, 2)), "MU": np.zeros((2, 2))}
        owned = benchmark._owned(fields, grid)
        np.testing.assert_array_equal(owned["U"], u[:, :, :-1])
        np.testing.assert_array_equal(owned["V"][0], v[0])
        np.testing.assert_array_equal(owned["V"][-1], v[-1])
        np.testing.assert_array_equal(owned["W"], w[1:, :, :])
        self.assertEqual(owned["W"][-1, 0, 0], w[-1, 0, 0])

    def test_target_grid_object_maps_dynamic_owned_counts(self):
        grid = benchmark._expected_grid({"expected_grid": {"nx": 40, "ny": 80, "nz": 64}})
        self.assertEqual(grid, {"nx": 40, "ny": 80, "nz": 64})
        fields = {
            "U": np.zeros((64, 80, 41)), "V": np.zeros((64, 81, 40)),
            "W": np.zeros((65, 80, 40)), "PH": np.zeros((65, 80, 40)),
            "T": np.zeros((64, 80, 40)), "MU": np.zeros((80, 40)),
        }
        owned = benchmark._owned(fields, grid)
        self.assertEqual(owned["U"].size, 64 * 80 * 40)
        self.assertEqual(owned["V"].size, 64 * 81 * 40)
        self.assertEqual(owned["W"].shape[0], 64)

    def test_six_hour_five_day_shared_state_pressure_pairs_and_budgets(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            grid = {"nx": 4, "ny": 3, "nz": 2}
            times = list(range(0, 432001, 21600))
            common_input = b"common-initial-state"
            runs = [
                write_run(root, "rk3_600", grid, times, initial_bytes=common_input, negative_zero_u=True),
                write_run(root, "rk3_300", grid, times, initial_bytes=common_input, growth=0.01,
                          h_seconds=300.0),
                write_run(root, "rk3_150", grid, times, initial_bytes=common_input, growth=0.008,
                          h_seconds=150.0),
                write_run(root, "ark_600", grid, times, initial_bytes=common_input, growth=0.02),
                write_run(root, "ark_300", grid, times, initial_bytes=common_input,
                          growth=0.015, h_seconds=300.0),
                write_run(root, "ark_150", grid, times, initial_bytes=common_input,
                          growth=0.012, h_seconds=150.0),
            ]
            share_input(runs)
            plan = write_plan(root, runs, expected_grid=grid, duration_seconds=432000,
                output_interval_seconds=21600, start_timestamp=timestamp(0),
                require_shared_initial_state=True, dry_case_no_qv=True, use_theta_m=1,
                include_total_pressure=True, include_mass_theta_budgets=True,
                extra_metrics=["mean_bias", "mean_absolute_error", "per_z", "max_location", "boundary_error_fraction"],
                comparison_pairs=[
                    {"name": "rk3_600_vs_300", "left": "rk3_600", "right": "rk3_300"},
                    {"name": "rk3_300_vs_150", "left": "rk3_300", "right": "rk3_150"},
                    {"name": "ark_600_vs_300", "left": "ark_600", "right": "ark_300"},
                    {"name": "ark_300_vs_150", "left": "ark_300", "right": "ark_150"},
                ],
                wave_mode1={"fields": ["T", "W", "P"]})
            report = benchmark.analyze(plan, "rk3_600", root / "five_day.json")
            self.assertEqual(len(report["comparisons"]["ark_600"]), 21)
            self.assertEqual(len(report["comparisons"]), 5)
            self.assertTrue(report["shared_initial_state_gate"]["comparison_certified"])
            self.assertEqual(report["shared_initial_state_gate"]["source_kind"], "wrfout_t0")
            self.assertIn("P_total", report["units"])
            field = report["comparisons"]["ark_600"][timestamp(21600)]["T"]
            self.assertIn("mean_bias", field)
            self.assertEqual(field["per_z"][0]["z_index"], 0)
            self.assertIn("rms", report["comparisons"]["ark_600"][timestamp(21600)]["P_total"])
            self.assertEqual(report["runs"][0]["qvapor_max_abs"], 0.0)
            self.assertIn("not air temperature", report["theta_interpretation"])
            self.assertAlmostEqual(report["budgets"]["rk3_600"][timestamp(0)]["mass_weighted_theta_K"], 300.0)
            self.assertAlmostEqual(report["budgets"]["rk3_600"][timestamp(432000)]["dry_mass_relative_change"], 0.0)
            self.assertEqual(report["wave_mode1_diagnostics"]["rk3_600"][timestamp(0)]["W"]["phase_status"],
                             "undefined_zero_mode")
            self.assertIsNone(report["wave_pairwise_diagnostics"]["ark_600_vs_300"][timestamp(0)]["W"]["coherence"])
            self.assertEqual(len(report["comparison_pair_sensitivities"]["ark_600_vs_300"]["times"]), 21)
            self.assertEqual(report["comparison_pair_sensitivities"]["ark_300_vs_150"]["h_seconds"],
                             {"ark_300": 300.0, "ark_150": 150.0})

    def test_wrong_grid_duplicate_timestamp_and_hash_reject(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            row = write_run(root, "rk3", {"nx": 4, "ny": 3, "nz": 2}, [0, 240])
            with self.assertRaisesRegex(ValueError, "grid"):
                benchmark.analyze(write_plan(root, [row], expected_grid={"nx": 16, "ny": 16, "nz": 16}),
                                  "rk3", root / "wrong-grid.json")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            row = write_run(root, "rk3", {"nx": 4, "ny": 3, "nz": 2}, [0, 240], duplicate_time=True)
            with self.assertRaisesRegex(ValueError, "duplicate"):
                benchmark.analyze(write_plan(root, [row]), "rk3", root / "duplicate.json")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            row = write_run(root, "rk3", {"nx": 4, "ny": 3, "nz": 2}, [0, 240])
            row["input_sha256"] = "0" * 64
            with self.assertRaisesRegex(ValueError, "mismatch"):
                benchmark.analyze(write_plan(root, [row]), "rk3", root / "bad-hash.json")

    def test_shared_input_and_t0_mismatch_reject_before_metrics(self):
        grid = {"nx": 4, "ny": 3, "nz": 2}
        times = [0, 21600, 432000]
        for mismatch in ("input", "T", "P", "MAPFAC_M", "start"):
            with self.subTest(mismatch=mismatch), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                runs = [
                    write_run(root, "rk3", grid, times, initial_bytes=b"shared"),
                    write_run(root, "ark", grid, times,
                              initial_bytes=b"different" if mismatch == "input" else b"shared",
                              initial_t_delta=1e-4 if mismatch == "T" else 0.0,
                              initial_p_delta=1e-4 if mismatch == "P" else 0.0,
                              mapfac=1.001 if mismatch == "MAPFAC_M" else 1.0,
                              start_offset_seconds=3600 if mismatch == "start" else 0),
                ]
                if mismatch != "input":
                    share_input(runs)
                plan = write_plan(root, runs, expected_grid=grid, duration_seconds=432000,
                                  start_timestamp=timestamp(0), require_shared_initial_state=True,
                                  dry_case_no_qv=True,
                                  use_theta_m=1)
                with (mock.patch.object(benchmark, "_metrics", side_effect=AssertionError("metric called")),
                      self.assertRaisesRegex(ValueError, "input|field T|field P|MAPFAC_M|start")):
                    benchmark.analyze(plan, "rk3", root / "strict-failure.json")

    def test_pressure_and_dry_qv_gates_and_initial_manifest_helper(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            row = write_run(root, "rk3", {"nx": 4, "ny": 3, "nz": 2}, [0, 240], pressure_base=-1.0)
            plan = write_plan(root, [row], expected_grid={"nx": 4, "ny": 3, "nz": 2},
                              duration_seconds=240, include_total_pressure=True)
            with self.assertRaisesRegex(ValueError, "pressure"):
                benchmark.analyze(plan, "rk3", root / "bad-pressure.json")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            row = write_run(root, "rk3", {"nx": 4, "ny": 3, "nz": 2}, [0, 240], qvapor=1e-6)
            missing_setting = write_plan(root, [row], expected_grid={"nx": 4, "ny": 3, "nz": 2},
                                         duration_seconds=240, dry_case_no_qv=True)
            with self.assertRaisesRegex(ValueError, "use_theta_m"):
                benchmark.analyze(missing_setting, "rk3", root / "missing-theta-setting.json")
            plan = write_plan(root, [row], expected_grid={"nx": 4, "ny": 3, "nz": 2},
                              duration_seconds=240, dry_case_no_qv=True, use_theta_m=1)
            with self.assertRaisesRegex(ValueError, "QVAPOR"):
                benchmark.analyze(plan, "rk3", root / "wet-case.json")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            grid = {"nx": 4, "ny": 3, "nz": 2}
            a = write_run(root, "rk3", grid, [0, 240], negative_zero_u=True)
            b = write_run(root, "ark", grid, [0, 240])
            share_input([a, b])
            for run in (a, b):
                for output in Path(run["path"]).glob("wrfout_d01*"):
                    output.unlink()
            state_a = benchmark.load_initial_state(Path(a["path"]), grid)
            state_b = benchmark.load_initial_state(Path(b["path"]), grid)
            self.assertEqual(state_a["source_kind"], "wrfinput")
            gate = benchmark.compare_initial_states({"rk3": state_a, "ark": state_b})
            self.assertEqual(gate["status"], "passed")
            self.assertEqual(gate["gate_scope"], "input-state preflight; not post-initialization evidence")
            self.assertTrue(gate["signed_zero_equivalent"])
            zero = benchmark._wave_pair(np.zeros((2, 3), complex), np.zeros((2, 3), complex))
            self.assertIsNone(zero["cross_phase_right_minus_left_rad"])
            self.assertIsNone(zero["coherence"])
            self.assertEqual(benchmark._wave_summary(np.zeros((2, 3), complex))["phase_status"],
                             "undefined_zero_mode")

    def test_wrfout_source_detects_post_init_change_despite_common_wrfinput(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            grid = {"nx": 4, "ny": 3, "nz": 2}
            a = write_run(root, "rk3", grid, [0, 240], initial_bytes=b"common")
            b = write_run(root, "ark", grid, [0, 240], initial_bytes=b"common", initial_t_delta=1e-4)
            share_input([a, b])
            input_a = benchmark.load_initial_state(Path(a["path"]), grid, source="auto")
            input_b = benchmark.load_initial_state(Path(b["path"]), grid, source="auto")
            self.assertEqual(input_a["source_kind"], "wrfinput")
            self.assertTrue(benchmark.compare_initial_states({"rk3": input_a, "ark": input_b})["status"] == "passed")
            history_a = benchmark.load_initial_state(Path(a["path"]), grid, source="wrfout")
            history_b = benchmark.load_initial_state(Path(b["path"]), grid, source="wrfout")
            self.assertEqual(history_a["input_sha256"], history_b["input_sha256"])
            self.assertEqual(history_a["source_kind"], "wrfout")
            with self.assertRaisesRegex(ValueError, "t=0 field T"):
                benchmark.compare_initial_states({"rk3": history_a, "ark": history_b})

    def test_wave_pair_uses_mode_coefficients_without_alignment(self):
        left = np.full((2, 3), 0.5 + 0.0j)
        right = -1j * left
        result = benchmark._wave_pair(left, right)
        self.assertAlmostEqual(result["right_to_left_amplitude_ratio"], 1.0)
        self.assertAlmostEqual(result["cross_phase_right_minus_left_rad"], -np.pi / 2)
        self.assertAlmostEqual(result["coherence"], 1.0)
        self.assertIn("no spatial alignment", result["phase_convention"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
