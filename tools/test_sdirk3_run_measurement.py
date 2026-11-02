import tempfile
import unittest
from pathlib import Path
from sdirk3_run_measurement import clock_endpoint, clock_measurement, collect_stages


def endpoint(before, real, after):
    return dict(monotonic_before_ns=before, realtime_ns=real, monotonic_after_ns=after)


class MeasurementTest(unittest.TestCase):
    def classify(self, a, b, **kwargs):
        return clock_measurement(a, b, monotonic_resolution_ns=1,
                                 realtime_resolution_ns=1, **kwargs)

    def test_capture_order_and_values(self):
        calls = []
        def mono():
            calls.append('m')
            return len(calls)
        def real():
            calls.append('r')
            return 99
        self.assertEqual(clock_endpoint(mono, real), endpoint(1, 99, 3))
        self.assertEqual(calls, ['m', 'r', 'm'])

    def test_interruption_before_and_after_realtime(self):
        # A naive before/before estimate indicates a 2-second jump.
        for a, b in [(endpoint(0, 2_000_000_000, 2_000_000_000),
                      endpoint(3_000_000_000, 3_000_000_000, 3_000_000_000)),
                     (endpoint(0, 0, 0),
                      endpoint(1_000_000_000, 1_000_000_000, 3_000_000_000))]:
            result = self.classify(a, b)
            self.assertEqual(result['classification'], 'unresolved')
            self.assertLessEqual(result['gap_interval_ns'][0], 0)
            self.assertGreaterEqual(result['gap_interval_ns'][1], 0)

    def test_start_pause_reproduces_naive_negative_gap(self):
        start = endpoint(0, 0, 1_794_000_000)
        end = endpoint(52_066_000_000, 50_272_000_000, 52_066_000_000)
        result = self.classify(start, end)
        naive = (end['realtime_ns'] - start['realtime_ns'] -
                 (end['monotonic_before_ns'] - start['monotonic_before_ns']))
        self.assertEqual(naive, -1_794_000_000)
        self.assertEqual(result['classification'], 'unresolved')
        self.assertEqual(result['start'], start)
        self.assertEqual(result['end'], end)
        self.assertLessEqual(result['gap_interval_ns'][0], 0)
        self.assertGreaterEqual(result['gap_interval_ns'][1], 0)

    def test_consistent_jump_boundary_and_invalid(self):
        a = endpoint(0, 100, 2)
        self.assertEqual(self.classify(a, endpoint(100, 200, 102))['classification'], 'consistent')
        self.assertEqual(self.classify(a, endpoint(100, 1_000_000_300, 102))['classification'], 'definite_gap')
        self.assertEqual(self.classify(a, endpoint(100, 1_000_000_200, 102))['classification'], 'unresolved')
        for bad in (endpoint(5, 0, 4), endpoint(1, 0, 3)):
            with self.assertRaises(ValueError):
                self.classify(a, bad)
        with self.assertRaises(ValueError):
            clock_measurement(a, endpoint(100, 200, 102), realtime_resolution_ns=0)

    def test_stream_dedup_and_empty(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            self.assertEqual(collect_stages(p)['stage_count'], 0)
            row = '[Newton] CONVERGED stage=2 iters=3\n'
            (p / 'rsl.error.0000').write_text(row + row + '[Newton] CONVERGED stage=2 iters=4\n')
            (p / 'launcher.stderr.log').write_text(row + row)
            (p / 'rsl.out.0000').write_text('[Newton] CONVERGED stage=3 iters=1\n')
            (p / 'launcher.stdout.log').write_text('[Newton] CONVERGED stage=4 iters=2\n')
            result = collect_stages(p)
            self.assertEqual(result['stage_count'], 5)
            self.assertEqual(result['stage_iterations'][0], {'stage': 2, 'iters': 3, 'count': 2})


if __name__ == '__main__':
    unittest.main()
