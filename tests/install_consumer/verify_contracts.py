"""D1.02：真实搬迁安装消费者，正例先行且负例核对具体拒绝诊断。"""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import sys
import uuid
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.evidence.process import execute
from tools.evidence.common import save_json, sha_file

HEADERS = ('identity', 'context', 'outcome', 'ports', 'observation', 'operation')


def read_profile(build):
    cache = {}
    for line in (build / 'CMakeCache.txt').read_text(encoding='utf-8').splitlines():
        match = re.fullmatch(r'([A-Za-z0-9_]+):[^=]+=(.*)', line)
        if match: cache[match[1]] = match[2]
    if cache.get('OCK_ENABLE_ASAN') not in ('ON', 'OFF'):
        raise ValueError('producer must explicitly declare OCK_ENABLE_ASAN')
    expected = {'CMAKE_GENERATOR': 'Visual Studio 17 2022', 'CMAKE_GENERATOR_PLATFORM': 'x64',
                'CMAKE_GENERATOR_TOOLSET': 'v143,version=14.44.35207', 'CMAKE_SYSTEM_VERSION': '10.0.26100.0'}
    if any(cache.get(key) != value for key, value in expected.items()):
        raise ValueError('producer is not the locked MSVC profile')
    return cache['OCK_ENABLE_ASAN'] == 'ON'


def verify_compile_trace(trace, asan):
    tokens = trace.casefold()
    if ('/fsanitize=address' in tokens) != asan:
        raise AssertionError('consumer ASan instrumentation differs from producer')
    if asan and (re.search(r'/rtc[1su]', tokens) or '/z7' not in tokens):
        raise AssertionError('ASan consumer has incompatible RTC/debug information')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--build', required=True)
    parser.add_argument('--config', required=True, choices=['Debug', 'Release'])
    parser.add_argument('--case', required=True, choices=['public_headers', 'installed_component_consumer', 'install_pruning_rejected'])
    args = parser.parse_args()
    build = Path(args.build).resolve()
    if not build.is_relative_to(ROOT / 'build'):
        raise ValueError('isolated workspace build required')
    asan = read_profile(build)
    os.environ['MSBUILDDISABLENODEREUSE'] = '1'
    work = ROOT / 'build/d1.02-sdk' / uuid.uuid4().hex
    logs = work / 'commands'
    logs.mkdir(parents=True)
    sequence = 0
    save_json(work / 'profile.json', {'asan': asan, 'configuration': args.config, 'producer_cache_sha256': sha_file(build / 'CMakeCache.txt')})

    def run(argv, success=True, needles=()):
        nonlocal sequence
        sequence += 1
        stem = f'{sequence:03}'
        result = execute(argv, ROOT, logs / (stem+'-stdout.log'), logs / (stem+'-stderr.log'), 180)
        save_json(logs / (stem+'-command.json'), result)
        output = ''.join((logs / (stem+suffix)).read_text(encoding='utf-8', errors='replace') for suffix in ('-stdout.log', '-stderr.log'))
        if result['status'] != 'Exited' or (result['exit_code'] == 0) != success or any(n not in output for n in needles):
            raise RuntimeError('unexpected child result '+stem+' '+str(result)+'\n'+output)
        return output

    def install(producer):
        original = work / ('install-'+uuid.uuid4().hex[:8])
        moved = work / ('relocated-'+uuid.uuid4().hex[:8])
        run(['cmake', '--install', str(producer), '--config', args.config, '--prefix', str(original)])
        shutil.copytree(original, moved)
        for path in moved.rglob('*.cmake'):
            content = path.read_text(encoding='utf-8').replace('\\', '/').casefold()
            if ROOT.as_posix().casefold() in content or original.as_posix().casefold() in content:
                raise AssertionError('SDK export contains source/build/original location')
        detached = work / (original.name+'-detached')
        assert original.resolve().is_relative_to(work.resolve()) and detached.resolve().is_relative_to(work.resolve())
        original.rename(detached)
        return moved

    def consume(prefix, headers=False, success=True, needles=(), component='CoreContracts'):
        folder = work / ('consumer-'+uuid.uuid4().hex[:8])
        source = folder / 'source'
        source.mkdir(parents=True)
        lines = ['cmake_minimum_required(VERSION 3.25)', 'project(ContractsConsumer LANGUAGES CXX)',
                 'set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")',
                 'get_filename_component(_compiler_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)',
                 'file(WRITE "${CMAKE_BINARY_DIR}/runtime-dir.txt" "${_compiler_dir}")',
                 f'find_package(OCK 0.1.0 CONFIG REQUIRED COMPONENTS {component})']
        if asan:
            lines += ['string(REGEX REPLACE "/RTC[1su]" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")',
                      'add_compile_options(/fsanitize=address)', 'add_link_options(/INCREMENTAL:NO)']
        if component == 'CoreContracts':
            lines += ['if(OCK_RUNTIME_AVAILABLE OR NOT OCK_IMPLEMENTATION_STAGE STREQUAL "CoreContracts")',
                      '  message(FATAL_ERROR "unexpected SDK stage")', 'endif()',
                      'get_target_property(_impl OCK::CoreContracts OCK_IMPLEMENTATION_STAGE)',
                      'get_target_property(_links OCK::CoreContracts INTERFACE_LINK_LIBRARIES)',
                      'if(NOT _impl STREQUAL "Implemented" OR NOT _links STREQUAL "OCK::Foundation")',
                      '  message(FATAL_ERROR "unexpected CoreContracts closure")', 'endif()']
        texts = ([f'#include <ock/contracts/{h}.hpp>\nint main(){{return 0;}}\n' for h in HEADERS] +
                 ['#define min(a,b) 137\n#define max(a,b) 251\n#include <ock/contracts/operation.hpp>\n#if !defined(min) || !defined(max)\n#error consumer macros must remain defined\n#endif\nstatic_assert(min(1,2)==137 && max(1,2)==251);\nint main(){return 0;}\n'] if headers else
                 [(ROOT / 'tests/install_consumer/contracts.cpp').read_text(encoding='utf-8')])
        for index, content in enumerate(texts):
            (source / f'consumer{index}.cpp').write_text(content, encoding='utf-8', newline='\n')
            lines += [f'add_executable(consumer{index} consumer{index}.cpp)',
                      f'target_link_libraries(consumer{index} PRIVATE OCK::{component})']
        (source / 'CMakeLists.txt').write_text('\n'.join(lines)+'\n', encoding='utf-8', newline='\n')
        target = folder / 'build'
        configure = ['cmake', '-S', str(source), '-B', str(target), '-G', 'Visual Studio 17 2022',
                     '-A', 'x64', '-T', 'v143,version=14.44.35207', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
                     '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded',
                     f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake', f'-DOCK_DIR={prefix}/lib/cmake/OCK']
        if component != 'CoreContracts':
            run(configure, False, [f'OCK component {component} is not implemented in the current SDK'])
            return
        run(configure)
        run(['cmake', '--build', str(target), '--config', args.config, '--parallel', '2', '--', '/nr:false'], success, needles)
        if success:
            runtime = Path((target / 'runtime-dir.txt').read_text(encoding='utf-8'))
            if not (runtime / 'cl.exe').is_file(): raise ValueError('actual compiler runtime directory missing')
            os.environ['PATH'] = str(runtime) + os.pathsep + os.environ.get('PATH', '')
            for index in range(len(texts)):
                trace = target / f'consumer{index}.dir' / args.config / f'consumer{index}.tlog' / 'CL.command.1.tlog'
                verify_compile_trace(trace.read_text(encoding='utf-16'), asan)
                run([str(target / args.config / f'consumer{index}.exe')])

    try:
        prefix = install(build)
        if args.case == 'public_headers':
            run([sys.executable, '-X', 'utf8', '-m', 'unittest', 'tests.install_consumer.test_contracts_driver', '-v'])
            consume(prefix, headers=True)
        elif args.case == 'installed_component_consumer':
            consume(prefix)
            producer = work / 'producer-no-tests'
            preset = 'win-msvc-asan' if asan else ('win-msvc-release' if args.config == 'Release' else 'win-msvc-debug')
            run(['cmake', '--preset', preset, '-B', str(producer), '-DBUILD_TESTING=OFF',
                 '-DOCK_ENABLE_ASAN='+('ON' if asan else 'OFF'),
                 '-DOCK_BUILD_G0_TESTS=OFF', '-DOCK_BUILD_DEPENDENCY_PROBES=OFF',
                 '-DOCK_DEPENDENCY_COMPONENTS=Foundation', '-DOCK_DEPENDENCIES_OFFLINE=ON'])
            consume(install(producer))
        else:
            consume(prefix)
            for header in ('ock/foundation/foundation.hpp', 'tl/expected.hpp'):
                pruned = work / ('pruned-'+uuid.uuid4().hex[:8])
                shutil.copytree(prefix, pruned)
                target = pruned / 'include' / header
                assert target.resolve().is_relative_to(work.resolve())
                target.unlink()
                consume(pruned, success=False, needles=['C1083', header])
            for component in ('Runtime', 'Data', 'Observation'):
                consume(prefix, component=component)
    except Exception:
        save_json(work / 'result.json', {'case': args.case, 'status': 'Failed', 'configuration': args.config,
                  'child_commands': sequence})
        print(json.dumps({'case': args.case, 'status': 'Failed', 'work': str(work)}, ensure_ascii=False))
        raise
    save_json(work / 'result.json', {'case': args.case, 'status': 'Passed', 'configuration': args.config,
              'child_commands': sequence, 'raw': [{'path': p.relative_to(ROOT).as_posix(), 'sha256': sha_file(p)} for p in sorted(logs.iterdir())]})
    print(json.dumps({'case': args.case, 'status': 'Passed', 'work': str(work)}, ensure_ascii=False))


if __name__ == '__main__':
    main()
