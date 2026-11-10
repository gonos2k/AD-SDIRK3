from fractions import Fraction as F
import numpy as np
from norm_exact import sum_squares, sqrt_bounds, enclosure, data_fnv, validate_residual
assert sum_squares([3.,4.,0.,-0.]) == 25
small=np.nextafter(0.,1.)
assert sum_squares([small])==F(1,2**2148)
lo,hi=sqrt_bounds(F(2))
assert lo*lo<=2<=hi*hi and hi-lo==F(1,2**256)
assert sqrt_bounds(F(25,4))==(F(5,2),F(5,2))
layout=[{'name':n,'start':i,'size':1,'sinv_min':1.,'sinv_max':1.} for i,n in enumerate(('ru','rv','rw','ph','t','mu'))]
r=np.asarray([3.,4.,0.,0.,0.,0.])
z=enclosure(r,layout,2.)
assert F(z['nu_upper_exact'])>0 and F.from_float(z['nu_upper_binary64'])>=F(z['nu_upper_exact'])
layout[1]['start']=0
try:
    enclosure(r,layout,2.);raise AssertionError('overlapping metric blocks accepted')
except ValueError:pass
print('PASS exact square sum incl subnormal, rational sqrt enclosure, upward norm gap and invalid layout')

info={'elements':6,'data_bytes':r.nbytes,'fnv64':data_fnv(r)}
validate_residual(r,info,6,info['fnv64'])
for bad in ({**info,'fnv64':'0'},{**info,'elements':5}):
    try:
        validate_residual(r,bad,6,info['fnv64']);raise AssertionError('tampered residual metadata accepted')
    except ValueError:pass
print('PASS actual data-byte FNV and inventory mismatch rejection')
