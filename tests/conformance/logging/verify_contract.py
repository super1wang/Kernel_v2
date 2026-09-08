"""执行本轮真实 Logging 子进程；末项只聚合本轮原始结果，不重跑共同项。"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.evidence.common import save_json,sha_file
from tools.evidence.process import execute
from tests.conformance.logging.qualification import CASES,qualify


def require(ok,message):
    if not ok:raise ValueError(message)


def good(command):
    tree=command.get('process_tree',{})
    return (command.get('status')=='Exited' and command.get('exit_code')==0
      and command.get('observed_exit_code')==0 and tree.get('assigned_before_resume') is True
      and tree.get('active_after')==0 and tree.get('terminated_owned_job') is False)


def check_result(directory,case,run_id,binary,binding):
    result=json.loads((directory/'result.json').read_text(encoding='utf8'))
    require(result['case']==case and result['run_id']==run_id and result['status']=='Passed','wrong case/run/status')
    require(result['binary_sha256']==sha_file(binary) and result['binding_sha256']==binding['sha256'],'stale binary/source')
    require(result['command_sha256']==sha_file(directory/'commands.json'),'command digest differs')
    command=json.loads((directory/'commands.json').read_text(encoding='utf8'))
    require(good(command),'failed or unowned command')
    require(command['argv']==[str(binary),case],'not this actual binary/case')
    require(command['binary_sha256']==result['binary_sha256'],'command binary differs')
    for stream in ('stdout','stderr'):
        item=command['raw'][stream];path=directory/(stream+'.log')
        require(item=={'sha256':sha_file(path),'size':path.stat().st_size},'raw digest/size differs')
    lines=(directory/'stdout.log').read_bytes().splitlines()
    require(lines.count(('logging_binding '+binding['sha256']+' case '+case).encode())==1,'compiled binding missing')
    require(lines.count(('assertions_completed '+case).encode())==1,'actual assertions incomplete')
    return result


def main():
    p=argparse.ArgumentParser()
    for key in ('binary','case','run-id','binding','work'):p.add_argument('--'+key,required=True)
    a=p.parse_args();require(a.case in CASES and re.fullmatch('[0-9a-f]{24}',a.run_id),'invalid fixed case/run')
    binding=json.loads(Path(a.binding).read_text(encoding='utf8'))
    sources=binding['sources'];require(len(sources)==len({s['path'] for s in sources}) and sources,'invalid source set')
    for source in sources:require(sha_file(ROOT/source['path'])==source['sha256'],'build source changed: '+source['path'])
    digest=hashlib.sha256(''.join(s['path']+':'+s['sha256']+'\n' for s in sources).encode()).hexdigest()
    require(digest==binding['sha256'],'invalid source identity')
    binary=Path(a.binary).resolve();binary_sha=sha_file(binary)
    run=Path(a.work)/a.run_id;out=run/a.case;out.mkdir(parents=True,exist_ok=False)
    command=execute([str(binary),a.case],ROOT,out/'stdout.log',out/'stderr.log',60)
    command['binary_sha256']=binary_sha
    command['raw']={s:{'sha256':sha_file(out/(s+'.log')),'size':(out/(s+'.log')).stat().st_size} for s in ('stdout','stderr')}
    save_json(out/'commands.json',command)
    result={'case':a.case,'run_id':a.run_id,'binary_sha256':binary_sha,'binding_sha256':digest,
            'status':'Passed' if good(command) else 'Failed','command_sha256':sha_file(out/'commands.json')}
    save_json(out/'result.json',result)
    try:
        check_result(out,a.case,a.run_id,binary,binding)
        if a.case==CASES[-1]:
            rows=[check_result(run/case,case,a.run_id,binary,binding) for case in CASES]
            qualification=qualify(binding,rows,a.run_id,binary_sha)
            save_json(run/'qualification.json',qualification)
            require(qualification['qualified'],'actual Logging factories unqualified')
    except Exception:
        result['status']='Failed';save_json(out/'result.json',result);raise
    print(json.dumps({'case':a.case,'status':'Passed','evidence':str(out)}))


if __name__=='__main__':
    try:main()
    except (ValueError,KeyError,OSError) as error:
        print('Logging qualification failed: '+str(error),file=sys.stderr);sys.exit(1)
