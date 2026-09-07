from pathlib import Path
import os,re,sys,uuid
sys.path.insert(0,str(Path.cwd()))
from tools.evidence.common import save_json,sha_file
from tools.evidence.process import execute
root=Path.cwd();base=root/'build/d1.04-integration-cc3edebc'
binary=base/'tests/contract/authorization/Debug/ock_policy_tests.exe'
registry=base/'tests/contract/authorization/policy-tests-Debug.cmake'
match=re.search(r'ENVIRONMENT_MODIFICATION \[==\[PATH=path_list_prepend:(.*?)\]==\]',registry.read_text(encoding='utf-8'))
assert match
runtime=Path(match[1]);assert runtime.is_dir()
out=root/'evidence/bootstrap/D1.04'/('asan-runtime-control-'+uuid.uuid4().hex[:12]);out.mkdir()
(out/'driver.py').write_bytes(Path(__file__).read_bytes());(out/'policy-tests.cmake').write_bytes(registry.read_bytes())
save_json(out/'inputs.json',{'binary':str(binary),'binary_sha256':sha_file(binary),'registry_sha256':sha_file(registry),'runtime':str(runtime),'runtime_dlls':[{'path':str(p),'sha256':sha_file(p)} for p in runtime.glob('*asan*.dll')]})
commands=[]
for label,argv in [('without-runtime',[str(binary),'--list']),('with-runtime',['cmake','-E','env','PATH='+str(runtime)+os.pathsep+os.environ.get('PATH',''),str(binary),'--list'])]:
 row=execute(argv,root,out/(label+'-stdout.log'),out/(label+'-stderr.log'),60);commands.append(row);save_json(out/'commands.json',commands);print(out.name,label,row['status'],row['exit_code'],flush=True)
assert commands[0]['status']=='Crashed' and commands[0]['exit_code']==3221225781
assert commands[1]['status']=='Exited' and commands[1]['exit_code']==0
assert all(c['process_tree']['active_after']==0 for c in commands)
assert len((out/'with-runtime-stdout.log').read_text().splitlines())==35
save_json(out/'result.json',{'status':'Passed','meaning':'same binary fails without runtime path and lists 35 with registered runtime path'})
