"""D1.06 真实安装 NativeSubset 消费；正控先行，逐反例独立源码和构建。"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import save_json, sha_file
from tools.evidence.process import execute
from tests.install_consumer.verify_contracts import read_profile, verify_compile_trace

CASES = ('metadata', 'public_headers', 'installed_host', 'no_tests_producer',
         'link_closure', 'install_pruning_rejected', 'private_dispatch')


def verify_actual_traces(producer, consumer, config, asan):
    """读真实 CL/link tlog，声明的配置不能替代实际编译/链接事实。"""
    records=[]
    for label, target, directory in [('producer','ock_Runtime',producer),
                                      ('consumer','ock_stateless_service',consumer)]:
        traces=list((directory/(target+'.dir')/config).glob('*.tlog/CL.command.1.tlog'))
        if len(traces)!=1:raise ValueError('actual compiler trace missing/ambiguous: '+label)
        path=traces[0];text=path.read_text(encoding='utf-16')
        verify_compile_trace(text,asan)
        expected='mdd' if config=='Debug' else 'md'
        if set(re.findall(r'/(mdd|md|mtd|mt)\b',text.casefold()))!={expected}:
            raise ValueError('actual CRT configuration differs: '+label)
        if re.search(r'/gl\b',text,re.I):raise ValueError('unexpected LTO in declared SDK profile')
        records.append({'kind':label,'path':str(path),'sha256':sha_file(path),'size':path.stat().st_size,
                        'config':config,'crt':expected,'asan':asan,'lto':False})
    links=list((consumer/'ock_stateless_service.dir'/config).glob('*.tlog/link.command.1.tlog'))
    if len(links)!=1:raise ValueError('actual link trace missing/ambiguous')
    text=links[0].read_text(encoding='utf-16').upper()
    if 'OCK_RUNTIME.LIB' not in text or not re.search(r'\bBCRYPT\.LIB\b',text):
        raise ValueError('actual required Runtime/bcrypt link absent')
    records.append({'kind':'link','path':str(links[0]),'sha256':sha_file(links[0]),'size':links[0].stat().st_size})
    return records


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--config', choices=['Debug', 'Release'], required=True)
    parser.add_argument('--case', choices=CASES, required=True)
    parser.add_argument('--expect-unavailable', action='store_true')
    a = parser.parse_args()
    producer = a.build.resolve()
    if not producer.is_relative_to(ROOT/'build'):
        raise ValueError('workspace producer required')
    asan = read_profile(producer)
    cache = dict(match.groups() for line in (producer/'CMakeCache.txt').read_text(encoding='utf-8').splitlines()
                 if (match := re.fullmatch(r'([A-Za-z0-9_]+):[^=]+=(.*)', line)))
    compiler_profiles = list((producer/'CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    if len(compiler_profiles)!=1:raise ValueError('ambiguous actual compiler identity')
    compiler_match = re.search(r'set\(CMAKE_CXX_COMPILER "([^"]+)"\)',compiler_profiles[0].read_text(encoding='utf-8'))
    if not compiler_match:raise ValueError('actual compiler identity missing')
    compiler = Path(compiler_match[1])
    if not compiler.is_file():
        raise ValueError('actual compiler missing')
    # 只改变当前验证进程及其子进程的环境，与 CTest compiler-dir 路径一致。
    os.environ['PATH'] = str(compiler.parent)+os.pathsep+os.environ.get('PATH', '')
    os.environ['MSBUILDDISABLENODEREUSE'] = '1'
    out = ROOT/'evidence/bootstrap/D1.06'/('sdk-'+a.case+'-'+uuid.uuid4().hex[:10])
    out.mkdir(parents=True)
    shutil.copyfile(__file__,out/'driver.py')
    shutil.copyfile(ROOT/'cmake/LockedMSVC.cmake',out/'LockedMSVC.cmake')
    records = []
    save_json(out/'profile.json', {'config':a.config, 'asan':asan, 'producer':str(producer),
              'cache_sha256':sha_file(producer/'CMakeCache.txt'), 'compiler':str(compiler),
              'compiler_sha256':sha_file(compiler), 'driver_sha256':sha_file(__file__)})

    def run(label, argv, success=True, diagnostic=None):
        raw = [out/(f'{len(records):03}-{label}-'+s+'.log') for s in ('stdout', 'stderr')]
        row = execute(argv, ROOT, *raw, 600)
        row['label'] = label
        row['raw'] = [{'path':p.name,'sha256':sha_file(p),'size':p.stat().st_size} for p in raw]
        records.append(row);save_json(out/'commands.json', records)
        tree = row['process_tree']
        data = b''.join(p.read_bytes() for p in raw)
        if not (row['status']=='Exited' and tree['assigned_before_resume'] and tree['active_after']==0 and not tree['terminated_owned_job']):
            raise ValueError('owned child incomplete: '+label)
        if (row['exit_code']==0) != success or (diagnostic and not re.search(diagnostic, data)):
            raise ValueError('unexpected exit/diagnostic: '+label)
        return data

    def configure(source, build, prefix=None, extra=(), success=True, diagnostic=None):
        args = ['cmake','-S',str(source),'-B',str(build),'-G','Visual Studio 17 2022','-A','x64',
                '-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
                '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),
                '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded$<$<CONFIG:Debug>:Debug>DLL',
                '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded']
        for key in ('CMAKE_CXX_FLAGS','CMAKE_CXX_FLAGS_DEBUG','CMAKE_CXX_FLAGS_RELEASE'):
            flags=cache[key]
            if asan:flags=re.sub(r'/RTC[1su]', '', flags)
            args.append('-D'+key+'='+flags)
        if asan:
            args += ['-DCMAKE_CXX_FLAGS='+cache['CMAKE_CXX_FLAGS']+' /fsanitize=address',
                     '-DCMAKE_EXE_LINKER_FLAGS=/INCREMENTAL:NO']
        if prefix:args += ['-DOCK_DIR='+str(prefix/'lib/cmake/OCK')]
        return run('configure-'+build.name, args+list(extra), success, diagnostic)

    def build(path, target='ock_stateless_service', success=True, diagnostic=None):
        return run('build-'+path.name, ['cmake','--build',str(path),'--config',a.config,
                   '--target',target,'--parallel','2','--','/nr:false'], success, diagnostic)

    if a.case == 'no_tests_producer':
        producer = out/'producer-build'
        configure(Path(cache['CMAKE_HOME_DIRECTORY']), producer, extra=(
            '-DBUILD_TESTING=OFF', '-DOCK_ENABLE_ASAN='+('ON' if asan else 'OFF'),
            '-DOCK_DEPENDENCIES_OFFLINE=ON', '-DOCK_DEPENDENCY_CACHE='+cache['OCK_DEPENDENCY_CACHE']))
        build(producer, 'ock_Runtime')
        if 'BUILD_TESTING:BOOL=OFF' not in (producer/'CMakeCache.txt').read_text():
            raise ValueError('producer tests not disabled')

    original = out/'original-prefix'
    run('install', ['cmake','--install',str(producer),'--config',a.config,'--prefix',str(original)])
    prefix = out/'relocated-prefix'
    shutil.copytree(original, prefix)
    detached = out/'unavailable-original-prefix'
    if not original.resolve().is_relative_to(out.resolve()) or not detached.resolve().is_relative_to(out.resolve()):
        raise ValueError('relocation outside owned evidence')
    original.rename(detached)
    for file in prefix.rglob('*.cmake'):
        content=file.read_text(encoding='utf-8').replace('\\','/').casefold()
        if ROOT.as_posix().casefold() in content:
            raise ValueError('installed CMake leaked workspace')
    source = out/'consumer'
    shutil.copytree(ROOT/'examples/stateless_service', source)
    save_json(out/'consumer-inputs.json', [{'path':p.relative_to(source).as_posix(),'sha256':sha_file(p)} for p in source.rglob('*') if p.is_file()])
    positive = out/'positive-build'
    if a.expect_unavailable:
        configure(source, positive, prefix, success=False, diagnostic=b'OCK component Runtime is not implemented')
        save_json(out/'result.json', {'scope':'旧SDK拒绝真实Runtime消费者，非逻辑运行red','confirmed':True})
        print(out);return
    configure(source, positive, prefix)
    build(positive)
    binary = positive/a.config/'ock_stateless_service.exe'
    report = json.loads(run('run-consumer', [str(binary)]))
    if report != {'sdk':'0.1.0-dev.2','stage':'NativeSubset','checks':dict.fromkeys(('read','compute','invalid_input','ready_gate','shutdown'),True)}:
        raise ValueError('real consumer checks mismatch')
    save_json(out/'actual-traces.json',verify_actual_traces(producer,positive,a.config,asan))

    if a.case == 'metadata':
        manifest = json.loads((prefix/'share/ock/sdk_api_manifest.json').read_text(encoding='utf-8'))
        if manifest['sdk_version']!='0.1.0-dev.2' or manifest['stage']!='NativeSubset' or manifest['targets']['Runtime']['kind']!='STATIC_LIBRARY':
            raise ValueError('installed metadata mismatch')
        for component in ('Data','State','Durable','Control','Adapter::Logging','Observation'):
            candidate=out/('reject-'+component.replace('::','-'));candidate.mkdir()
            (candidate/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(RejectedComponent LANGUAGES NONE)\nfind_package(OCK CONFIG REQUIRED COMPONENTS '+component+')\n')
            configure(candidate,candidate/'build',prefix,success=False,diagnostic=('OCK component '+component+' is not implemented').encode())
    elif a.case == 'public_headers':
        manifest = json.loads((prefix/'share/ock/sdk_api_manifest.json').read_text(encoding='utf-8'))
        headers=[x['path'].split('/include/',1)[1] for x in manifest['headers'] if x['classification']=='experimental']
        lines=['cmake_minimum_required(VERSION 3.25)','project(PublicHeaders LANGUAGES CXX)',
               'find_package(OCK CONFIG REQUIRED COMPONENTS Runtime)']
        directory=out/'headers';directory.mkdir()
        for index,header in enumerate(headers):
            stem='h'+str(index)
            (directory/(stem+'.cpp')).write_text('#define min(a,b) ((a)<(b)?(a):(b))\n#define max(a,b) ((a)>(b)?(a):(b))\n#include <'+header+'>\n#ifndef min\n#error public_header_removed_min\n#endif\n#ifndef max\n#error public_header_removed_max\n#endif\nint '+stem+'(){return 1;}\n')
            lines += ['add_library('+stem+' OBJECT '+stem+'.cpp)','target_link_libraries('+stem+' PRIVATE OCK::Runtime)']
        (directory/'one.cpp').write_text('#include <ock/runtime/host.hpp>\nint other();int main(){return other();}\n')
        (directory/'two.cpp').write_text('#include <ock/runtime/host.hpp>\nint other(){return ock::sdk::runtime_available?0:1;}\n'.replace('#include <ock/runtime/host.hpp>','#include <ock/runtime/host.hpp>\n#include <ock/foundation/sdk_version.hpp>'))
        lines += ['add_executable(two_tu one.cpp two.cpp)','target_link_libraries(two_tu PRIVATE OCK::Runtime)']
        (directory/'CMakeLists.txt').write_text('\n'.join(lines)+'\n')
        configure(directory,directory/'build',prefix)
        build(directory/'build','ALL_BUILD')
        run('two-tu',[str(directory/'build'/a.config/'two_tu.exe')])
    elif a.case in ('install_pruning_rejected','link_closure'):
        entries = [('runtime','lib/ock_Runtime.lib',b'ock_Runtime'),
                   ('public','include/ock/runtime/host.hpp',b'host.hpp'),
                   ('detail','include/ock/runtime/detail/host.hpp',b'host.hpp'),
                   ('expected','include/tl/expected.hpp',b'expected.hpp')]
        if a.case=='link_closure':entries=[('bcrypt',None,b'BCryptGenRandom')]
        for label,relative,needle in entries:
            damaged=out/('missing-'+label);shutil.copytree(prefix,damaged)
            if relative:
                victim=damaged/relative
                if not victim.is_file():raise ValueError('required installed file absent before pruning: '+relative)
                victim.unlink()
            else:
                export=damaged/'lib/cmake/OCK/OCKTargets.cmake'
                text=export.read_text(encoding='utf-8')
                token=';\\$<LINK_ONLY:bcrypt>'
                if text.count(token)!=1:raise ValueError('actual escaped bcrypt export missing/ambiguous')
                export.write_text(text.replace(token,''),encoding='utf-8')
            consumer=out/('consumer-'+label);shutil.copytree(source,consumer)
            if label=='bcrypt':
                cmake=consumer/'CMakeLists.txt';text=cmake.read_text().replace('OCK::CoreContracts;$<LINK_ONLY:bcrypt>','OCK::CoreContracts');cmake.write_text(text)
            dest=out/('build-'+label)
            if label=='runtime':configure(consumer,dest,damaged,success=False,diagnostic=needle)
            else:
                configure(consumer,dest,damaged)
                build(dest,success=False,diagnostic=needle)
    elif a.case == 'private_dispatch':
        fragments = {
            'dispatch':'auto bad=&ock::runtime::invocation::NativeAccess::dispatch;',
            'check':'auto bad=&ock::runtime::invocation::NativeAccess::check;',
            'inspect':'auto bad=&ock::runtime::invocation::NativeAccess::inspect;',
            'handler':'void bad(const ock::runtime::registry::Catalog& c){c.handler();}',
            'getter':'void bad(ock::runtime::host::HostSession& s){s.engine();}',
            'session':'ock::runtime::host::HostSession bad;',
            'bound':'ock::runtime::host::HostBound<native_service::Value,native_service::Value> bad;',
        }
        directory=out/'private';directory.mkdir()
        shutil.copyfile(source/'value.hpp',directory/'value.hpp')
        lines=['cmake_minimum_required(VERSION 3.25)','project(PrivateBoundaries LANGUAGES CXX)','find_package(OCK CONFIG REQUIRED COMPONENTS Runtime)']
        for name,body in fragments.items():
            (directory/(name+'.cpp')).write_text('#include "value.hpp"\n#include <ock/runtime/host.hpp>\n'+body+'\n')
            lines+=['add_library('+name+' OBJECT EXCLUDE_FROM_ALL '+name+'.cpp)','target_link_libraries('+name+' PRIVATE OCK::Runtime)']
        (directory/'CMakeLists.txt').write_text('\n'.join(lines)+'\n')
        configure(directory,directory/'build',prefix)
        for name in fragments:
            if name in ('dispatch','check','inspect'):
                diagnostic=rb'error C2248[^\r\n]*NativeAccess::'+name.encode()
            elif name in ('handler','getter'):
                diagnostic=rb'error C2039[^\r\n]*'+(b'handler' if name=='handler' else b'engine')
            else:
                diagnostic=rb'error C(?:2512|2660|2661|2280)[^\r\n]*Host'+(b'Session' if name=='session' else b'Bound')
            build(directory/'build',name,False,diagnostic)

    save_json(out/'artifacts.json',[{'path':p.relative_to(out).as_posix(),'sha256':sha_file(p),'size':p.stat().st_size}
              for p in (binary,prefix/'lib/ock_Runtime.lib',prefix/'lib/cmake/OCK/OCKTargets.cmake',prefix/'share/ock/sdk_api_manifest.json')])
    save_json(out/'result.json',{'case':a.case,'status':'Passed','scope':'局部SDK消费者合同，非D1.06包验收','consumer':report})
    print(json.dumps({'case':a.case,'evidence':str(out),'status':'Passed'},ensure_ascii=False))


if __name__ == '__main__':
    main()
