"""本地可重建的AI导航：核对完整身份后才输出，不替代规范或验收。"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT))
from tools.evidence.common import save_json, sha_file

ARCH = 'docs/01_Architecture_v3.3.md'
PLAN = 'docs/02_Execution_Plan_v3.3.md'
PROGRESS = 'docs/progress.md'
CONTRACTS = ['docs/contracts/native-host-api.md', 'docs/contracts/logging-api.md',
             'docs/contracts/native-footprint-method.md', 'docs/contracts/native-sdk-surface.md']


def build(root, head, branch):
    text = (root/PROGRESS).read_text(encoding='utf-8')
    if not re.search(r'\| D1\.06 \|[^\n]+\| InProgress \|', text):
        raise ValueError('当前薄导航仅支持D1.06；进度已变化，须完整读取规范后更新映射')
    def ref(path, sections):
        return {'path': path, 'sections': sections, 'sha256': sha_file(root/path)}
    return {'format':'ock.ai-current/1', 'normative':False,
            'current_package':'D1.06', 'package_state':'InProgress',
            'phase':'D1.06开发交付；按用户要求停止测试，提交推送后暂停；包级未验收',
            'branch':branch, 'head':head, 'last_passed':'D1.05', 'gate':'G1',
            'prerequisites':['D1.05','D0.06'],
            'architecture_refs':[ref(ARCH, ['A15','A16','A19','A21','A22','A23'])],
            'execution_plan_refs':[ref(PLAN, ['D1.06','G1','E03','E06'])],
            'progress':{'path':PROGRESS,'sha256':sha_file(root/PROGRESS)},
            'strategy':ref('docs/plans/D1.06.md', ['全部']),
            'contracts':[{'path':p,'sha256':sha_file(root/p),'status':'Candidate'}
                         for p in CONTRACTS if (root/p).is_file()],
            'active_reviews':[], 'review_index':'docs/reviews/D1.06-index.md',
            'stale':False,
            'limits':'导航无批准含义；Candidate不能作为冻结API；缺失摘要时查必要原始材料。'}


def stale_reasons(root, value, head, branch):
    reasons = []
    if not isinstance(value, dict):
        return ['navigation must be an object']
    if value.get('head') != head:
        reasons.append('HEAD changed')
    try:
        actual = build(root, head, branch)
        if value != actual:
            reasons.append('navigation/source/branch/progress mismatch')
    except (ValueError, OSError) as error:
        reasons.append(str(error))
    return reasons


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('check','refresh'))
    a = parser.parse_args()
    head = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    branch = subprocess.check_output(['git','branch','--show-current'],cwd=ROOT,text=True).strip()
    path = ROOT/'docs/ai-current.json'
    if a.action == 'refresh':
        save_json(path, build(ROOT, head, branch))
        print(json.dumps({'navigation':str(path),'status':'Refreshed',
                          'warning':'刷新只核对导航来源，不代表完成规范阅读或审核。'},ensure_ascii=False))
        return 0
    try:
        value = json.loads(path.read_text(encoding='utf-8'))
        reasons = stale_reasons(ROOT, value, head, branch)
    except (OSError, ValueError) as error:
        reasons = [str(error)]
    if reasons:
        print(json.dumps({'stale':True,'reasons':reasons,
                          'fallback':[ARCH,PLAN,PROGRESS],
                          'instruction':'禁止依赖旧摘要；完整读取规范和最新进度，复核当前映射后refresh。'},ensure_ascii=False))
        return 1
    print(json.dumps(value, ensure_ascii=False, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
