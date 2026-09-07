"""D1.05 真实子进程、编译边界和消费者证据；固定用例独立于 expected。"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import uuid

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import save_json, sha_file
from tools.evidence.process import execute

PREFIX = '''#include "examples/native_service/value.hpp"
#include "packages/runtime/invocation/invocation.hpp"
using namespace ock::contracts;
using namespace ock::runtime;
using namespace invocation;
using Value = native_service::Value;
'''
CONTROLS = {
    "private_dispatch": {
        "positive": ('void accepted(NativeBound<Value,Value>& b, const Value& v, const InvokeOptions& o) { (void)b.invoke(v,o); }', ''),
        "dispatch": ('void rejected(const registry::Catalog& c, const NativeEntry& e, WorkContext& w) { NativeAccess::dispatch(c,e,nullptr,w,nullptr); }', 'C2248'),
        "check": ('void rejected(const registry::Catalog& c, const NativeEntry& e) { (void)NativeAccess::check(c,e,CppTypeToken::of<Value>(),CppTypeToken::of<Value>()); }', 'C2248'),
        "inspect": ('void rejected(const registry::Catalog& c, const OperationKey& k) { (void)NativeAccess::inspect(c,k,{},Shape::Read,CppTypeToken::of<Value>(),CppTypeToken::of<Value>()); }', 'C2248'),
        "construct_bound": ('void rejected() { NativeBound<Value,Value> b({},nullptr); }', 'C2248'),
        "raw_handler": ('void rejected(NativeBound<Value,Value>& b) { (void)b.handler(); }', 'C2039'),
    },
    "no_self_wait": {
        "positive": ('void accepted(NativeEngine& e, NativeBound<Value,Value>& b, const InvokeOptions& o) { (void)b.invoke(Value{1},o); (void)e.snapshot({}); }', ''),
        "block": ('void rejected(NativeBound<Value,Value>& b) { b.block(); }', 'C2039'),
        "wait": ('void rejected(NativeBound<Value,Value>& b) { b.wait(); }', 'C2039'),
        "force_inline": ('void rejected(InvokeOptions& o) { o.force_inline = true; }', 'C2039'),
    },
}
ALLOCATIONS = {'allocation_probe', 'allocation_steady', 'allocation_governance', 'allocation_other_costs'}


def exited(row, code=0):
    tree = row['process_tree']
    return (row['status'] == 'Exited' and row['exit_code'] == code and
            tree['mechanism'] == 'WindowsJobObject' and tree['assigned_before_resume'] and
            tree['active_after'] == 0)


def allocation(raw, case):
    report = json.loads(raw.decode('utf-8'))
    assert report['format'] == 'ock.native-allocation/1' and report['case'] == case
    assert report['coverage'] and isinstance(report['samples'], list) and report['samples']
    assert isinstance(report['checks'], dict) and report['checks']
    return report


def main():
    parser = argparse.ArgumentParser()
    for key in ('case', 'binary', 'runtime-dir', 'includes', 'generator', 'platform',
                'toolset', 'sdk', 'config', 'work', 'root-build', 'target-metadata', 'example'):
        parser.add_argument('--' + key, required=True)
    args = parser.parse_args()
    short = args.case.split('.')[-1]
    assert short in set(CONTROLS) | ALLOCATIONS | {'example_consumer', 'component_boundary'}
    runtime = Path(args.runtime_dir)
    assert (runtime / 'cl.exe').is_file()
    # 先删除继承的 Path，避免 Windows 原始环境块保留 PATH/Path 两个拼写。
    inherited_path = os.environ.pop('PATH', '')
    os.environ['PATH'] = str(runtime) + os.pathsep + inherited_path
    os.environ['MSBUILDDISABLENODEREUSE'] = '1'
    out = Path(args.work) / uuid.uuid4().hex
    out.mkdir(parents=True, exist_ok=False)
    commands = []

    def command(name, argv):
        streams = [out / (name + suffix) for suffix in ('-stdout.log', '-stderr.log')]
        row = execute(argv, ROOT, *streams, 240)
        row['raw'] = [{'path': p.name, 'sha256': sha_file(p), 'size': p.stat().st_size} for p in streams]
        commands.append(row)
        save_json(out / 'commands.json', {'case': args.case, 'commands': commands})
        return row, streams[0].read_bytes(), streams[1].read_bytes()

    native, stdout, _ = command('native', [args.binary, args.case])
    assert exited(native)
    structure = {'case': args.case, 'header_sha256': sha_file(ROOT / 'packages/runtime/invocation/invocation.hpp')}
    if short in ALLOCATIONS:
        report = allocation(stdout, args.case)
        save_json(out / 'allocation.json', report)
        assert all(v is True for v in report['checks'].values())
        if short == 'allocation_probe':
            red, raw, _ = command('probe-rejection', [args.binary, '--allocation-injected-red'])
            save_json(out / 'probe-rejection.json', red)
            assert exited(red, 1)
            injected = json.loads(raw.decode('utf-8'))
            assert injected['format'] == 'ock.native-allocation/1'
            assert injected['case'] == '--allocation-injected-red'
            assert injected['checks']['verified'] is False
            assert any(s['name'] == 'intentional_single_allocation' and s['cpp'] > 0
                       for s in injected['samples'])
            structure['injected_report'] = injected
        structure['allocation_checks'] = report['checks']
    elif short == 'example_consumer':
        source = ROOT / 'examples/native_service/main.cpp'
        assert not re.search(r'#\s*include\s*[<"][^">]*tests[/\\]', source.read_text(encoding='utf-8'))
        demo, raw, _ = command('demo', [args.example])
        assert exited(demo)
        report = json.loads(raw.decode('utf-8'))
        assert report['checks'] == dict.fromkeys(('compute', 'read', 'invalid_input', 'provider_unavailable'), True)
        structure.update(example_source_sha256=sha_file(source), consumer=report)
    elif short == 'component_boundary':
        cmake = ROOT / 'tests/contract/native/CMakeLists.txt'
        assert not re.search(r'install\s*\(', cmake.read_text(encoding='utf-8'))
        target = json.loads(Path(args.target_metadata).read_text(encoding='utf-8'))
        assert target['link_libraries'].split('|') == ['ock_registry_internal', 'ock_policy_internal', 'OCK::CoreContracts']
        assert target['core_contracts_links'] == 'OCK::Foundation'
        assert target['registry_links'] == target['policy_links'] == 'OCK::CoreContracts'
        save_json(out / 'target-metadata.json', target)
        prefix = out / 'install'
        installed, _, _ = command('install', ['cmake', '--install', args.root_build, '--config', args.config, '--prefix', str(prefix)])
        assert exited(installed)
        assert not any(any(word in p.name.lower() for word in ('invocation', 'native', 'private_bridge', 'policy', 'registry'))
                       for p in prefix.rglob('*') if p.suffix in ('.lib', '.hpp'))
        for component in ('CoreContracts', 'Runtime'):
            source = out / component
            source.mkdir()
            lines = ['cmake_minimum_required(VERSION 3.25)', 'project(InstalledNativeBoundary LANGUAGES NONE)',
                     f'find_package(OCK 0.1.0 CONFIG REQUIRED COMPONENTS {component})']
            if component == 'CoreContracts':
                lines += ['if(OCK_RUNTIME_AVAILABLE)', 'message(FATAL_ERROR "Runtime unexpectedly available")', 'endif()']
            (source / 'CMakeLists.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
            configured, raw, err = command('installed-' + component, ['cmake', '-S', str(source), '-B', str(source / 'build'), f'-DOCK_DIR={prefix}/lib/cmake/OCK'])
            if component == 'CoreContracts':
                assert exited(configured)
            else:
                assert exited(configured, 1)
                assert b'OCK component Runtime is not implemented in the current SDK' in raw + err
        structure['actual_target'] = target
    else:
        sources = CONTROLS[short]
        includes = ' '.join('[==[' + p.replace('\\', '/') + ']==]' for p in args.includes.split('|') if p)
        lines = ['cmake_minimum_required(VERSION 3.25)', 'project(NativeCompileContract LANGUAGES CXX)',
                 'set(CMAKE_CXX_STANDARD 20)', 'set(CMAKE_CXX_STANDARD_REQUIRED ON)']
        for name, (body, _) in sources.items():
            (out / (name + '.cpp')).write_text(PREFIX + body + '\n', encoding='utf-8')
            lines += [f'add_library({name} OBJECT EXCLUDE_FROM_ALL {name}.cpp)',
                      f'target_compile_options({name} PRIVATE /EHsc /utf-8 /Zc:__cplusplus /permissive-)',
                      f'target_include_directories({name} PRIVATE {includes})']
        (out / 'CMakeLists.txt').write_text('\n'.join(lines) + '\n', encoding='utf-8')
        argv = ['cmake', '-S', str(out), '-B', str(out / 'build'), '-G', args.generator,
                f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake', '-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded']
        for switch, value in (('-A', args.platform), ('-T', args.toolset)):
            if value:
                argv += [switch, value]
        if args.sdk:
            argv += ['-DCMAKE_SYSTEM_VERSION=' + args.sdk]
        configured, _, _ = command('configure', argv)
        assert exited(configured)
        for name, (_, diagnostic) in sources.items():
            built, raw, err = command(name, ['cmake', '--build', str(out / 'build'), '--config', args.config,
                                           '--target', name, '--parallel', '2', '--', '/nr:false'])
            if not diagnostic:
                assert exited(built)
            else:
                assert exited(built, 1)
                text = (raw + err).decode('utf-8', errors='replace')
                errors = [line for line in text.splitlines() if re.search(r'error C\d+', line)]
                assert errors and all(name + '.cpp(' in line and 'error ' + diagnostic + ':' in line for line in errors), text
        structure['compile_controls'] = list(sources)
    save_json(out / 'structure.json', structure)
    print(json.dumps({'case': args.case, 'status': 'Passed', 'evidence': str(out)}))


if __name__ == '__main__':
    main()
