"""安装公开 API 单对连通/占用原始采集；不执行 pilot、预算或计数模式。"""
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
from tools.footprint.analyze import owned_success,require_complete
from tools.footprint.pair_record import validate_record


def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--prefix',required=True,type=Path);parser.add_argument('--install-record',required=True,type=Path)
    args=parser.parse_args();prefix=args.prefix.resolve()
    installed=json.loads(args.install_record.read_text(encoding='utf-8'))
    if Path(installed['prefix']).resolve()!=prefix:raise ValueError('installation identity mismatch')
    for row in installed['files']:
        p=prefix/row['path']
        if not p.resolve().is_relative_to(prefix) or digest(p)!=row['sha256'] or p.stat().st_size!=row['size']:raise ValueError('installed source changed')
    out=ROOT/'evidence/bootstrap/D1.06'/('footprint-native-pair-'+uuid.uuid4().hex[:12]);out.mkdir()
    shutil.copyfile(args.install_record,out/'install-record.json');shutil.copytree(prefix,out/'prefix')
    paths=[ROOT/'tools/evidence/process.py',ROOT/'cmake/LockedMSVC.cmake',ROOT/'cmake/msvc-validation-tools.json',ROOT/'dependencies.lock',ROOT/'docs/contracts/native-footprint-method.md']
    for directory in ('tools/footprint','tests/tools/footprint'):
        paths.extend(p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    rows=[]
    for p in paths:
        rel=p.relative_to(ROOT);dest=out/'source'/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
        rows.append({'path':rel.as_posix(),'sha256':digest(dest)})
    save(out/'sources.json',sorted(rows,key=lambda r:r['path']))
    for row in installed['files']:
        if digest(out/'prefix'/row['path'])!=row['sha256']:raise ValueError('relocated prefix copy mismatch')
    commands=[]
    def run(label,argv,timeout=120,observation=None):
        result=execute(argv,ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),timeout,observation=observation)
        result['label']=label;result['raw']=[{'path':p.name,'sha256':digest(p)} for p in (out/(label+'-stdout.log'),out/(label+'-stderr.log'))]
        commands.append(result);save(out/'commands.json',commands)
        print(label,result['status'],result['exit_code'],str(out),flush=True);return result
    unit=run('record-guards',[sys.executable,'-X','utf8','-m','unittest','discover','-s',str(out/'source/tests/tools/footprint'),'-p','test_pair_record.py','-v'])
    if not owned_success(unit):return 1
    source=out/'source/tools/footprint/consumer';build=ROOT/'build'/out.name
    configure=run('configure',['cmake','-S',str(source),'-B',str(build),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),'-DCMAKE_PREFIX_PATH='+str(out/'prefix')])
    if not owned_success(configure):return 1
    built=run('build',['cmake','--build',str(build),'--config','Debug','--parallel','2','--','/nr:false','/v:normal'])
    if not owned_success(built):return 1
    for name in ('CMakeCache.txt','footprint_baseline.vcxproj','footprint_native.vcxproj'):shutil.copyfile(build/name,out/name)
    configuration=ObservationConfig(**json.loads((out/'source/tools/footprint/configuration-development.json').read_text(encoding='utf-8')))
    tool_hint=json.loads((ROOT/'cmake/msvc-validation-tools.json').read_text(encoding='utf-8'))['source_hint']
    reports=[]
    for kind in ('baseline','native'):
        binary=build/'Debug'/('footprint_'+kind+'.exe')
        imports=run(kind+'-imports',[str(Path(tool_hint)/'dumpbin.exe'),'/imports',str(binary)])
        if not owned_success(imports):return 1
        command=run(kind,[str(binary)],30,configuration)
        try:
            require_complete(command)
            record=json.loads((out/(kind+'-stdout.log')).read_text(encoding='utf-8'))
            validate_record(record,kind)
        except (ValueError,KeyError) as exc:
            save(out/'result.json',{'status':'Failed','scope':'single Debug pair connectivity only','failed_consumer':kind,'error':str(exc),'budget_status':'NotApproved'});return 1
        observation=command['observation'];paths=sorted({p for item in observation['modules'] for p in item['paths']})
        modules=[{'path':p,'sha256':digest(Path(p)),'bytes':Path(p).stat().st_size} for p in paths]
        reports.append({'kind':kind,'binary':{'path':str(binary),'sha256':digest(binary),'bytes':binary.stat().st_size},'consumer':record,'modules':modules,'samples':len(observation['samples']),'missed_intervals':sum(s['missed_intervals'] for s in observation['samples']),'ready_threads':next(s['thread_ids'] for s in observation['samples'] if s['reason']=='HostReady'),'ready':observation['ready']})
    save(out/'pair.json',reports)
    save(out/'result.json',{'status':'Passed','scope':'single Debug installed consumer connectivity and raw occupancy collection only','order':['baseline','native'],'pair_count':1,'pilot':False,'budget_status':'NotApproved','sampling_coverage':'NotEstablished','allocation_counting':'NotMeasured','formal_latency':'NotMeasured'})
    return 0


if __name__=='__main__':sys.exit(main())
