from pathlib import Path
import sys,json,shutil,hashlib
ROOT=Path(__file__).resolve().parents[4];sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
out=Path(__file__).parent
shutil.copyfile(ROOT/'evidence/bootstrap/D1.03/review-final-20260908T0020/type_identity_budget_probe.cpp',out/'probe.cpp')
sources=[]
for rel in ['packages/runtime/registry/registry.hpp','packages/runtime/registry/registry.cpp','tests/contract/registration/registration_tests.cpp','tests/contract/registration/fixtures.hpp']:
 p=ROOT/rel;dst=out/'source'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dst);sources.append({'path':rel,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(out/'source.json').write_text(json.dumps(sources,indent=2))
incs=[ROOT,ROOT/'tests/compile/contracts',ROOT/'packages/contracts/include',ROOT/'packages/foundation/include',ROOT/'build/d0.06-a/cache/sources/expected-fe3b18aecb84/include']
lines=['cmake_minimum_required(VERSION 3.25)','project(IdentityBudgetRed LANGUAGES CXX)','add_executable(probe probe.cpp)','target_compile_features(probe PRIVATE cxx_std_20)','target_compile_options(probe PRIVATE /utf-8 /EHsc /permissive- /Zc:__cplusplus)','target_include_directories(probe PRIVATE '+' '.join('"'+p.as_posix()+'"' for p in incs)+')',f'target_link_libraries(probe PRIVATE "{ROOT.as_posix()}/build/d1.03-implementation-green/tests/contract/registration/Debug/ock_registry_internal.lib")']
(out/'CMakeLists.txt').write_text('\n'.join(lines))
commands=[]
def run(n,a):
 r=execute(a,ROOT,out/(n+'-stdout.log'),out/(n+'-stderr.log'),240);commands.append(r);(out/'commands.json').write_text(json.dumps(commands,indent=2));print(n,r['status'],r['exit_code'],flush=True);return r
b=ROOT/'build/d1.03-implementation-identity-red'
assert run('configure',['cmake','-S',str(out),'-B',str(b),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake'])['exit_code']==0
assert run('build',['cmake','--build',str(b),'--config','Debug','--target','probe','--','/nr:false'])['exit_code']==0
assert run('positive',[str(ROOT/'build/d1.03-implementation-green/tests/contract/registration/Debug/ock_registration_tests.exe'),'T05.registration.typed_binding_success'])['exit_code']==0
assert run('probe',[str(b/'Debug/probe.exe')])['exit_code']==17
