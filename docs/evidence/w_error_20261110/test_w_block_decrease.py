from copy import deepcopy
from w_block_decrease import validate_metadata
p={'N':1080491,'mask_scope':'identity','w_reference':{'layout':[{'name':'rw','start':432896,'size':215865,'sinv_min':1.,'sinv_max':1.}]}}
m={'origins':{'packet_sha256':'packet'}}
e={'status':'passed_conditional','manifest_sha256':'manifest','analysis':{'states':[{},{}]}}
assert validate_metadata(p,m,e,'packet','manifest')['size']==215865
for pp,mm,ee in [(p,{'origins':{'packet_sha256':'other'}},e),(p,m,{**e,'analysis':{'states':[{}]}})]:
    try:
        validate_metadata(pp,mm,ee,'packet','manifest');raise AssertionError('mixed proof accepted')
    except ValueError:pass
bad=deepcopy(p);bad['w_reference']['layout'][0]['sinv_min']=0
try:
    validate_metadata(bad,m,e,'packet','manifest');raise AssertionError('zero weight accepted')
except ValueError:pass
print('PASS packet-origin, two-endpoint and positive-weight gates')
