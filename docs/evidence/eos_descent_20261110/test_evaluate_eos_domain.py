import copy
from evaluate_eos_domain import evaluate_packet

def rec(lo,hi):return {'min':lo,'max':hi,'absmax':max(abs(lo),abs(hi))}
def fixture():
    ep={'theta_pert':[rec(0.,0.) for _ in range(2)],'dphi_pert_computed':[rec(0.,0.) for _ in range(2)],
        'alb':[rec(1.,1.) for _ in range(2)],'alt':[rec(1.,1.) for _ in range(2)],
        'p_pert':[rec(0.,0.) for _ in range(2)]}
    e={'scope':'same_call_EOS_endpoint_outputs_no_error_bound','levels':2,
       'c1h_dtype':7,'c2h_dtype':6,'rdnw_dtype':7,'c1h_hash':'42','c2h_hash':'43','rdnw_hash':'44',
       'c1h':[1.,1.],'c2h':[0.,0.],'rdnw':[1.,1.],
       'rd':287.,'cv':717.,'cp':1004.,'p0':100000.,'t0':300.,
       'B_ph_absmax':1.,'K0_ph_absmax':0.,'K1_ph_absmax':0.,
       'B_t_absmax':1.,'K0_t_absmax':0.,'K1_t_absmax':0.,
       'base':ep,'accepted':copy.deepcopy(ep)}
    return {'status':'completed','mask_scope':'identity','pair_equality':'numeric_finite',
        'mu_dtype':7,'mub_dtype':7,'mu_count':4,'a':1.,'mu_min':0.,'mu_absmax':0.,
        'mub_min':100.,'mub_absmax':100.,'B_mu_absmax':1.,
        'K_before_mu_absmax':0.,'K_after_mu_absmax':0.,'eos_scalars':e}

p=fixture();assert evaluate_packet(p)['status']=='bounded_positive'
b=copy.deepcopy(p);b['eos_scalars']['accepted']['alt'][1]=rec(100.,100.);assert evaluate_packet(b)['status']=='unresolved'
b=copy.deepcopy(p);b['eos_scalars']['accepted']['theta_pert'][0]=rec(100.,100.);assert evaluate_packet(b)['status']=='unresolved'
b=copy.deepcopy(p);b['eos_scalars']['accepted']['alb'][0]=rec(2.,2.);assert evaluate_packet(b)['status']=='unresolved'
b=copy.deepcopy(p);b['eos_scalars']['c1h_dtype']=3;assert evaluate_packet(b)['status']=='unresolved'
b=copy.deepcopy(p);b['eos_scalars']['cp']=-1;assert evaluate_packet(b)['status']=='unresolved'
b=copy.deepcopy(p);b['eos_scalars']['accepted']['alt'].pop();assert evaluate_packet(b)['status']=='unresolved'
b=copy.deepcopy(p);b['eos_scalars']['rdnw'][0]=-1.;assert evaluate_packet(b)['status']=='unresolved'
b=copy.deepcopy(p);b['eos_scalars']['rdnw'][0]=0.;assert evaluate_packet(b)['status']=='unresolved'
print('PASS: two levels, endpoint alt and theta, fixed coefficients, dtype, constants, shape, nonpositive rdnw')
