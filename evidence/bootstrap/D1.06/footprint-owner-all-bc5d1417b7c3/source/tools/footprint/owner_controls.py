"""有限 owner 故障控制入口；无校准、pilot、预算或生产采样线程。"""
import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import replace
import hashlib
import json
from pathlib import Path
import shutil
import sys
import time
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.footprint.windows_process import ObservationConfig
from tools.footprint.analyze import owned_success,require_complete


def save(path,value):
    path.write_text(json.dumps(value,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--mode',choices=['extra','all'],required=True)
    args=parser.parse_args()
    out=ROOT/'evidence/bootstrap/D1.06'/('footprint-owner-'+args.mode+'-'+uuid.uuid4().hex[:12])
    out.mkdir(parents=True)
    paths=[ROOT/'tools/evidence/process.py']
    for directory in ('tools/footprint','tests/tools/footprint'):
        paths.extend(p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    sources=[]
    for path in paths:
        relative=path.relative_to(ROOT);dest=out/'source'/relative;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(path,dest);sources.append({'path':relative.as_posix(),'sha256':hashlib.sha256(dest.read_bytes()).hexdigest()})
    save(out/'sources.json',sorted(sources,key=lambda row:row['path']))
    config=ObservationConfig(**json.loads((out/'source/tools/footprint/configuration-development.json').read_text(encoding='utf-8')))
    # latency 控制路径不要求 5ms 周期采样；阶段查询仍为真实 OS 操作。
    config=replace(config,mode='latency')
    consumer=out/'source/tests/tools/footprint/owner_consumer.py'
    commands=[];assertions=[]
    def run(label,argv,observation=None,timeout=20):
        result=execute(argv,ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),timeout,observation=observation)
        result['label']=label
        result['raw']=[{'path':p.name,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in (out/(label+'-stdout.log'),out/(label+'-stderr.log'))]
        return result
    def collect(result):
        commands.append(result);save(out/'commands.json',commands)
        print(result['label'],result['status'],result.get('observation',{}).get('status'),str(out),flush=True)
    def observed(mode,configuration=config,timeout=20):
        result=run(mode,[sys.executable,'-X','utf8',str(consumer),mode],configuration,timeout)
        collect(result);return result
    def rejected(result):
        try:require_complete(result)
        except ValueError:return True
        return False
    if args.mode=='all':
        unit=run('permanent-regressions',[sys.executable,'-X','utf8','-m','unittest','discover','-s',str(out/'source/tests/tools/footprint'),'-p','test_*.py','-v'])
        collect(unit);assertions.append({'name':'permanent_regressions','passed':owned_success(unit)})
        normal=observed('normal')
        assertions.append({'name':'full_protocol_positive','passed':not rejected(normal)})
    extra=observed('extra_phase')
    assertions.append({'name':'extra_final_event_rejected','passed':rejected(extra) and extra['observation']['status']=='ObserverFailed' and extra['process_tree']['active_after']==0,'errors':extra['observation']['errors']})
    if args.mode=='all':
        # 仅测试驱动辅助线程协调独立 sentinel Job；观察器不接收外部 PID/句柄。
        ready=out/'sentinel.ready';release=out/'sentinel.release'
        with ThreadPoolExecutor(max_workers=1,thread_name_prefix='owned-sentinel-control') as pool:
            future=pool.submit(run,'sentinel',[sys.executable,'-X','utf8',str(consumer),'sentinel',str(ready),str(release)],None,25)
            deadline=time.monotonic()+5
            try:
                while not ready.exists() and not future.done() and time.monotonic()<deadline:time.sleep(.02)
                if not ready.exists():raise RuntimeError('sentinel startup deadline')
                descendant=observed('descendants_alive',timeout=3)
                before_release_alive=not future.done()
            finally:
                release.write_text('release',encoding='ascii')
                sentinel=future.result(timeout=30);collect(sentinel)
        assertions.append({'name':'root_exit_owned_descendants_drained','passed':descendant['status']=='DescendantsAlive' and descendant['exit_code']==0 and descendant['process_tree']['total_processes']>=2 and descendant['process_tree']['active_after']==0 and descendant['process_tree']['terminated_owned_job'] and descendant['observation']['status']=='Incomplete' and rejected(descendant)})
        assertions.append({'name':'same_name_external_sentinel_survives','passed':before_release_alive and owned_success(sentinel) and sentinel['executable']==descendant['executable'] and sentinel['pid']!=descendant['pid'] and (out/'sentinel-stdout.log').read_bytes().strip()==b'sentinel released normally','scope':'test driver coordination thread only; separate execute-owned Job; no sampler thread','sentinel_pid':sentinel['pid'],'observed_pid':descendant['pid']})
        stage=observed('stage_timeout',replace(config,stage_timeout_ms=1600),timeout=8)
        assertions.append({'name':'absolute_stage_deadline','passed':stage['observation']['status']=='ObserverFailed' and any('StageDeadlineReached' in e for e in stage['observation']['errors']) and stage['process_tree']['active_after']==0 and stage['process_tree']['terminated_owned_job'] and len(stage['observation']['phases'])==1,'errors':stage['observation']['errors']})
    save(out/'assertions.json',assertions)
    ok=all(item['passed'] for item in assertions)
    save(out/'result.json',{'status':'Passed' if ok else 'Failed','scope':'finite owner fault controls only; latency protocol mode','budget_status':'NotApproved','pilot':False,'assertions':assertions})
    return 0 if ok else 1


if __name__=='__main__':sys.exit(main())
