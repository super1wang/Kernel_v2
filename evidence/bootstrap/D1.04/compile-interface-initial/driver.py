from pathlib import Path
import sys,os,re,importlib.util,shutil
sys.path.insert(0,str(Path.cwd()))
from tools.evidence.common import save_json,sha_file
from tools.evidence.process import execute
root=Path.cwd();out=root/'evidence/bootstrap/D1.04/compile-interface-initial';out.mkdir()
file=root/'tests/contract/authorization/verify_children.py'
spec=importlib.util.spec_from_file_location('policy_children',file);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
source=out/'source';header=source/'packages/runtime/policy/policy.hpp';header.parent.mkdir(parents=True);shutil.copyfile(root/'packages/runtime/policy/policy.hpp',header);shutil.copyfile(file,out/'verify_children.py')
line=next(x for x in (root/'build/d1.03-debug/tests/contract/registration/registration-tests-Debug.cmake').read_text().splitlines() if 'verify_children.py' in x)
args=re.findall(r'\[==\[(.*?)\]==\]',line)
def argument(name):return args[args.index('--'+name)+1]
os.environ['PATH']=argument('runtime-dir')+os.pathsep+os.environ.get('PATH','')
inc=[str(source)]+argument('includes').split('|')
lines=['cmake_minimum_required(VERSION 3.25)','project(PolicyInterfaceChecks LANGUAGES CXX)','set(CMAKE_CXX_STANDARD 20)','set(CMAKE_CXX_STANDARD_REQUIRED ON)'];targets=[]
for group,(positive,negative) in module.CONTROLS.items():
 for name,(body,diagnostic) in {'positive':(positive,''),**negative}.items():
  target=group+'_'+name;targets.append((target,diagnostic))
  (out/(target+'.cpp')).write_text('#include <array>\n#include "packages/runtime/policy/policy.hpp"\nusing namespace ock::contracts;using namespace ock::runtime::policy;\n'+body+'\n',encoding='utf-8')
  includes=' '.join('[==['+p.replace('\\','/')+']==]' for p in inc)
  lines += [f'add_library({target} OBJECT EXCLUDE_FROM_ALL {target}.cpp)',f'target_compile_options({target} PRIVATE /EHsc /utf-8 /Zc:__cplusplus /permissive-)',f'target_include_directories({target} PRIVATE {includes})']
(out/'CMakeLists.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
save_json(out/'source.json',{'header_sha256':sha_file(header),'wrapper_sha256':sha_file(file),'scope':'仅固定头声明和真实编译正反控制；未运行native行为或正式矩阵。'})
commands=[]
def run(name,argv):
 r=execute(argv,root,out/(name+'-stdout.log'),out/(name+'-stderr.log'),240);commands.append(r);save_json(out/'commands.json',commands);print(name,r['status'],r['exit_code'],flush=True);return r
r=run('configure',['cmake','-S',str(out),'-B',str(out/'build'),'-G',argument('generator'),'-A',argument('platform'),'-T',argument('toolset'),'-DCMAKE_SYSTEM_VERSION='+argument('sdk'),'-DCMAKE_TOOLCHAIN_FILE='+str(root/'cmake/LockedMSVC.cmake'),'-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded'])
assert r['status']=='Exited' and r['exit_code']==0
results=[]
for target,diagnostic in targets:
 r=run(target,['cmake','--build',str(out/'build'),'--config','Debug','--target',target,'--parallel','2','--','/nr:false'])
 raw=(out/(target+'-stdout.log')).read_bytes()+(out/(target+'-stderr.log')).read_bytes();text=raw.decode('utf-8',errors='replace')
 valid=r['status']=='Exited' and r['process_tree']['active_after']==0 and ((r['exit_code']==0) if not diagnostic else (r['exit_code']!=0 and re.search(r'error (?:'+diagnostic+r')',text) and target+'.cpp' in text))
 results.append({'target':target,'passed':bool(valid)});save_json(out/'results.json',results)
 assert valid,target
