"""v2 必要护栏与既有 held-thread 工件复用；不运行整套校准。"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import uuid
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.footprint.windows_process import ObservationConfig
from tools.footprint.analyze import owned_success,require_complete,summarize_observation


def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--calibration-record',type=Path,required=True);args=parser.parse_args()
    record=json.loads(args.calibration_record.read_text(encoding='utf-8'));binary=Path(record['path'])
    if sha(binary)!=record['sha256'] or record['configuration']!='Debug':raise ValueError('frozen calibration artifact mismatch')
    previous=args.calibration_record.parent
    for name in ('tools/footprint/consumer/channel.hpp','tools/footprint/controls/calibration.cpp','tools/footprint/controls/required.cpp'):
        if (ROOT/name).read_bytes()!=(previous/'source'/name).read_bytes():raise ValueError('calibration consumer source changed')
    required=next(r for r in json.loads((previous/'loaded-modules.json').read_text(encoding='utf-8')) if r['role']=='required_test_distribution')
    if sha(Path(required['path']))!=required['sha256']:raise ValueError('calibration DLL changed')
    out=ROOT/'evidence/bootstrap/D1.06'/('footprint-v2-held-'+uuid.uuid4().hex[:12]);out.mkdir()
    paths=[ROOT/'tools/evidence/process.py',ROOT/'docs/contracts/native-footprint-method.md']
    for directory in ('tools/footprint','tests/tools/footprint'):
        paths.extend(p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    sources=[]
    for p in paths:
        rel=p.relative_to(ROOT);dest=out/'source'/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
        sources.append({'path':rel.as_posix(),'sha256':sha(dest)})
    save(out/'sources.json',sorted(sources,key=lambda row:row['path']))
    save(out/'reused-artifacts.json',{'artifact':record,'required':required,'previous':str(previous),'consumer_sources_byte_equal':True})
    configuration=ObservationConfig(**json.loads((ROOT/'tools/footprint/configuration-development.json').read_text(encoding='utf-8')))
    commands=[]
    def run(label,argv,config=None):
        result=execute(argv,ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),30,observation=config)
        result['label']=label;result['raw']=[{'path':p.name,'sha256':sha(p)} for p in (out/(label+'-stdout.log'),out/(label+'-stderr.log'))]
        commands.append(result);save(out/'commands.json',commands);print(label,result['status'],result['exit_code'],str(out),flush=True);return result
    unit=run('guards',[sys.executable,'-X','utf8','-m','unittest','discover','-s',str(out/'source/tests/tools/footprint'),'-p','test_*.py','-v'])
    if not owned_success(unit):return 1
    observed={}
    for mode in ('none','thread'):
        result=run(mode,[str(binary),'--control',mode],configuration)
        require_complete(result);observed[mode]=result
        save(out/(mode+'-summary.json'),summarize_observation(result))
    def ids(mode,phase):return next(s['thread_ids'] for s in observed[mode]['observation']['samples'] if s['reason']==phase)
    baseline=ids('none','HostReady');before=ids('thread','HostConstructionBegin');held=ids('thread','HostReady');after=ids('thread','ShutdownComplete')
    good=(len(held)==len(before)+1 and set(before)<set(held) and after==before and len(baseline)==len(before))
    save(out/'result.json',{'status':'Passed' if good else 'Failed','scope':'v2 guards and held-thread boundary control only','baseline':baseline,'before':before,'held':held,'after_join':after,'baseline_thread_attribution':'Unresolved','pilot':False,'budget_status':'NotApproved'})
    return 0 if good else 1


if __name__=='__main__':sys.exit(main())
