"""有限Release时延或独立线程来源诊断；不执行pilot或批准预算。"""
import argparse
from dataclasses import replace
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import uuid
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.footprint.windows_process import ObservationConfig
from tools.footprint.analyze import require_complete,owned_success
from tools.footprint.pair_record import validate_record
from tools.footprint.thread_record import validate_thread_record

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--mode',choices=['latency','thread-origin'],required=True)
    parser.add_argument('--install-record',type=Path,required=True);args=parser.parse_args()
    config='Release' if args.mode=='latency' else 'Debug'
    install=json.loads(args.install_record.read_text(encoding='utf-8'));prefix=Path(install['prefix'])
    stage='sdk-stage-e353eebb66' if config=='Release' else 'sdk-stage-da08b48728'
    if install.get('source_inputs_sha256')!=sha(ROOT/'evidence/bootstrap/D1.06'/stage/'inputs.json'):
        raise ValueError('diagnostic requires the fixed verified non-ASan configuration')
    if not (prefix/'lib/cmake/OCK'/('OCKTargets-'+config.lower()+'.cmake')).is_file():
        raise ValueError('missing matching imported configuration')
    out=ROOT/'evidence/bootstrap/D1.06'/('footprint-'+args.mode+'-'+uuid.uuid4().hex[:12]);out.mkdir()
    save(out/'install-record.json',install)
    for row in install['files']:
        path=prefix/row['path']
        if not path.resolve().is_relative_to(prefix.resolve()) or sha(path)!=row['sha256'] or path.stat().st_size!=row['size']:raise ValueError('installation changed')
    shutil.copytree(prefix,out/'prefix')
    sources=[]
    files=[ROOT/'tools/evidence/process.py',ROOT/'cmake/LockedMSVC.cmake',ROOT/'cmake/msvc-validation-tools.json',ROOT/'dependencies.lock']
    for base in ('tools/footprint','tests/tools/footprint'):
        files.extend(p for p in (ROOT/base).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    for p in files:
        relative=p.relative_to(ROOT);target=out/'source'/relative;target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(p,target)
        sources.append({'path':relative.as_posix(),'sha256':sha(target)})
    save(out/'sources.json',sources)
    source=out/'source/tools/footprint/consumer';build=ROOT/'build'/out.name
    if args.mode=='thread-origin':
        shutil.copyfile(source/'CMakeLists.txt',out/'original-consumer-CMakeLists.txt')
        with (source/'CMakeLists.txt').open('a',encoding='utf-8') as f:
            f.write('\nforeach(_target footprint_baseline footprint_native)\n target_compile_definitions(${_target} PRIVATE OCK_FOOTPRINT_THREAD_ORIGIN=1)\n target_link_libraries(${_target} PRIVATE Psapi)\nendforeach()\n'
                    'add_executable(footprint_held_thread pair_main.cpp)\ntarget_compile_features(footprint_held_thread PRIVATE cxx_std_20)\n'
                    'target_compile_options(footprint_held_thread PRIVATE /utf-8 /EHsc /Zc:__cplusplus /permissive-)\n'
                    'target_compile_definitions(footprint_held_thread PRIVATE OCK_FOOTPRINT_THREAD_ORIGIN=1 OCK_FOOTPRINT_HELD_THREAD=1)\n'
                    'target_link_libraries(footprint_held_thread PRIVATE Psapi)\n')
        save(out/'diagnostic-cmake.json',{'path':str(source/'CMakeLists.txt'),'sha256':sha(source/'CMakeLists.txt'),'scope':'only diagnostic target definitions; original source hash retained'})
    save(out/'effective-sources.json',[{**row,'sha256':sha(out/'source'/row['path'])} for row in sources])
    tool=json.loads((ROOT/'cmake/msvc-validation-tools.json').read_text())['source_hint'];os.environ['PATH']=tool+os.pathsep+os.environ.get('PATH','')
    commands=[]
    def run(label,argv,observation=None):
        streams=[out/(label+'-'+s+'.log') for s in ('stdout','stderr')]
        row=execute(argv,ROOT,*streams,180 if observation is None else 30,observation=observation)
        row['label']=label;row['raw']=[{'path':p.name,'sha256':sha(p),'size':p.stat().st_size} for p in streams]
        commands.append(row);save(out/'commands.json',commands);print(label,row['status'],row['exit_code'],out,flush=True)
        if not owned_success(row) or row.get('observed_exit_code')!=0:raise ValueError('owned command failed: '+label)
        return row,streams
    run('configure',['cmake','-S',str(source),'-B',str(build),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207',
        '-DCMAKE_SYSTEM_VERSION=10.0.26100.0','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),'-DCMAKE_PREFIX_PATH='+str(out/'prefix')])
    run('build',['cmake','--build',str(build),'--config',config,'--parallel','2','--','/nr:false','/v:normal'])
    for p in [build/'CMakeCache.txt',*build.glob('footprint_*.vcxproj')]:shutil.copyfile(p,out/p.name)
    observation=ObservationConfig(**json.loads((out/'source/tools/footprint/configuration-development.json').read_text()))
    observation=replace(observation,mode='latency' if args.mode=='latency' else 'occupancy')
    records=[]
    for kind in (['baseline','native'] if args.mode=='latency' else ['baseline','native','held_thread']):
        binary=build/config/('footprint_'+kind+'.exe');row,streams=run(kind,[str(binary)],observation);require_complete(row)
        consumer=json.loads(streams[0].read_text());validate_record(consumer,'native' if kind=='native' else 'baseline')
        entry={'kind':kind,'binary':{'path':str(binary),'sha256':sha(binary),'bytes':binary.stat().st_size},'consumer':consumer,'ready':row['observation']['ready']}
        if args.mode=='latency':
            if streams[1].read_bytes() or len(row['observation']['samples'])!=12:raise ValueError('not a clean phase-only latency run')
        else:
            record=json.loads(streams[1].read_text());validate_thread_record(record,row['pid'],kind=='held_thread')
            entry['thread_origin']=record
            entry['modules']=[{'path':p,'sha256':sha(Path(p)),'bytes':Path(p).stat().st_size} for p in sorted({t['module'] for t in record['threads']})]
        records.append(entry);save(out/'records.json',records)
    save(out/'result.json',{'status':'DevelopmentChecksPassed','mode':args.mode,'configuration':config,'pilot':False,'budget_status':'NotApproved','thread_causal_attribution':'NotEstablished','scope':'finite diagnostic only; no formal measurement approval'})
    return 0

if __name__=='__main__':sys.exit(main())
