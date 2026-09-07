"""在真实独立审核与三配置局部结果齐备后登记实现审核，不替代正式门禁。"""
import argparse
from pathlib import Path
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import inputs, read_json, save_json, sha_file, digest, now, under

parser = argparse.ArgumentParser()
parser.add_argument('delta_review')
parser.add_argument('foundation_run_review')
parser.add_argument('newline_review')
args = parser.parse_args()
spec_path = 'tests/runs/d1.05-win-msvc-debug.json'
source = inputs(ROOT, read_json(ROOT/spec_path), spec_path)
identity = digest(source)
assert identity == '028ca1c8afab6dfb2b5c3c6758ce4b71f13311957cc78bdc2463e6993a265560'
normalization = 'docs/contracts/native-invocation-transport.md'
current = {row['path']: row for row in source}
runs = []
for name in ('integration-debug-eb47edd21d57', 'integration-release-871673f28330', 'integration-asan-f65332a33b8b'):
    folder = ROOT/'evidence/bootstrap/D1.05'/name
    result = read_json(folder/'result.json')
    assert result['status'] == 'Passed' and not any(result['source_changed'].values())
    tested = read_json(folder/'source-inputs.json')
    assert digest(tested) == '43ad6c2ecd162908932248ba430ab48c86a94a7d213cfca804cd0e7f972a02ef'
    assert {row['path'] for row in tested} == set(current)
    assert [row['path'] for row in tested if row != current[row['path']]] == [normalization]
    import zipfile
    with zipfile.ZipFile(folder/'source-inputs.zip') as archive:
        assert archive.read(normalization) == (ROOT/normalization).read_bytes() + b'\n'
    cases = result['cases']
    assert len(cases) == 32 and all(row['status'] == 'Passed' for row in cases)
    junit = ET.parse(folder/'native-junit.xml').getroot()
    assert int(junit.get('tests')) == 32
    assert all(int(junit.get(key, '0')) == 0 for key in ('failures','disabled','skipped'))
    for command in read_json(folder/'commands.json'):
        assert command['status'] == 'Exited' and command['exit_code'] == 0
        assert command['process_tree']['assigned_before_resume'] and command['process_tree']['active_after'] == 0
        for raw in command['raw']:
            p = folder/raw['path']
            assert sha_file(p) == raw['sha256'] and p.stat().st_size == raw['size']
    runs.append({'path': folder.relative_to(ROOT).as_posix(), 'result_sha256': sha_file(folder/'result.json'), 'junit_sha256': sha_file(folder/'native-junit.xml'), 'tests': 32, 'tested_inputs_sha256': digest(tested)})
headers = read_json(ROOT/'sdk/sdk_api_manifest.json')['headers']
assert all(sha_file(ROOT/row['path']) == row['sha256'] for row in headers if 'sha256' in row)
names = [
    'evidence/bootstrap/D1.05/spec-foundations-20260908-independent.md',
    'evidence/bootstrap/D1.05/foundations-independent-code-20260908.md',
    'evidence/bootstrap/D1.05/policy-independent-spec-code-20260908.md',
    'evidence/bootstrap/D1.05/native-pipeline-independent-final-20260908.md',
    'evidence/bootstrap/D1.05/native-pipeline-final-source-binding-20260908.md',
    'evidence/bootstrap/D1.05/independent-allocation-wrapper-final-review.md',
    args.delta_review,
    args.foundation_run_review,
    args.newline_review,
]
evidence = [{'path': name, 'sha256': sha_file(under(ROOT,name))} for name in names]
outputs = [ROOT/f'docs/reviews/D1.05-{kind}.json' for kind in ('spec','code')]
assert not any(p.exists() for p in outputs), '不得覆盖既有技术审核'
for kind, output in zip(('spec','code'), outputs):
    save_json(output, {
        'format': 'ock.technical-review/1', 'task_id': 'D1.05', 'review_kind': kind,
        'review_status': 'Approved', 'reviewed_inputs_sha256': identity, 'reviewed_at': now(),
        'reviewer': '独立AI review_foundations、inline_policy；主集成者核对范围闭合、最终源码及实际三配置',
        'actor_type': 'AI',
        'approval_text': '内部Native调用、Policy逐次准入、借用上下文与事实合同、真实分派及消费者已分别完成独立规格/代码复核；计数和运输增量经过真实反例、范围复核及三配置96次局部集成。已补齐基础CODE归档、多Unknown集合及非空借用子断言。局部43ad来源与最终028ca来源仅运输说明末尾一个多余LF不同，已逐字节核对并独立批准；没有改写原测试输入。全部代码/测试字节相同，审核范围绑定最终255输入，正式矩阵与包级状态另行计算。',
        'evidence': evidence,
        'limitations': ['这是AI审核，不是human Approved。', '正式283/283/285及九CHECK与完整门禁通过前，D1.05保持InProgress。', 'Native仅支持现行运输约束下无资源声明Read/Compute；SDK Runtime、Host及其他模块未在本包完成。', '零分配只覆盖已校准配置、模块、线程和固定预热场景；各计数通道独立，未知范围不记零。']
    })
save_json(ROOT/'evidence/bootstrap/D1.05/precommit-verification.json', {
    'kind': 'actual-local-verification-not-package-gate', 'generated_at': now(),
    'source_count': len(source), 'inputs_sha256': identity, 'runs': runs,
    'sdk_header_hashes_checked': len(headers), 'review_evidence': evidence,
    'documentation_only_normalization': normalization,
    'package_status': 'InProgress',
})
print(identity, 'local tests=96; technical review records saved; formal package gate pending')
