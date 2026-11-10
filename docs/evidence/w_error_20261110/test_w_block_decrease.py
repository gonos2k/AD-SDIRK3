from copy import deepcopy
from w_block_decrease import REFERENCE_SOURCE_SHA256, validate_metadata

p = {'N': 1080491, 'mask_scope': 'identity',
     'w_reference': {'layout': [{'name': 'rw', 'start': 432896, 'size': 215865,
                               'sinv_min': 1., 'sinv_max': 1.}]},
     'w_runtime': {'metric': {'block': 'rw', 'start': 432896, 'count': 215865,
                              'min': 1., 'max': 1.}}}
m = {'origins': {'packet_sha256': 'packet'}, 'files': {'R0': {'sha256': 'r0'}, 'R1': {'sha256': 'r1'}},
     'grid': {'packed_N': 1080491, 'W_count': 215865}, 'W_inverse_weight_extrema': {'min': 1., 'max': 1.}}
e = {'status': 'passed_conditional', 'manifest_sha256': 'manifest',
     'reference_source_sha256': REFERENCE_SOURCE_SHA256, 'reference_binary_sha256': 'platform-specific',
     'input_sha256': {'R0': 'r0', 'R1': 'r1'},
     'analysis': {'states': [{}, {}], 'packed_N': 1080491,
                  'W_count_per_state': 215865, 'max_W_inverse_weight': 1.}}
assert validate_metadata(p, m, e, 'packet', 'manifest')['size'] == 215865

# Contradictory metadata must fail before any decrease calculation.
mutations = [
    lambda p, m, e: m['origins'].update(packet_sha256='other'),
    lambda p, m, e: e['analysis'].update(states=[{}]),
    lambda p, m, e: p['w_reference']['layout'][0].update(sinv_min=0),
    lambda p, m, e: e['input_sha256'].update(R0='0' * 64),
    lambda p, m, e: e['input_sha256'].pop('R1'),
    lambda p, m, e: e['input_sha256'].update(extra='unregistered'),
    lambda p, m, e: e['analysis'].update(packed_N=1080490),
    lambda p, m, e: e['analysis'].update(W_count_per_state=215864),
    lambda p, m, e: m['grid'].update(W_count=215864),
    lambda p, m, e: p['w_runtime']['metric'].update(start=0),
    lambda p, m, e: p['w_runtime']['metric'].update(max=2.),
    lambda p, m, e: e['analysis'].update(max_W_inverse_weight=2.),
    lambda p, m, e: m['W_inverse_weight_extrema'].update(min=.5),
    lambda p, m, e: e.update(reference_source_sha256='0' * 64),
]
for change in mutations:
    pp, mm, ee = deepcopy((p, m, e))
    change(pp, mm, ee)
    try:
        validate_metadata(pp, mm, ee, 'packet', 'manifest')
    except ValueError:
        continue
    raise AssertionError('contradictory metadata accepted')

# Rebuilding the same reference source on another platform is allowed.
other_binary = deepcopy(e)
other_binary['reference_binary_sha256'] = 'different-platform-binary'
assert validate_metadata(p, m, other_binary, 'packet', 'manifest') == p['w_reference']['layout'][0]
print('PASS 14 contradictory metadata cases and platform-independent source binding')
