#!/usr/bin/env python3
"""Build the registered terminal diagnostic executable; never runs WRF."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

W = pathlib.Path('/private/tmp/terminal-jvp-replay-20261102')
E = pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102')
P = pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101')
A = P / 'native_control'
N = pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_build')
C = W / 'external/libtorch_wrf/sdirk3'
BASE = '5dc7fc7c8c064f71dc2a272099852f4a529bb73f'
CHANGED_HEADER = 'wrf_sdirk3_jvp_fwad_or_fd.h'
HELPER = 'wrf_sdirk3_terminal_jvp_probe.h'
NEWTON = 'wrf_sdirk3_newton_solver.cpp'
EXPECTED = {NEWTON, 'wrf_sdirk3_tile_unified_impl.cpp',
            'wrf_sdirk3_pressure_stage_probe.cpp', 'wrf_sdirk3_v_primary_probe.cpp'}


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def output(args, **kwargs):
    return subprocess.check_output(list(map(str, args)), text=True, **kwargs).strip()


def stack_check(stack):
    assert output(['xcrun', '--show-sdk-path']) == stack['SDK_path']
    assert output(['xcrun', '--show-sdk-version']) == stack['SDK_version']
    assert sha('/usr/bin/g++') == stack['compiler_driver_sha256']
    assert sha(stack['compiler_resolved_path']) == stack['compiler_resolved_sha256']
    assert output(['/usr/bin/g++', '--version']) == stack['compiler_version']
    assert output(['sw_vers']) == stack['OS']


def clean(target):
    assert output(['git', 'rev-parse', 'HEAD'], cwd=W) == target
    assert not output(['git', 'status', '--porcelain', '--untracked-files=all'], cwd=W)


def build_version(path):
    lines = output(['otool', '-l', path]).splitlines()
    i = next(i for i, line in enumerate(lines) if line.strip() == 'cmd LC_BUILD_VERSION')
    return lines[i:i + 6]


def main():
    if len(sys.argv) != 4:
        raise SystemExit('usage: build_terminal.py TARGET_SHA REG_PATH REG_SHA')
    target, regname, regsha = sys.argv[1:]
    reg = pathlib.Path(regname)
    assert sha(reg) == regsha
    policy = json.loads(reg.read_text())
    assert policy['status'] == 'registered' and policy['source_commit'] == target
    for path, digest in policy.get('immutable_inputs', {}).items():
        assert sha(path) == digest, path
    clean(target)
    changed = output(['git', 'diff', '--name-only', BASE, target], cwd=W).splitlines()
    # No changed WRF/Fortran or other production sources may enter the reused stack.
    protected = ('dyn_em/', 'Registry/', 'frame/', 'share/', 'phys/', 'main/', 'arch/',
                 'external/libtorch_wrf/sdirk3/')
    prod = {p for p in changed if p.startswith(protected) and '/tests/' not in p
            and not p.endswith('CMakeLists.txt')}
    allowed = {'external/libtorch_wrf/sdirk3/' + n for n in (NEWTON, CHANGED_HEADER, HELPER)}
    assert prod == allowed, (prod, allowed)

    prior_path = A / 'build_receipt.json'
    prior = json.loads(prior_path.read_text())
    assert prior['source_commit'] == BASE
    assert sha(A / 'wrf.exe') == prior['executable_sha256']
    assert sha(A / 'libwrf_sdirk3_libtorch.a') == prior['archive_sha256']
    records = prior['source_objects']
    assert len(records) == 31 and len({r['source'] for r in records}) == 31
    proof_path = N / 'complete_provenance.json'
    assert sha(proof_path) == prior['fortran_provenance_sha256']
    proof = json.loads(proof_path.read_text())
    for path, digest in proof['object_module_archive_sha256'].items():
        assert sha(N / 'source' / path) == digest, path
    for path, digest in proof['dylib_sha256'].items():
        if digest != 'macOS shared cache':
            assert sha(path) == digest, path
    stack = json.loads((P / 'current_sdk_provenance.json').read_text())
    stack_check(stack)
    # Pin all pre-existing project headers, except the intentionally changed one.
    for path, digest in prior['header_sha256'].items():
        if pathlib.Path(path).name != CHANGED_HEADER:
            assert sha(W / path) == digest, path
    consumers = set()
    for r in records:
        obj = pathlib.Path(r['object'])
        dep = obj.with_suffix('.d')
        assert sha(obj) == r['object_sha256'], obj
        member = subprocess.check_output(['ar', '-p', str(A / 'libwrf_sdirk3_libtorch.a'), obj.name])
        assert hashlib.sha256(member).hexdigest() == r['object_sha256'], obj
        if CHANGED_HEADER in dep.read_text(errors='strict'):
            consumers.add(r['source'])
        if r['source'] not in EXPECTED:
            assert sha(C / r['source']) == r['source_sha256'], r['source']
    assert consumers == EXPECTED, consumers
    # Any direct include of the new helper in production must be in Newton.
    helper_users = {p.name for p in C.glob('*.cpp') if HELPER in p.read_text(errors='strict')}
    assert helper_users == {NEWTON}, helper_users
    assert pathlib.Path(next(a for a in next(r for r in records if r['source'] == NEWTON)['argv']
                             if a.endswith('/' + NEWTON))).name == NEWTON
    assert shutil.disk_usage(E).free >= 5 * 1024**3
    out = E / 'native_terminal'
    assert not out.exists()
    source_before = {str(p.relative_to(W)): sha(p) for p in C.rglob('*')
                     if p.is_file() and p.suffix in ('.h', '.hpp', '.cpp')}
    out.mkdir()
    built = []
    for r in records:
        name = r['source']
        if name not in EXPECTED:
            built.append(r)
            continue
        obj = out / (pathlib.Path(name).stem + '.o')
        argv = r['argv'].copy()
        oldroot = str(pathlib.Path(next(a for a in argv if a.endswith('/' + name))).parent)
        argv = [a.replace(oldroot, str(C)) for a in argv]
        argv[-1] = str(obj)
        if name == NEWTON:
            argv.insert(1, '-DWRF_SDIRK3_TERMINAL_JVP_PROBE')
        with (out / (obj.stem + '.log')).open('xb') as log:
            subprocess.run(argv, cwd=C, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
        dep = obj.with_suffix('.d')
        assert CHANGED_HEADER in dep.read_text(errors='strict')
        if name == NEWTON:
            assert HELPER in dep.read_text(errors='strict')
        built.append({'source': name, 'source_sha256': sha(C / name), 'object': str(obj),
                      'object_sha256': sha(obj), 'argv': argv, 'depfile_sha256': sha(dep)})
    archive = out / 'libwrf_sdirk3_libtorch.a'
    subprocess.run(['ar', 'rcs', str(archive), *[r['object'] for r in built]], check=True, timeout=120)
    subprocess.run(['ranlib', str(archive)], check=True, timeout=120)
    members = [m for m in output(['ar', '-t', archive]).splitlines() if not m.startswith('__.SYMDEF')]
    assert members == [pathlib.Path(r['object']).name for r in built]
    assert len(set(members)) == 31
    for r in built:
        data = subprocess.check_output(['ar', '-p', str(archive), pathlib.Path(r['object']).name])
        assert hashlib.sha256(data).hexdigest() == r['object_sha256']
    link = prior['link_argv'].copy()
    link[2] = str(out / 'wrf.exe')
    link[link.index('-L' + str(A))] = '-L' + str(out)
    link[link.index(str(A / 'libwrf_sdirk3_libtorch.a'))] = str(archive)
    with (out / 'link.log').open('xb') as log:
        subprocess.run(link, cwd=prior['link_cwd'], stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
    otool = output(['otool', '-L', out / 'wrf.exe'])
    (out / 'otool.txt').write_text(otool + '\n')
    assert otool.splitlines()[1:] == (A / 'otool.txt').read_text().splitlines()[1:]
    assert build_version(out / 'wrf.exe') == build_version(A / 'wrf.exe')
    stack_check(stack)
    clean(target)
    assert source_before == {str(p.relative_to(W)): sha(p) for p in C.rglob('*')
                             if p.is_file() and p.suffix in ('.h', '.hpp', '.cpp')}
    assert sha(reg) == regsha and shutil.disk_usage(E).free >= 5 * 1024**3
    receipt = {'source_commit': target, 'registration_sha256': regsha,
               'prior_receipt_sha256': sha(prior_path), 'fortran_provenance_sha256': sha(proof_path),
               'current_sdk_provenance_sha256': sha(P / 'current_sdk_provenance.json'),
               'source_hashes': source_before, 'affected_sources': sorted(consumers),
               'source_objects': built, 'archive_members': members, 'archive_sha256': sha(archive),
               'link_argv': link, 'link_cwd': prior['link_cwd'], 'executable_sha256': sha(out / 'wrf.exe'),
               'otool_sha256': sha(out / 'otool.txt'), 'postbuild_free_bytes': shutil.disk_usage(E).free,
               'model_run': False}
    (out / 'build_receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(receipt['executable_sha256'])


if __name__ == '__main__':
    main()
