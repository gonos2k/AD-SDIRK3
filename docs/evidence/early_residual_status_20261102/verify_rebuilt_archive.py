"""Verify the c7 Newton rebuild and the nine reused affected objects."""
import hashlib
import json
from pathlib import Path

E=Path(__file__).resolve().parent
W=Path('/private/tmp/early-residual-status-20261102')
C='external/libtorch_wrf/sdirk3/'
OLD_PROD='e7fc08a1bb19732ae551fd35413fb246b2598b84'
NEW_PROD='c7a282e3032b6f760d9d8154ee9536ab3688a95b'

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def verify(receipt, target):
 assert target == NEW_PROD == receipt['source_commit'] == receipt['compiled_source_commit']
 previous_path=E/'attempt1/early_build_receipt.json'
 assert sha(previous_path)==receipt['previous_build_receipt_sha256']
 previous=json.loads(previous_path.read_text())
 assert previous['compiled_source_commit']==OLD_PROD
 assert receipt['previous_archive_sha256']==previous['archive_sha256']
 changed={C+'wrf_sdirk3_newton_solver.cpp',C+'tests/test_newton_early_residual.cpp'}
 assert receipt['source_hashes'].keys()==previous['source_hashes'].keys()
 for path, digest in receipt['source_hashes'].items():
  assert sha(W/path)==digest,path
  if path not in changed: assert digest==previous['source_hashes'][path],path
 current={x['source']:x for x in receipt['compiled']}; old={x['source']:x for x in previous['compiled']}
 assert len(current)==len(old)==10 and current.keys()==old.keys()
 for name, item in current.items():
  assert item['source_sha256']==receipt['source_hashes'][C+name],name
  obj=Path(item['argv'][item['argv'].index('-o')+1]); dep=obj.with_suffix('.d')
  assert sha(obj)==item['object_sha256'] and sha(dep)==item['depfile_sha256'],name
  if name=='wrf_sdirk3_newton_solver.cpp':
   assert item['compiled_source_commit']==NEW_PROD
  else:
   assert item['compiled_source_commit']==OLD_PROD
   assert {k:v for k,v in item.items() if k!='compiled_source_commit'}==old[name],name
 assert sha(E/'native_build/libwrf_sdirk3_libtorch.a')==receipt['archive_sha256']
