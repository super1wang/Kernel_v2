"""隔离进程核对非法 worker 析构在寿命边界 fail-fast；不是合格后端的普通成功路径。"""
import argparse
from pathlib import Path
import sys
import uuid
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.evidence.common import save_json

def main():
    parser=argparse.ArgumentParser();parser.add_argument('executable');parser.add_argument('backend');args=parser.parse_args()
    evidence=ROOT/'evidence/bootstrap/B4'/('worker-fatal-'+args.backend+'-'+uuid.uuid4().hex[:10]);evidence.mkdir(parents=True)
    row=execute([args.executable,args.backend],ROOT,evidence/'stdout.log',evidence/'stderr.log',15)
    save_json(evidence/'process.json',row)
    assert row['status']=='Exited' and row['exit_code']==3,row
    assert row['process_tree']['assigned_before_resume'] and row['process_tree']['active_after']==0
    assert not row['process_tree']['terminated_owned_job']
    assert b'worker-destruction-boundary' in (evidence/'stderr.log').read_bytes()
    print('worker self-destruction rejected at boundary:',args.backend,evidence)

if __name__=='__main__':main()
