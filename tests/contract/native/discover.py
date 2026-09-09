"""从实际 Native runner 发现用例；不从 expected 制造测试注册。"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    for key in ('binary', 'output', 'runtime-dir', 'includes', 'generator',
                'platform', 'toolset', 'sdk', 'config', 'work', 'root-build',
                'target-metadata', 'example'):
        parser.add_argument('--' + key, required=True)
    args = parser.parse_args()
    env = dict(os.environ)
    env['PATH'] = args.runtime_dir + os.pathsep + env.get('PATH', '')
    result = subprocess.run([args.binary, '--list'], capture_output=True,
                            check=True, env=env)
    names = result.stdout.decode('utf-8').splitlines()
    if (not names or len(names) != len(set(names)) or
            not all(re.fullmatch(r'T(?:02|03|06|23)[.]native[.][a-z_0-9]+', n)
                    for n in names)):
        raise ValueError('invalid native case discovery')
    output = Path(args.output)
    for kind in ('stdout', 'stderr'):
        (output.parent / f'native-discovery-{args.config}-{kind}.log').write_bytes(
            getattr(result, kind))
    def quote(value):
        value = str(value)
        if ']==]' in value:
            raise ValueError('unsupported CMake delimiter in argument')
        return '[==[' + value + ']==]'
    wrappers = {'private_dispatch', 'no_self_wait', 'example_consumer',
                'component_boundary', 'allocation_probe', 'allocation_steady',
                'allocation_governance', 'allocation_other_costs'}
    lines = []
    for name in names:
        argv = [args.binary, name]
        if name.split('.')[-1] in wrappers:
            argv = [sys.executable, '-X', 'utf8',
                    str(Path(__file__).with_name('verify_children.py')),
                    '--case', name]
            for key, value in vars(args).items():
                if key != 'output':
                    argv += ['--' + key.replace('_', '-'), value]
        lines.append('add_test(' + quote(name) + ' ' +
                     ' '.join(map(quote, argv)) + ')')
        label = 'B5;D3.04' if name.split('.')[-1] in {
            'managed_admission', 'managed_resources', 'managed_record',
            'managed_record_ownership', 'execution_table_ownership',
            'managed_execution_path', 'managed_execution_resource_wait',
            'execution_source_observation', 'execution_service_control', 'host_execution_lifecycle', 'host_execution_resources'} else 'D1.05'
        lines.append('set_tests_properties(' + quote(name) +
                     f' PROPERTIES TIMEOUT 600 LABELS "{label}" '
                     'ENVIRONMENT "MSBUILDDISABLENODEREUSE=1" '
                     'ENVIRONMENT_MODIFICATION ' +
                     quote('PATH=path_list_prepend:' + args.runtime_dir) + ')')
    output.write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({'actual_cases': names, 'count': len(names)}))


if __name__ == '__main__':
    main()
