#!/usr/bin/env python3
"""Check full-Y energy in source and transformed physical coordinates."""
from __future__ import annotations

import unittest

import numpy as np

import wave_energy_spatial_reference as reference
import wave_rotating_reference as rotating


class RotatingEnergyMetricTest(unittest.TestCase):
    def test_source_physical_energy_identity(self):
        column = reference.column(nz=8, nx=16)
        ny, nz = 12, int(column["nz"])
        rng = np.random.default_rng(1729)
        # Every row has nonzero PH, theta, and MU, including complex phases.
        q = rng.standard_normal(rotating.state_size(column, ny))
        q = q + 1j * rng.standard_normal(q.size)
        rows, v_inner = rotating._unpack(column, q, ny)
        physical_rows = rows @ column["T"].T
        hx = column["H_source_physical"]
        row_energy = sum(np.vdot(x, hx @ x).real for x in physical_rows) / ny
        # T leaves U unchanged; V carries the same physical kinetic weight.
        v_weights = np.real(np.diag(hx)[:nz]) / ny
        physical_energy = row_energy + np.sum(np.abs(v_inner)**2 * v_weights)
        h = rotating.uniform_energy_metric(column, ny)
        source_energy = np.vdot(q, h @ q).real
        self.assertGreater(physical_energy, 0.0)
        self.assertAlmostEqual(source_energy / physical_energy, 1.0, places=12)
        wrong_row_energy = sum(np.vdot(row, hx @ row).real for row in rows) / ny
        self.assertGreater(abs(wrong_row_energy / row_energy - 1.0), 0.1)


if __name__ == "__main__":
    unittest.main()
