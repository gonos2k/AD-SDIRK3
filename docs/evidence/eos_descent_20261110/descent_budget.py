"""Exact scalar budget; it does not supply endpoint evaluation-error bounds."""
from pathlib import Path
from fractions import Fraction
import json,math
O=Path(__file__).resolve().parent / 'run' / 'accepted_step_probe.json'
r=json.loads(O.read_text());q0=Fraction.from_float(r['native_scaled_rms_before']);q1=Fraction.from_float(r['native_scaled_rms_after']);gap=q0-q1
assert gap==Fraction(4295394570649,2251799813685248)
z={'target':'exact Stage3 merit decrease at the same stored pair','native_gap_exact':str(gap),'native_gap_approx':float(gap),'sufficient_condition':'e0+e1 < native_gap; e0 includes all base RHS/scaling/norm/conversion error and e1 all accepted endpoint error','full_endpoint_error_bounds_available':False,'exact_decrease_certified':False,'EOS_error_scope':'a separate subroutine-local component cannot be directly summed with this RMS budget without propagation'}
print(json.dumps(z,indent=2))
