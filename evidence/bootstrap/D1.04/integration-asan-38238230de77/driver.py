"""完整D1.04集成开发轮；仅在实现就绪后运行，不替代正式矩阵。"""
from pathlib import Path
import json
import sys
import uuid
import zipfile
import argparse
import os
import re

ROOT = Path.cwd()
sys.path.insert(0, str(ROOT))
from tools.evidence.common import inputs, read_json, save_json, sha_bytes, sha_file, junit_cases
from tools.evidence.process import execute

parser = argparse.ArgumentParser()
parser.add_argument('--profile', choices=('debug', 'release', 'asan'), default='debug')
profile = parser.parse_args().profile
out = ROOT / 'evidence/bootstrap/D1.04' / ('integration-' + profile + '-' + uuid.uuid4().hex[:12])
out.mkdir(parents=True, exist_ok=False)
spec_path = f'tests/runs/d1.04-win-msvc-{profile}.json'
spec = read_json(spec_path)
rows = inputs(ROOT, spec, spec_path)
frozen_expected = sorted(c['id'] for c in read_json(spec['expected_manifest'])['cases'] if '.policy.' in c['id'])
assert len(frozen_expected) == 35
save_json(out / 'source-inputs.json', rows)
(out / 'driver.py').write_bytes(Path(__file__).read_bytes())
with zipfile.ZipFile(out / 'source-inputs.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for row in rows:
        data = (ROOT / row['path']).read_bytes()
        assert len(data) == row['size'] and sha_bytes(data) == row['sha256']
        archive.writestr(row['path'], data)

build = 'build/d1.04-integration-' + uuid.uuid4().hex[:8]
commands = []

def run(name, argv, timeout=1200):
    streams = [out / (name + suffix) for suffix in ('-stdout.log', '-stderr.log')]
    command = execute(argv, ROOT, *streams, timeout)
    command['raw'] = [dict(path=p.name, sha256=sha_file(p), size=p.stat().st_size) for p in streams]
    commands.append(command)
    save_json(out / 'commands.json', commands)
    print(out.name, name, command['status'], command['exit_code'], flush=True)
    assert command['status'] == 'Exited' and command['exit_code'] == 0
    assert command['process_tree']['active_after'] == 0
    return streams[0]

def archive_materials():
    base = ROOT / build / 'tests/contract/authorization'
    files = sorted(p for p in base.rglob('*') if p.is_file()) if base.exists() else []
    for name in ('CMakeCache.txt', f'toolchain-{spec["configuration"]}.json', 'ock-target-graph.json', 'CTestTestfile.cmake'):
        p = ROOT / build / name
        if p.is_file():
            files.append(p)
    materials = []
    with zipfile.ZipFile(out / 'build-artifacts.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for p in files:
            data = p.read_bytes()
            relative = p.relative_to(ROOT).as_posix()
            archive.writestr(relative, data)
            materials.append(dict(path=relative, size=len(data), sha256=sha_bytes(data)))
    with zipfile.ZipFile(out / 'build-artifacts.zip') as archive:
        assert len(archive.namelist()) == len(materials)
        for row in materials:
            data = archive.read(row['path'])
            assert len(data) == row['size'] and sha_bytes(data) == row['sha256']
    save_json(out / 'build-artifacts.json', materials)

result = {'status': 'Failed', 'build_dir': build, 'kind': 'bootstrap-integration-only', 'profile': spec['profile'], 'configuration': spec['configuration']}
try:
    run('configure', [x.replace(spec['build_dir'], build) for x in spec['configure']])
    run('build', [x.replace(spec['build_dir'], build) for x in spec['build']])
    binary = ROOT / build / f'tests/contract/authorization/{spec["configuration"]}/ock_policy_tests.exe'
    registry = ROOT / build / f'tests/contract/authorization/policy-tests-{spec["configuration"]}.cmake'
    runtime_match = re.search(r'ENVIRONMENT_MODIFICATION \[==\[PATH=path_list_prepend:(.*?)\]==\]', registry.read_text(encoding='utf-8'))
    assert runtime_match, 'missing registered runtime directory'
    runtime = Path(runtime_match[1])
    assert runtime.is_dir()
    actual_list = run('native-list', ['cmake', '-E', 'env', 'PATH=' + str(runtime) + os.pathsep + os.environ.get('PATH', ''), str(binary), '--list']).read_text(encoding='utf-8').splitlines()
    expected = frozen_expected
    assert len(expected) == 35 and sorted(actual_list) == expected
    junit = out / 'policy-junit.xml'
    run('ctest-policy', ['ctest', '--test-dir', build, '-C', spec['configuration'], '-R', '^T(07|19|20)[.]policy[.]', '--output-on-failure', '--no-tests=error', '--output-junit', str(junit)])
    cases = junit_cases(junit)
    assert sorted(c['name'] for c in cases) == expected and all(c['status'] == 'Passed' for c in cases)
    after_rows = inputs(ROOT, read_json(spec_path), spec_path)
    before = {r['path']: r for r in rows}
    after = {r['path']: r for r in after_rows}
    changed = {'added': sorted(after.keys() - before.keys()),
               'removed': sorted(before.keys() - after.keys()),
               'modified': sorted(p for p in before.keys() & after.keys() if before[p] != after[p])}
    stable = not any(changed.values())
    result.update(status='Passed' if stable else 'SourceChanged', cases=cases, source_changed=changed)
    assert stable, changed
finally:
    archive_materials()
    save_json(out / 'result.json', result)
print(json.dumps(result), flush=True)
