"""G3-C 有限 Embedded pilot；复用进程采样，不产生预算或 Gate Passed。"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tests.install_consumer import verify_baseline as driver
from tools.evidence.process import execute
from tools.footprint.windows_process import ObservationConfig
from tools.footprint.analyze import require_complete,statistics

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,value):path.write_text(json.dumps(value,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--prefix',required=True,type=Path)
    parser.add_argument('--config',choices=('Debug','Release'),default='Debug');args=parser.parse_args()
    prefix=args.prefix.resolve()
    manifest=json.loads((prefix/'share/ock/sdk_api_manifest.json').read_text(encoding='utf-8'))
    assert manifest['installation_profile']=='Embedded'
    out=ROOT/'evidence/B5'/('embedded-pilot-'+uuid.uuid4().hex[:10]);out.mkdir(parents=True)
    driver._work=out;driver._records=[]
    source_files=['tools/footprint/embedded_consumer/CMakeLists.txt','tools/footprint/embedded_consumer/main.cpp',
      'tools/footprint/consumer/channel.hpp','examples/embedded_service/fixture.hpp']
    for relative in source_files:
        destination=out/'source'/relative;destination.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(ROOT/relative,destination)
    installed=[{'path':str(p.relative_to(prefix)),'bytes':p.stat().st_size,'sha256':digest(p)}
      for p in sorted(prefix.rglob('*')) if p.is_file()]
    save(out/'inputs.json',{'prefix':str(prefix),'installed':installed,
      'sources':[{'path':p,'sha256':digest(ROOT/p)} for p in source_files],
      'configuration':args.config,'scope':'pilot only; no approved Embedded budget'})
    build=ROOT/'build'/out.name
    driver.run(['cmake','-S',str(out/'source/tools/footprint/embedded_consumer'),'-B',str(build),
      '-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207',
      '-DCMAKE_SYSTEM_VERSION=10.0.26100.0','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),
      '-DCMAKE_PREFIX_PATH='+str(prefix)])
    driver.run(['cmake','--build',str(build),'--config',args.config,'--parallel','4','--','/nr:false'])
    configuration=ObservationConfig(**json.loads((ROOT/'tools/footprint/configuration-development.json').read_text(encoding='utf-8')))
    summaries=[]
    for index,kind in enumerate(('baseline','embedded','embedded','baseline')):
        binary=build/args.config/('footprint_'+kind+'.exe');label=f'{index}-{kind}'
        record=execute([str(binary)],ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),30,observation=configuration)
        save(out/(label+'-command.json'),record);require_complete(record)
        output=(out/(label+'-stdout.log')).read_text(encoding='utf-8').splitlines()
        result=json.loads(output[-1]);assert result['kind']==kind and result['released'] is True
        if kind=='embedded':
            assert {k:v for k,v in result.items() if k not in ('construction_ticks','qpc_frequency')}=={'kind':'embedded','workers':2,'warmup':4,'invoke':40,'submit':40,
                'resource_child_cancel':True,'default_memory_log':True,'released':True}
        assert type(result['construction_ticks']) is int and result['construction_ticks']>=0
        assert type(result['qpc_frequency']) is int and result['qpc_frequency']>0
        obs=record['observation'];samples=obs['samples']
        phases={s['reason']:s for s in samples if s.get('thread_ids') is not None}
        paths=sorted({p for group in obs['modules'] for p in group['paths']})
        summary={'kind':kind,'run':label,'binary':{'path':str(binary),'bytes':binary.stat().st_size,'sha256':digest(binary)},
          'modules':[{'path':p,'bytes':Path(p).stat().st_size,'sha256':digest(Path(p))} for p in paths],
          'ready':obs['ready'],'construction_ms':result['construction_ticks']*1000/result['qpc_frequency'],
          'private_bytes':statistics([s['private_bytes'] for s in samples]),
          'working_set_bytes':statistics([s['working_set_bytes'] for s in samples]),
          'boundary_threads':{k:len(v['thread_ids']) for k,v in phases.items()},
          'boundaries':{k:{f:v[f] for f in ('private_bytes','working_set_bytes','peak_working_set_bytes','peak_commit_bytes')} for k,v in phases.items()},
          'missed_intervals':sum(s['missed_intervals'] for s in samples),'consumer':result}
        summaries.append(summary);save(out/'pilot.json',summaries)
        print(label,'complete',summary['boundary_threads'],flush=True)
    for row in installed:
        p=prefix/row['path'];assert p.stat().st_size==row['bytes'] and digest(p)==row['sha256']
    save(out/'result.json',{'status':'Collected','budget_status':'NotApproved','formal':False,
        'scope':'one ABBA block of actual Embedded and baseline; phase thread samples are not continuous thread peak proof'})
    print(out,flush=True)

if __name__=='__main__':main()
