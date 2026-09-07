from pathlib import Path
import sys,re,json,hashlib
ROOT=Path(__file__).resolve().parents[4];sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
out=Path(__file__).parent
registered=ROOT/'build/d1.03-implementation-green/tests/contract/registration/registration-tests-Debug.cmake'
line=next(x for x in registered.read_text().splitlines() if x.startswith('add_test') and 'T02.registration.manifest_owned_budget' in x)
argv=re.findall(r'\[==\[(.*?)\]==\]',line)[1:]
sources=[]
for rel in ['packages/runtime/registry/registry.cpp','packages/runtime/registry/registry.hpp','tests/contract/registration/registration_tests.cpp','tests/contract/registration/verify_children.py']:
 p=ROOT/rel;sources.append({'path':rel,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(out/'source.json').write_text(json.dumps(sources,indent=2))
r=execute(argv,ROOT,out/'stdout.log',out/'stderr.log',600)
(out/'commands.json').write_text(json.dumps(r,indent=2));print(r['status'],r['exit_code'])
assert r['status']=='Exited' and r['exit_code']==0
