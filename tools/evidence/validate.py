"""独立重算证据：不信任报告里的 Passed、汇总数量或人工状态。"""
import os
import argparse
import json
import shutil
from pathlib import Path
import sys
import zipfile
import xml.etree.ElementTree as ET
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'build/python-deps'))
from jsonschema import Draft202012Validator
from functools import lru_cache
from tools.evidence.common import read_json, sha_file, sha_bytes, digest, under, inputs, expected_cases, junit_cases


@lru_cache(maxsize=1)
def evidence_schema():
    schema = read_json(ROOT / 'schemas/evidence-v1.schema.json')
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema)


def audit(report_path, root=None, check_claims=True):
    report_path = Path(report_path).resolve(); folder = report_path.parent
    errors = []
    def require(condition, message):
        if not condition:
            errors.append(message)
    try:
        r = read_json(report_path)
        schema_errors = list(evidence_schema().iter_errors(r))
        if schema_errors:
            return ['evidence schema: ' + error.message for error in schema_errors], 'Failed'
        spec = read_json(under(folder, r['manifest']['snapshot']))
        expected = read_json(under(folder, r['expected_snapshot']))
        current = root is not None
        source_root = Path(root if current else r['source']['root']).resolve()
        if 'selftest_fixtures' in r:
            require(sha_file(under(folder, r['selftest_fixtures']['path'])) == r['selftest_fixtures']['sha256'], 'selftest fixture archive changed')
        runtime_paths=[]
        for row in r['runtime_artifacts']:
            value=row['path'];resolved=under(source_root,value)
            require(value==Path(value).as_posix() and resolved.is_relative_to(source_root/'build'),'runtime path must be canonical and under build')
            require(any(Path(value).match(pattern) for pattern in spec.get('runtime_artifact_patterns',[])),'runtime artifact outside declared patterns')
            runtime_paths.append(os.path.normcase(str(resolved)))
        require(len(runtime_paths)==len(set(runtime_paths)),'duplicate runtime artifact path')
        if r['runtime_artifacts']:
            attachment=r['runtime_archive']
            require(sha_file(under(folder,attachment['path']))==attachment['sha256'],'runtime archive changed')
            with zipfile.ZipFile(folder/attachment['path']) as archive:
                require(set(archive.namelist())=={row['path'] for row in r['runtime_artifacts']} and len(archive.namelist())==len(runtime_paths),'runtime archive contents differ or contain duplicate entries')
                for row in r['runtime_artifacts']:
                    raw=archive.read(row['path'])
                    require(sha_bytes(raw)==row['sha256'] and len(raw)==row['size'],'runtime child artifact changed')
        for requirement in spec.get('required_runtime_artifacts',[]):
            require(len({os.path.normcase(str(under(source_root,row['path']))) for row in r['runtime_artifacts'] if Path(row['path']).match(requirement['pattern'])})>=requirement['minimum'],'required runtime child artifacts missing')
        require(r['evidence_format'] == 'ock.evidence/1', 'unsupported evidence format')
        require(r['task_id'] == spec['task_id'] == expected['task_id'], 'task identity mismatch')
        require(r['run_id'] == folder.name, 'run identity mismatch')
        require(folder.parent.name == r['task_id'] and folder.parent.parent.name == spec['profile'], 'profile/task directory mismatch')
        require(type(r['source']['dirty']) is bool, 'dirty identity missing')
        require(len(r['source']['commit']) == 40, 'commit identity missing')
        require(digest(r['source']['inputs']) == r['source']['build_inputs_sha256'], 'source manifest fingerprint mismatch')
        require(folder.parents[2].name == r['source']['commit'][:12] + '-' + r['source']['build_inputs_sha256'][:12], 'source directory mismatch')
        with zipfile.ZipFile(under(folder, r['source']['archive'])) as archive:
            require(sha_file(folder / r['source']['archive']) == r['source']['archive_sha256'], 'source archive hash mismatch')
            rows = r['source']['inputs']; require(bool(rows), 'empty source snapshot')
            require(set(archive.namelist()) == {row['path'] for row in rows} and len(archive.namelist()) == len(rows) == len({row['path'] for row in rows}), 'source archive entries mismatch or duplicate')
            for row in rows:
                data = archive.read(row['path'])
                require(sha_bytes(data) == row['sha256'] and len(data) == row['size'], 'source archive entry changed: ' + row['path'])
            require(sha_bytes(archive.read(r['manifest']['path'])) == r['manifest']['sha256'], 'manifest source hash mismatch')
            require(json.loads(archive.read(r['manifest']['path'])) == spec, 'manifest snapshot differs from source')
            require(sha_bytes(archive.read(spec['expected_manifest'])) == r['requirements_manifest_sha256'], 'expected source hash mismatch')
            require(json.loads(archive.read(spec['expected_manifest'])) == expected, 'expected snapshot differs from source')
            require(sha_bytes(archive.read(spec['dependency_lock'])) == r['build']['dependency_lock_sha256'], 'dependency lock hash mismatch')
            for name in spec['required_artifacts']:
                require(name in archive.namelist(), 'required artifact missing from snapshot: ' + name)
        if current:
            require(str(source_root) == r['source']['root'], 'current workspace path mismatch')
            require(inputs(source_root, spec, r['manifest']['path']) == r['source']['inputs'], 'current source set or fingerprint differs')
            for row in r['collector']:
                require(sha_file(row['path']) == row['sha256'], 'collector changed: ' + row['path'])
        require(r['build']['profile'] == spec['profile'] and r['build']['configuration'] == spec['configuration'] and r['build']['preset'] == spec['preset'], 'build profile mismatch')
        require(r['build']['directory'] == spec['build_dir'] and r['build']['dependency_lock'] == spec['dependency_lock'], 'build or dependency identity mismatch')
        cases = expected_cases(expected, spec['profile']); names = [case['id'] for case in cases]
        require(r['tests']['expected'] == names, 'recorded expected differs from independent manifest')
        commands = r['commands']; by_id = {c['id']: c for c in commands}
        require(len(commands) == len(by_id), 'duplicate command ids')
        for c in commands:
            require(c['status'] == 'Exited' and c['exit_code'] == 0, 'command failed: ' + c['id'])
            require(c.get('observed_exit_code') == c['exit_code'], 'command exit observation mismatch: ' + c['id'])
            tree = c['process_tree']
            require(tree['assigned_before_resume'] and tree['active_after'] == 0 and not tree['terminated_owned_job'], 'command tree did not finish normally: ' + c['id'])
            require(c['cwd'] == str(source_root), 'command cwd mismatch: ' + c['id'])
            require(c['started_at'] <= c['finished_at'], 'command time order invalid')
            require(len(c['raw']) == 2, 'missing raw streams: ' + c['id'])
            for raw in c['raw']:
                path = under(folder, raw['path'])
                require(path.is_file(), 'missing raw file: ' + raw['path'])
                if path.is_file():
                    require(sha_file(path) == raw['sha256'] and path.stat().st_size == raw['size'], 'raw file fingerprint mismatch: ' + raw['path'])
                require(raw['truncated'] is False and raw['redacted'] is False, 'raw stream incomplete: ' + raw['path'])
            if current and c.get('executable'):
                require(sha_file(c['executable']) == c['executable_sha256'], 'command executable changed')
        for field in ('configure', 'build'):
            require(field in by_id, 'missing ' + field + ' command')
            if field in by_id:
                require(by_id[field]['argv'] == spec[field], field + ' argv mismatch')
                require(by_id[field]['role'] == field, field + ' role mismatch')
        for name in ('cmake', 'ctest'):
            require(name + '-version' in by_id and by_id[name + '-version']['argv'] == [name, '--version'], 'tool version command missing')
        artifacts = r['build']['artifacts']
        require({row['path'] for row in artifacts} == set(spec['build_outputs']) and len(artifacts) == len(spec['build_outputs']), 'build artifacts missing/duplicated')
        if 'archive' in r['build']:
            require(sha_file(under(folder, r['build']['archive'])) == r['build']['archive_sha256'], 'build archive fingerprint mismatch')
            with zipfile.ZipFile(folder / r['build']['archive']) as archive, zipfile.ZipFile(folder / r['source']['archive']) as source_archive:
                from tools.evidence.build_identity import observe
                observed, identity_errors = observe(spec, source_root, archive.read, source_archive.read)
                errors.extend(identity_errors)
                require(r['build'].get('observed') == observed, 'recorded build identity differs from actual artifacts')
                require(set(archive.namelist()) == set(spec['build_outputs']) and len(archive.namelist()) == len(spec['build_outputs']), 'build archive contents mismatch or duplicate')
                for row in artifacts:
                    data = archive.read(row['path'])
                    require(sha_bytes(data) == row['sha256'] and len(data) == row['size'], 'captured build artifact changed')
        else:
            errors.append('build archive missing')
        if current:
            for row in artifacts:
                path = under(source_root, row['path'])
                require(path.is_file() and sha_file(path) == row['sha256'], 'current build artifact differs: ' + row['path'])
            for row in r['build']['test_executables']:
                require(sha_file(row['path']) == row['sha256'], 'test executable fingerprint differs')
        common = ['ctest', '--test-dir', str(under(source_root, spec['build_dir'])), '-C', spec['configuration'], '-R', spec['test_regex']]
        require('discover' in by_id, 'discovery was not executed')
        if 'discover' in by_id:
            c = by_id['discover']
            require(c['argv'] == [*common, '--show-only=json-v1'] and c['role'] == 'discover', 'discovery argv mismatch')
            discovery = read_json(folder / c['raw'][0]['path'])
            actual = [t['name'] for t in discovery['tests']]
            require(actual == r['tests']['discovered'], 'discovery differs from actual command output')
            require(len(actual) == len(set(actual)) and set(actual) == set(names) and bool(actual), 'discovery has missing/extra/zero tests')
            require(read_json(folder / 'discovered.json') == discovery, 'discovered snapshot differs from raw')
            binary_paths = set()
            for test in discovery['tests']:
                binary = Path(test['command'][0])
                if not binary.is_absolute():
                    value = shutil.which(str(binary)); binary = Path(value) if value else under(source_root, spec['build_dir']) / binary
                binary_paths.add(str(binary.resolve()))
            require({row['path'] for row in r['build']['test_executables']} == binary_paths and bool(binary_paths), 'tested executable identity missing/extra')
        require(spec['repeat'] >= max(c.get('repeat_required', 1) for c in cases), 'requested repetitions insufficient')
        rounds = r['tests']['rounds']
        require([row['round'] for row in rounds] == list(range(1, spec['repeat'] + 1)), 'missing/duplicate repetition')
        for row in rounds:
            identifier = f'test-{row["round"]:03}'
            require(row['command_id'] == identifier and identifier in by_id, 'round lacks its actual command')
            junit = under(folder, row['junit']['path'])
            require(junit.name == f'round-{row["round"]:03}-junit.xml', 'JUnit reused from another round')
            c = by_id.get(identifier, {})
            require(c.get('argv') == [*common, '--no-tests=error', '--output-on-failure', '--output-junit', str(junit)], 'test argv/JUnit provenance mismatch')
            require(c.get('role') == 'test', 'test role mismatch')
            require(junit.is_file(), 'missing current JUnit')
            if junit.is_file():
                require(sha_file(junit) == row['junit']['sha256'] and junit.stat().st_size == row['junit']['size'], 'JUnit fingerprint mismatch')
                actual = junit_cases(junit)
                require(actual == row['executed'], 'executed summary differs from actual JUnit')
                require(bool(actual) and {t['name'] for t in actual} == set(names) and len(actual) == len(names), 'JUnit case set mismatch')
                require(all(t['status'] == 'Passed' for t in actual), 'required case failed or skipped')
        required_checks = expected.get('checks', [])
        configured = {c['id']: c for c in spec['checks']}
        require(len(configured) == len(spec['checks']), 'duplicate configured checks')
        require(set(configured) == {c['id'] for c in required_checks}, 'configured checks differ from independent expected')
        for check in required_checks:
            selected = configured.get(check['id'])
            if not selected:
                continue
            actual = [c for c in r['checks'] if c['id'] == check['id']]
            require(len(actual) == selected.get('repeat', 1) >= check.get('repeat_required', 1), 'check repetitions missing')
            require([c['round'] for c in actual] == list(range(1, len(actual) + 1)), 'check repetitions duplicated')
            for row in actual:
                c = by_id.get(row['command_id'], {})
                argv = selected['argv']
                if argv[0] == 'python':
                    argv = [sys.executable, '-X', 'utf8', *argv[1:]]
                require(c.get('argv') == argv and c.get('role') == 'check', 'non-CTest check provenance mismatch')
                require(row['status'] == 'Passed' and c.get('exit_code') == 0 and c.get('status') == 'Exited', 'non-CTest check failed')
        require({c['id'] for c in r['checks']} == set(configured), 'missing/extra non-CTest result')
        identifiers = {'cmake-version', 'ctest-version', 'configure', 'build', 'discover'} | {f'test-{i:03}' for i in range(1, spec['repeat'] + 1)} | {c['command_id'] for c in r['checks']}
        require(set(by_id) == identifiers, 'missing/extra commands')
        require(not r['collection_issues'], 'collector recorded incomplete facts')
        if spec.get('conformance_manifest'):
            matches=[row for row in r['checks'] if row['id']=='CHECK.conformance.bootstrap']
            require(len(matches)==1,'actual conformance report missing')
            if len(matches)==1:
                command=by_id[matches[0]['command_id']]
                require(read_json(folder/command['raw'][0]['path'])==r['conformance'],'conformance summary differs from actual result')
                with zipfile.ZipFile(folder/r['source']['archive']) as source_archive:
                    contract=json.loads(source_archive.read(spec['conformance_manifest']))
                require(r['conformance']['contract_version']==contract['port_contract_version'],'conformance contract version differs')
                require(r['conformance']['self_check_passed'] is True,'mock/fault harness self-check failed')
        else:
            require(r['conformance'] is None,'unexpected conformance claim')
        required_reviews = spec.get('review_required', ['human'])
        require(bool(required_reviews) and len(required_reviews) == len(set(required_reviews)) and 'human' in required_reviews, 'required review set invalid')
        require(r['review']['required'] == required_reviews, 'required review set changed')
        approved_reviews = set()
        for item in r['review']['records']:
            require(item['path'] in spec['review_records'], 'unrequested review record')
            if current:
                require(sha_file(under(source_root, item['path'])) == item['sha256'], 'review record changed')
                require(read_json(under(source_root, item['path'])) == item['record'], 'review snapshot mismatch')
            record = item['record']
            if record.get('task_id') == r['task_id'] and record.get('review_status') == 'Approved' and record.get('reviewed_inputs_sha256') == r['source']['build_inputs_sha256'] and record.get('approval_text'):
                approved_reviews.add(record.get('review_kind','human'))
        package = 'Failed' if errors else 'Passed' if set(required_reviews).issubset(approved_reviews) else 'InProgress'
        if check_claims:
            require(r['automated_status'] == ('Failed' if errors else 'Passed'), 'automated status is inconsistent with actual evidence')
            require(r['package_status'] == package, 'package status lacks complete evidence/review')
        return errors, package
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile, ET.ParseError) as exc:
        errors.append('incomplete or invalid evidence: ' + str(exc))
        return errors, 'Failed'


def validate_report(report_path, root=None):
    return audit(report_path, root, check_claims=True)[0]


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('report', type=Path)
    parser.add_argument('--historical', action='store_true', help='只核对历史快照，不宣称当前工作区符合该证据')
    args = parser.parse_args()
    errors = validate_report(args.report, None if args.historical else ROOT)
    print(json.dumps({'check_id': 'CHECK.evidence.integrity', 'historical_only': args.historical, 'errors': errors}, ensure_ascii=False, indent=2))
    return bool(errors)

if __name__ == '__main__':
    sys.exit(main())
