import json,subprocess,tempfile,pathlib,numpy as np,hashlib
E=pathlib.Path(__file__).resolve().parent; exe=E/'eos_mpfr'
names=['base_ph','base_t','base_mu','base_alt','base_p_pert','accepted_ph','accepted_t','accepted_mu','accepted_alt','accepted_p_pert','mu_base','alb','p_base','c1h','c2h','rdnw']
with tempfile.TemporaryDirectory() as td:
 p=pathlib.Path(td);v={'base_ph':[0,0,4],'base_t':[4,8],'base_mu':[0],'base_alt':[2,4],'base_p_pert':[4,4],'mu_base':[2],'alb':[2,2],'p_base':[0,0],'c1h':[1,1],'c2h':[-0.0,0.0],'rdnw':[1,1]}
 for n in names:
  q=n.replace('accepted_','base_') if n.startswith('accepted_') else n;np.save(p/(n+'.npy'),np.array(v[q],dtype='<f8'))
 cmd=[str(exe),'1','2','1','1','1','2','1','0']+[str(p/(n+'.npy')) for n in names]
 def run():return subprocess.run(cmd,capture_output=True,text=True)
 r=run();assert r.returncode==0,r.stderr;j=json.loads(r.stdout);assert all(s['alt']['linf_upper']==0 and s['p_pert']['linf_upper']==0 for s in j['states'])
 header=p/'base_t.npy';original=header.read_bytes();assert b"'fortran_order': False" in original;header.write_bytes(original.replace(b"'fortran_order': False",b"'fortran_order': True "));assert run().returncode!=0;header.write_bytes(original)
 np.save(p/'accepted_p_pert.npy',np.array([4.25,4],dtype='<f8'));r=run();assert r.returncode==0,r.stderr;assert json.loads(r.stdout)['states'][1]['p_pert']['linf_upper']>=0.25
 np.save(p/'mu_base.npy',np.array([1e-200],dtype='<f8'));r=run();assert r.returncode!=0 and 'near-zero' in r.stderr
 np.save(p/'mu_base.npy',np.array([2.],dtype='<f8'));np.save(p/'rdnw.npy',np.array([-1.,1.],dtype='<f8'));r=run();assert r.returncode!=0 and 'rdnw' in r.stderr
 np.save(p/'rdnw.npy',np.array([1.,1.],dtype='<f8'));np.save(p/'accepted_ph.npy',np.array([0.,0.,-2.],dtype='<f8'));r=run();assert r.returncode==0,r.stderr;assert json.loads(r.stdout)['states'][1]['alt']['linf_upper']>=2.0
 # End-to-end receipt wrapper, including original shapes and flat-byte hashes.
 np.save(p/'accepted_ph.npy',np.array([0.,0.,4.],dtype='<f8'));np.save(p/'accepted_p_pert.npy',np.array([4.,4.],dtype='<f8'))
 raw=p/'eos_raw';raw.mkdir()
 records=[]
 for n in names:
  f=raw/(n+'.npy');f.write_bytes((p/(n+'.npy')).read_bytes());x=np.load(f);h=14695981039346656037
  for byte in x.tobytes():h=((h^byte)*1099511628211)&((1<<64)-1)
  shape=[1,3,1] if n.endswith('_ph') else [1,1] if n.endswith('_mu') or n=='mu_base' else [2] if n in ('c1h','c2h','rdnw') else [1,2,1]
  records.append({'name':n,'path':'eos_raw/'+n+'.npy','shape':shape,'source_dtype':7 if n.startswith(('base_','accepted_')) else 6,'elements':x.size,'data_bytes':x.nbytes,'fnv64':str(h)})
 receipt=p/'receipt.json';receipt.write_text(json.dumps({'raw_eos':{'scope':'rounded_EOS_subroutine_inputs_and_outputs_only','files':records},'eos_scalars':{'rd':1,'cv':1,'cp':2,'p0':1,'t0':0}}))
 w=subprocess.run(['python3',str(E/'run_eos_mpfr.py'),str(receipt),str(p/'analysis.json')],capture_output=True,text=True);assert w.returncode==0,w.stderr
 result=json.loads((p/'analysis.json').read_text());assert result['analysis']['cells_per_state']==2
 for state in result['analysis']['states']:
  for field in ('alt','p_pert'):
   b=state[field];assert float.fromhex(b['linf_upper_binary64_hex'])==b['linf_upper'];assert float.fromhex(b['l2_upper_binary64_hex'])==b['l2_upper'];assert all(float.fromhex(h)==v for h,v in zip(b['per_level_linf_upper_binary64_hex'],b['per_level_linf_upper']))
 records[0]['source_dtype']=6;receipt.write_text(json.dumps({'raw_eos':{'scope':'rounded_EOS_subroutine_inputs_and_outputs_only','files':records},'eos_scalars':{'rd':1,'cv':1,'cp':2,'p0':1,'t0':0}}));w=subprocess.run(['python3',str(E/'run_eos_mpfr.py'),str(receipt),str(p/'bad.json')],capture_output=True,text=True);assert w.returncode!=0
 records[0]['source_dtype']=7;records[10]['source_dtype']=8;receipt.write_text(json.dumps({'raw_eos':{'scope':'rounded_EOS_subroutine_inputs_and_outputs_only','files':records},'eos_scalars':{'rd':1,'cv':1,'cp':2,'p0':1,'t0':0}}));w=subprocess.run(['python3',str(E/'run_eos_mpfr.py'),str(receipt),str(p/'bad.json')],capture_output=True,text=True);assert w.returncode!=0
 records[10]['source_dtype']=6;f=raw/'mu_base.npy';np.save(f,np.array([2.+2**-40],dtype='<f8'));x=np.load(f);h=14695981039346656037
 for byte in x.tobytes():h=((h^byte)*1099511628211)&((1<<64)-1)
 records[10]['fnv64']=str(h);receipt.write_text(json.dumps({'raw_eos':{'scope':'rounded_EOS_subroutine_inputs_and_outputs_only','files':records},'eos_scalars':{'rd':1,'cv':1,'cp':2,'p0':1,'t0':0}}));w=subprocess.run(['python3',str(E/'run_eos_mpfr.py'),str(receipt),str(p/'bad.json')],capture_output=True,text=True);assert w.returncode!=0
 np.save(f,np.array([1e300],dtype='<f8'));x=np.load(f);h=14695981039346656037
 for byte in x.tobytes():h=((h^byte)*1099511628211)&((1<<64)-1)
 records[10]['fnv64']=str(h);receipt.write_text(json.dumps({'raw_eos':{'scope':'rounded_EOS_subroutine_inputs_and_outputs_only','files':records},'eos_scalars':{'rd':1,'cv':1,'cp':2,'p0':1,'t0':0}}));w=subprocess.run(['python3',str(E/'run_eos_mpfr.py'),str(receipt),str(p/'bad.json')],capture_output=True,text=True);assert w.returncode!=0
 print('PASS exact two-level, output mismatch, near-zero D, signed rdnw, perturbation-phi sign, NPY order, dtype and exact promotion, out-of-range, bound-hex, receipt wrapper')
print('source_sha256',hashlib.sha256((E/'eos_mpfr.cpp').read_bytes()).hexdigest())
