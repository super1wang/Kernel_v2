"""真实 MSVC 夹具反例；仅独立控制目录，失败命令原样保留。"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import sys
import uuid
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/compile/contracts'))
import fixture
from measure import wrapper_arguments


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--discovery',required=True)
    p.add_argument('--profile',required=True)
    options = p.parse_args()
    out = ROOT/'evidence/bootstrap/tool-speedup'/('compile-controls-'+uuid.uuid4().hex[:12])
    out.mkdir(parents=True)
    original = wrapper_arguments(options.discovery,'T02.contracts.read_shape')
    arguments = original[original.index('--binary'):]
    if '--profile' in arguments:
        del arguments[arguments.index('--profile'):arguments.index('--profile')+2]
    arguments += ['--profile',str(Path(options.profile).resolve()),'--fixture',str(out/'valid'),
                  '--run-id',out.name]
    parser = argparse.ArgumentParser()
    fixture.arguments(parser)
    a = parser.parse_args(arguments)
    os.environ['PATH'] = a.runtime_dir+os.pathsep+os.environ.get('PATH','')
    os.environ['MSBUILDDISABLENODEREUSE'] = '1'
    a.work = str(out/'commands')
    records = []
    counter = 0
    def command(label,argv):
        nonlocal counter
        counter += 1
        paths = [out/(str(counter)+'-'+label+'-'+stream+'.log') for stream in ('stdout','stderr')]
        row = fixture.execute(argv,ROOT,*paths,180)
        row['label'] = label
        row['raw'] = [{'path':x.name,'sha256':fixture.sha_file(x),'size':x.stat().st_size} for x in paths]
        records.append(row)
        fixture.save_json(out/'commands.json',records)
        return row,b''.join(x.read_bytes() for x in paths)
    checks = {}
    fixed = fixture.targets()
    fixed['badpositive'] = {'case':'fixture_control','positive':True,
                                                'body':'#error intentional_positive_control_failure'}
    positive = next(key for key,item in fixed.items() if item['case']=='read_shape' and item['positive'])
    negative = next(key for key,item in fixed.items() if item['case']=='read_shape' and not item['positive'])
    with patch.object(fixture,'targets',return_value=fixed):
        fixture.prepare(a,command)
        fixture.build_target(a,positive,command)
        # 同一 target 再次运行：对象被限定删除，原始日志必须再次包含实际 .cpp 编译。
        fixture.build_target(a,positive,command)
        checks['positive_recompiled_twice'] = True
        try:
            fixture.build_target(a,'badpositive',command)
        except ValueError as exc:
            checks['actual_positive_failure_rejected'] = 'positive control failed' in str(exc)
        else:
            checks['actual_positive_failure_rejected'] = False
        row,raw = command('bad-toolset',['cmake','--build',str(Path(a.fixture)/'build'),'--config',a.config,
                          '--target',negative,'--','/nr:false','/p:PlatformToolset=fixture_missing_toolset'])
        if row['status'] != 'Exited' or row['exit_code'] == 0 or b'MSB8020' not in raw:
            raise ValueError('actual missing-toolset control did not produce expected infrastructure error')
        try:
            fixture.check_compile(row,raw,negative,positive=False)
        except ValueError:
            checks['actual_toolchain_failure_not_contract_rejection'] = True
        else:
            checks['actual_toolchain_failure_not_contract_rejection'] = False
        valid = Path(a.fixture)
        actual_objects = list((valid/'build'/(positive+'.dir')/a.config).glob('*.obj'))
        if not actual_objects: raise ValueError('positive did not leave an actual object')
        try:
            fixture.prepare(a,command)
        except FileExistsError:
            checks['existing_run_not_reprepared'] = True
        a.fixture = str(out/'failed')
        a.run_id += '-failed'
        profile = Path(a.profile).read_text(encoding='utf-8')
        profile = re.sub(r'^toolset=.*$', 'toolset=v999',profile,flags=re.MULTILINE)
        bad_profile = out/'bad-profile.txt'
        bad_profile.write_text(profile,encoding='utf-8')
        a.profile = str(bad_profile)
        a.toolset = 'v999'
        try:
            fixture.prepare(a,command)
        except ValueError as exc:
            checks['actual_prepare_failure_no_ready'] = 'preparation failed' in str(exc)
        else:
            checks['actual_prepare_failure_no_ready'] = False
        destination = Path(a.fixture)/'build/old-positive.obj'
        shutil.copyfile(actual_objects[0],destination)
        try:
            fixture.require_ready(a)
        except ValueError:
            checks['failed_prepare_actual_old_object_cannot_pass'] = True
        else:
            checks['failed_prepare_actual_old_object_cannot_pass'] = False
    fixture.save_json(out/'checks.json',checks)
    print(json.dumps({'evidence':str(out),'checks':checks},ensure_ascii=False))
    if len(checks) != 6 or not all(checks.values()):
        raise ValueError('real controls incomplete or failed')


if __name__ == '__main__':
    main()
