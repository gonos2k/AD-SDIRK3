#!/usr/bin/env python3
"""Record one explicit future launch; never infer success from native trace text."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import signal
import subprocess
import threading
import time


def file_pin(path):
    path = Path(path).resolve()
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1 << 20), b''):
            digest.update(block)
    return {'path': str(path), 'bytes': path.stat().st_size, 'sha256': digest.hexdigest()}


def record_launch(command, native_executable, output):
    """Wait on our direct child handle, including a child already observed as zombie.

    The returned OS status belongs to the launcher, not independently to MPI ranks.
    No numeric-PID lookup, signal injection, restart, or failed-run acceptance.
    """
    native = Path(native_executable).resolve(strict=True)
    launcher = shutil.which(command[0])
    if launcher is None:
        raise ValueError('launcher executable not found')
    if not any(Path(arg).resolve() == native for arg in (launcher, *command[1:])):
        raise ValueError('the pinned native executable must appear in the command')
    pins = {'launcher': file_pin(launcher), 'native': file_pin(native),
            'recorder': file_pin(__file__)}
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    trace = output / 'native.trace'
    trace.touch(exist_ok=False)
    receipt = {'schema': 'native_exit_launch_receipt_v1', 'command': command,
               'pins': pins, 'environment_overrides': {'NATIVE_MPI_FINALIZE_TRACE': str(trace)},
               'launcher_return_code': None, 'launcher_session_isolated': True,
               'status': 'prepared', 'events': []}

    def event(phase, **fields):
        receipt['events'].append({'phase': phase, 'pid': os.getpid(),
            'thread_id': threading.get_native_id(), 'monotonic_ns': time.monotonic_ns(),
            'wall_ns': time.time_ns(), **fields})
        (output / 'launcher.json').write_text(json.dumps(receipt, indent=2) + '\n')

    env = os.environ.copy()
    env['NATIVE_MPI_FINALIZE_TRACE'] = str(trace)
    event('launcher_spawn_enter')
    try:
        # Terminal Ctrl-C reaches this recorder, then is forwarded once below.
        proc = subprocess.Popen(command, env=env, start_new_session=True)
    except OSError as exc:
        receipt.update(status='launch_failed', error=str(exc))
        event('launcher_spawn_failed')
        return receipt
    try:
        event('launcher_spawn_return', child_pid=proc.pid)
        event('launcher_wait_enter', child_pid=proc.pid)
        code = proc.wait()
    except KeyboardInterrupt:
        # Forward only to the still-owned Popen child; never guess a PID from ps.
        event('launcher_wait_interrupted', child_pid=proc.pid)
        if proc.poll() is None:
            proc.send_signal(signal.SIGINT)
        code = proc.wait()
    receipt.update(status='launcher_reaped', launcher_return_code=code,
                   trace=file_pin(trace))
    event('launcher_wait_return', child_pid=proc.pid, return_code=code)
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--native-executable', required=True)
    parser.add_argument('--output', type=Path, required=True, help='new evidence directory')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('an explicit command after -- is required')
    receipt = record_launch(command, args.native_executable, args.output)
    code = receipt['launcher_return_code']
    # A zero shell status says only that the OS-waited launcher returned zero.
    if code is None:
        return 125
    return code if code >= 0 else 128 - code


if __name__ == '__main__':
    raise SystemExit(main())
