"""D1.06 有限开发集成；每轮冻结相关源码，所有真实子进程由唯一 Job 包装拥有。"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import save_json, sha_file
from tools.evidence.process import execute


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--target', action='append', required=True)
    p.add_argument('--case', action='append', default=[], help='relative executable path::case')
    p.add_argument('--config', choices=['Debug', 'Release'], default='Debug')
    p.add_argument('--asan', choices=['ON', 'OFF'], default='OFF')
    p.add_argument('--expect-build-red', default='')
    a = p.parse_args()
    tag = uuid.uuid4().hex[:10]
    out = ROOT/'evidence/bootstrap/D1.06'/('sdk-stage-'+tag)
    source = out/'source'
    source.mkdir(parents=True)
    inputs = []
    paths = [ROOT/'CMakeLists.txt', ROOT/'dependencies.lock']
    for folder in ('packages', 'cmake', 'sdk', 'tests', 'tools', 'examples'):
        paths += [x for x in (ROOT/folder).rglob('*') if x.is_file() and '__pycache__' not in x.parts]
    for original in paths:
        relative = original.relative_to(ROOT)
        dest = source/relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(original, dest)
        inputs.append({'path':relative.as_posix(), 'sha256':sha_file(dest), 'size':dest.stat().st_size})
    save_json(out/'inputs.json', inputs)
    build = ROOT/'build'/('d1.06-sdk-'+tag)
    records = []
    def run(label, argv):
        raw = [out/(label+'-'+s+'.log') for s in ('stdout', 'stderr')]
        row = execute(argv, ROOT, *raw, 600)
        row['label'] = label
        row['raw'] = [{'path':x.name, 'sha256':sha_file(x), 'size':x.stat().st_size} for x in raw]
        records.append(row)
        save_json(out/'commands.json', records)
        print(label, row['status'], row['exit_code'], out, flush=True)
        tree = row['process_tree']
        if row['status'] != 'Exited' or not tree['assigned_before_resume'] or tree['active_after'] != 0 or tree['terminated_owned_job']:
            raise ValueError('owned child incomplete: '+label)
        return row['exit_code'], b''.join(x.read_bytes() for x in raw)
    argv = ['cmake', '-S', str(source), '-B', str(build), '-G', 'Visual Studio 17 2022', '-A', 'x64',
            '-T', 'v143,version=14.44.35207', '-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
            '-DCMAKE_TOOLCHAIN_FILE='+str(source/'cmake/LockedMSVC.cmake'),
            '-DOCK_DEPENDENCY_CACHE='+str(ROOT/'build/d0.06-a/cache'), '-DOCK_DEPENDENCIES_OFFLINE=ON',
            '-DOCK_ENABLE_ASAN='+a.asan, '-DOCK_BUILD_NATIVE_TESTS=ON',
            '-DOCK_BUILD_HOST_TESTS='+('ON' if 'ock_host_tests' in a.target else 'OFF')]
    if run('configure', argv)[0]:
        return 1
    code, raw = run('build', ['cmake', '--build', str(build), '--config', a.config, '--target', *a.target, '--parallel', '2', '--', '/nr:false'])
    if a.expect_build_red:
        confirmed = code != 0 and a.expect_build_red.encode() in raw
        save_json(out/'result.json', {'scope':'指定新增合同编译 red，不是逻辑断言 red', 'confirmed':confirmed,
                                     'expected_diagnostic':a.expect_build_red, 'build':str(build)})
        return 0 if confirmed else 1
    if code:
        return code
    for i, item in enumerate(a.case):
        relative, case = item.split('::', 1)
        binary = build/relative.replace('{config}', a.config)
        if run('case-'+str(i), [str(binary), case])[0]:
            return 1
    save_json(out/'result.json', {'scope':'局部开发集成，非包验收', 'status':'Passed', 'build':str(build),
                                 'config':a.config, 'asan':a.asan, 'targets':a.target, 'cases':a.case})
    return 0


if __name__ == '__main__':
    sys.exit(main())
