from fractions import Fraction as F
from analyze_w_runtime import levels, analyze, EXPECTED_EOS_NAMES
import struct

c = {'rdn_source_dtype': 6, 'rdn_values': [0,1], 'rdn_count': 2,
     'rdn_used_begin':1,'rdn_used_end_exclusive':2,'gravity_f32':1,'top_rdnw_f32':1,
     'canonical_horizontal':True,'cq1_all_one':True,
     'msfty_min':1,'msfty_max':1,'alpha_w_min':2,'alpha_w_max':3,
     'level_mass_w_min':8,'level_mass_w_max':9,'velocity_mass_w_min':4,'velocity_mass_w_max':4}
h=14695981039346656037
for byte in struct.pack('<2f',0,1):h=((h^byte)*1099511628211)&((1<<64)-1)
c['rdn_fnv64_f32']=str(h)
r=levels(c,[.25,.5],F(2))
assert [F(v) for v in r['per_level_error_exact']]==[0,F(3,8),F(1,2)]
assert F(r['weighted_rms_upper_exact'])==F(1,2)
# Deliberately different level_mass shows the bound uses the observed alpha operand.
assert F(r['effective_denominator_lower_exact'])==2
for bad in ({**c,'alpha_w_min':0},{**c,'rdn_fnv64_f32':'0'},{**c,'cq1_all_one':False}):
    try:
        levels(bad,[.25,.5],F(1));raise AssertionError('invalid observation accepted')
    except ValueError:pass
print('PASS actual alpha/map coefficient, observed weight, top/interior and invalid metadata gates')

for bad in ({'status':'passed','files':{},'analysis':{'states':[{},{}]}},
            {'status':'passed','files':{k:'unused' for k in EXPECTED_EOS_NAMES},'analysis':{'states':[{}]}}):
    try:
        analyze({}, {}, bad, {});raise AssertionError('missing artifacts/endpoints accepted')
    except ValueError as e:
        assert 'required' in str(e)
print('PASS missing 16-file set and endpoint cardinality rejection')
