"""D1.05 开发验证：冻结源码快照、独占 Windows Job、每轮单独证据。"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import uuid

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.evidence.process import execute

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--config', choices=['Debug', 'Release'], default='Debug')
    parser.add_argument('--case', action='append', default=[])
    parser.add_argument('--runtime-from', type=Path)
    args = parser.parse_args()
    if args.runtime_from:
        args.runtime_from=args.runtime_from.resolve()
        if not args.runtime_from.is_relative_to(ROOT/'evidence/bootstrap/D1.05') or not args.runtime_from.is_dir():
            raise ValueError('runtime source must be a preserved D1.05 snapshot')
    tag = uuid.uuid4().hex[:12]
    out = ROOT / 'evidence/bootstrap/D1.05' / ('pipeline-' + tag)
    out.mkdir(parents=True, exist_ok=False)
    rows = []
    for folder in ('packages', 'tests/contract/native', 'tests/contract/authorization',
                   'tests/compile/contracts', 'examples/native_service'):
        for path in (ROOT / folder).rglob('*'):
            if not path.is_file() or '__pycache__' in path.parts:
                continue
            relative = path.relative_to(ROOT)
            dest = out / 'source' / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            original=path
            if args.runtime_from and relative.as_posix().startswith('packages/runtime/invocation/'):
                original=args.runtime_from/relative
                if not original.is_file():raise ValueError('missing preserved runtime input')
            shutil.copyfile(original, dest)
            rows.append({'path': relative.as_posix(),
                         'sha256': hashlib.sha256(dest.read_bytes()).hexdigest()})
    (out / 'source.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    source = out / 'source'
    includes = [source, source/'tests/compile/contracts', source/'packages/contracts/include',
                source/'packages/foundation/include',
                ROOT/'build/d0.06-a/cache/sources/expected-fe3b18aecb84/include']
    units = ['packages/runtime/policy/policy.cpp', 'packages/runtime/registry/registry.cpp',
             'packages/runtime/invocation/invocation.cpp', 'tests/contract/native/native_tests.cpp',
             'tests/contract/native/allocation_probe.cpp']
    lines = ['cmake_minimum_required(VERSION 3.25)', 'project(NativeStage LANGUAGES CXX)',
             'add_executable(native_stage ' + ' '.join('"'+(source/u).as_posix()+'"' for u in units) + ')',
             'target_compile_features(native_stage PRIVATE cxx_std_20)',
             'target_compile_options(native_stage PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive- /Zc:nrvo-)',
             'target_include_directories(native_stage PRIVATE ' + ' '.join('"'+p.as_posix()+'"' for p in includes)+')']
    (out/'CMakeLists.txt').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    commands = []
    def run(name, argv):
        record = execute(argv, ROOT, out/(name+'-stdout.log'), out/(name+'-stderr.log'), 600)
        commands.append(record)
        (out/'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
        print(name, record['status'], record['exit_code'], out, flush=True)
        if record['status'] != 'Exited' or record['process_tree']['active_after'] != 0:
            raise RuntimeError('owned command incomplete: '+name)
        return record['exit_code']
    build = ROOT/'build'/('d1.05-stage-'+tag)
    if run('configure', ['cmake','-S',str(out),'-B',str(build),'-G','Visual Studio 17 2022',
                        '-A','x64','-T','v143,version=14.44.35207', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
                        '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake')]):
        return 1
    if run('build', ['cmake','--build',str(build),'--config',args.config,'--parallel','2','--','/nr:false']):
        return 1
    binary = build/args.config/'native_stage.exe'
    if run('list',[str(binary),'--list']):
        return 1
    names = (out/'list-stdout.log').read_text(encoding='utf-8').splitlines()
    if not names or len(names) != len(set(names)) or set(args.case)-set(names):
        raise ValueError('invalid discovery or unknown case')
    failed = False
    for name in names:
        if not args.case or name in args.case:
            failed = bool(run(name,[str(binary),name])) or failed
    return int(failed)

if __name__ == '__main__':
    sys.exit(main())
