"""B2 根工程真实安装后迁移消费；保留每条命令与失败原文。"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import tempfile
import uuid
import os
import re

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tests.install_consumer import verify_baseline as driver


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', required=True)
    parser.add_argument('--config', required=True)
    args = parser.parse_args()
    producer=Path(args.build).resolve()
    cache=dict(m.groups() for line in (producer/'CMakeCache.txt').read_text(encoding='utf-8').splitlines() if (m:=re.fullmatch(r'([^#/][^:]*):[^=]+=(.*)',line)))
    asan=cache.get('OCK_ENABLE_ASAN')=='ON'
    profile=next((producer/'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    compiler=re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)',profile.read_text(encoding='utf-8')).group(1)
    os.environ['PATH']=str(Path(compiler).parent)+os.pathsep+os.environ.get('PATH','')
    work = Path(tempfile.mkdtemp(prefix='b2-install-', dir=ROOT/'build'))
    evidence = ROOT/'evidence/bootstrap/B2'/('sdk-install-'+uuid.uuid4().hex[:10])
    evidence.mkdir(parents=True)
    driver._work = evidence
    driver._records = []
    run = driver.run
    original, relocated = work/'original', work/'relocated'
    run(['cmake','--install',args.build,'--config',args.config,'--prefix',str(original)])
    shutil.copytree(original, relocated)
    for p in relocated.rglob('*.cmake'):
        data = p.read_text(encoding='utf-8').replace('\\','/')
        if ROOT.as_posix() in data or original.as_posix() in data or any(dep in data.lower() for dep in ('jsoncons','cli11','ock_dep_asio')):
            raise AssertionError('source/private dependency leaked into installed export')
    source = work/'consumer-source'
    shutil.copytree(ROOT/'tests/install_consumer/b2', source)
    manifest = json.loads((relocated/'share/ock/sdk_api_manifest.json').read_text(encoding='utf-8'))
    if manifest['stage'] in ('B3Subset','B4Subset','B5Subset','B6Subset'):
        (source/'thin.cpp').write_text('#include <ock/control_client/client.hpp>\n#include <ock/local_ipc/pipe.hpp>\n#include <iostream>\nint main(){auto sid=ock::local_ipc::current_user_sid();if(!sid)return 1;auto text=ock::control_client::quote(*sid);if(!text)return 2;std::cout<<*text;return 0;}\n',encoding='utf-8')
        with (source/'CMakeLists.txt').open('a',encoding='utf-8') as cmake:
            cmake.write('''
find_package(OCK CONFIG REQUIRED COMPONENTS ControlClient Adapter::LocalIPC)
add_executable(installed_thin thin.cpp)
target_link_libraries(installed_thin PRIVATE OCK::ControlClient OCK::Adapter::LocalIPC)
target_link_options(installed_thin PRIVATE /MAP)
function(check_thin target)
  if(target MATCHES "^OCK::(Runtime|Control|Dynamic|Workspace)$")
    message(FATAL_ERROR "Server target in thin client closure: ${target}")
  endif()
  get_target_property(links ${target} INTERFACE_LINK_LIBRARIES)
  foreach(link IN LISTS links)
    if(link MATCHES "^OCK::")
      check_thin(${link})
    endif()
  endforeach()
endfunction()
check_thin(OCK::ControlClient)
check_thin(OCK::Adapter::LocalIPC)
''')
    with (source/'CMakeLists.txt').open('a', encoding='utf-8') as cmake:
        for index, header in enumerate(manifest['headers']):
            if header['classification'] != 'experimental': continue
            include = header['path'].split('/include/',1)[1]
            (source/f'h{index}.cpp').write_text(f'#include <{include}>\n', encoding='utf-8')
            cmake.write(f'add_library(h{index} OBJECT h{index}.cpp)\ntarget_link_libraries(h{index} PRIVATE OCK::{header["target"]})\n')
    base = ['cmake','-S',str(source),'-B',str(work/'consumer'),'-G','Visual Studio 17 2022','-A','x64',
            '-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
            f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake',f'-DCMAKE_PREFIX_PATH={relocated}']
    if asan:
        base += ['-DCMAKE_CXX_FLAGS=/fsanitize=address','-DCMAKE_CXX_FLAGS_DEBUG='+re.sub(r'/RTC[1su]','',cache['CMAKE_CXX_FLAGS_DEBUG']),'-DCMAKE_EXE_LINKER_FLAGS=/INCREMENTAL:NO']
    run(base)
    run(['cmake','--build',str(work/'consumer'),'--config',args.config,'--parallel','4'])
    run([str(work/'consumer'/args.config/'installed_b2.exe')])
    if manifest['stage'] in ('B3Subset','B4Subset','B5Subset','B6Subset'):
        run([str(work/'consumer'/args.config/'installed_thin.exe')])
        maps=list((work/'consumer').rglob('installed_thin.map'))
        assert len(maps)==1,maps
        link_map=maps[0].read_text(encoding='utf-8',errors='replace')
        assert all(name in link_map for name in ('ock_ControlClient','ock_Adapter_LocalIPC'))
        assert not any(name in link_map for name in ('ock_Runtime','ock_Control:','ock_Dynamic'))
        shutil.copy2(maps[0],evidence/'installed-thin.map')
        cli_maps=list((producer/'apps/ock').rglob('ock.map'))
        cli_maps=[path for path in cli_maps if args.config in path.parts]
        assert len(cli_maps)==1,cli_maps
        cli_map=cli_maps[0].read_text(encoding='utf-8',errors='replace')
        assert all(name in cli_map for name in ('ock_ControlClient','ock_Adapter_LocalIPC'))
        assert not any(name in cli_map for name in ('ock_Runtime','ock_Control:','ock_Dynamic'))
        shutil.copy2(cli_maps[0],evidence/'ock-cli.map')
        run([str(relocated/'bin/ock.exe'),'--help'])
    (evidence/'result.json').write_text(json.dumps({'scope':'relocated B2 SDK and standalone public headers', 'passed':True, 'artifacts':str(work), 'sdk_version':manifest['sdk_version']}), encoding='utf-8')
    print('B2 installed consumer and standalone public headers passed:', work, 'evidence:', evidence)


if __name__ == '__main__': main()
