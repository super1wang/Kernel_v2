"""同一 CTest 读取期的编译夹具；只复用配置，不复用编译结论。"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import digest, read_json, save_json, sha_file
from tools.evidence.process import execute


def arguments(parser):
    for name in ('binary','includes','generator','platform','toolset','sdk','config','work','runtime-dir','profile','fixture','run-id'):
        parser.add_argument('--'+name, required=True)


def targets():
    from verify_children import NEGATIVE
    result = {}
    for index, (case, negatives) in enumerate(NEGATIVE.items()):
        bodies = {'positive':'auto accepted=make_compute_definition(compute_handler,input(AtomicMode::PureCompute));', **negatives}
        for variant, (name, body) in enumerate(bodies.items()):
            target = 'cc'+str(index).zfill(2)+('_p' if name == 'positive' else '_n'+str(variant).zfill(2))
            result[target] = {'case':case, 'name':name, 'positive':name == 'positive', 'body':body}
    return result


def good(row):
    tree = row.get('process_tree', {})
    return (row.get('status') == 'Exited' and row.get('exit_code') == 0
            and tree.get('active_after') == 0 and tree.get('assigned_before_resume') is True
            and tree.get('terminated_owned_job') is False)


def check_compile(row, raw, target, *, positive):
    tree = row.get('process_tree', {})
    if (row.get('status') != 'Exited' or tree.get('active_after') != 0
            or tree.get('assigned_before_resume') is not True or tree.get('terminated_owned_job') is not False):
        raise ValueError('compile process ownership/exit failed')
    if not re.search(rb'(?<![A-Za-z_0-9])'+re.escape(target.encode())+rb'\.cpp\b', raw):
        raise ValueError('target source was not actually compiled')
    if positive:
        if not good(row):
            raise ValueError('positive control failed')
    elif (row.get('exit_code') in (None, 0)
          or not re.search(rb'error C(?:2248|2672|2664|2280|2039|3892|2440|2783|2784|7602)\b', raw)
          or re.search(rb'fatal error|error (?:MSB|LNK)|error C(?:1083|1902|1900)\b', raw)):
        raise ValueError('not a target C++ contract rejection')


def check_execution(expected, observed):
    if list(expected) != list(observed) or len(set(observed)) != len(observed):
        raise ValueError('missing/duplicate/unexpected target execution')


def check_discovered(names):
    cases = {item['case'] for item in targets().values()}
    expected = {('T05' if name == 'typed_binding_fingerprint' else 'T02')+'.contracts.'+name for name in cases}
    observed = [name for name in names if name.startswith('T02.contracts.') or name == 'T05.contracts.typed_binding_fingerprint']
    if set(observed) != expected or len(observed) != len(expected):
        raise ValueError('compile wrapper discovery mismatch')


def check_identity(expected, observed):
    if expected != observed:
        raise ValueError('fixture identity mismatch')


def read_ready(path):
    try:
        row = read_json(Path(path)/'ready.json')
    except (OSError, ValueError) as exc:
        raise ValueError('fixture ready unavailable') from exc
    if row.get('status') != 'Ready':
        raise ValueError('fixture ready invalid')
    return row


def profile(a):
    result = {}
    for line in Path(a.profile).read_text(encoding='utf-8').splitlines():
        key, value = line.split('=', 1)
        if key in result:
            raise ValueError('duplicate profile field')
        result[key] = value
    for key in ('generator','platform','toolset','sdk','config'):
        if result[key] != getattr(a, key):
            raise ValueError('parent profile identity mismatch: '+key)
    if a.config not in ('Debug','Release','RelWithDebInfo','MinSizeRel'):
        raise ValueError('invalid configuration')
    if result['asan'] not in ('ON','OFF') or result['crt'] not in ('MultiThreadedDebugDLL','MultiThreadedDLL'):
        raise ValueError('unsupported actual CRT/ASan profile')
    return result


def input_identity(a):
    p = profile(a)
    runtime = Path(a.runtime_dir).resolve()
    if Path(p['compiler']).resolve() != runtime/'cl.exe':
        raise ValueError('actual compiler differs from runtime')
    cmake = shutil.which('cmake')
    if not cmake: raise ValueError('actual cmake executable missing')
    paths = {Path(a.profile).resolve(), Path(a.binary).resolve(), Path(p['compiler']).resolve(),Path(cmake).resolve()}
    for name in ('c1xx.dll','c2.dll'):
        paths.add(runtime/name)
    for base in [Path(x) for x in a.includes.split('|') if x] + [ROOT/'tests/compile/contracts',ROOT/'tests/conformance/core_contracts']:
        if not base.is_dir():
            raise ValueError('source include directory missing')
        paths.update(x.resolve() for x in base.rglob('*') if x.is_file() and x.suffix in ('.h','.hpp','.cpp','.py','.txt','.cmake'))
    paths.update((ROOT/x).resolve() for x in ('dependencies.lock','cmake/LockedMSVC.cmake','cmake/msvc-validation-tools.json','tools/development/msvc_isolation.py','tools/evidence/process.py','tools/evidence/common.py'))
    return {'run_id':a.run_id, 'profile':p, 'includes':a.includes,
            'inputs':[{'path':str(x),'sha256':sha_file(x)} for x in sorted(paths)],
            'targets':targets()}


def generated_identity(directory):
    paths = list(directory.glob('*.cpp'))+[directory/'CMakeLists.txt',directory/'build/CMakeCache.txt']
    paths += list((directory/'build').glob('*.vcxproj'))
    paths += list((directory/'build/CMakeFiles').glob('*/CMakeCXXCompiler.cmake'))
    return [{'path':str(x.relative_to(directory)), 'sha256':sha_file(x)} for x in sorted(paths)]


def require_ready(a):
    directory = Path(a.fixture).resolve()
    if not re.fullmatch(r'[a-zA-Z0-9_-]{8,100}', a.run_id):
        raise ValueError('invalid run identity')
    row = read_ready(directory)
    if not good(row.get('configure', {})) or row.get('identity_sha256') != digest(row.get('identity')):
        raise ValueError('fixture ready lacks successful preparation identity')
    check_identity(row['identity'], input_identity(a))
    check_identity(row['generated'], generated_identity(directory))
    return row


def quote(value):
    value = str(value).replace('\\','/')
    if ']==]' in value or '\n' in value or '\r' in value:
        raise ValueError('invalid CMake argument')
    return '[==['+value+']==]'


def prepare(a, command):
    directory = Path(a.fixture).resolve()
    # mkdir(exist_ok=False)拒绝旧 ready、旧失败目录和另一轮产物，绝不清空重用。
    directory.mkdir(parents=True, exist_ok=False)
    identity = input_identity(a)
    p = identity['profile']
    lines = ['cmake_minimum_required(VERSION 3.25)',
             'set(CMAKE_MSVC_RUNTIME_LIBRARY '+quote(p['crt'])+')',
             'project(CoreContractFixture LANGUAGES CXX)',
             'set(CMAKE_CXX_STANDARD 20)', 'set(CMAKE_CXX_STANDARD_REQUIRED ON)',
             'set(CMAKE_CXX_EXTENSIONS OFF)',
             'set(CMAKE_CXX_FLAGS '+quote(p['flags'])+')',
             'set(CMAKE_CXX_FLAGS_'+a.config.upper()+' '+quote(p['config_flags'])+')',
             'if(NOT CMAKE_CXX_COMPILER_VERSION STREQUAL '+quote(p['compiler_version'])+')',
             'message(FATAL_ERROR "actual compiler version mismatch")', 'endif()',
             'if(NOT CMAKE_CXX_COMPILER STREQUAL '+quote(p['compiler'])+')',
             'message(FATAL_ERROR "actual compiler path mismatch")', 'endif()',
             'if(NOT CMAKE_VERSION STREQUAL '+quote(p['cmake_version'])+')',
             'message(FATAL_ERROR "actual CMake version mismatch")','endif()',
             'if(NOT CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION STREQUAL '+quote(a.sdk)+')',
             'message(FATAL_ERROR "actual SDK mismatch")','endif()']
    includes = [x for x in a.includes.split('|') if x]+[str(ROOT/'tests/compile/contracts')]
    for target, item in targets().items():
        (directory/(target+'.cpp')).write_text('#include "test_support.hpp"\n'+item['body']+'\n',encoding='utf-8',newline='\n')
        lines += ['add_library('+target+' OBJECT EXCLUDE_FROM_ALL '+target+'.cpp)',
                  'target_compile_options('+target+' PRIVATE '+' '.join(quote(x) for x in p['options'].split('|') if x)+')',
                  'target_include_directories('+target+' PRIVATE '+' '.join(map(quote, includes))+')']
    (directory/'CMakeLists.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
    argv = ['cmake','-S',str(directory),'-B',str(directory/'build'),'-G',a.generator,
            '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),'-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded']
    for switch, value in (('-A',a.platform),('-T',a.toolset)):
        if value: argv += [switch,value]
    if a.sdk: argv += ['-DCMAKE_SYSTEM_VERSION='+a.sdk]
    row, _ = command('configure',argv)
    if not good(row):
        raise ValueError('fixture preparation failed; ready is not published')
    check_identity(identity,input_identity(a))
    save_json(directory/'ready.json', {'status':'Ready','identity':identity,'identity_sha256':digest(identity),
                                     'generated':generated_identity(directory),'configure':row})


def build_target(a, target, command):
    if target not in targets():
        raise ValueError('unknown compile target')
    require_ready(a)
    directory = Path(a.fixture).resolve()
    # 只删除本夹具本 target/config 的对象文件；重复运行也必须实际重编。
    for obj in (directory/'build'/(target+'.dir')/a.config).glob('*.obj'):
        if not obj.resolve().is_relative_to(directory):
            raise ValueError('object path escapes owned fixture')
        obj.unlink()
    row, raw = command(target,['cmake','--build',str(directory/'build'),'--config',a.config,
                               '--target',target,'--parallel','2','--','/nr:false'])
    check_compile(row,raw,target,positive=targets()[target]['positive'])
    require_ready(a)


def main():
    parser = argparse.ArgumentParser()
    arguments(parser)
    a = parser.parse_args()
    # 仅本进程环境；owned execute继承，不改系统或用户环境。
    os.environ['MSBUILDDISABLENODEREUSE'] = '1'
    out = Path(a.work)/('setup-'+a.run_id)
    out.mkdir(parents=True,exist_ok=False)
    commands = []
    def command(name, argv):
        paths = [out/(name+'-'+x+'.log') for x in ('stdout','stderr')]
        row = execute(argv,ROOT,*paths,180)
        row['raw'] = [{'path':p.name,'sha256':sha_file(p),'size':p.stat().st_size} for p in paths]
        commands.append(row)
        save_json(out/'commands.json',commands)
        return row,b''.join(p.read_bytes() for p in paths)
    prepare(a,command)
    print(json.dumps({'status':'Ready','fixture':a.fixture,'run_id':a.run_id,'evidence':str(out)},ensure_ascii=False))


if __name__ == '__main__':
    main()
