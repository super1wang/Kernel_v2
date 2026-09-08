"""局部原版/夹具对照驱动；所有真实子进程复用 owned execute。"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.evidence.process import execute
from tools.evidence.common import save_json, sha_file, junit_cases


def wrapper_arguments(discovery, name):
    line = next(x for x in Path(discovery).read_text(encoding='utf-8').splitlines()
                if x.startswith('add_test(') and '[==['+name+']==]' in x)
    original = re.findall(r'\[==\[(.*?)\]==\]',line)[1:]
    result = []
    index = 0
    while index < len(original):
        value = original[index]
        index += 1
        if value in ('--fixture','--run-id'):
            # CTest运行期双引号值不在bracket列表中；已物化的bracket值也不跨run复用。
            if index < len(original) and not original[index].startswith('--'):
                index += 1
        else:
            result.append(value)
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--discovery', required=True)
    p.add_argument('--mode', choices=['baseline','fixture','red','guards','configure','ctest'], required=True)
    p.add_argument('--profile')
    p.add_argument('--build')
    p.add_argument('--baseline-snapshot')
    a = p.parse_args()
    out = ROOT/'evidence/bootstrap/tool-speedup'/('compile-'+a.mode+'-'+uuid.uuid4().hex[:12])
    out.mkdir(parents=True)
    records = []
    started = time.perf_counter()
    def run(label, argv):
        row = execute(argv, ROOT, out/(label+'-stdout.log'), out/(label+'-stderr.log'), 600)
        row['raw'] = [{'path':str(x.relative_to(out)),'sha256':sha_file(x),'size':x.stat().st_size}
                      for x in (out/(label+'-stdout.log'),out/(label+'-stderr.log'))]
        records.append(row)
        save_json(out/'commands.json', records)
        return (row['status'] == 'Exited' and row['exit_code'] == 0 and row['process_tree']['active_after'] == 0
                and not row['process_tree']['terminated_owned_job'])
    if a.mode == 'configure':
        build = ROOT/'build'/out.name
        ok = run('configure',['cmake','-S',str(ROOT),'-B',str(build),'-G','Visual Studio 17 2022','-A','x64',
                              '-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
                              '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),
                              '-DOCK_DEPENDENCY_CACHE='+str(ROOT/'build/d0.06-a/cache'),
                              '-DOCK_DEPENDENCIES_OFFLINE=ON','-DOCK_BUILD_CORE_CONTRACTS_TESTS=ON'])
        if ok: ok = run('build',['cmake','--build',str(build),'--config','Debug','--target','ock_contracts_tests','--parallel','2','--','/nr:false'])
        save_json(out/'build.json',{'build':str(build)})
    elif a.mode == 'ctest':
        ctest = ['ctest','--test-dir',str(Path(a.build).resolve()/'tests/compile/contracts'),'-C','Debug','-R',
                         '^T02[.]contracts[.]|^T05[.]contracts[.]typed_binding_fingerprint$|^T06[.]contracts[.]executor_reject_exception_cleanup$',
                         '--parallel','2']
        ok = run('discovery',[*ctest,'--show-only=json-v1'])
        if not ok: raise ValueError('CTest discovery failed; raw evidence retained')
        from importlib import import_module
        sys.path.insert(0,str(ROOT/'tests/compile/contracts'))
        fixture = import_module('fixture')
        expected = [('T05' if x == 'typed_binding_fingerprint' else 'T02')+'.contracts.'+x
                    for x in sorted({item['case'] for item in fixture.targets().values()})]
        expected += ['T06.contracts.executor_reject_exception_cleanup','T02.contracts.compile_fixture_setup','T02.contracts.compile_fixture_guards']
        discovery = json.loads((out/'discovery-stdout.log').read_text(encoding='utf-8'))
        fixture.check_execution(sorted(expected),sorted(x['name'] for x in discovery['tests']))
        setup = next(x for x in discovery['tests'] if x['name']=='T02.contracts.compile_fixture_setup')['command']
        dry_fixture = Path(setup[setup.index('--fixture')+1])
        if dry_fixture.exists(): raise ValueError('discovery unexpectedly prepared a fixture')
        ok = run('ctest',[*ctest,'--output-on-failure','--output-junit',str(out/'junit.xml')]) and ok
        rows = junit_cases(out/'junit.xml')
        fixture.check_execution(sorted(expected), sorted(x['name'] for x in rows))
        ok = ok and all(x['status']=='Passed' for x in rows)
        if dry_fixture.exists(): raise ValueError('execution reused discovery run identity')
        save_json(out/'execution-check.json', {'expected':sorted(expected),'executed':rows,
                                              'discovery_fixture_unused':str(dry_fixture)})
    elif a.mode in ('red','guards'):
        ok = run('unit', [sys.executable,'-X','utf8','-m','unittest','discover','-s',str(ROOT/'tests/tools/compile'),'-v'])
    else:
        names = ['T02.contracts.read_shape','T02.contracts.typed_value_validation']
        commands = [wrapper_arguments(a.discovery,name) for name in names]
        source = ROOT/'tests/compile/contracts/verify_children.py'
        runtime = commands[0][commands[0].index('--runtime-dir')+1]
        # 等同 CTest ENVIRONMENT_MODIFICATION，仅此测量进程及其owned子进程。
        os.environ['PATH'] = runtime+os.pathsep+os.environ.get('PATH','')
        os.environ['MSBUILDDISABLENODEREUSE'] = '1'
        save_json(out/'environment.json',{'runtime_path_prepend':runtime,'MSBUILDDISABLENODEREUSE':'1'})
        measured_source = Path(a.baseline_snapshot) if a.mode == 'baseline' and a.baseline_snapshot else source
        if a.mode == 'baseline' and not a.baseline_snapshot:
            raise ValueError('baseline requires the preserved original wrapper snapshot')
        (out/'verify_children.snapshot.py').write_bytes(measured_source.read_bytes())
        input_paths = [measured_source,Path(a.discovery),Path(commands[0][commands[0].index('--binary')+1])]
        input_paths += list((ROOT/'tests/compile/contracts').glob('*.py')) + list((ROOT/'tests/compile/contracts').glob('*.hpp'))
        input_paths += [ROOT/'dependencies.lock',ROOT/'cmake/LockedMSVC.cmake',ROOT/'cmake/msvc-validation-tools.json']
        save_json(out/'inputs.json',[{'path':str(x.resolve()),'sha256':sha_file(x)} for x in input_paths])
        ok = True
        for index, argv in enumerate(commands):
            argv[argv.index('--work')+1] = str(out/'children')
            if '--profile' in argv:
                del argv[argv.index('--profile'):argv.index('--profile')+2]
            if a.mode == 'fixture':
                argv += ['--fixture',str(out/'fixture'),'--run-id',out.name,'--profile',str(Path(a.profile).resolve())]
                if index == 0:
                    setup = [sys.executable,'-X','utf8',str(ROOT/'tests/compile/contracts/fixture.py')]
                    setup += argv[argv.index('--binary'):]
                    ok = run('setup', setup) and ok
            else:
                # 执行保留的原始字节；__file__仍指原位置，保证ROOT和test_support查找语义不变。
                code = "import sys;from pathlib import Path;s=sys.argv.pop(1);p=sys.argv.pop(1);sys.argv[0]=p;exec(compile(Path(s).read_bytes(),p,'exec'),{'__name__':'__main__','__file__':p})"
                argv = [*argv[:3],'-c',code,str(measured_source.resolve()),argv[3],*argv[4:]]
            ok = run('case-'+str(index), argv) and ok
    save_json(out/'result.json', {'mode':a.mode,'success':ok,'elapsed_seconds':time.perf_counter()-started,
                                'driver_sha256':sha_file(__file__)})
    print(json.dumps({'evidence':str(out),'success':ok},ensure_ascii=False))
    if not ok: sys.exit(1)


if __name__ == '__main__':
    main()
