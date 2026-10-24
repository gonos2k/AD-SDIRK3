#!/usr/bin/env python3
"""No WRF/MPI runs: own-child lifecycle and source-extracted shutdown oracle."""
from __future__ import annotations

import importlib.util
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
# Frozen preprocessed modified regions at merged PR #317. No git-history access
# is needed in shallow CI. module_wrf_top covers wrf_finalize only.
DEFAULT_OFF_SHA = {
    'main/wrf.F': '5abc05435fd0fa6c393bf6f92f95478ba2ef61fe818ceb12636fe6bc268d0ce0',
    'main/module_wrf_top.F': 'a187936d79fcee3cd1d09beac43c70e5542bf7809c236288e071fb9c55672167',
    'frame/wrf_shutdown.F': '9e19d0332513a8811c2f466436815390dbe51eadc1f67aa97236854c0aab5ca6',
}
spec = importlib.util.spec_from_file_location('native_exit_trace', HERE / 'native_exit_trace.py')
recorder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(recorder)


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, **kwargs)
    if result.returncode:
        raise AssertionError(f'{command!r}\n{result.stdout}\n{result.stderr}')
    return result


def finalize_body(source):
    return re.search(r'(?ims)^   SUBROUTINE wrf_finalize\(.*?^   END SUBROUTINE wrf_finalize',
                     source).group(0)


FAKE_PMPI = r'''
#include <assert.h>
#include <stdlib.h>
#include "mpi.h"
static int init_calls, finalize_calls;
MPI_Fint *expected_pointer;
void pmpi_init_(MPI_Fint *p) {
  assert(p != NULL); assert(++init_calls == 1); *p = 0;
}
void pmpi_finalize_(MPI_Fint *p) {
  assert(p != NULL); assert(init_calls == 1); assert(++finalize_calls == 1);
  if (expected_pointer) assert(p == expected_pointer);
  *p = getenv("FAKE_IERR") ? atoi(getenv("FAKE_IERR")) : 0;
}
#ifndef TRACE_ENABLED
void mpi_init_(MPI_Fint *p) { pmpi_init_(p); }
void mpi_finalize_(MPI_Fint *p) { pmpi_finalize_(p); }
#endif
'''


class NativeExitTrace(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = Path(self.tmp.name)

    def test_owned_child_zero_nonzero_and_zombie(self):
        for code in (0, 7):
            result = recorder.record_launch([sys.executable, '-c', f'raise SystemExit({code})'],
                                            sys.executable, self.path / str(code))
            self.assertEqual(result['launcher_return_code'], code)
            self.assertEqual(result['status'], 'launcher_reaped')
            self.assertEqual(result['events'][-1]['return_code'], code)
            self.assertEqual(result['trace']['bytes'], 0)  # Not a diagnostic pass.
        # Observe our own dead child without poll()/wait(), then reap via its handle.
        proc = subprocess.Popen([sys.executable, '-c', 'raise SystemExit(0)'])
        self.addCleanup(proc.wait)
        deadline = time.monotonic() + 5
        state = ''
        while time.monotonic() < deadline:
            state = subprocess.run(['ps', '-p', str(proc.pid), '-o', 'stat='],
                                   capture_output=True, text=True).stdout.strip()
            if state.startswith('Z'):
                break
            time.sleep(0.01)
        self.assertTrue(state.startswith('Z'), state)
        self.assertEqual(proc.wait(), 0)
        self.assertEqual(subprocess.run(['ps', '-p', str(proc.pid), '-o', 'stat='],
                                       capture_output=True, text=True).stdout.strip(), '')

    def test_signal_status_and_no_directory_reuse(self):
        result = recorder.record_launch([sys.executable, '-c',
            'import os,signal; os.kill(os.getpid(),signal.SIGTERM)'], sys.executable,
            self.path / 'signaled')
        self.assertEqual(result['launcher_return_code'], -15)
        with self.assertRaises(FileExistsError):
            recorder.record_launch([sys.executable, '-c', 'pass'], sys.executable,
                                   self.path / 'signaled')
        cli = subprocess.run([sys.executable, str(HERE / 'native_exit_trace.py'),
            '--native-executable', sys.executable, '--output', str(self.path / 'cli'),
            '--', sys.executable, '-c', 'raise SystemExit(7)'], capture_output=True)
        self.assertEqual(cli.returncode, 7)

    def test_terminal_interrupt_is_forwarded_once(self):
        ready, seen = self.path / 'ready', self.path / 'seen'
        child = self.path / 'child.py'
        child.write_text('''import os,signal,sys,time
from pathlib import Path
count = 0
def interrupted(_signum, _frame):
    global count
    count += 1
    Path(sys.argv[2]).write_text(str(count))
signal.signal(signal.SIGINT, interrupted)
Path(sys.argv[1]).write_text(str(os.getpgrp()))
deadline = time.monotonic() + 3
while count == 0 and time.monotonic() < deadline:
    time.sleep(0.01)
time.sleep(0.2)
raise SystemExit(7)
''')
        output = self.path / 'interrupt'
        proc = subprocess.Popen([sys.executable, str(HERE / 'native_exit_trace.py'),
            '--native-executable', sys.executable, '--output', str(output), '--',
            sys.executable, str(child), str(ready), str(seen)], start_new_session=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic() + 3
            while not ready.exists() and time.monotonic() < deadline:
                time.sleep(0.01)
            self.assertTrue(ready.is_file())
            os.killpg(proc.pid, signal.SIGINT)  # Only this test's isolated recorder group.
            stdout, stderr = proc.communicate(timeout=5)
            self.assertEqual(proc.returncode, 7, (stdout, stderr))
            self.assertNotEqual(int(ready.read_text()), proc.pid)
            self.assertEqual(seen.read_text(), '1')
            receipt = json.loads((output / 'launcher.json').read_text())
            self.assertEqual(receipt['launcher_return_code'], 7)
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.communicate(timeout=5)

    def fake_objects(self):
        cc = shutil.which(os.environ.get('CC', 'cc'))
        if cc is None:
            self.skipTest('C compiler not installed')
        (self.path / 'mpi.h').write_text('typedef int MPI_Fint;\n')
        (self.path / 'fake.c').write_text(FAKE_PMPI)
        common = [cc, '-std=c11', '-Wall', '-Wextra', '-Werror',
                  '-pthread', '-I', str(self.path)]
        run(common + ['-c', str(HERE / 'native_mpi_finalize_trace.c'), '-o', str(self.path / 'trace.o')])
        run(common + ['-DTRACE_ENABLED', '-c', str(self.path / 'fake.c'), '-o', str(self.path / 'fake.o')])
        return common

    def test_observer_forwards_ierr_once_and_adds_no_MPI_queries(self):
        common = self.fake_objects()
        driver = self.path / 'driver.c'
        driver.write_text('''#include "mpi.h"
extern MPI_Fint *expected_pointer;
extern void mpi_init_(MPI_Fint *), mpi_finalize_(MPI_Fint *);
int main(void) {
  MPI_Fint ierr; mpi_init_(&ierr); expected_pointer = &ierr;
  mpi_finalize_(&ierr); return ierr;
}
''')
        exe = self.path / 'observer.exe'
        run(common + [str(driver), str(self.path / 'trace.o'), str(self.path / 'fake.o'), '-o', str(exe)])
        for code in (0, 17):
            trace = self.path / f'trace-{code}'
            env = {**os.environ, 'NATIVE_MPI_FINALIZE_TRACE': str(trace), 'FAKE_IERR': str(code)}
            proc = subprocess.run([str(exe)], env=env, capture_output=True)
            self.assertEqual(proc.returncode, code)
            lines = trace.read_text().splitlines()
            self.assertEqual([re.search(r'phase=(\w+)', s)[1] for s in lines],
                ['observer_loaded', 'mpi_init_enter', 'mpi_init_return',
                 'mpi_finalize_enter', 'mpi_finalize_return', 'observer_destructor'])
            self.assertIn(f'ierr={code}', lines[-2])
            times = [int(re.search(r'monotonic_ns=(\d+)', s)[1]) for s in lines]
            self.assertEqual(times, sorted(times))
            self.assertEqual(len({re.search(r'pthread_token=(\d+)', s)[1] for s in lines}), 1)
        # The fake header/library provides no MPI state-query symbols: linking
        # already rejects added MPI_Initialized/Finalized/Query_thread calls.
        env = dict(os.environ)
        env.pop('NATIVE_MPI_FINALIZE_TRACE', None)
        env.pop('FAKE_IERR', None)
        run([str(exe)], env=env)

    def test_default_off_preprocessing_and_actual_shutdown_bodies(self):
        fc = shutil.which(os.environ.get('FC', 'gfortran'))
        if fc is None:
            self.skipTest('GNU Fortran not installed')
        self.fake_objects()
        flags = [fc, '-cpp', '-ffree-form', '-ffree-line-length-none',
                 '-DDM_PARALLEL', '-DNO_IEEE_MODULE', '-DWRFPLUS=0']
        paths = ('main/wrf.F', 'main/module_wrf_top.F', 'frame/wrf_shutdown.F')
        for rel in paths:
            after = (ROOT / rel).read_text()
            if rel.endswith('module_wrf_top.F'):
                after = finalize_body(after)
            path = self.path / 'pp.F'
            path.write_text(after)
            text = run(flags + ['-E', '-P', str(path)]).stdout
            canonical = '\n'.join(line.rstrip() for line in text.splitlines() if line.strip())
            self.assertEqual(hashlib.sha256(canonical.encode()).hexdigest(), DEFAULT_OFF_SHA[rel], rel)

        body = finalize_body((ROOT / 'main/module_wrf_top.F').read_text())
        module = self.path / 'top.F90'
        module.write_text('''module module_wrf_top
implicit none
integer :: head_grid=0, config_flags=0
logical :: coupler_on=.false.
contains
subroutine wrf_init
integer ierr
call MPI_INIT(ierr)
end subroutine
subroutine wrf_dfi
end subroutine
subroutine wrf_run
end subroutine
''' + body + '\nend module\n')
        dm = (ROOT / 'external/RSL_LITE/module_dm.F').read_text()
        dm = re.search(r'(?ims)^   SUBROUTINE wrf_dm_shutdown\s*$.*?^   END SUBROUTINE wrf_dm_shutdown', dm)[0]
        stubs = self.path / 'stubs.F90'
        stubs.write_text(dm + '''
subroutine med_shutdown_io(head,config)
integer head,config
end subroutine
subroutine wrf_debug(level,message)
integer level
character*(*) message
end subroutine
subroutine WRFU_Finalize
end subroutine
subroutine cpl_finalize
end subroutine
''')
        exe = self.path / 'fortran-oracle.exe'
        link_flags = []
        if sys.platform == 'darwin':
            sdk = run(['xcrun', '--show-sdk-path']).stdout.strip()
            link_flags = [f'-Wl,-syslibroot,{sdk}']
        run(flags + ['-DWRF_NATIVE_EXIT_TRACE', '-J', str(self.path), '-I', str(self.path),
            str(module), str(ROOT / 'main/wrf.F'), str(ROOT / 'frame/wrf_shutdown.F'), str(stubs),
            str(self.path / 'trace.o'), str(self.path / 'fake.o'), '-pthread', *link_flags,
            '-o', str(exe)], cwd=self.path)
        trace = self.path / 'oracle.trace'
        env = {**os.environ, 'NATIVE_MPI_FINALIZE_TRACE': str(trace), 'FAKE_IERR': '0'}
        run([str(exe)], env=env)
        phases = [re.search(r'phase=(\w+)', line)[1] for line in trace.read_text().splitlines()]
        self.assertEqual(phases, ['observer_loaded', 'mpi_init_enter', 'mpi_init_return',
            'model_run_return', 'io_cleanup_enter', 'io_cleanup_return',
            'framework_finalize_enter', 'framework_finalize_return', 'wrf_shutdown_enter',
            'mpi_finalize_enter', 'mpi_finalize_return', 'wrf_shutdown_return',
            'wrf_finalize_return', 'observer_destructor'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
