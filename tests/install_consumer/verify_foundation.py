"""D1.01真实安装消费及失效模式，所有子命令保存原始Job记录。"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import uuid
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.evidence.common import save_json,sha_file

PROGRAM = """#include <ock/foundation/foundation.hpp>
#include <ock/foundation/sdk_version.hpp>
#include <type_traits>
static_assert(std::is_same_v<ock::foundation::Result<int,int>,tl::expected<int,int>>);
static_assert(!ock::sdk::runtime_available);
int main() {ock::foundation::Result<int> result=42;return result && result.value()==42 ? 0:1;}
"""


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build',required=True);parser.add_argument('--config',required=True);parser.add_argument('--case',choices=['public_headers','installed_consumer','missing_dependency_rejected','backend_mode_rejected'],required=True)
    args=parser.parse_args();build=Path(args.build).resolve()
    if not build.is_relative_to(ROOT/'build'):raise ValueError('isolated workspace build required')
    work=ROOT/'build/d1.01-sdk'/uuid.uuid4().hex;work.mkdir(parents=True);logs=work/'commands';logs.mkdir();sequence=0
    def run(argv,success=True,needle=None):
        nonlocal sequence
        sequence+=1;stem=f'{sequence:03}'
        r=execute(argv,ROOT,logs/(stem+'-stdout.log'),logs/(stem+'-stderr.log'),180)
        save_json(logs/(stem+'-command.json'),r)
        output=(logs/(stem+'-stdout.log')).read_text(encoding='utf-8',errors='replace')+(logs/(stem+'-stderr.log')).read_text(encoding='utf-8',errors='replace')
        if r['status']!='Exited' or (r['exit_code']==0)!=success or (needle is not None and needle not in output):
            raise RuntimeError('unexpected child result '+stem+' '+str(r)+'\n'+output)
        return output
    def install(producer):
        original=work/('install-'+uuid.uuid4().hex[:8]);moved=work/('relocated-'+uuid.uuid4().hex[:8])
        run(['cmake','--install',str(producer),'--config',args.config,'--prefix',str(original)])
        shutil.copytree(original,moved)
        for p in moved.rglob('*.cmake'):
            text=p.read_text(encoding='utf-8').replace('\\','/').casefold()
            if ROOT.as_posix().casefold() in text or original.as_posix().casefold() in text:raise AssertionError('SDK export contains build/source location')
        detached=work/(original.name+'-detached')
        assert original.resolve().is_relative_to(work.resolve()) and detached.resolve().is_relative_to(work.resolve())
        original.rename(detached)
        return moved
    def consume(prefix,mode='normal',success=True,needle=None):
        folder=work/('consumer-'+uuid.uuid4().hex[:8]);source=folder/'source';source.mkdir(parents=True)
        cmake=['cmake_minimum_required(VERSION 3.25)','project(FoundationConsumer LANGUAGES CXX)','find_package(OCK 0.1.0 CONFIG REQUIRED COMPONENTS Foundation)']
        if mode=='headers':
            cmake.append('file(WRITE "${CMAKE_BINARY_DIR}/msbuild-command.txt" "${CMAKE_VS_MSBUILD_COMMAND}")')
        if mode=='no-exceptions':
            cmake.append('set_property(TARGET OCK::Foundation PROPERTY INTERFACE_COMPILE_OPTIONS "/utf-8;/Zc:__cplusplus;/permissive-;/EHs-c-")')
        headers=['ock/foundation/sdk_version.hpp','ock/foundation/foundation.hpp','windows-macros'] if mode=='headers' else [None]
        for index,header in enumerate(headers):
            text=('#define min(a,b) unexpected_min_expansion\n#define max(a,b) unexpected_max_expansion\n#include <ock/foundation/foundation.hpp>\n#if !defined(min) || !defined(max)\n#error Consumer macros must remain defined\n#endif\nint main(){return 0;}\n' if header=='windows-macros' else '#include <'+header+'>\nint main(){return 0;}\n' if header else PROGRAM)
            (source/f'consumer{index}.cpp').write_text(text,encoding='utf-8',newline='\n')
            cmake.extend([f'add_executable(consumer{index} consumer{index}.cpp)',f'target_link_libraries(consumer{index} PRIVATE OCK::Foundation)'])
        (source/'CMakeLists.txt').write_text('\n'.join(cmake)+'\n',encoding='utf-8',newline='\n')
        target=folder/'build'
        run(['cmake','-S',str(source),'-B',str(target),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake',*(['--debug-trycompile'] if mode=='headers' else []),f'-DOCK_DIR={prefix}/lib/cmake/OCK'])
        if mode=='headers':
            # 查询真实 MSBuild 求值结果；不通过 /p 覆盖被测属性。
            msbuild=(target/'msbuild-command.txt').read_text(encoding='utf-8')
            assert Path(msbuild).is_file()
            projects=[next(target.glob('CMakeFiles/*/CompilerIdCXX/CompilerIdCXX.vcxproj')),
                      next(target.glob('CMakeFiles/CMakeScratch/TryCompile-*/cmTC*.vcxproj')),target/'consumer0.vcxproj']
            for index,project in enumerate(projects):
                configuration=args.config if index==2 else "Debug"
                properties=json.loads(run([msbuild,str(project),'-getProperty:VcpkgEnabled,UserRootDir',f'/p:Configuration={configuration}','/p:Platform=x64','/nr:false']))['Properties']
                assert properties['VcpkgEnabled']=='false'
                assert Path(properties['UserRootDir']).resolve()==(ROOT/'cmake/msbuild-user').resolve()
        run(['cmake','--build',str(target),'--config',args.config,'--parallel','2','--','/nr:false'],success,needle)
        if success:
            for index in range(len(headers)):run([str(target/args.config/f'consumer{index}.exe')])
    prefix=install(build)
    if args.case=='public_headers':consume(prefix,'headers')
    elif args.case=='installed_consumer':
        consume(prefix)
        # 独立无测试生产配置也必须提供完整Foundation安装面。
        producer=work/'producer-no-tests'
        run(['cmake','--preset','win-msvc-debug','-B',str(producer),'-DBUILD_TESTING=OFF','-DOCK_BUILD_G0_TESTS=OFF','-DOCK_BUILD_DEPENDENCY_PROBES=OFF','-DOCK_DEPENDENCY_COMPONENTS=Foundation','-DOCK_DEPENDENCIES_OFFLINE=ON'])
        consume(install(producer))
    elif args.case=='missing_dependency_rejected':
        consume(prefix)
        target=prefix/'include/tl/expected.hpp';assert target.resolve().is_relative_to(work.resolve());target.unlink()
        consume(prefix,success=False,needle='tl/expected.hpp')
    else:
        consume(prefix)
        header=prefix/'include/tl/expected.hpp';original=header.read_bytes()
        old=b'#define TL_EXPECTED_VERSION_MAJOR 1';assert original.count(old)==1
        header.write_bytes(original.replace(old,b'#define TL_EXPECTED_VERSION_MAJOR 2'))
        consume(prefix,success=False,needle='requires tl::expected 1.1.0')
        header.write_bytes(original)
        consume(prefix,'no-exceptions',success=False,needle='requires the locked exception-enabled backend mode')
    save_json(work/'result.json',{'case':args.case,'status':'Passed','configuration':args.config,'child_commands':sequence,'raw':[{'path':p.relative_to(ROOT).as_posix(),'sha256':sha_file(p)} for p in sorted(logs.iterdir())]})
    print(json.dumps({'case':args.case,'status':'Passed','work':str(work)},ensure_ascii=False))
if __name__=='__main__':main()
