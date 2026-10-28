#!/usr/bin/env python3
"""Focused integrity checks for recompute_partial_metrics.py."""
import hashlib
import importlib.util
import csv
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

import numpy as np


SCRIPT = Path(__file__).with_name('recompute_partial_metrics.py')
SPEC = importlib.util.spec_from_file_location('recompute_partial_metrics', SCRIPT)
METRICS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(METRICS)


class VerifiedAssetTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / 'arrays').mkdir()
        self.asset = {
            'file': 'arrays/test.npz', 'bytes': 0, 'sha256': '',
            'shape': [2, 3],
        }
        self.write_asset(np.arange(6, dtype=np.float64).reshape(2, 3))

    def tearDown(self):
        self.temp.cleanup()

    def write_asset(self, diff, reference_change=None, *, refresh_pins=True):
        if reference_change is None:
            reference_change = np.ones_like(diff)
        path = self.root / self.asset['file']
        np.savez(path, diff=diff, reference_change=reference_change)
        if refresh_pins:
            self.refresh_pins()

    def refresh_pins(self):
        path = self.root / self.asset['file']
        self.asset['bytes'] = path.stat().st_size
        self.asset['sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()

    def test_normal_synthetic_asset_passes(self):
        diff, signal = METRICS.load_verified_asset(self.root, self.asset)
        self.assertEqual(diff.shape, (2, 3))
        self.assertEqual(signal.dtype, np.dtype('float64'))

    def test_tampered_asset_fails_sha256(self):
        path = self.root / self.asset['file']
        content = bytearray(path.read_bytes())
        content[0] ^= 1
        path.write_bytes(content)
        with self.assertRaisesRegex(ValueError, 'SHA-256 mismatch'):
            METRICS.load_verified_asset(self.root, self.asset)

    def test_wrong_declared_size_fails(self):
        self.asset['bytes'] += 1
        with self.assertRaisesRegex(ValueError, 'size mismatch'):
            METRICS.load_verified_asset(self.root, self.asset)

    def test_shape_mismatch_fails_after_valid_pins_for_each_array(self):
        for key in ('diff', 'reference_change'):
            with self.subTest(array=key):
                values = np.ones((3, 2), dtype=np.float64)
                diff = values if key == 'diff' else np.ones((2, 3), dtype=np.float64)
                signal = values if key == 'reference_change' else np.ones((2, 3), dtype=np.float64)
                self.write_asset(diff, signal)
                with self.assertRaisesRegex(ValueError, f'{key} shape mismatch'):
                    METRICS.load_verified_asset(self.root, self.asset)

    def test_dtype_mismatch_fails_after_valid_pins_for_each_array(self):
        for key in ('diff', 'reference_change'):
            with self.subTest(array=key):
                valid = np.ones((2, 3), dtype=np.float64)
                invalid = np.ones((2, 3), dtype=np.float32)
                self.write_asset(invalid if key == 'diff' else valid,
                                 invalid if key == 'reference_change' else valid)
                with self.assertRaisesRegex(ValueError, f'{key} dtype must be float64'):
                    METRICS.load_verified_asset(self.root, self.asset)

    def test_nonfinite_values_fail_after_valid_pins(self):
        for key in ('diff', 'reference_change'):
            for value in (np.nan, np.inf, -np.inf):
                with self.subTest(array=key, value=value):
                    bad = np.ones((2, 3), dtype=np.float64)
                    bad[0, 0] = value
                    valid = np.ones((2, 3), dtype=np.float64)
                    self.write_asset(bad if key == 'diff' else valid,
                                     bad if key == 'reference_change' else valid)
                    with self.assertRaisesRegex(ValueError, f'{key} contains NaN or Inf'):
                        METRICS.load_verified_asset(self.root, self.asset)

    def test_all_archived_assets_and_cli_output_match_published_rows(self):
        root = SCRIPT.parent
        manifest = json.loads((root / 'array_asset_manifest.json').read_text())
        for asset in manifest['assets']:
            with self.subTest(asset=asset['file']):
                METRICS.load_verified_asset(root, asset)
        result = subprocess.run(
            [sys.executable, str(SCRIPT)], check=True, capture_output=True, text=True)
        actual_rows = list(csv.DictReader(io.StringIO(result.stdout)))
        expected_rows = list(csv.DictReader(
            io.StringIO((root / 'partial_distances.csv').read_text())))
        self.assertEqual(len(actual_rows), 14)
        self.assertEqual(len(actual_rows), len(expected_rows))
        for actual, expected in zip(actual_rows, expected_rows):
            self.assertEqual((actual['model_hours'], actual['field'], actual['owned_points']),
                             (expected['model_hours'], expected['field'], expected['owned_points']))
            for column in ('rms', 'max_abs', 'mean_bias', 'reference_change_rms',
                           'relative_rms_to_reference_change'):
                self.assertAlmostEqual(float(actual[column]), float(expected[column]), delta=1e-12)

    def test_cli_fails_nonzero_for_tampered_asset(self):
        evidence = self.root / 'evidence'
        (evidence / 'arrays').mkdir(parents=True)
        script = evidence / SCRIPT.name
        script.write_bytes(SCRIPT.read_bytes())
        asset_path = evidence / 'arrays' / 'test.npz'
        np.savez(asset_path, diff=np.ones((2, 3)), reference_change=np.ones((2, 3)))
        asset = {
            'file': 'arrays/test.npz', 'model_hours': 24, 'field': 'U',
            'bytes': asset_path.stat().st_size,
            'sha256': hashlib.sha256(asset_path.read_bytes()).hexdigest(),
            'shape': [2, 3],
        }
        (evidence / 'array_asset_manifest.json').write_text(
            json.dumps({'assets': [asset]}))
        asset_path.write_bytes(asset_path.read_bytes()[:-1] + b'X')
        result = subprocess.run(
            [sys.executable, str(script)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, '')
        self.assertIn('SHA-256 mismatch', result.stderr)


if __name__ == '__main__':
    unittest.main()
