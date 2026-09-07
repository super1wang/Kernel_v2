from pathlib import Path
import sys,uuid
ROOT=Path.cwd();sys.path.insert(0,str(ROOT))
from tools.evidence.common import save_json,sha_file
from tools.evidence.process import execute
out=ROOT/'evidence/bootstrap/D1.04'/('encoder-alias-'+uuid.uuid4().hex[:12]);out.mkdir()
src=out/'source';rows=[]
for name in ['packages/runtime/policy/policy.hpp','packages/runtime/policy/policy.cpp','tests/contract/authorization/fixtures.hpp','tests/compile/contracts/test_support.hpp']:
 p=src/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes((ROOT/name).read_bytes());rows.append(dict(path=name,sha256=sha_file(p)))
save_json(out/'source.json',rows)
(out/'main.cpp').write_bytes((ROOT/'evidence/bootstrap/D1.04/encoder-alias-probe.cpp').read_bytes())
includes=[src,ROOT,ROOT/'packages/contracts/include',ROOT/'packages/foundation/include',ROOT/'build/d0.06-a/cache/sources/expected-fe3b18aecb84/include']
(out/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(EncoderAlias LANGUAGES CXX)\nadd_executable(probe "'+(src/'packages/runtime/policy/policy.cpp').as_posix()+'" main.cpp)\ntarget_compile_features(probe PRIVATE cxx_std_20)\ntarget_compile_options(probe PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive-)\ntarget_include_directories(probe PRIVATE '+' '.join('"'+p.as_posix()+'"' for p in includes)+')\n')
build=ROOT/'build'/out.name;commands=[]
def run(name,argv):
 streams=[out/(name+s) for s in ['-stdout.log','-stderr.log']];r=execute(argv,ROOT,*streams,300);r['raw']=[dict(path=p.name,sha256=sha_file(p)) for p in streams];commands.append(r);save_json(out/'commands.json',commands);print(out.name,name,r['status'],r['exit_code'],flush=True);return r['exit_code']==0 and r['status']=='Exited'
ok=run('configure',['cmake','-S',str(out),'-B',str(build),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake'])
if ok:ok=run('build',['cmake','--build',str(build),'--config','Debug','--','/nr:false'])
if ok:
 run('control',[str(build/'Debug/probe.exe'),'control']);run('alias',[str(build/'Debug/probe.exe'),'alias'])
