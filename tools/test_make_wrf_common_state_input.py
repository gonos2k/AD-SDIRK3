import tempfile
import unittest
from pathlib import Path

import numpy as np
from netCDF4 import Dataset

from make_wrf_common_state_input import BASE_FIELDS, STATE_FIELDS, TIME_ZERO, make_input


def write_fixture(path: Path, times: list[str], *, thm: bool = True, qvapor: bool = True,
                  timeless_p: bool = False) -> None:
    shape = {"U": (2, 2, 3), "V": (2, 3, 2), "W": (3, 2, 2), "PH": (3, 2, 2),
             "T": (2, 2, 2), "THM": (2, 2, 2), "MU": (2, 2), "P": (2, 2, 2),
             "PB": (2, 2, 2), "PHB": (3, 2, 2), "MUB": (2, 2), "KEEP": (2, 2), "QVAPOR": (2, 2, 2)}
    dims = {"U": ("bottom_top", "south_north", "west_east_stag"),
            "V": ("bottom_top", "south_north_stag", "west_east"),
            "W": ("bottom_top_stag", "south_north", "west_east"),
            "PH": ("bottom_top_stag", "south_north", "west_east"),
            "PHB": ("bottom_top_stag", "south_north", "west_east"),
            "MU": ("south_north", "west_east"), "MUB": ("south_north", "west_east"),
            "KEEP": ("south_north", "west_east"), "QVAPOR": ("bottom_top", "south_north", "west_east")}
    with Dataset(path, "w") as ds:
        ds.createDimension("Time", len(times)); ds.createDimension("DateStrLen", 19)
        for name, n in (("bottom_top", 2), ("bottom_top_stag", 3), ("south_north", 2),
                        ("south_north_stag", 3), ("west_east", 2), ("west_east_stag", 3)):
            ds.createDimension(name, n)
        tv = ds.createVariable("Times", "S1", ("Time", "DateStrLen"))
        tv[:] = np.stack([np.frombuffer(t.encode("ascii"), dtype="S1") for t in times])
        names = list(STATE_FIELDS) + list(BASE_FIELDS) + ["KEEP"]
        if not thm: names.remove("THM")
        if qvapor: names.append("QVAPOR")
        for name in names:
            vdims = dims.get(name, ("bottom_top", "south_north", "west_east"))
            var = ds.createVariable(name, "f4", vdims if timeless_p and name == "P" else ("Time",) + vdims)
            if name in BASE_FIELDS: var.units = "Pa"
            if name in STATE_FIELDS: var.units = "fixture"
            if timeless_p and name == "P":
                var[:] = np.full(shape[name], 1.0, dtype=np.float32)
                continue
            for ti in range(len(times)):
                base_value = 100.0 if name in BASE_FIELDS else 10.0 + len(name)
                bump = 0.0 if name in BASE_FIELDS or name == "QVAPOR" else 1000.0 * ti
                var[ti] = np.full(shape[name], base_value + bump, dtype=np.float32)


class CommonStateInputTest(unittest.TestCase):
    def test_copies_all_eight_fields_and_preserves_base(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); base = root / "base.nc"; source = root / "source.nc"; out = root / "new.nc"
            write_fixture(base, [TIME_ZERO]); write_fixture(source, [TIME_ZERO, "0001-01-01_00:01:00"])
            receipt = make_input(base, source, "0001-01-01_00:01:00", out)
            self.assertEqual(tuple(receipt["copied_fields"]), STATE_FIELDS)
            with Dataset(base) as b, Dataset(source) as s, Dataset(out) as n:
                for name in STATE_FIELDS: np.testing.assert_array_equal(n[name][0], s[name][1])
                for name in BASE_FIELDS + ("KEEP",): np.testing.assert_array_equal(n[name][:], b[name][:])
                self.assertEqual(n["Times"][0].tobytes().decode("ascii"), TIME_ZERO)
            with self.assertRaises(FileExistsError): make_input(base, source, "0001-01-01_00:01:00", out)

    def test_missing_thm_fails_without_creating_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); base = root / "base.nc"; source = root / "source.nc"; out = root / "new.nc"
            write_fixture(base, [TIME_ZERO]); write_fixture(source, [TIME_ZERO, "0001-01-01_00:01:00"], thm=False)
            with self.assertRaisesRegex(ValueError, "THM"): make_input(base, source, "0001-01-01_00:01:00", out)
            self.assertFalse(out.exists())

    def test_mismatched_qvapor_context_fails_without_creating_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); base = root / "base.nc"; source = root / "source.nc"; out = root / "new.nc"
            write_fixture(base, [TIME_ZERO]); write_fixture(source, [TIME_ZERO, "0001-01-01_00:01:00"])
            with Dataset(source, "r+") as ds: ds["QVAPOR"][1, 0, 0, 0] = 0.5
            with self.assertRaisesRegex(ValueError, "QVAPOR"): make_input(base, source, "0001-01-01_00:01:00", out)
            self.assertFalse(out.exists())

    def test_qvapor_presence_must_match_between_files(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); base = root / "base.nc"; source = root / "source.nc"; out = root / "new.nc"
            write_fixture(base, [TIME_ZERO]); write_fixture(source, [TIME_ZERO, "0001-01-01_00:01:00"], qvapor=False)
            with self.assertRaisesRegex(ValueError, "QVAPOR"): make_input(base, source, "0001-01-01_00:01:00", out)
            self.assertFalse(out.exists())

    def test_state_variable_must_have_time_as_leading_dimension(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); base = root / "base.nc"; source = root / "source.nc"; out = root / "new.nc"
            write_fixture(base, [TIME_ZERO]); write_fixture(source, [TIME_ZERO, "0001-01-01_00:01:00"], timeless_p=True)
            with self.assertRaisesRegex(ValueError, "Time"): make_input(base, source, "0001-01-01_00:01:00", out)
            self.assertFalse(out.exists())

    def test_each_base_field_mismatch_fails_without_creating_output(self):
        for name in BASE_FIELDS:
            with self.subTest(name=name), tempfile.TemporaryDirectory() as temp:
                root = Path(temp); base = root / "base.nc"; source = root / "source.nc"; out = root / "new.nc"
                write_fixture(base, [TIME_ZERO]); write_fixture(source, [TIME_ZERO, "0001-01-01_00:01:00"])
                with Dataset(source, "r+") as ds: ds[name][1, ...] += 1.0
                with self.assertRaisesRegex(ValueError, name): make_input(base, source, "0001-01-01_00:01:00", out)
                self.assertFalse(out.exists())


if __name__ == "__main__": unittest.main()
