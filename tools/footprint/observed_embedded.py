"""A21.4 额外观测成本；固定范围、真实安装消费者，不覆盖默认预算。"""
import argparse
import json
from pathlib import Path
import shutil
import sys
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tests.install_consumer import verify_baseline as driver
from tools.evidence.common import sha_file,save_json
from tools.evidence.process import execute
from tools.footprint.windows_process import ObservationConfig
from tools.footprint.analyze import require_complete,statistics


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--prefix',required=True,type=Path)
    args=parser.parse_args();prefix=args.prefix.resolve()
    assert json.loads((prefix/'share/ock/sdk_api_manifest.json').read_text())['installation_profile']=='Embedded'
    out=ROOT/'evidence/B5'/('observed-embedded-'+uuid.uuid4().hex[:10]);out.mkdir(parents=True)
    print(out,flush=True);driver._work=out;driver._records=[]
    sources=['tools/footprint/observed_embedded/CMakeLists.txt','tools/footprint/observed_embedded/main.cpp',
      'tools/footprint/consumer/channel.hpp','examples/embedded_service/fixture.hpp']
    for relative in sources:
        copied=out/'source'/relative;copied.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/relative,copied)
    inventory=[{'path':p.relative_to(prefix).as_posix(),'size':p.stat().st_size,'sha256':sha_file(p)} for p in sorted(prefix.rglob('*')) if p.is_file()]
    methods=sources+['tools/footprint/observed_embedded.py','tools/evidence/process.py','tools/footprint/windows_process.py']
    inputs={'prefix':str(prefix),'installed':inventory,'configuration':'Release','allocation':False,
      'sources':[{'path':p,'sha256':sha_file(ROOT/p)} for p in methods],
      'scope':'36 processes: 3 ABBA blocks for each of default/trace, trace/file, file/slow; operation-boundary trace; default Host logging retained'}
    save_json(out/'inputs.json',inputs)
    build=ROOT/'build'/out.name
    driver.run(['cmake','-S',str(out/'source/tools/footprint/observed_embedded'),'-B',str(build),
      '-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207',
      '-DCMAKE_SYSTEM_VERSION=10.0.26100.0','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),'-DCMAKE_PREFIX_PATH='+str(prefix)])
    driver.run(['cmake','--build',str(build),'--config','Release','--parallel','4','--','/nr:false'])
    binary=build/'Release/observed_embedded.exe';shutil.copy2(binary,out/binary.name)
    save_json(out/'binary.json',{'path':str(binary),'size':binary.stat().st_size,'sha256':sha_file(binary)})
    config=ObservationConfig(**json.loads((ROOT/'tools/footprint/configuration-development.json').read_text()))
    rows=[]
    for a,b in ((0,1),(1,2),(2,3)):
        for block in range(3):
            for mode in (a,b,b,a):
                label=f'{len(rows):02d}-mode{mode}'
                log=out/(label+'-trace.log');argv=[str(binary),'--mode',str(mode)]
                if mode>=2:argv+=['--log',str(log)]
                raw=execute(argv,ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),30,observation=config)
                save_json(out/(label+'-command.json'),raw);require_complete(raw)
                result=json.loads((out/(label+'-stdout.log')).read_text().splitlines()[-1])
                assert result['mode']==mode and result['calls']==85 and result['released'] is True
                assert result['attempted']==(170 if mode else 0) and result['accepted']+result['rejected']==result['attempted']
                assert len(result['operation_ticks'])==85 and all(type(x) is int and x>=0 for x in result['operation_ticks'])
                if mode>=2:
                    assert 0<result['file_records']<=result['accepted'] and len(log.read_bytes().splitlines())==result['file_records']
                obs=raw['observation'];phases={s['reason']:s for s in obs['samples'] if s.get('thread_ids') is not None}
                before=set(phases['HostConstructionBegin']['thread_ids']);ready=set(phases['HostReady']['thread_ids'])
                added=ready-before
                assert len(added)==(4 if mode>=2 else 3),phases
                assert all(not added.intersection(s['thread_ids']) for key,s in phases.items() if key in ('ShutdownComplete','OwnersReleased','ExitPermitted'))
                row={'run':label,'pair':[a,b],'block':block,'mode':mode,'consumer':result,
                  'ready':{key:phases['HostReady'][key] for key in ('private_bytes','working_set_bytes','thread_ids')},
                  'threads':{key:s['thread_ids'] for key,s in phases.items()},
                  'private_bytes':statistics([s['private_bytes'] for s in obs['samples']]),
                  'working_set_bytes':statistics([s['working_set_bytes'] for s in obs['samples']]),
                  'cpu_ms':result['cpu_100ns']/10000,
                  'operation_ms':statistics([x*1000/result['qpc_frequency'] for x in result['operation_ticks']])}
                if mode>=2:row['file']={'path':log.name,'size':log.stat().st_size,'sha256':sha_file(log)}
                rows.append(row);save_json(out/'samples.json',rows)
                print(label,'passed',row['ready']['private_bytes'],'bytes; CPU',row['cpu_ms'],'ms',flush=True)
    assert len(rows)==36
    for item in inventory:assert sha_file(prefix/item['path'])==item['sha256']
    for item in inputs['sources']:assert sha_file(ROOT/item['path'])==item['sha256']
    summary={str(mode):{key:statistics([row[key] if key=='cpu_ms' else row['ready'][key] for row in rows if row['mode']==mode])
        for key in ('cpu_ms','private_bytes','working_set_bytes')} for mode in range(4)}
    save_json(out/'result.json',{'status':'Collected','budget_evaluation':'NotApplicableToAdditionalModes',
      'scope':inputs['scope'],'default_budget_reports_unchanged':True,'samples':36,'summary':summary})
    print('Additional observation cost collected:',out,flush=True)


if __name__=='__main__':main()
