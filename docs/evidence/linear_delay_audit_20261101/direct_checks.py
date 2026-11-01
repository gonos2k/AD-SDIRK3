#!/usr/bin/env python3
"""Validate the fixed PRESSURE_V_DIRECT receipt; no model execution."""
import math

def assess(record):
 if not isinstance(record,dict):return {'classification':'INCONCLUSIVE','reason':'missing direct receipt'}
 status=record.get('status')
 if status=='allocation_failed':return {'classification':'INCONCLUSIVE','failure_class':'resource_allocation','reason':'resource allocation failed','record':record}
 if status=='evaluation_failed':return {'classification':'INCONCLUSIVE','failure_class':'direct_evaluation_failed','reason':'direct evaluation invalid or unsupported','record':record}
 if status=='inner_budget_exhausted':
  required=('cap','tolerance','restarts','fallback','applications','arnoldi_iterations','pressure_xz_subsolves','last_true_relative_residual')
  if not all(k in record for k in required) or record.get('cap')!=4 or record.get('tolerance')!=.01 or record.get('restarts')!=0 or record.get('fallback') is not False:return {'classification':'INCONCLUSIVE','reason':'incomplete/mismatched budget receipt'}
  a=record['applications'];n=record['arnoldi_iterations'];sub=record['pressure_xz_subsolves'];last=record['last_true_relative_residual']
  if type(a) is not int or a<1 or type(n) is not int or not 4<=n<=4*a or type(sub) is not int or sub<1 or not isinstance(last,(int,float)) or isinstance(last,bool) or not math.isfinite(last) or last<.01:return {'classification':'INCONCLUSIVE','reason':'invalid four-step budget counters/residual'}
  return {'classification':'NO-GO','failure_class':'inner_budget_exhausted','reason':'valid fail-closed four-step inner budget exhausted','record':record}
 if status!='ok':return {'classification':'INCONCLUSIVE','reason':'missing or unsupported direct status'}
 required=('cap','tolerance','restarts','fallback','residual_metric','applications','arnoldi_iterations','pressure_xz_subsolves','primary_core_calls','full_packed_extensions','full_P0_inverse_calls','full_P0_apply_calls_in_inner','V_basis_elements','pressure_cache_elements','max_true_relative_residual','p1_first_scaled_relative_residual','p1_non_v_scaled_relative_residual')
 if not all(k in record for k in required):return {'classification':'INCONCLUSIVE','reason':'incomplete direct success receipt'}
 if record['cap']!=4 or record['tolerance']!=.01 or record['restarts']!=0 or record['fallback'] is not False or record['residual_metric']!='V_schur_rhs_with_exact_packed_alias_S_weights':return {'classification':'INCONCLUSIVE','reason':'direct configuration/metric mismatch'}
 a=record['applications'];n=record['arnoldi_iterations'];sub=record['pressure_xz_subsolves'];core=record['primary_core_calls'];ext=record['full_packed_extensions']
 if any(type(x) is not int for x in (a,n,sub,core,ext)) or a<=0 or n<0 or n>4*a or sub!=a+n or not a+n<=core<=2*a+2*n or ext!=a:return {'classification':'INCONCLUSIVE','reason':'direct operation counters invalid'}
 if record['full_P0_inverse_calls']!=0 or record['full_P0_apply_calls_in_inner']!=0 or record['V_basis_elements']!=202240 or record['pressure_cache_elements']!=204800:return {'classification':'INCONCLUSIVE','reason':'unexpected full-P0 work or fixed-grid size'}
 for key in ('max_true_relative_residual','p1_first_scaled_relative_residual','p1_non_v_scaled_relative_residual'):
  v=record[key]
  if not isinstance(v,(int,float)) or isinstance(v,bool) or not math.isfinite(v) or v<0:return {'classification':'INCONCLUSIVE','reason':'invalid '+key}
 if record['max_true_relative_residual']>.01:return {'classification':'NO-GO','failure_class':'inner_residual_failed','reason':'actual V-Schur residual exceeds 0.01','record':record}
 if record['p1_first_scaled_relative_residual']>.01 or record['p1_non_v_scaled_relative_residual']>1e-8:return {'classification':'NO-GO','failure_class':'p1_reconstruction_failed','reason':'actual first P1 reconstruction residual exceeds limit','record':record}
 return {'classification':'VALID','record':record,'residual_scope':'postpoint P1 diagnostic; original-A residual is the saved native true check'}
