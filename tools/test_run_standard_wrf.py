#!/usr/bin/env python3
"""Fault-inject controller boundaries without starting WRF, MPI, or a shell."""
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import run_standard_wrf as runtime


class FakeClock:
    def __init__(self):
        self.value = 0.0

    def monotonic(self):
        return self.value

    def time_ns(self):
        return int((1_700_000_000 + self.value) * 1e9)

    def sleep(self, seconds):
        self.value += seconds


class FakeProcess:
    pid = 910001

    def __init__(self):
        self.returncode = None
        self.terminated = 0
        self.waited = 0

    def poll(self):
        return self.returncode

    def terminate(self):
        self.terminated += 1
        self.returncode = -15

    def kill(self):
        self.returncode = -9

    def wait(self, timeout=None):
        self.waited += 1
        return self.returncode


class WrapperTests(unittest.TestCase):
    def test_cumulative_and_duplicate_terminal_rows(self):
        self.assertTrue(runtime.terminal_wrapper_calls_ok(
            [(300, 0), (600, 0), (2160, 0), (2160, 0)], 2160))
        self.assertTrue(runtime.terminal_wrapper_calls_ok([], 0))

    def test_failures_decrease_and_missing_final_are_rejected(self):
        for rows in ([], [(300, 0)], [(2163, 0)], [(300, 1), (2160, 0)],
                     [(600, 0), (300, 0), (2160, 0)], [(2160, -1)]):
            with self.subTest(rows=rows):
                self.assertFalse(runtime.terminal_wrapper_calls_ok(rows, 2160))

    def test_process_parser_rejects_unusable_snapshots(self):
        for stdout in ('', '\n', '12 1 500\n', 'x 1 500 R Mon Oct 26\n'):
            with self.subTest(stdout=stdout), patch.object(
                    runtime.subprocess, 'run', return_value=SimpleNamespace(stdout=stdout)):
                with self.assertRaises((RuntimeError, ValueError)):
                    runtime.processes()

    def test_process_parser_preserves_birth_and_rss(self):
        text = '12 1 500 R Mon Oct 26 09:00:00 2026\n'
        with patch.object(runtime.subprocess, 'run', return_value=SimpleNamespace(stdout=text)):
            self.assertEqual(runtime.processes()[12], {
                'ppid': 1, 'rss': 500 * 1024, 'state': 'R',
                'birth': 'Mon Oct 26 09:00:00 2026'})


class ControllerTests(unittest.TestCase):
    def scenario(self, outcomes, *, cap=100, disk_ok=True, authorized=True):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            case = root / 'runs' / 'ark_h600'
            case.mkdir(parents=True)
            (root / 'initial_views' / 'rk3_h600').mkdir(parents=True)
            exe = root / 'fake.exe'
            exe.write_bytes(b'NOT AN EXECUTABLE')
            (case / 'wrfinput_d01').write_bytes(b'input')
            (case / 'namelist.input').write_bytes(b'quiet')
            manifest = {
                'resource_authorized': authorized,
                'no_retries_or_cold_restart': True,
                'runtime_policy': {'ranks': 1, 'argv': ['fake-mpirun', '-np', '1', str(exe)]},
                'limits': {'case_wall_seconds': cap, 'total_wall_seconds': cap,
                           'rss_bytes': 32 * 1024**3, 'reserve_disk_bytes': 20 * 1024**3},
                'cases': {'ark_h600': {
                    'directory': str(case), 'method': 'ARK324', 'h_seconds': 600,
                    'expected_steps': 720, 'expected_duration_seconds': 432000,
                    'namelist_sha256': runtime.sha(case / 'namelist.input')}},
                'executable': {'path': str(exe), 'sha256': runtime.sha(exe)},
                'common_input': {'sha256': runtime.sha(case / 'wrfinput_d01')},
                'common_environment': {}, 'analysis_contract': {},
            }
            path = root / 'manifest.json'
            path.write_text(json.dumps(manifest))
            argv = ['--manifest', str(path), '--manifest-sha256', runtime.sha(path),
                    '--case', 'ark_h600', '--go']
            proc, clock = FakeProcess(), FakeClock()
            sampler_calls, cleanups, launched = [], [], []
            samples = iter(outcomes)
            disk_calls = []

            def disk_usage(*args):
                disk_calls.append(True)
                available = disk_ok or len(disk_calls) == 1
                return SimpleNamespace(free=100 * 1024**3 if available else 0)

            def popen(*args, **kwargs):
                launched.append(args)
                (case / 'rsl.out.0000').write_text(
                    'Timing for main: 1 elapsed seconds\n' * 720 +
                    'Tile 1: Steps= 300, Failed= 0\n'
                    'Tile 1: Steps= 600, Failed= 0\n'
                    'Tile 1: Steps= 2160, Failed= 0\nSUCCESS COMPLETE WRF\n')
                (case / 'rsl.error.0000').write_text('')
                return proc

            def snapshot():
                sampler_calls.append(clock.value)
                item = next(samples)
                if item in ('timeout', 'timeout_done'):
                    clock.value += 3
                    if item == 'timeout_done':
                        proc.returncode = 0
                    raise subprocess.TimeoutExpired('ps', 3)
                if item == 'error':
                    raise subprocess.CalledProcessError(1, 'ps')
                if item == 'missing':
                    return {}
                if item == 'slow':
                    clock.value += 3
                if item == 'done':
                    proc.returncode = 0
                return {proc.pid: {'ppid': 1, 'rss': 1024, 'state': 'R', 'birth': 'owned'}}

            def cleanup(process, tracked, executable):
                cleanups.append(process.returncode is None)
                if process.returncode is None:
                    process.terminate()
                process.wait()
                return {'signals': [], 'errors': [], 'remaining_owned': {}}

            helper = SimpleNamespace(_load_run=lambda *a: {
                'grid': {}, 'physical_minima_over_all_frames': {},
                'physical_minimum_total_pressure_Pa': 1, 'timestamps': [], 'step_logs': {}})
            with patch.object(runtime.subprocess, 'Popen', side_effect=popen), \
                    patch.object(runtime, 'processes', side_effect=snapshot), \
                    patch.object(runtime, 'cleanup', side_effect=cleanup), \
                    patch.object(runtime, 'get_helper', return_value=helper), \
                    patch.object(runtime, 'initial_gate', return_value={'status': 'passed'}), \
                    patch.object(runtime.time, 'monotonic', side_effect=clock.monotonic), \
                    patch.object(runtime.time, 'time_ns', side_effect=clock.time_ns), \
                    patch.object(runtime.time, 'sleep', side_effect=clock.sleep), \
                    patch.object(runtime.shutil, 'disk_usage', side_effect=disk_usage), \
                    patch.dict(runtime.os.environ, {}, clear=True), \
                    patch.object(runtime.sys, 'stdout', new=io.StringIO()):
                if not authorized:
                    with self.assertRaises((AssertionError, ValueError, SystemExit)):
                        runtime.main(argv)
                    self.assertFalse(launched)
                    return None
                code = runtime.main(argv)
            receipt = json.loads((case / 'forecast_receipt.json').read_text())
            return code, receipt, sampler_calls, cleanups

    def test_one_or_two_timeout_samples_recover_without_early_cleanup(self):
        for misses in (1, 2):
            with self.subTest(misses=misses):
                code, receipt, calls, cleanups = self.scenario(['timeout'] * misses + ['done'])
                self.assertEqual(code, 0)
                self.assertEqual(receipt['status'], 'completed')
                self.assertEqual(len(calls), misses + 1)
                self.assertEqual(cleanups, [False])

    def test_three_consecutive_timeouts_stop_as_monitoring_loss(self):
        code, receipt, calls, cleanups = self.scenario(['timeout'] * 3)
        self.assertNotEqual(code, 0)
        self.assertIn('monitor', receipt['stop_reason'])
        self.assertEqual(len(calls), 3)
        self.assertEqual(cleanups, [True])
        self.assertFalse(receipt['cost_eligible'])

    def test_valid_sample_resets_the_consecutive_timeout_count(self):
        code, receipt, calls, cleanups = self.scenario(
            ['timeout', 'timeout', 'sample', 'timeout', 'timeout', 'done'])
        self.assertEqual(code, 0)
        self.assertEqual(receipt['status'], 'completed')
        self.assertEqual(len(calls), 6)
        self.assertEqual(cleanups, [False])

    def test_normal_exit_during_third_timeout_still_validates_outputs(self):
        code, receipt, calls, cleanups = self.scenario(
            ['timeout', 'timeout', 'timeout_done'])
        self.assertEqual(code, 0)
        self.assertEqual(receipt['status'], 'completed')
        self.assertEqual(len(calls), 3)
        self.assertEqual(cleanups, [False])

    def test_cap_is_checked_during_timeout_grace(self):
        code, receipt, calls, cleanups = self.scenario(['timeout'] * 3, cap=2)
        self.assertNotEqual(code, 0)
        self.assertIn('resource_cap', receipt['stop_reason'])
        self.assertEqual(len(calls), 1)
        self.assertEqual(cleanups, [True])

    def test_slow_successful_query_cannot_bypass_the_cap(self):
        code, receipt, calls, cleanups = self.scenario(['slow'], cap=2)
        self.assertNotEqual(code, 0)
        self.assertIn('resource_cap', receipt['stop_reason'])
        self.assertEqual(len(calls), 1)
        self.assertEqual(cleanups, [True])

    def test_cap_takes_precedence_if_third_timeout_also_exhausts_it(self):
        code, receipt, calls, _ = self.scenario(['timeout'] * 3, cap=10)
        self.assertNotEqual(code, 0)
        self.assertIn('resource_cap', receipt['stop_reason'])
        self.assertEqual(len(calls), 3)

    def test_nonfinite_wall_limits_never_launch(self):
        for cap in (float('inf'), float('nan')):
            with self.subTest(cap=cap), self.assertRaises(ValueError):
                self.scenario([], cap=cap)

    def test_disk_cap_is_checked_before_process_query(self):
        code, receipt, calls, _ = self.scenario([], disk_ok=False)
        self.assertNotEqual(code, 0)
        self.assertIn('resource_cap', receipt['stop_reason'])
        self.assertEqual(calls, [])

    def test_non_timeout_query_error_does_not_get_timeout_grace(self):
        code, receipt, calls, cleanups = self.scenario(['error'])
        self.assertNotEqual(code, 0)
        self.assertEqual(len(calls), 1)
        self.assertEqual(cleanups, [True])

    def test_missing_live_launcher_snapshot_fails_closed(self):
        code, receipt, calls, cleanups = self.scenario(['missing'])
        self.assertNotEqual(code, 0)
        self.assertEqual(len(calls), 1)
        self.assertEqual(cleanups, [True])

    def test_missing_resource_authorization_never_spawns(self):
        self.scenario([], authorized=False)

    def test_cleanup_snapshot_error_still_reaps_owned_popen(self):
        proc = FakeProcess()
        with patch.object(runtime, 'processes', side_effect=subprocess.CalledProcessError(1, 'ps')), \
                patch.object(runtime.signal, 'pthread_sigmask', return_value=set()), \
                patch.object(runtime.os, 'kill') as kill, \
                patch.object(runtime.time, 'sleep'):
            result = runtime.cleanup(proc, {}, Path('/fake/model.exe'))
        self.assertEqual(proc.terminated, 1)
        self.assertGreaterEqual(proc.waited, 1)
        self.assertTrue(result['errors'])
        kill.assert_not_called()

    def test_cleanup_never_signals_a_recycled_pid(self):
        proc = FakeProcess()
        child = proc.pid + 1
        tracked = {child: {'birth': 'original'}}
        rows = {child: {'ppid': 1, 'rss': 1024, 'state': 'R', 'birth': 'recycled'}}
        with patch.object(runtime, 'processes', return_value=rows), \
                patch.object(runtime.os, 'getsid', return_value=child), \
                patch.object(runtime.os, 'kill') as kill, \
                patch.object(runtime.signal, 'pthread_sigmask', return_value=set()), \
                patch.object(runtime.time, 'sleep'):
            runtime.cleanup(proc, tracked, Path('/fake/model.exe'))
        self.assertEqual(proc.terminated, 1)
        self.assertGreaterEqual(proc.waited, 1)
        kill.assert_not_called()

    def test_cleanup_never_signals_an_unverified_executable(self):
        proc = FakeProcess()
        child = proc.pid + 1
        rows = {child: {'ppid': proc.pid, 'rss': 1024, 'state': 'R', 'birth': 'owned'}}
        with patch.object(runtime, 'processes', return_value=rows), \
                patch.object(runtime.os, 'getsid', return_value=proc.pid), \
                patch.object(runtime.subprocess, 'run', return_value=SimpleNamespace(
                    stdout='n/not/the/model.exe\n')), \
                patch.object(runtime.os, 'kill') as kill, \
                patch.object(runtime.signal, 'pthread_sigmask', return_value=set()), \
                patch.object(runtime.time, 'sleep'):
            result = runtime.cleanup(proc, {}, Path('/fake/model.exe'))
        self.assertTrue(result['errors'])
        kill.assert_not_called()


class ObserverTests(unittest.TestCase):
    def test_observer_accepts_the_new_controller_command_and_publishes_exit(self):
        source = Path(__file__).resolve().parents[1] / 'docs/evidence/ark600_five_day_interrupted/execution/next_observer.py'
        spec = importlib.util.spec_from_file_location('next_observer_loop', source)
        observer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(observer)
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            case = root / 'runs' / 'ark_h600'
            case.mkdir(parents=True)
            observer.ROOT, observer.CASE = root, case
            (root / 'launch_receipt.json').write_text(json.dumps({
                'runner_pid': 910001,
                'command': ['python3', '-B', '/repo/tools/run_standard_wrf.py',
                            '--manifest', str(root / 'manifest.json')]}))
            (root / 'manifest.json').write_text(json.dumps({
                'limits': {'case_wall_seconds': 64800}}))
            (case / 'launcher_pid.json').write_text(json.dumps({
                'wall_start_ns': observer.time.time_ns()}))
            outcome = {'status': 'failed_or_incomplete', 'actual_outer_step_count': 251,
                       'return_code': 1, 'stop_reason': 'monitoring_unavailable',
                       'elapsed_seconds': 1, 'wall_seconds': 1, 'cost_eligible': False}

            def finish_on_first_poll(_):
                (case / 'forecast_receipt.json').write_text(json.dumps(outcome))

            with patch.object(observer, 'sha', return_value=observer.HELPER_SHA), \
                    patch.object(observer, 'closed', return_value=False), \
                    patch.object(observer.subprocess, 'run', return_value=SimpleNamespace(
                        returncode=0, stdout='python3 -B /repo/tools/run_standard_wrf.py')), \
                    patch.object(observer.time, 'sleep', side_effect=finish_on_first_poll) as sleep, \
                    patch.object(observer, 'analyze') as analyze:
                observer.main()
            sleep.assert_called_once()
            analyze.assert_not_called()
            self.assertEqual(json.loads((root / 'progress.json').read_text())['status'],
                             'failed_or_incomplete')
            self.assertEqual(json.loads((root / 'checkpoint_observer_receipt.json').read_text())[
                'forecast_status'], 'failed_or_incomplete')

    def test_terminal_progress_replaces_stale_running_state(self):
        source = Path(__file__).resolve().parents[1] / 'docs/evidence/ark600_five_day_interrupted/execution/next_observer.py'
        spec = importlib.util.spec_from_file_location('next_observer', source)
        observer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(observer)
        for status, steps, rc in [('failed_or_incomplete', 251, 1), ('completed', 720, 0)]:
            with self.subTest(status=status), tempfile.TemporaryDirectory() as directory:
                observer.ROOT = Path(directory)
                target = observer.ROOT / 'progress.json'
                target.write_text(json.dumps({'status': 'running_no_completion_claim',
                                              'accepted_outer_step_log_records': 250}))
                observer.publish_terminal_progress({
                    'status': status, 'actual_outer_step_count': steps, 'return_code': rc,
                    'stop_reason': None if rc == 0 else 'monitor_unavailable',
                    'elapsed_seconds': 1, 'wall_seconds': 1, 'cost_eligible': rc == 0}, {})
                result = json.loads(target.read_text())
                self.assertEqual(result['status'], status)
                self.assertEqual(result['completed_outer_step_log_records'], steps)
                self.assertEqual(result['five_day_complete'], status == 'completed')
                self.assertFalse(result['accuracy_pass_claim'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
