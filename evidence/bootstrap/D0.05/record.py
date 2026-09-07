"""D0.05 开发期原始记录；不生成正式证据或包级批准。"""
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import platform
import subprocess
import sys
import uuid
import zipfile

ROOT = Path(__file__).resolve().parents[3]
def sha(data): return hashlib.sha256(data).hexdigest()
def now(): return datetime.now(timezone.utc).isoformat()
def save(path, value): path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')
def git(*args): return subprocess.check_output(['git', *args], cwd=ROOT, text=True, encoding='utf-8').strip()

def main():
    tag = sys.argv[1]
    argv = [sys.executable, '-X', 'utf8', 'tests/model/commit_model/run.py', *sys.argv[2:]]
    if sys.argv[2:] == ['--outcome-contract']:
        argv = [sys.executable, '-X', 'utf8', 'evidence/bootstrap/D0.05/outcome_crosscheck.py']
    run_id = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ') + '-' + tag + '-' + uuid.uuid4().hex[:8]
    out = ROOT / 'evidence/bootstrap/D0.05' / run_id
    out.mkdir(parents=True, exist_ok=False)
    paths = {Path(__file__).resolve(), ROOT / 'evidence/bootstrap/D0.05/outcome_crosscheck.py',
             ROOT / 'tests/model/execution_model/model.py', ROOT / 'schemas/outcome-v1.schema.json'}
    for name in ('tests/model/commit_model', 'tests/model/dedup_model'):
        paths.update(p for p in (ROOT / name).rglob('*.py') if '__pycache__' not in p.parts)
    for name in ('docs/01_Architecture_v3.3.md', 'docs/02_Execution_Plan_v3.3.md',
                 'docs/contracts/outcome.md', 'docs/contracts/commit.md', 'docs/contracts/intent.md',
                 'docs/reviews/D0.01-approval.json', 'docs/reviews/D0.03-approval.json',
                 'tests/manifests/d0.05.expected.json'):
        if (ROOT / name).is_file(): paths.add(ROOT / name)
    inputs = []
    with zipfile.ZipFile(out / 'source-inputs.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(paths):
            rel = path.relative_to(ROOT).as_posix(); data = path.read_bytes()
            inputs.append({'path': rel, 'sha256': sha(data), 'size': len(data)})
            archive.writestr(rel, data)
    save(out / 'source-inputs.json', inputs)
    result = {'format': 'ock.bootstrap-package/1', 'task_id': 'D0.05', 'run_id': run_id,
              'source': {'commit': git('rev-parse', 'HEAD'), 'dirty': bool(git('status', '--porcelain')),
                         'inputs_sha256': sha(json.dumps(inputs, sort_keys=True, separators=(',', ':')).encode()),
                         'archive_sha256': sha((out / 'source-inputs.zip').read_bytes())},
              'formal_evidence_status': 'Incomplete', 'review_status': 'Pending',
              'environment': {'python': platform.python_version(), 'platform': platform.platform(),
                              'executable': sys.executable, 'executable_sha256': sha(Path(sys.executable).read_bytes())},
              'commands': [], 'limitations': ['Python 模型步，不是实际线程/SQLite/设备。', 'D0.06 正式采集与人工评审独立完成。']}
    command = {'argv': argv, 'cwd': str(ROOT), 'started_at': now()}
    process = subprocess.run(argv, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=120)
    command.update(finished_at=now(), exit_code=process.returncode)
    for name, data in [('stdout', process.stdout), ('stderr', process.stderr)]:
        filename = '01-' + name + '.bin'; (out / filename).write_bytes(data)
        command[name] = {'path': filename, 'sha256': sha(data), 'size': len(data)}
    result['commands'].append(command)
    result['source_unchanged'] = all(sha((ROOT / item['path']).read_bytes()) == item['sha256'] for item in inputs)
    save(out / 'commands.json', result)
    print(out.relative_to(ROOT).as_posix())
    sys.stdout.buffer.write(process.stdout); sys.stderr.buffer.write(process.stderr)
    return process.returncode

if __name__ == '__main__': sys.exit(main())
