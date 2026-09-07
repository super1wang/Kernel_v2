"""D1.04开发证据：固定owned Job，每轮独立目录，禁止覆盖历史。"""
import json,sys,uuid,hashlib,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
out=ROOT/'evidence/bootstrap/D1.04'/('implementation-'+uuid.uuid4().hex[:12]);out.mkdir()
sources=[]
for folder in ('packages/runtime/policy','tests/contract/authorization'):
 for p in (ROOT/folder).rglob('*'):
  if p.is_file() and '__pycache__' not in p.parts:
   rel=p.relative_to(ROOT);dst=out/'source'/rel;dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dst);sources.append({'path':rel.as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(out/'source.json').write_text(json.dumps(sources,indent=2));commands=[]
def run(n,args):
 r=execute(args,ROOT,out/(n+'-stdout.log'),out/(n+'-stderr.log'),30 if '--quick' in sys.argv and n.startswith(('T07.','T19.','T20.')) else 600);commands.append(r);(out/'commands.json').write_text(json.dumps(commands,indent=2));print(n,r['status'],r['exit_code'],str(out),flush=True)
 if r['status']!='Exited' or r['process_tree']['active_after']!=0:raise RuntimeError('Owned process did not exit cleanly: '+n)
 return r['exit_code']
if '--stage' in sys.argv:
 snapshot=out/'source'
 includes=[out/'source',ROOT,ROOT/'tests/compile/contracts',ROOT/'packages/contracts/include',ROOT/'packages/foundation/include',ROOT/'build/d0.06-a/cache/sources/expected-fe3b18aecb84/include']
 lines=['cmake_minimum_required(VERSION 3.25)','project(PolicyStage LANGUAGES CXX)',f'add_executable(policy_stage "{snapshot.as_posix()}/packages/runtime/policy/policy.cpp" "{snapshot.as_posix()}/tests/contract/authorization/policy_tests.cpp")','target_compile_features(policy_stage PRIVATE cxx_std_20)','target_compile_definitions(policy_stage PRIVATE OCK_POLICY_STAGE)','target_compile_options(policy_stage PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive-)', 'target_include_directories(policy_stage PRIVATE '+' '.join('"'+p.as_posix()+'"' for p in includes)+')']
 (out/'CMakeLists.txt').write_text('\n'.join(lines));b=ROOT/'build'/('d1.04-stage-'+uuid.uuid4().hex[:8])
 if run('configure',['cmake','-S',str(out),'-B',str(b),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake']):sys.exit(1)
 if run('build',['cmake','--build',str(b),'--target','policy_stage','--config','Debug','--','/nr:false']):sys.exit(1)
 binary=b/'Debug/policy_stage.exe'
 if run('list',[str(binary),'--list']):sys.exit(1)
 failed=False
 for name in (out/'list-stdout.log').read_text().splitlines():
  if not any(a.startswith('--case=') for a in sys.argv) or '--case='+name in sys.argv:failed=bool(run(name,[str(binary),name])) or failed
 sys.exit(int(failed))
elif '--bootstrap-red' in sys.argv:
 includes=[out/'source',ROOT/'tests/compile/contracts',ROOT/'packages/contracts/include',ROOT/'packages/foundation/include',ROOT/'build/d0.06-a/cache/sources/expected-fe3b18aecb84/include']
 # 已保存的先行测试快照不含尚不存在的policy头；核心合同正控制使用相同工具链。
 (out/'positive.cpp').write_text('#include <ock/contracts/operation.hpp>\nstatic_assert(sizeof(ock::contracts::ContractDigest)==32);\n')
 (out/'negative.cpp').write_text('#include "tests/contract/authorization/policy_tests.cpp"\n')
 (out/'source/tests/compile/contracts').mkdir(parents=True,exist_ok=True)
 shutil.copyfile(ROOT/'tests/compile/contracts/test_support.hpp',out/'source/tests/compile/contracts/test_support.hpp')
 lines=['cmake_minimum_required(VERSION 3.25)','project(PolicyRed LANGUAGES CXX)']
 for n in ('positive','negative'):
  lines += [f'add_library({n} OBJECT EXCLUDE_FROM_ALL {n}.cpp)',f'target_compile_features({n} PRIVATE cxx_std_20)',f'target_compile_options({n} PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive-)',f'target_include_directories({n} PRIVATE '+' '.join('"'+p.as_posix()+'"' for p in includes)+')']
 (out/'CMakeLists.txt').write_text('\n'.join(lines));b=ROOT/'build'/('d1.04-red-'+uuid.uuid4().hex[:8])
 assert run('configure',['cmake','-S',str(out),'-B',str(b),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake'])==0
 assert run('positive',['cmake','--build',str(b),'--target','positive','--config','Debug','--','/nr:false'])==0
 assert run('negative',['cmake','--build',str(b),'--target','negative','--config','Debug','--','/nr:false'])!=0
else:
 b=ROOT/'build/d1.04-implementation'
 if '--configure' in sys.argv:
  if run('configure',['cmake','--preset','win-msvc-debug','-B',str(b),'-DOCK_BUILD_DEPENDENCY_PROBES=OFF','-DOCK_BUILD_G0_TESTS=OFF']):sys.exit(1)
 if run('build',['cmake','--build',str(b),'--config','Debug','--target','ock_policy_tests','--parallel','2','--','/nr:false']):sys.exit(1)
 binary=b/'tests/contract/authorization/Debug/ock_policy_tests.exe'
 if run('list',[str(binary),'--list']):sys.exit(1)
 failed=False
 for name in (out/'list-stdout.log').read_text().splitlines():
  if not any(a.startswith('--case=') for a in sys.argv) or '--case='+name in sys.argv:failed=bool(run(name,[str(binary),name])) or failed
 sys.exit(int(failed))
