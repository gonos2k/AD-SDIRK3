#!/usr/bin/env python3
"""Draft fixed PRESSURE_COUPLED receipt validator; no model execution."""
import math

def assess(record):
 if not isinstance(record,dict):return {'classification':'INCONCLUSIVE','reason':'missing coupled record'}
 status=record.get('status')
 if status=='allocation_failed':return {'classification':'INCONCLUSIVE','reason':'resource allocation failed','failure_class':'resource_allocation','record':record}
 if status=='inner_failure':
  required=('cap','tolerance','restarts','fallback','applications','arnoldi_iterations','last_true_relative_residual')
  if not all(k in record for k in required):return {'classification':'INCONCLUSIVE','reason':'incomplete inner-failure record'}
  if record['cap']!=4 or record['tolerance']!=.01 or record['restarts']!=0 or record['fallback'] is not False:return {'classification':'INCONCLUSIVE','reason':'inner-failure configuration mismatch'}
  apps=record['applications'];arnoldi=record['arnoldi_iterations'];last=record['last_true_relative_residual']
  if type(apps) is not int or apps<=0 or type(arnoldi) is not int or not 0<=arnoldi<=4*apps or (last is not None and (not isinstance(last,(int,float)) or isinstance(last,bool) or not math.isfinite(last) or last<0)):return {'classification':'INCONCLUSIVE','reason':'invalid inner-failure counters or residual'}
  return {'classification':'NO-GO','failure_class':'inner_solve_failure','reason':'explicit fail-closed inner solve failure','record':record}
 if status!='ok':return {'classification':'INCONCLUSIVE','reason':'missing or unsupported coupled status'}
 required=('cap','tolerance','restarts','fallback','applications','inverse_calls','action_calls','arnoldi_iterations','max_true_relative_residual','base_clone_ms','p1_first_scaled_relative_residual','p1_non_v_scaled_relative_residual')
 if not all(k in record for k in required):return {'classification':'INCONCLUSIVE','reason':'incomplete coupled success record'}
 if record['cap']!=4 or record['tolerance']!=.01 or record['restarts']!=0 or record['fallback'] is not False:return {'classification':'INCONCLUSIVE','reason':'coupled configuration mismatch'}
 for key in ('applications','inverse_calls','action_calls'):
  if type(record[key]) is not int or record[key]<=0:return {'classification':'INCONCLUSIVE','reason':'invalid '+key}
 if type(record['arnoldi_iterations']) is not int or record['arnoldi_iterations']<0:return {'classification':'INCONCLUSIVE','reason':'invalid arnoldi_iterations'}
 applications=record['applications'];arnoldi=record['arnoldi_iterations']
 if arnoldi>4*applications or record['inverse_calls']!=applications+arnoldi or not applications+arnoldi<=record['action_calls']<=applications+2*arnoldi:return {'classification':'INCONCLUSIVE','reason':'coupled operation counters violate registered cap/accounting'}
 for key in ('max_true_relative_residual','base_clone_ms','p1_first_scaled_relative_residual','p1_non_v_scaled_relative_residual'):
  value=record[key]
  if not isinstance(value,(int,float)) or isinstance(value,bool) or not math.isfinite(value) or value<0:return {'classification':'INCONCLUSIVE','reason':'invalid '+key}
 if record['max_true_relative_residual']>.01:return {'classification':'NO-GO','failure_class':'inner_solve_failure','reason':'actual inner residual exceeds registered tolerance','record':record}
 if record['p1_non_v_scaled_relative_residual']>1e-8:return {'classification':'NO-GO','failure_class':'p1_reconstruction_failure','reason':'non-V P1 residual exceeds diagnostic bound','record':record}
 return {'classification':'VALID','record':record,'residual_scope':'P1 diagnostic after first native interval; original A residual remains saved native check'}

def classify_explicit(text,record):
 if 'coupled P1 reconstruction failed non-V equations' in text:return {'classification':'NO-GO','failure_class':'p1_reconstruction_failure','reason':'explicit coupled P1 non-V reconstruction failure'}
 return assess(record)
