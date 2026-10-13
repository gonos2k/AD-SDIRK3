#!/usr/bin/env python3
"""Focused offline tests for source-profile runtime CF selection."""
from __future__ import annotations
import importlib.util
import sys
import unittest
from pathlib import Path
from unittest.mock import patch

TOOL = Path(__file__).with_name("test_native_wave_refinement.py")
SPEC = importlib.util.spec_from_file_location("native_wave_refinement_cf_selector", TOOL)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

LEGACY_SHA = MODULE.LEGACY_PHASE_B_PACKET_SHA256
LEGACY_PACKET = Path(__file__)
LEGACY_TILE0 = "[NATIVE TRANSPORT CONFIG] tile=0 native_set=1 phi_adv_z=1 v_sca_adv_order=3"
LEGACY_TILE1 = "[NATIVE TRANSPORT CONFIG] tile=1 native_set=1 phi_adv_z=1 v_sca_adv_order=3"
MODERN = "[NATIVE TRANSPORT CONFIG] tile=0 native_set=1 phi_adv_z=1 v_sca_adv_order=3 cf1=2.09557629 cf2=-1.73508799 cf3=0.639511704"


class RuntimeCfSelectorTests(unittest.TestCase):
    def test_pinned_legacy_tile0_exact_line_uses_constructor_defaults(self):
        with patch.object(MODULE, "sha256_file", return_value=LEGACY_SHA):
            self.assertEqual(MODULE.select_runtime_cf_coefficients(LEGACY_TILE0, LEGACY_PACKET),
                ([0.5, 0.0, 0.0],
                 "legacy_phaseB_packet_constructor_defaults_no_cf_record", 0))

    def test_pinned_legacy_archived_tile1_exact_line_is_supported(self):
        # The old RSL capture and native-test logs differed only in tile id.
        with patch.object(MODULE, "sha256_file", return_value=LEGACY_SHA):
            self.assertEqual(MODULE.select_runtime_cf_coefficients(LEGACY_TILE1, LEGACY_PACKET)[0],
                             [0.5, 0.0, 0.0])

    def test_modern_cf_record_wins_even_if_packet_hash_is_legacy(self):
        with patch.object(MODULE, "sha256_file", return_value=LEGACY_SHA):
            values, source, count = MODULE.select_runtime_cf_coefficients(MODERN, LEGACY_PACKET)
        self.assertEqual(values, [2.09557629, -1.73508799, 0.639511704])
        self.assertEqual(source, "captured_rsl_native_transport_config")
        self.assertEqual(count, 1)

    def test_legacy_packet_with_changed_no_cf_line_is_rejected(self):
        near_miss = LEGACY_TILE0.replace("phi_adv_z=1", "phi_adv_z=2")
        with patch.object(MODULE, "sha256_file", return_value=LEGACY_SHA):
            with self.assertRaisesRegex(AssertionError, "exact pinned legacy record"):
                MODULE.select_runtime_cf_coefficients(near_miss, LEGACY_PACKET)

    def test_nonlegacy_packet_with_exact_old_no_cf_line_is_rejected(self):
        with patch.object(MODULE, "sha256_file", return_value="0" * 64):
            with self.assertRaisesRegex(AssertionError, "exact pinned legacy record"):
                MODULE.select_runtime_cf_coefficients(LEGACY_TILE0, LEGACY_PACKET)

    def test_pinned_legacy_packet_with_no_transport_record_is_rejected(self):
        with patch.object(MODULE, "sha256_file", return_value=LEGACY_SHA):
            with self.assertRaisesRegex(AssertionError, "no NATIVE TRANSPORT CONFIG CF record"):
                MODULE.select_runtime_cf_coefficients("", LEGACY_PACKET)

    def test_nonfinite_cf_is_rejected(self):
        line = MODERN.replace("cf1=2.09557629", "cf1=1e999")
        with self.assertRaisesRegex(AssertionError, "nonfinite CF coefficients"):
            MODULE.select_runtime_cf_coefficients(line, LEGACY_PACKET)

    def test_mixed_cf_and_old_no_cf_rows_are_rejected(self):
        with patch.object(MODULE, "sha256_file", return_value=LEGACY_SHA):
            with self.assertRaisesRegex(AssertionError, "mixes CF and no-CF"):
                MODULE.select_runtime_cf_coefficients(MODERN + "\n" + LEGACY_TILE0, LEGACY_PACKET)

    def test_conflicting_cf_rows_are_rejected(self):
        second = MODERN.replace("cf1=2.09557629", "cf1=2.0")
        with self.assertRaisesRegex(AssertionError, "tuples differ"):
            MODULE.select_runtime_cf_coefficients(MODERN + "\n" + second, LEGACY_PACKET)

    def test_duplicate_trailing_cf_tuple_on_one_row_is_rejected(self):
        line = MODERN + " cf1=9.0 cf2=8.0 cf3=7.0"
        with self.assertRaisesRegex(AssertionError, "exactly one cf1/cf2/cf3 tuple"):
            MODULE.select_runtime_cf_coefficients(line, LEGACY_PACKET)


if __name__ == "__main__":
    unittest.main()
