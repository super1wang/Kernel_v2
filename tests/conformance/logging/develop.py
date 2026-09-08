"""D1.06 Logging 局部 Debug：冻结相关源、owned Job、独立原始证据。"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import re
import sys
import uuid

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.evidence.process import execute


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--case',action='append',default=[])
    parser.add_argument('--mutation',choices=['unclipped-flush'])
    args=parser.parse_args()
    tag = uuid.uuid4().hex[:12]
    out = ROOT / 'evidence/bootstrap/D1.06' / ('logging-debug-' + tag)
    out.mkdir(parents=True, exist_ok=False)
    source = out / 'source'
    rows = []
    for folder in ('packages/foundation/include', 'packages/contracts/include',
                   'packages/runtime/include/ock/runtime', 'packages/runtime/observability',
                   'tests/conformance/logging'):
        for path in (ROOT / folder).rglob('*'):
            if not path.is_file() or '__pycache__' in path.parts:
                continue
            relative = path.relative_to(ROOT)
            dest = source / relative
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, dest)
            rows.append({'path': relative.as_posix(), 'sha256': hashlib.sha256(dest.read_bytes()).hexdigest()})
    for path in (ROOT/'cmake/LockedMSVC.cmake', ROOT/'dependencies.lock', ROOT/'docs/contracts/logging-api.md'):
        rows.append({'path': path.relative_to(ROOT).as_posix(), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
    for name in ('allocation_probe.hpp','allocation_probe.cpp'):
        path=ROOT/'tests/contract/native'/name;relative=path.relative_to(ROOT);dest=source/relative;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(path,dest)
        rows.append({'path':relative.as_posix(),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()})
    if args.mutation:
        relative='packages/runtime/observability/logging.cpp'
        target=source/relative
        original=target.read_bytes()
        old=b'std::min(evicted_,through.accepted_sequence)'
        if original.count(old)!=1:raise ValueError('mutation anchor must occur exactly once')
        target.write_bytes(original.replace(old,b'evicted_'))
        changed=hashlib.sha256(target.read_bytes()).hexdigest()
        for row in rows:
            if row['path']==relative:row['sha256']=changed
        (out/'mutation.json').write_text(json.dumps({'name':args.mutation,'scope':'isolated source snapshot only',
          'path':relative,'before_sha256':hashlib.sha256(original).hexdigest(),'after_sha256':changed},indent=2),encoding='utf-8')
    (out/'sources.json').write_text(json.dumps(rows, indent=2), encoding='utf-8')
    expected = ROOT/'build/d0.06-a/cache/sources/expected-fe3b18aecb84/include'
    units = [source/'tests/conformance/logging/logging_tests.cpp',source/'tests/contract/native/allocation_probe.cpp']
    units += list((source/'packages/runtime/observability').glob('*.cpp'))
    includes = [source/'packages/foundation/include', source/'packages/contracts/include',
                source/'packages/runtime/include', expected]
    lines = ['cmake_minimum_required(VERSION 3.25)', 'project(LoggingStage LANGUAGES CXX)',
             'enable_testing()', 'add_executable(logging_stage '+ ' '.join('"'+p.as_posix()+'"' for p in units)+')',
             'target_compile_features(logging_stage PRIVATE cxx_std_20)',
             'target_compile_options(logging_stage PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive- /Zc:nrvo-)',
             'target_include_directories(logging_stage PRIVATE '+' '.join('"'+p.as_posix()+'"' for p in includes)+')']
    cases = [f'T22.logging.{backend}.{case}' for backend in ('memory','test') for case in
             ('accept_reject_drop_accounting','format_redaction','error_isolation','flush_accepted_range','shutdown_callback_lifetime')]
    cases+=['T19.logging.memory_pages','T23.logging.fixed_write_allocation']
    if set(args.case)-set(cases):raise ValueError('unknown case')
    selected=[name for name in cases if not args.case or name in args.case]
    for name in cases:
        lines.append(f'add_test(NAME {name} COMMAND logging_stage {name})')
    (out/'CMakeLists.txt').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    commands = []
    def run(label, argv):
        record = execute(argv, ROOT, out/(label+'-stdout.log'), out/(label+'-stderr.log'), 600)
        record['raw'] = [{'path': p.name, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in
                         (out/(label+'-stdout.log'), out/(label+'-stderr.log'))]
        commands.append(record)
        (out/'commands.json').write_text(json.dumps(commands, indent=2), encoding='utf-8')
        print(label, record['status'], record['exit_code'], out, flush=True)
        return (record['status']=='Exited' and record['exit_code']==0 and
                record['process_tree']['active_after']==0 and not record['process_tree']['terminated_owned_job'])
    build = ROOT/'build'/('d1.06-logging-'+tag)
    if not run('configure',['cmake','-S',str(out),'-B',str(build),'-G','Visual Studio 17 2022',
                           '-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
                           '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake')]):return 1
    if not run('build',['cmake','--build',str(build),'--config','Debug','--parallel','2','--','/nr:false']):return 1
    if not run('list',[str(build/'Debug/logging_stage.exe'),'--list']):return 1
    discovered=(out/'list-stdout.log').read_text(encoding='utf-8').splitlines()
    if sorted(discovered)!=sorted(cases):raise ValueError('missing/duplicate/unexpected discovery')
    ok=run('ctest',['ctest','--test-dir',str(build),'-C','Debug','-R','^('+'|'.join(re.escape(n) for n in selected)+')$',
                    '--verbose','--output-on-failure','--output-junit',str(out/'junit.xml')])
    from tools.evidence.common import junit_cases
    actual=junit_cases(out/'junit.xml')
    if sorted(x['name'] for x in actual)!=sorted(selected):raise ValueError('missing/duplicate/unexpected execution')
    ok=ok and all(x['status']=='Passed' for x in actual)
    if 'T23.logging.fixed_write_allocation' in selected:
        reports=[]
        for line in (out/'ctest-stdout.log').read_text(encoding='utf-8').splitlines():
            marker=line.find('{"format":"ock.logging-allocation/1"')
            if marker>=0:reports.append(json.loads(line[marker:]))
        if len(reports)!=1:raise ValueError('missing/duplicate full allocation report in raw CTest stdout')
        report=reports[0]
        (out/'allocation-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
        ok=ok and report.get('verified') is True
        for backend in ('memory','test'):
            measured=[r for r in report['samples'] if r['backend']==backend and r['window'] in ('fixed_accepted_write','sustained_full_write')]
            ok=ok and len(measured)==80 and all(r['success'] and r['cpp']==0 and r['crt'] in (0,None) and r['asan_allocations'] in (0,None) for r in measured)
    (out/'result.json').write_text(json.dumps({'status':'Passed' if ok else 'Failed', 'scope':'local Debug Logging only',
                                               'mutation':args.mutation,'expected':selected,'discovered':discovered,'executed':actual},indent=2),encoding='utf-8')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
