from pathlib import Path
import sys,json,shutil
ROOT=Path(__file__).resolve().parents[4]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
out=Path(__file__).parent
src=ROOT/'tests/contract/registration/registration_tests.cpp'
shutil.copyfile(src,out/'registration_tests.cpp')
(out/'positive.cpp').write_text('#include "test_support.hpp"\nauto positive=make_compute_definition(compute_handler,input(AtomicMode::PureCompute));\n')
incs=[ROOT,ROOT/'tests/compile/contracts',ROOT/'packages/contracts/include',ROOT/'packages/foundation/include',ROOT/'build/d0.06-a/cache/sources/expected-fe3b18aecb84/include']
lines=['cmake_minimum_required(VERSION 3.25)','project(RegistrationRed LANGUAGES CXX)']
for n,s in [('positive',out/'positive.cpp'),('negative',out/'registration_tests.cpp')]:
 lines += [f'add_library({n} OBJECT EXCLUDE_FROM_ALL "{s.as_posix()}")',f'target_compile_features({n} PRIVATE cxx_std_20)',f'target_compile_options({n} PRIVATE /utf-8 /EHsc /permissive-)',f'target_include_directories({n} PRIVATE '+ ' '.join('"'+p.as_posix()+'"' for p in incs)+')']
(out/'CMakeLists.txt').write_text('\n'.join(lines))
commands=[]
def run(n,a):
 r=execute(a,ROOT,out/(n+'-stdout.log'),out/(n+'-stderr.log'),180);commands.append(r);(out/'commands.json').write_text(json.dumps(commands,indent=2));print(n,r['status'],r['exit_code']);return r
b=ROOT/'build/d1.03-implementation-red'
assert run('configure',['cmake','-S',str(out),'-B',str(b),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake'])['exit_code']==0
assert run('positive',['cmake','--build',str(b),'--target','positive','--config','Debug','--','/nr:false'])['exit_code']==0
assert run('negative',['cmake','--build',str(b),'--target','negative','--config','Debug','--','/nr:false'])['exit_code']!=0
