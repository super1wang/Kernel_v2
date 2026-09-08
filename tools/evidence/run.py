"""D0.06 正式开发证据入口：实际启动配置/构建/发现/逐轮执行。"""
import math
import argparse
from pathlib import Path
import platform
import os
import re
import shutil
import subprocess
import sys
import uuid
import zipfile
import xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import now, sha_file, digest, read_json, save_json, under, inputs, expected_cases, junit_cases, runtime_path_allowed
from tools.evidence.process import execute


COLLECTOR_PATHS = [*sorted((ROOT / 'tools/evidence').glob('*.py')), ROOT / 'schemas/evidence-v1.schema.json']
LOADED_COLLECTOR = [{'path': str(path), 'sha256': sha_file(path)} for path in COLLECTOR_PATHS]

def run(root, manifest_path):
    if LOADED_COLLECTOR != [{'path': str(path), 'sha256': sha_file(path)} for path in COLLECTOR_PATHS]:
        raise ValueError('collector sources changed after process initialization; start a fresh process')
    root = Path(root).resolve(); manifest_path = Path(manifest_path).resolve()
    relative = manifest_path.relative_to(root).as_posix()
    spec = read_json(manifest_path)
    if spec['format'] != 'ock.run-manifest/1' or not re.fullmatch(r'D\d+\.\d{2}', spec['task_id']):
        raise ValueError('unsupported task/manifest')
    if not re.fullmatch(r'[a-zA-Z0-9_.-]+', spec['profile']):
        raise ValueError('invalid profile')
    if type(spec['repeat']) is not int or not 1 <= spec['repeat'] <= 100:
        raise ValueError('repeat must be 1..100')
    if type(spec['timeout_seconds']) not in (int,float) or not math.isfinite(spec['timeout_seconds']) or not 0 < spec['timeout_seconds'] <= 3600:
        raise ValueError('command timeout must be finite, <=3600 seconds')
    build_dir = under(root, spec['build_dir'])
    if not build_dir.is_relative_to(root / 'build'):
        raise ValueError('build must remain in workspace build directory')
    if not spec['source_patterns'] or not spec['build_outputs']:
        raise ValueError('source patterns and build outputs cannot be empty')
    for field in ('configure', 'build'):
        if not spec[field] or Path(spec[field][0]).stem.lower() != 'cmake':
            raise ValueError('configure/build must invoke real cmake')
    for name in spec['build_outputs']:
        if not under(root, name).is_relative_to(root / 'build'):
            raise ValueError('build output outside workspace build')
    expected = read_json(under(root, spec['expected_manifest']))
    if expected['task_id'] != spec['task_id']:
        raise ValueError('expected task mismatch')
    required = expected_cases(expected, spec['profile'])
    source_inputs = inputs(root, spec, relative)
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=root))
    run_id = now().replace('-', '').replace(':', '').split('.')[0] + 'Z-' + uuid.uuid4().hex[:12]
    source_id = commit[:12] + '-' + digest(source_inputs)[:12]
    out = root / 'evidence' / source_id / spec['profile'] / spec['task_id'] / run_id
    out.mkdir(parents=True, exist_ok=False)
    save_json(out / 'manifest.json', spec); save_json(out / 'expected.json', expected)
    save_json(out / 'source-inputs.json', source_inputs)
    with zipfile.ZipFile(out / 'source-inputs.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for row in source_inputs:
            archive.write(root / row['path'], row['path'])
    fixture_root = ROOT / 'build/evidence-fixtures'
    prior_fixtures = set(fixture_root.glob('*')) if fixture_root.exists() else set()
    prior_runtime = {p.resolve() for pattern in spec.get('runtime_artifact_patterns',[]) for p in root.glob(pattern) if p.is_file()}
    report = {'evidence_format': 'ock.evidence/1', 'task_id': spec['task_id'], 'run_id': run_id,
      'started_at': now(), 'source': {'commit': commit, 'dirty': dirty, 'root': str(root),
      'inputs': source_inputs, 'build_inputs_sha256': digest(source_inputs), 'archive': 'source-inputs.zip',
      'archive_sha256': sha_file(out / 'source-inputs.zip')},
      'manifest': {'path': relative, 'sha256': sha_file(manifest_path), 'snapshot': 'manifest.json'},
      'requirements_manifest_sha256': sha_file(under(root, spec['expected_manifest'])),
      'expected_snapshot': 'expected.json', 'build': {'profile': spec['profile'], 'configuration': spec['configuration'],
      'preset': spec['preset'], 'directory': spec['build_dir'], 'dependency_lock': spec['dependency_lock'],
      'dependency_lock_sha256': sha_file(under(root, spec['dependency_lock'])), 'artifacts': [], 'test_executables': []},
      'environment': {'python': platform.python_version(), 'platform': platform.platform(),
      'public_subset': {name: os.environ[name] for name in ('PROCESSOR_ARCHITECTURE', 'NUMBER_OF_PROCESSORS', 'VSCMD_VER', 'VCToolsVersion', 'WindowsSDKVersion', 'LANG') if name in os.environ}},
      'collector': [dict(row) for row in LOADED_COLLECTOR],
      'conformance': None, 'runtime_artifacts': [], 'commands': [], 'tests': {'expected': [case['id'] for case in required], 'discovered': [], 'rounds': []},
      'checks': [], 'review': {'required': spec.get('review_required', ['human']), 'records': []}, 'collection_issues': [], 'errors': [],
      'automated_status': 'NotRun', 'package_status': 'InProgress'}

    def command(identifier, role, argv, timeout=None):
        if argv[0] == 'python':
            argv = [sys.executable, '-X', 'utf8', *argv[1:]]
        index = len(report['commands']) + 1
        stdout = out / f'{index:03}-{identifier}-stdout.log'
        stderr = out / f'{index:03}-{identifier}-stderr.log'
        item = execute(argv, root, stdout, stderr, timeout or spec['timeout_seconds'])
        item.update(id=identifier, role=role, raw=[])
        for path in (stdout, stderr):
            item['raw'].append({'path': path.name, 'sha256': sha_file(path), 'size': path.stat().st_size,
                                'truncated': False, 'redacted': False})
        if 'executable' in item:
            item['executable_sha256'] = sha_file(item['executable'])
        report['commands'].append(item)
        return item

    def succeeded(item):
        return item['status'] == 'Exited' and item['exit_code'] == 0 and item['process_tree']['active_after'] == 0

    for name in ('cmake', 'ctest'):
        command(name + '-version', 'metadata', [name, '--version'], 10)
    good = succeeded(command('configure', 'configure', spec['configure']))
    if good:
        good = succeeded(command('build', 'build', spec['build']))
    if good:
        for name in spec['build_outputs']:
            path = under(root, name)
            if not path.is_file():
                report['collection_issues'].append('missing build artifact: ' + name)
                good = False; continue
            report['build']['artifacts'].append({'path': name, 'sha256': sha_file(path), 'size': path.stat().st_size})
        with zipfile.ZipFile(out / 'build-artifacts.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
            for row in report['build']['artifacts']:
                archive.write(root / row['path'], row['path'])
        report['build']['archive'] = 'build-artifacts.zip'
        report['build']['archive_sha256'] = sha_file(out / 'build-artifacts.zip')
        try:
            from tools.evidence.build_identity import observe
            observed, issues = observe(spec, root, lambda name: under(root,name).read_bytes(), lambda name: under(root,name).read_bytes())
            report['build']['observed'] = observed
            report['collection_issues'].extend(issues)
        except (OSError,ValueError,KeyError) as error:
            report['collection_issues'].append('missing build identity: '+str(error))
    if good:
        for check in spec['checks']:
            repeats = check.get('repeat', 1)
            if type(repeats) is not int or not 1 <= repeats <= 100:
                raise ValueError('invalid check repeats')
            for iteration in range(1, repeats + 1):
                identifier = f'check-{len(report["checks"]):03}'
                item = command(identifier, 'check', check['argv'], check.get('timeout_seconds'))
                report['checks'].append({'id': check['id'], 'round': iteration, 'command_id': identifier,
                                         'status': 'Passed' if succeeded(item) else 'Failed'})
                if check['id'] == 'CHECK.conformance.bootstrap' and succeeded(item):
                    report['conformance'] = read_json(out / item['raw'][0]['path'])
        common = ['ctest', '--test-dir', str(build_dir), '-C', spec['configuration'], '-R', spec['test_regex']]
        discovery = command('discover', 'discover', [*common, '--show-only=json-v1'])
        if succeeded(discovery):
            try:
                found = read_json(out / discovery['raw'][0]['path'])
                save_json(out / 'discovered.json', found)
                report['tests']['discovered'] = [test['name'] for test in found['tests']]
                binaries = set()
                for test in found['tests']:
                    binary = Path(test['command'][0])
                    if not binary.is_absolute():
                        value = shutil.which(str(binary)); binary = Path(value) if value else build_dir / binary
                    if not binary.is_file():
                        raise ValueError('missing discovered test executable: ' + str(binary))
                    binaries.add(binary.resolve())
                report['build']['test_executables'] = [{'path': str(path), 'sha256': sha_file(path)} for path in sorted(binaries)]
                names = report['tests']['discovered']
                good = bool(names) and len(names) == len(set(names)) and set(names) == set(report['tests']['expected'])
            except (ValueError, KeyError, OSError) as exc:
                report['collection_issues'].append(str(exc)); good = False
        else:
            good = False
        if good:
            for iteration in range(1, spec['repeat'] + 1):
                junit = out / f'round-{iteration:03}-junit.xml'
                item = command(f'test-{iteration:03}', 'test', [*common, '--no-tests=error', '--output-on-failure', '--output-junit', str(junit)])
                row = {'round': iteration, 'command_id': item['id'], 'executed': [], 'junit': {'path': junit.name}}
                if junit.is_file():
                    row['junit'].update(sha256=sha_file(junit), size=junit.stat().st_size)
                    try:
                        row['executed'] = junit_cases(junit)
                    except (ValueError, KeyError, OSError, ET.ParseError) as exc:
                        report['collection_issues'].append('JUnit parse error: ' + str(exc))
                else:
                    report['collection_issues'].append('missing current round JUnit')
                report['tests']['rounds'].append(row)
    for name in spec.get('review_records', []):
        path = under(root, name)
        if path.is_file():
            report['review']['records'].append({'path': name, 'sha256': sha_file(path), 'record': read_json(path)})
    if 'review_policy' in spec:
        attachment = out / 'review-records.zip'
        with zipfile.ZipFile(attachment, 'w', zipfile.ZIP_DEFLATED) as archive:
            for row in report['review']['records']:
                archive.write(under(root, row['path']), row['path'])
        report['review']['archive'] = {'path': attachment.name, 'sha256': sha_file(attachment)}
    # 内层故意失败的自测仍保留原始运行ZIP；故障注入后的副本单独随夹具保存。
    new_fixtures = sorted(set(fixture_root.glob('*')) - prior_fixtures) if fixture_root.exists() else []
    if new_fixtures:
        attachment = out / 'selftest-fixtures.zip'
        with zipfile.ZipFile(attachment, 'w', zipfile.ZIP_DEFLATED) as archive:
            for fixture in new_fixtures:
                for file in sorted(fixture.rglob('*')):
                    if file.is_file() and (file.is_relative_to(fixture / 'evidence') or file.is_relative_to(fixture / 'original-runs')):
                        archive.write(file, file.relative_to(fixture_root).as_posix())
        report['selftest_fixtures'] = {'path': attachment.name, 'sha256': sha_file(attachment), 'count': len(new_fixtures)}
    runtime = sorted({p.resolve() for pattern in spec.get('runtime_artifact_patterns',[]) for p in root.glob(pattern) if p.is_file()} - prior_runtime)
    if runtime:
        attachment = out / 'runtime-artifacts.zip'
        with zipfile.ZipFile(attachment,'w',zipfile.ZIP_DEFLATED) as archive:
            for file in runtime:
                name=file.relative_to(root).as_posix()
                under(root,name)
                if not runtime_path_allowed(root,name): raise ValueError('runtime artifact outside isolated child output roots')
                archive.write(file,name)
                report['runtime_artifacts'].append({'path':name,'sha256':sha_file(file),'size':file.stat().st_size})
        report['runtime_archive']={'path':attachment.name,'sha256':sha_file(attachment)}
    report['finished_at'] = now()
    path = out / 'report.json'
    save_json(path, report)
    from tools.evidence.validate import audit
    errors, package = audit(path, root, check_claims=False)
    report['errors'] = errors
    report['automated_status'] = 'Failed' if errors else 'Passed'
    report['package_status'] = 'Failed' if errors else package
    save_json(path, report)
    # 快读摘要只是导航；生成失败不改写已完成的原报告及门禁结论。
    try:
        from tools.evidence.summary import write_summary
        write_summary(path)
    except (ValueError, ImportError, OSError) as error:
        save_json(out / 'summary-error.json', {'format': 'ock.evidence-summary-error/1',
                  'report': path.name, 'report_sha256': sha_file(path), 'error': str(error)})
        print('summary generation failed: ' + str(error), file=sys.stderr, flush=True)
    print(f'{spec["task_id"]} {spec["profile"]}: {report["automated_status"]}; {path.relative_to(root).as_posix()}', flush=True)
    return path


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('manifest', type=Path)
    args = parser.parse_args(); path = run(ROOT, args.manifest.resolve())
    return read_json(path)['automated_status'] != 'Passed' or (path.parent / 'summary-error.json').is_file()

if __name__ == '__main__':
    sys.exit(main())
