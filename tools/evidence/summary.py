"""证据快读导航：复制原报告声明，不产生门禁结论，不替代完整 audit。"""
import argparse
from datetime import datetime
import json
from pathlib import Path
import sys
import uuid
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT/'build/python-deps'))
from jsonschema import Draft202012Validator
from tools.evidence.common import digest, expected_cases, junit_cases, read_json, save_json, sha_bytes, sha_file, under


def _duration(start, end):
    if start is None or end is None:
        return None
    seconds = (datetime.fromisoformat(end)-datetime.fromisoformat(start)).total_seconds()
    if seconds < 0:
        raise ValueError('negative evidence duration')
    return seconds


def _schema(value):
    schema = read_json(ROOT/'schemas/evidence-summary-v1.schema.json')
    Draft202012Validator.check_schema(schema)
    errors = list(Draft202012Validator(schema).iter_errors(value))
    if errors:
        raise ValueError('summary schema: '+errors[0].message)


def _command_costs(commands):
    def cost(rows):
        return {'count':None if rows is None else len(rows),
                'duration_seconds':None if rows is None or any(r['duration_seconds'] is None for r in rows)
                  else sum(r['duration_seconds'] for r in rows)}
    def category(row):
        argv = row['argv'] or []
        if argv and Path(argv[0]).stem.lower() == 'cmake':
            if '--install' in argv: return 'install'
            if '--build' in argv: return 'build'
        return row['role']
    roles = None if commands is None else [
        {'role':role, **cost([r for r in commands if r['role']==role])}
        for role in sorted({r['role'] for r in commands}, key=lambda v:'' if v is None else v)]
    return {'scope':'top-level-only', 'total':None if commands is None else len(commands),
            'duration_seconds':cost(commands)['duration_seconds'], 'by_role':roles,
            **{name:cost(None if commands is None else [r for r in commands if category(r)==name])
               for name in ('configure','build','install')}, 'nested_command_count':None}


def build_summary(report_path):
    """读取已有本地材料；必需材料缺失/摘要不符直接拒绝，不修改报告。"""
    try:
        return _build(Path(report_path).resolve())
    except (OSError, KeyError, TypeError, ET.ParseError, zipfile.BadZipFile) as error:
        raise ValueError('summary source unavailable or malformed: '+str(error)) from error


def _build(path):
    folder = path.parent
    references = []

    def reference(name, role, expected_sha=None, expected_size=None):
        p = under(folder, name)
        if not p.is_file():
            raise ValueError('missing summary source: '+str(name))
        row = {'path': name, 'role': role, 'sha256': sha_file(p), 'size': p.stat().st_size}
        if expected_sha is not None and row['sha256'] != expected_sha:
            raise ValueError('source digest mismatch: '+name)
        if expected_size is not None and row['size'] != expected_size:
            raise ValueError('source size mismatch: '+name)
        references.append(row)
        return p

    reference(path.name, 'report')
    report = read_json(path)
    if report.get('evidence_format') != 'ock.evidence/1':
        raise ValueError('unsupported evidence report')
    manifest_path = reference(report['manifest']['snapshot'], 'manifest')
    expected_path = reference(report['expected_snapshot'], 'expected')
    manifest = read_json(manifest_path)
    expected = read_json(expected_path)

    def verify_snapshot(snapshot, parsed, declared_sha, member):
        if declared_sha is None or sha_file(snapshot) == declared_sha:
            return
        # run.py 会重排 JSON；只读指定原成员，不把快照字节误当原文件字节。
        if not member:
            raise ValueError('original snapshot member unavailable')
        under(folder, member)
        source = report['source']
        archive_path = under(folder, source['archive'])
        if sha_file(archive_path) != source['archive_sha256']:
            raise ValueError('source archive digest mismatch')
        with zipfile.ZipFile(archive_path) as archive:
            members = archive.namelist()
            if len(members) != len(set(members)):
                raise ValueError('duplicate source archive member')
            for name in members:
                under(folder, name)
            original = archive.read(member)
        if sha_bytes(original) != declared_sha or json.loads(original) != parsed:
            raise ValueError('snapshot differs from recorded original digest/content')

    verify_snapshot(manifest_path, manifest, report['manifest'].get('sha256'), report['manifest'].get('path'))
    verify_snapshot(expected_path, expected, report.get('requirements_manifest_sha256'), manifest.get('expected_manifest'))
    names = [c['id'] for c in expected_cases(expected, manifest['profile'])]
    tests = report.get('tests')
    found = tests.get('discovered') if tests is not None else None
    if found is not None and (not isinstance(found, list) or len(found) != len(set(found))):
        raise ValueError('invalid discovered names')
    if (folder/'discovered.json').is_file():
        discovered = read_json(reference('discovered.json', 'discovered'))
        actual = [c['name'] for c in discovered['tests']]
        if found is not None and found != actual:
            raise ValueError('recorded discovery differs from snapshot')
        if len(actual) != len(set(actual)):
            raise ValueError('duplicate discovery names')
        found = actual
    elif found:
        raise ValueError('missing discovered snapshot')
    if tests is not None and 'expected' in tests and tests['expected'] != names:
        raise ValueError('recorded expected differs from independent snapshot')

    for key in ('source', 'build'):
        item = report.get(key, {})
        if item.get('archive'):
            reference(item['archive'], key+'-archive', item['archive_sha256'])
    for key in ('runtime_archive', 'selftest_fixtures'):
        if report.get(key):
            item = report[key]
            reference(item['path'], key, item['sha256'])
    if report.get('review', {}).get('archive'):
        item = report['review']['archive']
        reference(item['path'], 'review-archive', item['sha256'])
    if (folder/'source-inputs.json').is_file():
        reference('source-inputs.json', 'source-inputs')

    signals = []
    commands = None
    if 'commands' in report:
        commands = []
        for c in report['commands']:
            if not c['raw']:
                raise ValueError('command raw references cannot be empty')
            for raw in c['raw']:
                reference(raw['path'], 'command-raw', raw['sha256'], raw['size'])
                if raw.get('truncated') or raw.get('redacted'):
                    signals.append('command raw incomplete: '+raw['path'])
            row = {k:c.get(k) for k in ('id','role','argv','status','exit_code','started_at','finished_at')}
            row['duration_seconds'] = _duration(row['started_at'], row['finished_at'])
            commands.append(row)
            if c.get('status') != 'Exited' or c.get('exit_code') != 0:
                signals.append('command not successful: '+str(c.get('id')))
            if 'observed_exit_code' in c and c['observed_exit_code'] != c.get('exit_code'):
                signals.append('command exit observation mismatch: '+str(c.get('id')))
            if 'process_tree' in c:
                tree = c['process_tree']
                if tree.get('active_after') != 0 or not tree.get('assigned_before_resume') or tree.get('terminated_owned_job'):
                    signals.append('command tree not quiescent: '+str(c.get('id')))
    rounds = None
    executed_names = set()
    if tests is not None and 'rounds' in tests:
        rounds = []
        for r in tests['rounds']:
            junit = r['junit']
            actual = junit_cases(reference(junit['path'], 'junit', junit['sha256'], junit['size']))
            if 'executed' in r and r['executed'] != actual:
                raise ValueError('recorded execution differs from JUnit')
            actual_names = {c['name'] for c in actual}
            executed_names.update(actual_names)
            row = {'round':r['round'], 'executed':len(actual),
                   'failed':sorted(c['name'] for c in actual if c['status']=='Failed'),
                   'skipped':sorted(c['name'] for c in actual if c['status']=='Skipped'),
                   'missing':sorted(set(names)-actual_names), 'unexpected':sorted(actual_names-set(names))}
            rounds.append(row)
            for kind in ('failed','skipped','missing','unexpected'):
                if row[kind]:
                    signals.append(f'round {r["round"]} {kind}: '+', '.join(row[kind]))
        indices = [r['round'] for r in rounds]
        required_rounds = manifest.get('repeat')
        if required_rounds is not None and indices != list(range(1, required_rounds+1)):
            signals.append('required execution rounds missing or unexpected')
        for c in expected_cases(expected, manifest['profile']):
            if len(rounds) < c.get('repeat_required', 1):
                signals.append('required repetition missing: '+c['id'])
    discovery = {'missing':None if found is None else sorted(set(names)-set(found)),
                 'unexpected':None if found is None else sorted(set(found)-set(names))}
    for key, values in discovery.items():
        if values:
            signals.append('discovery '+key+': '+', '.join(values))
    for error in report.get('errors', []):
        signals.append('report error: '+error)
    for error in report.get('collection_issues', []):
        signals.append('collection issue: '+error)
    for check in report.get('checks', []):
        if check.get('status') != 'Passed':
            signals.append('check not successful: '+str(check.get('id')))
    if report.get('automated_status') == 'Passed':
        if signals or rounds is None or commands is None or found is None:
            raise ValueError('report Passed claim contradicts failures or unavailable execution coverage')

    result = {
      'format':'ock.evidence-summary/1', 'purpose':'navigation-only; full audit remains required',
      'task_id':report.get('task_id'), 'run_id':report.get('run_id'), 'profile':manifest.get('profile'),
      'report_claims':{'automated_status':report.get('automated_status'), 'package_status':report.get('package_status')},
      'duration_seconds':_duration(report.get('started_at'), report.get('finished_at')),
      'counts':{'expected':len(names), 'discovered':None if found is None else len(found),
                'executed':None if rounds is None else sum(r['executed'] for r in rounds),
                'rounds':None if rounds is None else len(rounds)},
      'set_digests':{'expected':digest(sorted(names)), 'discovered':None if found is None else digest(sorted(found)),
                     'executed':None if rounds is None else digest(sorted(executed_names))},
      'discovery':discovery, 'rounds':rounds, 'commands':commands, 'command_costs':_command_costs(commands),
      'failure_signals':signals,
      'source_claims':{k:report.get(k) for k in ('source','manifest','requirements_manifest_sha256','collector')},
      'references':sorted(references, key=lambda r:(r['path'],r['role']))}
    # 保留来源身份而不复制整份输入清单；完整清单仍通过 report 引用定位。
    if isinstance(result['source_claims']['source'], dict):
        result['source_claims']['source'] = {k:v for k,v in result['source_claims']['source'].items() if k != 'inputs'}
    result['content_sha256'] = digest(result)
    _schema(result)
    return result


def write_summary(report_path):
    report_path = Path(report_path).resolve()
    value = build_summary(report_path)
    output = report_path.parent/'summary.json'
    temporary = output.with_name('.summary-'+uuid.uuid4().hex+'.tmp')
    try:
        try:
            save_json(temporary, value)
            temporary.replace(output)
        finally:
            temporary.unlink(missing_ok=True)
    except OSError as error:
        raise ValueError('summary write failed: '+str(error)) from error
    return output


def read_summary(summary_path):
    """机器核对摘要及来源；不解压归档，不调用完整 audit，不输出完整报告。"""
    try:
        path = Path(summary_path).resolve()
        value = read_json(path)
        _schema(value)
        if value['content_sha256'] != digest({k:v for k,v in value.items() if k!='content_sha256'}):
            raise ValueError('summary content digest mismatch')
        for row in value['references']:
            source = under(path.parent, row['path'])
            if not source.is_file() or source.stat().st_size != row['size'] or sha_file(source) != row['sha256']:
                raise ValueError('stale summary source: '+row['path'])
        reports = [r for r in value['references'] if r['role']=='report']
        if len(reports) != 1:
            raise ValueError('exactly one report reference required')
        actual = build_summary(under(path.parent, reports[0]['path']))
        if actual != value:
            raise ValueError('summary differs from deterministic source extraction')
        return value
    except (OSError, KeyError, TypeError) as error:
        raise ValueError('summary unavailable or malformed: '+str(error)) from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=Path)
    parser.add_argument('--write', action='store_true', help='generate beside an existing report')
    args = parser.parse_args()
    try:
        if args.write:
            print(write_summary(args.path))
        else:
            import json
            print(json.dumps(read_summary(args.path), ensure_ascii=False, indent=2))
    except ValueError as error:
        print('summary rejected: '+str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
