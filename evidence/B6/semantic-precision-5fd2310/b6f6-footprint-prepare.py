import sys, json, subprocess, hashlib
from pathlib import Path
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.footprint.formal import identity as native
from tools.footprint.embedded_formal import identity as embedded

source = '5fd2310aec5f8a24ac14b948677930e2a7ced7e3'
out = ROOT / 'evidence/B6/semantic-precision-5fd2310'
out.mkdir(parents=True, exist_ok=True)


def save(p, v):
    p.write_text(json.dumps(v, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


budgets = json.loads((ROOT / 'footprint-budgets.json').read_text(encoding='utf-8'))
save(out / 'previous-budgets.json', budgets)
stamp = datetime.now(timezone.utc).isoformat()
methods = {}
inputs = {}
for key in budgets['budgets']:
    profile, mode = key.split('/')
    d, v, _ = native(profile, mode)
    methods['native/' + key] = d
    inputs.update({r['path'].replace('\\', '/'): r['sha256'] for r in v['sources']})
    budgets['budgets'][key].update(method_digest=d, approved_at=stamp)
for key in budgets['embedded']['budgets']:
    profile, mode = key.split('/')
    d, v, _ = embedded(profile)
    methods['embedded/' + key] = d
    inputs.update({r['path'].replace('\\', '/'): r['sha256'] for r in v['sources']})
    budgets['embedded']['budgets'][key].update(method_digest=d, approved_at=stamp)
rows = []
for path, h in sorted(inputs.items()):
    committed = subprocess.check_output(['git', 'show', source + ':' + path], cwd=ROOT)
    assert hashlib.sha256(committed).hexdigest() == h, path
    rows.append({'path': path, 'sha256': h})
decision = 'docs/reviews/B6-final-linearization-footprint-budget.md'
(ROOT / decision).write_text(
    '# B6 最终线性化收口 footprint 来源刷新批准\n\nAI 技术批准时间：' + stamp + '。生产来源 `' + source + '`。\n\n'
    'F6 收口修复（ExecutionService/ManagedInvocation 两处 Runtime 私有文件 + State 测试）触发同一最终来源刷新，'
    '旧 912cb755 证据保持历史。仅刷新来源方法摘要、批准时间及本批准文档绑定；沿用既有全部原数值预算、pilot 标识、'
    'Native 7 模式和 Embedded 3 配置（Release 含 startup），每项六组 ABBA。方法脚本、分配零窗口、线程等式与结构上界不变。'
    '所有方法输入逐字节匹配生产来源。\n\n'
    '该批准只允许按原预算测量，不代表机器 Passed；出现失败保留原文，禁止提高阈值。全部通过并归档机器 acceptance 前 B7 HOLD。\n\n'
    + '\n'.join('- ' + k + ': `' + v + '`' for k, v in methods.items()) + '\n', encoding='utf-8')
budgets['decision'] = decision
budgets['embedded']['decision'] = decision
budgets['embedded']['decision_sha256'] = sha(ROOT / decision)
save(ROOT / 'footprint-budgets.json', budgets)
save(out / 'source-binding.json', {
    'production_commit': source,
    'checkout_before_gate_correction': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
    'checked_at': stamp, 'all_inputs_match_production_commit': True,
    'input_count': len(rows), 'inputs': rows, 'methods': methods})
print(json.dumps({'inputs': len(rows), 'methods': len(methods), 'output': str(out)}))
