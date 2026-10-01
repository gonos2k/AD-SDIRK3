from pathlib import Path
import hashlib
import json
import os
import subprocess
import time
from netCDF4 import Dataset

root = Path('/private/tmp/sdirk3-fp64-trajectory-wrf-20260929')
val = root / '.validation'
cases = ['0p46875', '0p234375', '0p1171875', '0p05859375']
records = []
for token in cases:
    run_dir = val / f'fp64_adjcarry_observer_enabled_{token}_15'
    expected = run_dir / 'fp64_carry_final.bin'
    if expected.exists():
        raise SystemExit(f'refusing to overwrite prior observer result: {expected}')
    start = time.perf_counter()
    env = dict(os.environ, OMP_NUM_THREADS='1')
    with (run_dir / 'run.log').open('wb') as log:
        proc = subprocess.run(['mpirun', '-np', '1', './wrf.exe'], cwd=run_dir,
                              env=env, stdout=log, stderr=subprocess.STDOUT,
                              timeout=1200)
    elapsed = time.perf_counter() - start
    if proc.returncode:
        raise SystemExit(f'{token}: mpirun exited {proc.returncode}')
    if not expected.is_file() or expected.stat().st_size != 24531 * 8:
        raise SystemExit(f'{token}: missing/incorrect FP64 dump: {expected}')
    err = (run_dir / 'rsl.error.0000').read_text(errors='replace')
    if 'SUCCESS COMPLETE WRF' not in err:
        raise SystemExit(f'{token}: WRF did not complete successfully')
    record = {
        'case': token,
        'elapsed_s': elapsed,
        'returncode': proc.returncode,
        'namelist_sha256': hashlib.sha256((run_dir/'namelist.input').read_bytes()).hexdigest(),
        'wrfinput_sha256': hashlib.sha256((run_dir/'wrfinput_d01').read_bytes()).hexdigest(),
        'wrf_exe_sha256': hashlib.sha256((run_dir/'wrf.exe').resolve().read_bytes()).hexdigest(),
        'fp64_dump_sha256': hashlib.sha256(expected.read_bytes()).hexdigest(),
        'run_dir': str(run_dir),
    }
    name = 'wrfout_d01_0001-01-01_00:00:00'
    path = run_dir / name
    record['wrfout_file'] = str(path)
    record['wrfout_file_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
    with Dataset(path) as ds:
        record['wrfout_times'] = [b''.join(row).decode('ascii') for row in ds.variables['Times'][:]]
    records.append(record)
    print(json.dumps(record), flush=True)
(val / 'fp64_carry_observer' / 'enabled_dyadic_ladder_runs.json').write_text(
    json.dumps(records, indent=2) + '\n')
