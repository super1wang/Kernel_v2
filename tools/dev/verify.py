"""L0/L1显式影响集；只报告开发事实，不写正式报告或包级Passed。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import save_json, read_json, sha_file, junit_cases, under
from tools.evidence.build_identity import observe
from tools.evidence.process import execute

PYTHON_GROUPS = {
    'dev-tools': ('tests/tools/dev', 'test_*.py'),
    'evidence-tools': ('tests/tools/evidence', 'test_*.py'),
    'compile-tools': ('tests/tools/compile', 'test_*.py'),
}
FAMILIES = {'foundation': '.foundation.', 'contracts': '.contracts.',
            'registry': '.registration.', 'policy': '.policy.', 'native': '.native.',
            'sdk': '.sdk.'}


def select(paths):
    groups, risks, reasons, unknown = set(), set(), set(), []
    for raw in sorted(set(paths)):
        p = raw.replace('\\', '/')
        if p == 'tests/compile/contracts/test_support.hpp' or p.startswith('tests/conformance/core_contracts/'):
            groups.update(('contracts', 'registry', 'policy', 'native'))
        if p.startswith(('packages/', 'tests/contract/', 'tests/compile/', 'tests/conformance/core_contracts/')) and p.endswith(('.h', '.hpp')):
            risks.add('release')
        if p.startswith(('build/', 'evidence/')):
            continue
        if p.startswith(('tools/dev/', 'tests/tools/dev/')):
            groups.add('dev-tools')
        elif p.startswith(('tools/evidence/', 'tests/tools/evidence/')):
            groups.add('evidence-tools')
            reasons.add('证据采集或结果映射变化：验证证据工具影响集')
        elif p.startswith('tests/tools/compile/'):
            groups.add('compile-tools')
        elif p.startswith('tests/compile/contracts/'):
            groups.update(('contracts', 'compile-tools'))
            reasons.add('编译夹具或测试注册变化：验证编译合同及夹具反例')
        elif p.startswith('tests/conformance/core_contracts/'):
            groups.update(('contracts', 'registry', 'policy', 'native'))
        elif p.startswith('packages/foundation/'):
            groups.update(FAMILIES)
            risks.update(('release', 'asan'))
            reasons.add('公共Foundation合同影响全部Native消费者')
        elif p.startswith('packages/contracts/'):
            groups.update(('contracts', 'registry', 'policy', 'native', 'sdk'))
            risks.update(('release', 'asan'))
            reasons.add('公共CoreContracts影响直接及传递消费者')
        elif p.startswith(('packages/runtime/registry/', 'tests/contract/registration/')):
            groups.update(('registry', 'native'))
        elif p.startswith(('packages/runtime/policy/', 'tests/contract/authorization/')):
            groups.update(('policy', 'native'))
        elif p.startswith(('packages/runtime/invocation/', 'tests/contract/native/', 'examples/native_service/')):
            groups.add('native')
        elif p.startswith('tests/unit/foundation/'):
            groups.add('foundation')
        elif p in ('CMakeLists.txt', 'CMakePresets.json', 'dependencies.lock') or p.startswith(
                ('cmake/', 'sdk/', 'tests/install_consumer/', 'tests/architecture/',
                 'tests/manifests/', 'tests/runs/', 'schemas/')):
            groups.add('full-package')
            risks.update(('release', 'asan'))
            reasons.add('构建/公开安装/固定集合/Schema变化：当前包全量')
        elif p in ('AGENTS.md', 'docs/progress.md') or p.startswith(
                ('docs/reviews/', 'docs/validation/')):
            # 流程/索引仍需人工或AI差异复核，不生成测试成功结论。
            continue
        else:
            unknown.append(p)
            groups.add('full-package')
            reasons.add('显式映射缺失：当前包全量；未冻结集合时拒绝运行')
    return {'groups': sorted(groups), 'risks': sorted(risks),
            'full_reasons': sorted(reasons), 'unknown': unknown}


def build_command(spec, directory):
    return [x.replace(spec['build_dir'], directory) for x in spec['build']]


def capture_build(root, spec, directory, configure, build):
    actual = dict(spec, build_dir=directory, configure=configure, build=build)
    if spec.get('toolchain_artifact'):
        actual['toolchain_artifact'] = spec['toolchain_artifact'].replace(spec['build_dir'], directory)
    read = lambda name: under(root, name).read_bytes()
    identity, errors = observe(actual, root, read, read)
    if errors:
        raise ValueError('; '.join(errors))
    binaries = []
    for name in spec['build_outputs']:
        if not name.endswith('.exe'):
            continue
        relative = name.replace(spec['build_dir'], directory)
        path = under(root, relative)
        if path.is_file():
            binaries.append({'path':relative, 'sha256':sha_file(path), 'size':path.stat().st_size})
    if not binaries:
        raise ValueError('没有实际构建可执行文件，禁止依赖旧运行声明')
    return {'identity':identity, 'binaries':binaries}


def package_cases(root, package, profile):
    path = root/'tests/manifests'/f'{package.lower()}.expected.json'
    if not path.is_file():
        raise ValueError(f'{package}尚无固定expected，不能用旧包成功代替当前包覆盖')
    rows = read_json(path)['cases']
    names = [c['id'] for c in rows if 'profiles' not in c or profile in c['profiles']]
    if not names or len(names) != len(set(names)):
        raise ValueError('固定集合为空或重复')
    return names


def exact_regex(names):
    if not names:
        raise ValueError('禁止空CTest集合')
    return '^(' + '|'.join(re.escape(n) for n in sorted(names)) + ')$'


def check_executed(names, rows):
    actual = [r['name'] for r in rows]
    if sorted(actual) != sorted(names) or any(r['status'] != 'Passed' for r in rows):
        raise ValueError('实际执行缺项、多项、重复、失败或跳过')


def changed_paths(root, base):
    # --no-renames把移动两端均作为输入；HEAD基线包含暂存和未暂存。
    def git(*args):
        return subprocess.check_output(['git', *args], cwd=root).decode('utf-8').split('\0')
    return sorted(set(filter(None, git('diff', '--no-renames', '--name-only', '-z', base, '--') +
                             git('ls-files', '--others', '--exclude-standard', '-z'))))


def source_digest(root):
    # 开发快照仅用于检测运行中改动，不替代正式source-inputs。
    names = subprocess.check_output(['git', 'ls-files', '-co', '--exclude-standard', '-z'], cwd=root)
    rows = []
    for name in sorted(set(filter(None, names.decode('utf-8').split('\0')))):
        if name.startswith(('build/', 'evidence/', '.git/')) or '__pycache__' in name:
            continue
        p = root/name
        if p.is_file():
            rows.append((name, sha_file(p)))
    return hashlib.sha256(json.dumps(rows, ensure_ascii=False).encode('utf-8')).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', choices=[f'D1.0{i}' for i in range(1, 7)])
    parser.add_argument('--changed', action='store_true')
    parser.add_argument('--base', default='HEAD')
    parser.add_argument('--paths', nargs='+')
    parser.add_argument('--risk', choices=('asan', 'release'), action='append', default=[])
    parser.add_argument('--full', action='store_true')
    parser.add_argument('--reason')
    parser.add_argument('--plan', action='store_true', help='仅输出选择，不运行或声明通过')
    a = parser.parse_args()
    if a.full and not a.reason:
        parser.error('--full必须给出--reason，说明升级条件')
    if a.paths and a.changed:
        parser.error('--paths与--changed二选一')
    paths = a.paths if a.paths else changed_paths(ROOT, a.base)
    selection = select(paths)
    if a.full:
        selection['groups'] = sorted(set(selection['groups']) | {'full-package'})
        selection['full_reasons'].append(a.reason)
    selection.update(package=a.package, paths=paths,
                     profiles=['debug'] + sorted(set(a.risk) | set(selection['risks'])))
    if a.plan:
        print(json.dumps(selection, ensure_ascii=False, indent=2))
        return 0
    if not selection['groups']:
        print(json.dumps({'status': 'NoRelevantChanges', 'selection': selection}, ensure_ascii=False))
        return 0
    out = ROOT/'evidence/bootstrap/D1.06'/('dev-fast-' + uuid.uuid4().hex[:12])
    out.mkdir(parents=True, exist_ok=False)
    result = {'format': 'ock.dev-feedback/1', 'kind': 'development-only',
              'status': 'Failed', 'selection': selection, 'commands': []}
    before = source_digest(ROOT)
    result['source_digest_before'] = before

    def run(label, argv):
        stdout, stderr = out/(label+'-stdout.log'), out/(label+'-stderr.log')
        row = execute(argv, ROOT, stdout, stderr, 1800)
        row['raw'] = [{'path': p.name, 'sha256': sha_file(p)} for p in (stdout, stderr)]
        result['commands'].append(row)
        save_json(out/'result.json', result)
        if (row['status'] != 'Exited' or row['exit_code'] != 0 or
                row['process_tree']['active_after'] != 0 or row['process_tree']['terminated_owned_job']):
            raise ValueError(f'{label}失败；停止后续验证')
        return stdout.read_bytes() + stderr.read_bytes()

    try:
        if 'full-package' in selection['groups']:
            for profile in selection['profiles']:
                package_cases(ROOT, a.package, 'win-msvc-'+profile)
        for group in selection['groups']:
            if group not in PYTHON_GROUPS:
                continue
            directory, pattern = PYTHON_GROUPS[group]
            if not list((ROOT/directory).glob(pattern)):
                raise ValueError(f'{group}测试尚未实现，不能空跑')
            raw = run(group, [sys.executable, '-X', 'utf8', '-m', 'unittest', 'discover',
                              '-s', directory, '-p', pattern, '-v', '-f'])
            counts = re.findall(rb'Ran (\d+) tests? in ', raw)
            if not counts or int(counts[-1]) == 0 or re.search(rb'skipped=|expected failures=', raw):
                raise ValueError('Python工具测试空跑、跳过或预期失败')
        native_groups = set(selection['groups']) - set(PYTHON_GROUPS)
        if native_groups:
            for profile in selection['profiles']:
                # 当前D1.06 P0复用已实现构建配置，绝不以D1.05 expected替代D1.06 full。
                spec = read_json(ROOT/f'tests/runs/d1.05-win-msvc-{profile}.json')
                if 'full-package' in native_groups:
                    names = package_cases(ROOT, a.package, spec['profile'])
                else:
                    known = package_cases(ROOT, 'D1.05', spec['profile'])
                    for group in native_groups:
                        if not any(FAMILIES[group] in n for n in known):
                            raise ValueError(f'{group}映射没有固定用例，拒绝少跑')
                    names = [n for n in known if any(FAMILIES[g] in n for g in native_groups)]
                if 'contracts' in native_groups or 'full-package' in native_groups:
                    support = read_json(ROOT/'tests/manifests/d1.06-tools.expected.json')
                    names = sorted(set(names) | {c['id'] for c in support['cases']})
                regex = exact_regex(names)
                build = f'build/dev-fast/{profile}'
                configure = [x.replace(spec['build_dir'], build) for x in spec['configure'] if x != '--fresh']
                run(profile+'-configure', configure)
                build_argv = build_command(spec, build)
                run(profile+'-build', build_argv)
                result.setdefault('builds', {})[profile] = capture_build(ROOT, spec, build, configure, build_argv)
                junit = out/(profile+'-junit.xml')
                run(profile+'-ctest', ['ctest', '--test-dir', build, '-C', spec['configuration'],
                    '-R', regex, '--stop-on-failure', '--output-on-failure', '--no-tests=error',
                    '--output-junit', str(junit)])
                check_executed(names, junit_cases(junit))
        result['source_digest_after'] = source_digest(ROOT)
        if result['source_digest_after'] != before:
            raise ValueError('运行期间源码变化，开发结果无效；不拼接结果')
        result['status'] = 'DevelopmentChecksPassed'
    except Exception as error:
        result['error'] = str(error)
    finally:
        save_json(out/'result.json', result)
    print(json.dumps({'status': result['status'], 'evidence': str(out),
                      'error': result.get('error')}, ensure_ascii=False))
    return 0 if result['status'] == 'DevelopmentChecksPassed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
