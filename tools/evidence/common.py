"""证据格式的共享纯函数；摘要是完整性校验，不是外部审计签名。"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def now():
    return datetime.now(timezone.utc).isoformat()


def sha_bytes(data):
    return hashlib.sha256(data).hexdigest()


def sha_file(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def digest(value):
    return sha_bytes(json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode('utf-8'))


def read_json(path):
    def unique(pairs):
        result = {}
        for key, value in pairs:
            if key in result:
                raise ValueError('duplicate JSON member: ' + key)
            result[key] = value
        return result
    def invalid_constant(value):
        raise ValueError('nonfinite JSON number: ' + value)
    return json.loads(Path(path).read_text(encoding='utf-8'), object_pairs_hook=unique, parse_constant=invalid_constant)


def save_json(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')


def under(root, name):
    root = Path(root).resolve()
    if not isinstance(name, str) or not name or Path(name).is_absolute() or '..' in Path(name).parts:
        raise ValueError('relative workspace path required: ' + str(name))
    path = (root / name).resolve()
    if not path.is_relative_to(root):
        raise ValueError('path escapes workspace: ' + name)
    return path


def inputs(root, spec, spec_relative):
    selected = {under(root, spec_relative), under(root, spec['expected_manifest']), under(root, spec['dependency_lock'])}
    if 'review_policy' in spec:
        selected.add(under(root, spec['review_policy']['path']))
    for name in spec['required_artifacts']:
        selected.add(under(root, name))
    for pattern in spec['source_patterns']:
        under(root, pattern)
        pattern = pattern + '/*' if pattern.endswith('/**') else pattern
        for path in root.glob(pattern):
            rel = path.relative_to(root)
            if path.is_file() and rel.parts[0] not in ('.git', 'build', 'evidence') and '__pycache__' not in rel.parts and path.suffix not in ('.pyc', '.pyo'):
                selected.add(under(root, rel.as_posix()))
    result = []
    for path in sorted(selected):
        if path.is_file():
            result.append({'path': path.relative_to(root).as_posix(), 'sha256': sha_file(path), 'size': path.stat().st_size})
    return result


def runtime_path_allowed(root, name):
    """仅隔离构建和既有子验证产物区；不包含正式报告树，避免自归档。"""
    root=Path(root).resolve()
    path=under(root,name)
    return name==Path(name).as_posix() and any(path.is_relative_to(root/base)
        for base in ('build','evidence/bootstrap','evidence/G1'))


def expected_cases(expected, profile):
    cases = [c for c in expected['cases'] if 'profiles' not in c or profile in c['profiles']]
    names = [c['id'] for c in cases]
    if not names or len(names) != len(set(names)):
        raise ValueError('empty or duplicate expected tests')
    for case in cases:
        if not re.fullmatch(r'T\d{2}\.[a-zA-Z0-9_.-]+', case['id']):
            raise ValueError('invalid test id')
        count = case.get('repeat_required', 1)
        if type(count) is not int or count < 1:
            raise ValueError('invalid required repetition')
    return cases


def junit_cases(path):
    root = ET.parse(path).getroot()
    if root.tag != 'testsuite':
        raise ValueError('expected a CTest testsuite')
    rows = []
    for case in root.findall('testcase'):
        state = case.get('status', 'run')
        if state not in ('run', 'notrun', 'fail'):
            raise ValueError('unknown JUnit testcase status: ' + state)
        status = 'Passed'
        if state == 'fail' or case.find('failure') is not None or case.find('error') is not None:
            status = 'Failed'
        elif case.find('skipped') is not None or state == 'notrun':
            status = 'Skipped'
        rows.append({'name': case.attrib['name'], 'status': status})
    if int(root.attrib['tests']) != len(rows):
        raise ValueError('JUnit declared test count differs from actual cases')
    if len(rows) != len({row['name'] for row in rows}):
        raise ValueError('duplicate JUnit case')
    return rows
