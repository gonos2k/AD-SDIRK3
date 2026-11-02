#!/usr/bin/env python3
"""Build the registered matched pressure diagnostic executable; never runs WRF."""
import hashlib
import json
import pathlib
import shutil
import subprocess
import sys

W = pathlib.Path('/private/tmp/matched-pressure-cost-20261102')
E = pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/matched-pressure-cost-20261102')
P = pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/linear-delay-audit-20261101')
A = pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/terminal-jvp-replay-20261102/native_terminal')
N = pathlib.Path('/Users/yhlee/AD-SDIRK3-review-evidence/newton-local-linearization-20261031/native_build')
C = W / 'external/libtorch_wrf/sdirk3'
BASE = '08b21c1117bf52c93ac3bf1793831f4300dcdc56'
CHANGED_HEADER = 'wrf_sdirk3_terminal_jvp_probe.h'
HELPER = 'wrf_sdirk3_matched_pressure_probe.h'
NEWTON = 'wrf_sdirk3_newton_solver.cpp'
TILE = 'wrf_sdirk3_tile_unified_impl.cpp'
EXPECTED = {NEWTON, TILE}
# Depfiles from the inherited object set, pinned before the matched build.
DEP_SHA256 = {'wrf_sdirk3_globals.cpp': '36b2e989f5ec472d705349a484cfa3b5c3303b40d454a123f3fa0ebf16ed3c25', 'wrf_sdirk3_operator_contract.cpp': 'ac4ec85ca07b358c2909645ac45e7c08d9d912868b238dcfbbbbd2c984568c12', 'wrf_sdirk3_tile_unified_interface.cpp': 'a54f6b1fdfac9c40e053c02a09caaf2c65fa2726d86e2a04b7f2abf754f75aa3', 'wrf_sdirk3_interface_zerocopy.cpp': '595c0dcbbef0108a4b89b8cc8f7bc78abac6de3cf668ac86149c789560209931', 'wrf_sdirk3_zero_copy_interface.cpp': 'eeb394bbe840b3ddc14ca74bb86679413cbfd390e0c6bc597caa47709aeeab6a', 'wrf_sdirk3_tile_unified.cpp': '1cb0ea56642c605af65162c8a8f7e574dff3a2d0f0e0b11e8fcc21ce95055c6a', 'wrf_sdirk3_tile_unified_impl.cpp': 'aaeefcd1a111b6669bc508dab1561ed2b496871abb35844737d08b656f67a0b4', 'wrf_sdirk3_newton_solver.cpp': '3f1d619d99262cbf193a81b5e9b8309e3847f2a6808a8ef9714197e88d6ba8fd', 'wrf_sdirk3_jvp_autograd.cpp': '8561555886f35583fad4c7c626938b928ddf29ddfd8b678b9e9eedc616057588', 'wrf_hydrostatic_pressure.cpp': '6932a0b0541b13f6a8645a007edddc7a45cb3bad453536f618051e6618a63f1d', 'wrf_sdirk3_acoustic_substep.cpp': '0bbe90c3da51eb9ad4043c2897794eac5e08ed78aad93984d8e949f7807f7c7d', 'wrf_sdirk3_base_state_zerocopy.cpp': '7753c450299bbaf919a11f911359f796a4a64dd8c085651dc9722f84a80a2f39', 'wrf_tile_boundary_optimizer.cpp': '0f6c1f21eb43ff1c66e9d080417cafc40a6448096fded5141cf53bf603515f45', 'wrf_sdirk3_config.cpp': '91a52f3e0dbf94e125982656f48ef4258d43d1e52a0e5147154fd6dd8aa0a03d', 'wrf_sdirk3_unified_preconditioner.cpp': 'a9edc33241068890a1171a595a4a08ebe43d7d2254f96a863f022989f81538a2', 'wrf_sdirk3_unified_rhs_extended.cpp': '86453b778fa397a967772032a47a69c829fb6a148f1be16d8ea2a2a29d352d93', 'wrf_sdirk3_cache_utils.cpp': '62315149b5a0174d9c215d43d04b8371b72687403b551cee3addd164e2fb69b3', 'wrf_sdirk3_boundary_ad.cpp': 'c5e015518d7a818439a3920de0dda0849e91ea7cf052f9a60e86aab95fd0e67e', 'wrf_sdirk3_rayleigh_damping_ad.cpp': 'fd6af4b0a526b438b50b272989b128c2d4e85f592a35faa2b04d731eb4dbcbdc', 'wrf_sdirk3_mpi_safety_impl.cpp': 'd887fb9d9a2f3f823f1c2fa909701750d9214db22bc1bce9e0e19bd32a23ada6', 'wrf_sdirk3_halo_exchange.cpp': '189f492806fde46412341de62b8bc855bedc89d4f15b71ce5dcc9de9107751cc', 'wrf_sdirk3_ad_halo_exchange.cpp': 'd108c0e92e57fe3ee841cef71af9c6146987ff3ca9a34290aec16e27433ca51a', 'wrf_sdirk3_rhs_run_close.cpp': '61f217a5fc67d36f7c4c59aedc835c05a928c386edc6cd2d346c1d106b3a05b3', 'wrf_sdirk3_u_slow_diagnostics.cpp': '9401225b1d326549c412fa1acf8fe6dc5472cf1f8b491a61e7273144126260e4', 'wrf_sdirk3_pressure_stage_probe.cpp': '5f124a002a1f06dcc82908666fd56e98b942dd13fe0eec94cf1947d47316416c', 'wrf_sdirk3_reference_pressure.cpp': 'fd8e6bab08fa2ffc093aa6f290941aaadc740fe942910667b61a337a903c1ea5', 'wrf_sdirk3_reference_pressure_model.cpp': 'bfe769068c6025c37f0fb421db754e511df012b01230726e1d9107c9ccde25cc', 'wrf_sdirk3_pressure_packed_adapter.cpp': '057bd5bae0ea9e5dc87d168fc4d4ad4f2a244f45eaecf68d0c2c0c4179cc9976', 'wrf_sdirk3_native_pressure_reference.cpp': '73d8f6e77099b73431b5f314080b1ffba83b0226cb71e1720fccd3ec4a5ec8ea', 'wrf_sdirk3_v_primary_probe.cpp': 'bf4376b58979d3ef24558ae8bb6f1613d6f431097c26fd0ae55887e9da103691', 'wrf_sdirk3_pressure_v_direct.cpp': '7b0fc228e863eb4232c889ce6ead4ab05fa2edd430d63210dac0d8fb8f081e91'}


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
        raise SystemExit('usage: build_matched.py TARGET_SHA REG_PATH REG_SHA')
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
    allowed = {'external/libtorch_wrf/sdirk3/' + n for n in (NEWTON, TILE, CHANGED_HEADER, HELPER)}
    assert prod == allowed, (prod, allowed)

    prior_path = A / 'build_receipt.json'
    assert sha(prior_path) == 'e88f16d369b26c5615894ece3f5a9e9dc90a554fd79e1c450af682d255cc79f8'
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
    assert sha(P / 'current_sdk_provenance.json') == prior['current_sdk_provenance_sha256']
    stack = json.loads((P / 'current_sdk_provenance.json').read_text())
    stack_check(stack)
    # Pin all pre-existing project headers, except the intentionally changed one.
    for path, digest in prior['source_hashes'].items():
        if pathlib.Path(path).suffix in ('.h', '.hpp') and pathlib.Path(path).name != CHANGED_HEADER:
            assert sha(W / path) == digest, path
    consumers = set()
    for r in records:
        obj = pathlib.Path(r['object'])
        dep = obj.with_suffix('.d')
        assert sha(obj) == r['object_sha256'], obj
        assert sha(dep) == DEP_SHA256[r['source']], dep
        if 'depfile_sha256' in r:
            assert sha(dep) == r['depfile_sha256'], dep
        member = subprocess.check_output(['ar', '-p', str(A / 'libwrf_sdirk3_libtorch.a'), obj.name])
        assert hashlib.sha256(member).hexdigest() == r['object_sha256'], obj
        if CHANGED_HEADER in dep.read_text(errors='strict'):
            consumers.add(r['source'])
        if r['source'] not in EXPECTED:
            assert sha(C / r['source']) == r['source_sha256'], r['source']
    assert consumers == {NEWTON}, consumers
    # Any direct include of the new helper in production must be in Newton.
    helper_users = {p.name for p in C.glob('*.cpp') if HELPER in p.read_text(errors='strict')}
    assert helper_users == {NEWTON}, helper_users
    assert pathlib.Path(next(a for a in next(r for r in records if r['source'] == NEWTON)['argv']
                             if a.endswith('/' + NEWTON))).name == NEWTON
    assert shutil.disk_usage(E).free >= 5 * 1024**3
    out = E / 'native_matched'
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
            assert argv.count('-DWRF_SDIRK3_TERMINAL_JVP_PROBE') == 1
            argv.remove('-DWRF_SDIRK3_TERMINAL_JVP_PROBE')
            assert '-DWRF_SDIRK3_PRESSURE_V_DIRECT' not in argv
            argv.insert(1, '-DWRF_SDIRK3_MATCHED_PRESSURE_PROBE')
        with (out / (obj.stem + '.log')).open('xb') as log:
            subprocess.run(argv, cwd=C, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=600)
        dep = obj.with_suffix('.d')
        if name == NEWTON:
            assert CHANGED_HEADER in dep.read_text(errors='strict')
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
               'source_hashes': source_before, 'changed_header_consumers': sorted(consumers), 'rebuilt_sources': sorted(EXPECTED),
               'source_objects': built, 'archive_members': members, 'archive_sha256': sha(archive),
               'link_argv': link, 'link_cwd': prior['link_cwd'], 'executable_sha256': sha(out / 'wrf.exe'),
               'otool_sha256': sha(out / 'otool.txt'), 'postbuild_free_bytes': shutil.disk_usage(E).free,
               'model_run': False}
    (out / 'build_receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
    print(receipt['executable_sha256'])


if __name__ == '__main__':
    main()
