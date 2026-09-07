from pathlib import Path
import sys, uuid, zipfile
ROOT=Path.cwd();sys.path.insert(0,str(ROOT))
from tools.evidence.common import save_json, sha_file
from tools.evidence.process import execute
out=ROOT/'evidence/bootstrap/D1.04'/('send-alias-red-'+uuid.uuid4().hex[:12]);out.mkdir()
source=out/'source';rows=[]
for name in ['packages/runtime/policy/policy.hpp','packages/runtime/policy/policy.cpp','tests/contract/authorization/fixtures.hpp','tests/contract/authorization/send_cases.hpp','tests/contract/authorization/authority_cases.hpp','tests/compile/contracts/test_support.hpp']:
    p=source/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/'evidence/bootstrap/D1.04/implementation-encoder-copy-a7768c1d0216/policy-before.cpp' if name=='packages/runtime/policy/policy.cpp' else ROOT/name).read_bytes());rows.append(dict(path=name,sha256=sha_file(p),size=p.stat().st_size))
save_json(out/'source.json',rows)
(out/'driver.py').write_bytes(Path(__file__).read_bytes())
names=['transmission_start_arbitration','failed_start_not_started','unknown_start_no_retry','unsubscribe_inflight','subscription_connection_cleanup']
main='#include "tests/contract/authorization/send_cases.hpp"\n#include <cstdlib>\n#include <new>\nvoid* operator new(std::size_t n){if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}\nvoid operator delete(void* p) noexcept {policy_test::authority_cases::observe_returned_buffer_free(p);std::free(p);}\nvoid operator delete(void* p,std::size_t) noexcept {::operator delete(p);}\nvoid* operator new[](std::size_t n){return ::operator new(n);}\nvoid operator delete[](void* p) noexcept {::operator delete(p);}\nvoid operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}\nint main(int argc,char** argv){try {CHECK(argc==2);std::string name=argv[1];\n'
for n in names:main+='if(name=="'+n+'"){policy_test::send_cases::'+n+'();return 0;}\n'
main+='throw std::runtime_error("unknown action test");}catch(const std::exception& e){std::cerr<<e.what()<<"\\n";return 1;}}\n'
(out/'main.cpp').write_text(main,encoding='utf-8')
includes=[source,ROOT,ROOT/'packages/contracts/include',ROOT/'packages/foundation/include',ROOT/'build/d0.06-a/cache/sources/expected-fe3b18aecb84/include']
lines=['cmake_minimum_required(VERSION 3.25)','project(PolicyActionCheck LANGUAGES CXX)',f'add_executable(action_check "{source.as_posix()}/packages/runtime/policy/policy.cpp" main.cpp)','target_compile_features(action_check PRIVATE cxx_std_20)','target_compile_options(action_check PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive-)', 'target_include_directories(action_check PRIVATE '+' '.join('"'+p.as_posix()+'"' for p in includes)+')']
(out/'CMakeLists.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
build=ROOT/'build'/('d1.04-send-'+uuid.uuid4().hex[:8]);commands=[]
def run(name,argv):
    streams=[out/(name+s) for s in ['-stdout.log','-stderr.log']]
    row=execute(argv,ROOT,*streams,300);row['raw']=[dict(path=p.name,sha256=sha_file(p),size=p.stat().st_size) for p in streams];commands.append(row);save_json(out/'commands.json',commands);print(out.name,name,row['status'],row['exit_code'],flush=True)
    return row['status']=='Exited' and row['exit_code']==0 and row['process_tree']['active_after']==0
ok=run('configure',['cmake','-S',str(out),'-B',str(build),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake'])
if ok:ok=run('build',['cmake','--build',str(build),'--target','action_check','--config','Debug','--','/nr:false'])
if ok:
    for name in names:ok=run(name,[str(build/'Debug/action_check.exe'),name]) and ok
save_json(out/'result.json',dict(status='Passed' if ok else 'Failed',kind='five-send-bootstrap-tests',source=rows))
with zipfile.ZipFile(out/'build-artifacts.zip','w',zipfile.ZIP_DEFLATED) as archive:
    material=[]
    for p in build.rglob('*'):
        if p.is_file():
            rel=p.relative_to(build).as_posix();archive.write(p,rel);material.append(dict(path=rel,sha256=sha_file(p),size=p.stat().st_size))
save_json(out/'build-artifacts.json',material)
sys.exit(0 if ok else 1)
