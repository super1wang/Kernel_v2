from pathlib import Path
import sys,shutil
sys.path.insert(0,str(Path.cwd()))
from tools.evidence.process import execute
from tools.evidence.common import save_json
out=Path('evidence/bootstrap/D1.04/develop-owned-controls');out.mkdir()
shutil.copyfile('build/d1.04-develop-check.py',out/'check.py')
cases=[('red','evidence/bootstrap/D1.04/implementation-develop-before-owned-fix/develop.py','alive')]+[(k,'tests/contract/authorization/develop.py',k) for k in ('alive','clean','failure','timeout')]
for name,source,kind in cases:
 r=execute([sys.executable,'-X','utf8','build/d1.04-develop-check.py',source,str(out/name),kind],Path.cwd(),out/(name+'-stdout.log'),out/(name+'-stderr.log'),30)
 save_json(out/(name+'-command.json'),r);print(name,r['status'],r['exit_code'],flush=True)
 assert r['status']=='Exited' and r['exit_code']==(1 if name=='red' else 0)
