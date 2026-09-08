"""有限 Debug 分配模式：真实注入 red、正常单对与无计数消费者回归。"""
import argparse
from dataclasses import replace
import json
from pathlib import Path
import shutil
import sys
import uuid
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.footprint.windows_process import ObservationConfig,PHASES
from tools.footprint.analyze import owned_success,require_complete,require_sampling_v2
from tools.footprint.pair_record import validate_record
from tools.footprint.allocation_record import validate_allocation
from tools.footprint.pair_develop import digest,save


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--prefix',type=Path,required=True);parser.add_argument('--install-record',type=Path,required=True)
    args=parser.parse_args();prefix=args.prefix.resolve();installed=json.loads(args.install_record.read_text(encoding='utf-8'))
    if Path(installed['prefix']).resolve()!=prefix:raise ValueError('installation identity mismatch')
    for row in installed['files']:
        p=prefix/row['path']
        if not p.resolve().is_relative_to(prefix) or digest(p)!=row['sha256']:raise ValueError('installed source changed')
    out=ROOT/'evidence/bootstrap/D1.06'/('footprint-allocation-'+uuid.uuid4().hex[:12]);out.mkdir()
    shutil.copyfile(args.install_record,out/'install-record.json');shutil.copytree(prefix,out/'prefix')
    paths=[ROOT/'tools/evidence/process.py',ROOT/'cmake/LockedMSVC.cmake',ROOT/'cmake/msvc-validation-tools.json',ROOT/'dependencies.lock',ROOT/'docs/contracts/native-footprint-method.md',ROOT/'tests/contract/native/allocation_probe.hpp',ROOT/'tests/contract/native/allocation_probe.cpp']
    for directory in ('tools/footprint','tests/tools/footprint'):
        paths.extend(p for p in (ROOT/directory).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    sources=[]
    for p in paths:
        rel=p.relative_to(ROOT);dest=out/'source'/rel;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,dest)
        sources.append({'path':rel.as_posix(),'sha256':digest(dest)})
    save(out/'sources.json',sorted(sources,key=lambda row:row['path']))
    for row in installed['files']:
        if digest(out/'prefix'/row['path'])!=row['sha256']:raise ValueError('relocated prefix copy changed')
    commands=[]
    def run(label,argv,configuration=None):
        result=execute(argv,ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),120 if configuration is None else 30,observation=configuration)
        result['label']=label;result['raw']=[{'path':p.name,'sha256':digest(p)} for p in (out/(label+'-stdout.log'),out/(label+'-stderr.log'))]
        commands.append(result);save(out/'commands.json',commands);print(label,result['status'],result['exit_code'],str(out),flush=True);return result
    unit=run('allocation-record-guards',[sys.executable,'-X','utf8','-m','unittest','discover','-s',str(out/'source/tests/tools/footprint'),'-p','test_allocation_record.py','-v'])
    if not owned_success(unit):return 1
    config=ObservationConfig(**json.loads((out/'source/tools/footprint/configuration-development.json').read_text(encoding='utf-8')))
    assertions=[];artifacts=[]
    for mode in ('allocation','occupancy'):
        build=ROOT/'build'/(out.name+'-'+mode)
        configured=run(mode+'-configure',['cmake','-S',str(out/'source/tools/footprint/consumer'),'-B',str(build),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),'-DCMAKE_PREFIX_PATH='+str(out/'prefix'),'-DOCK_FOOTPRINT_MODE='+mode])
        if not owned_success(configured):return 1
        built=run(mode+'-build',['cmake','--build',str(build),'--config','Debug','--parallel','2','--','/nr:false','/v:normal'])
        if not owned_success(built):return 1
        shutil.copyfile(build/'CMakeCache.txt',out/(mode+'-CMakeCache.txt'))
        targets=['native_injected','baseline','native'] if mode=='allocation' else ['baseline','native']
        for target in targets:
            kind='baseline' if target=='baseline' else 'native';label=mode+'-'+target;injected=target=='native_injected'
            binary=build/'Debug'/('footprint_'+target+'.exe')
            artifacts.append({'mode':mode,'target':target,'path':str(binary),'sha256':digest(binary),'bytes':binary.stat().st_size})
            shutil.copyfile(build/('footprint_'+target+'.vcxproj'),out/(label+'.vcxproj'))
            result=run(label,[str(binary)],replace(config,mode=mode))
            try:
                if injected:
                    tree=result['process_tree'];observation=result['observation']
                    if not (result['status']=='Exited' and result['exit_code']==1 and tree['assigned_before_resume'] and tree['active_after']==0 and not tree['terminated_owned_job'] and observation['status']=='Complete' and [p['phase'] for p in observation['phases']]==list(PHASES)):
                        raise ValueError('injection did not complete business protocol with intentional failure')
                    require_sampling_v2(observation)
                else:require_complete(result)
                records=[json.loads(line) for line in (out/(label+'-stdout.log')).read_text(encoding='utf-8').splitlines()]
                if len(records)!=(2 if mode=='allocation' else 1):raise ValueError('consumer output count mismatch')
                validate_record(records[0],kind,'allocation' if mode=='allocation' else 'disabled')
                if mode=='allocation':
                    validate_allocation(records[1],kind,'Debug',False,injected=injected)
                    save(out/(label+'-counts.json'),records[1])
                assertions.append({'name':label,'passed':True,'exit_code':result['exit_code'],'injection_red_proven':injected})
            except (ValueError,KeyError) as exc:
                save(out/'result.json',{'status':'Failed','scope':'Debug allocation increment only','failed_case':label,'error':str(exc),'pilot':False,'budget_status':'NotApproved'});return 1
    save(out/'artifacts.json',artifacts)
    save(out/'result.json',{'status':'Passed','scope':'Debug real allocation injection, allocation pair and changed uncounted consumer pair only','assertions':assertions,'configuration':'Debug','asan_executed':False,'release_executed':False,'pilot':False,'budget_status':'NotApproved','thread_attribution':'Unresolved'})
    return 0


if __name__=='__main__':sys.exit(main())
