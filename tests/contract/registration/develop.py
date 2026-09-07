"""D1.03开发回合，每次独立证据目录和owned进程。"""
import sys,json,uuid,shutil,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
out=ROOT/'evidence/bootstrap/D1.03'/('implementation-'+uuid.uuid4().hex[:12]);out.mkdir()
files=list((ROOT/'packages/runtime/registry').glob('*'))+list((ROOT/'tests/contract/registration').glob('*'))
sources=[]
for p in files:
 if p.is_file():
  rel=p.relative_to(ROOT);dst=out/'source'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dst);sources.append({'path':rel.as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(out/'source.json').write_text(json.dumps(sources,indent=2))
commands=[]
def run(n,a):
 r=execute(a,ROOT,out/(n+'-stdout.log'),out/(n+'-stderr.log'),600);commands.append(r);(out/'commands.json').write_text(json.dumps(commands,indent=2));print(n,r['status'],r['exit_code'],str(out),flush=True);return r['status']=='Exited' and r['exit_code']==0 and r['process_tree']['active_after']==0
b=ROOT/'build/d1.03-implementation-green'
if '--configure' in sys.argv:
 if not run('configure',['cmake','--preset','win-msvc-debug','-B',str(b),'-DOCK_BUILD_DEPENDENCY_PROBES=OFF','-DOCK_BUILD_G0_TESTS=OFF']):sys.exit(1)
if not run('build',['cmake','--build',str(b),'--config','Debug','--target','ock_registration_tests','--parallel','2','--','/nr:false']):sys.exit(1)
binary=b/'tests/contract/registration/Debug/ock_registration_tests.exe'
if not run('list',[str(binary),'--list']):sys.exit(1)
names=(out/'list-stdout.log').read_text().splitlines()
ok=True
for n in names:ok=run(n,[str(binary),n]) and ok
sys.exit(0 if ok else 1)
