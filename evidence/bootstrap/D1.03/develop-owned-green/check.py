from pathlib import Path
import sys,ast,json
sys.path.insert(0,str(Path.cwd()))
from tools.evidence.process import execute as actual_execute
source=Path('tests/contract/registration/develop.py')
out=Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=False)
(out/'develop-source.py').write_bytes(source.read_bytes())
function=next(n for n in ast.parse(source.read_text(encoding='utf-8')).body if isinstance(n,ast.FunctionDef) and n.name=='run')
module=ast.Module(body=[function],type_ignores=[])
namespace={'execute':lambda argv,root,stdout,stderr,timeout:actual_execute(argv,root,stdout,stderr,5),'ROOT':Path.cwd(),'out':out,'commands':[],'json':json}
exec(compile(module,str(source),'exec'),namespace)
kind=sys.argv[2]
script={'alive':'import subprocess,sys; subprocess.Popen([sys.executable,"-c","import time; time.sleep(60)"])','clean':'pass','failure':'import sys; sys.exit(7)'}[kind]
observed=namespace['run']('fixture',[sys.executable,'-c',script])
expected=kind=='clean'
(out/'assertion.json').write_text(json.dumps({'kind':kind,'actual_success':observed,'expected_success':expected,'passed':observed==expected}))
assert observed==expected,(kind,observed,expected)
