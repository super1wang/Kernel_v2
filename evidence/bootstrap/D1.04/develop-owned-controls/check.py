from pathlib import Path
import ast,json,sys
sys.path.insert(0,str(Path.cwd()))
from tools.evidence.process import execute as actual_execute
source=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=False);kind=sys.argv[3]
(out/'develop-source.py').write_bytes(source.read_bytes())
function=next(n for n in ast.parse(source.read_text(encoding='utf-8')).body if isinstance(n,ast.FunctionDef) and n.name=='run')
namespace={'execute':lambda argv,root,stdout,stderr,timeout:actual_execute(argv,root,stdout,stderr,3),'ROOT':Path.cwd(),'out':out,'commands':[],'json':json}
exec(compile(ast.Module(body=[function],type_ignores=[]),str(source),'exec'),namespace)
script={'alive':'import subprocess,sys; subprocess.Popen([sys.executable,"-c","import time; time.sleep(60)"])','clean':'pass','failure':'import sys; sys.exit(7)','timeout':'import time; time.sleep(60)'}[kind]
rejected=False
try:accepted=namespace['run']('fixture',[sys.executable,'-c',script])==0
except RuntimeError:accepted=False;rejected=True
record=namespace['commands'][-1]
expected_status={'alive':'DescendantsAlive','clean':'Exited','failure':'Exited','timeout':'Timeout'}[kind]
assert record['status']==expected_status,record
result={'kind':kind,'status':record['status'],'actual_success':accepted,'expected_success':kind=='clean','strict_rejection':rejected,'passed':accepted==(kind=='clean')}
(out/'assertion.json').write_text(json.dumps(result,indent=2));assert result['passed'],result
