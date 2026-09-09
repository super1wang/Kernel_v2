"""D3.07/G3-C：已批准有限预算下的同来源 Embedded 正式采样。"""
import argparse
from dataclasses import replace
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.common import digest,now,read_json,save_json,sha_file
from tools.evidence.process import execute
from tools.footprint.windows_process import ObservationConfig,PHASES
from tools.footprint.analyze import require_complete,owned_success,statistics,paired_differences,validate_budget
from tools.footprint.formal import bounded_metrics

BLOCKS=6
PROFILES={'win-msvc-release':('Release',False),'win-msvc-debug':('Debug',True),'win-msvc-asan':('RelWithDebInfo',True)}

def item(path):
    p=Path(path).resolve();return {'path':str(p),'sha256':sha_file(p),'bytes':p.stat().st_size}

def verify(rows):
    for row in rows:
        if item(row['path'])!=row:raise ValueError('material changed: '+row['path'])

def identity(profile):
    config,allocation=PROFILES[profile]
    paths=[ROOT/p for p in ('dependencies.lock','CMakeLists.txt','CMakePresets.json','tools/evidence/process.py','tools/evidence/common.py','docs/contracts/embedded-footprint-method.md')]
    for base in ('packages','cmake','sdk','tools/footprint','tests/contract/native','examples/embedded_service'):
        paths.extend(p for p in (ROOT/base).rglob('*') if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc','.pyo'))
    sources=[{'path':p.relative_to(ROOT).as_posix(),'sha256':sha_file(p)} for p in sorted(set(paths))]
    observation=replace(ObservationConfig(**read_json(ROOT/'tools/footprint/configuration-development.json')),mode='allocation' if allocation else 'occupancy')
    value={'format':'ock.embedded-footprint-method/1','profile':profile,'configuration':config,'blocks':BLOCKS,
        'architecture':'x64','asan':profile=='win-msvc-asan','allocation':allocation,'lto':False,'cpu_workers':2,
        'logging':'default-memory-128','os':platform.platform(),'cpu':platform.processor(),'logical_cpus':os.cpu_count(),
        'python':platform.python_version(),'observation':observation.identity(),'sources':sources}
    return digest(value),value,observation

def allocations(objects,kind,profile):
    probes=next(o for o in objects if o.get('format')=='ock.footprint-allocation/1')
    counts=next(o for o in objects if o.get('format')=='ock.embedded-allocation/2')
    if probes['verified'] is not True or [(r['window'],r['index'],r['success']) for r in probes['samples']]!=[('probe',i,True) for i in range(12)]+[('probe_negative',0,True)]:raise ValueError('allocation probes missing')
    if counts['process_channel']!=('ASan' if profile=='win-msvc-asan' else 'DebugCRT'):raise ValueError('wrong allocator channel')
    if counts['verified'] is not True or counts['background_positive']<=0 or counts['negative']!=0:raise ValueError('allocator controls failed')
    rows=counts['samples'];zero=[r for r in rows if r['zero_required']]
    expected=[('warmup',i,False) for i in range(4)]
    labels=(('invoke',True),('submit_wait_result',False)) if kind=='embedded' else (('local_call',True),('second_local_call',True))
    expected.extend((name,i,zero_required) for i in range(40) for name,zero_required in labels)
    if kind=='embedded':expected.append(('resource_child_cancel',0,False))
    if [(r['window'],r['index'],r['zero_required']) for r in rows]!=expected:raise ValueError('allocation window identity/order mismatch')
    if len(rows)!=(85 if kind=='embedded' else 84) or len(zero)!=(40 if kind=='embedded' else 80):raise ValueError('allocation windows missing')
    if any(r['cpp']!=0 or r['process_allocations']!=0 for r in zero):raise ValueError('bounded Invoke allocation regression')
    return counts

def check_limits(metrics,budget):
    bounded_metrics(metrics,budget['limits'])

def collect(args):
    profile=args.profile;configuration,allocation=PROFILES[profile];asan=profile=='win-msvc-asan'
    method,inputs,observation=identity(profile);created=now()
    approval=read_json(ROOT/'footprint-budgets.json')['embedded']
    budgets=approval['budgets']
    if approval['actor_type']!='AI' or sha_file(ROOT/approval['decision'])!=approval['decision_sha256'] or sha_file(ROOT/'docs/reviews/automatic-acceptance-policy.json')!=approval['policy_sha256']:
        raise ValueError('budget review binding is incomplete')
    frozen_approval=[item(ROOT/'footprint-budgets.json'),item(ROOT/approval['decision']),item(ROOT/'docs/reviews/automatic-acceptance-policy.json')]
    keys=[profile+'/'+('allocation' if allocation else 'occupancy')]
    if profile=='win-msvc-release':keys.append(profile+'/startup')
    for key in keys:validate_budget(budgets[key],method_digest=method,report_created=created,run_ids=[])
    out=ROOT/'evidence/G3/C'/uuid.uuid4().hex;out.mkdir(parents=True)
    work=ROOT/'build'/('embedded-formal-'+out.name);prefix=work/'installed';build=work/'consumer'
    save_json(out/'identity.json',inputs)
    save_json(out/'budget-binding.json',{'file':item(ROOT/'footprint-budgets.json'),'selected':{k:budgets[k] for k in keys}})
    save_json(out/'source.json',{'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
        'dirty_status':subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],cwd=ROOT,text=True),'inputs_sha256':method})
    commands=[];inventory=list(frozen_approval);rows=[]
    tools=Path(read_json(ROOT/'cmake/msvc-validation-tools.json')['source_hint'])
    os.environ['PATH']=str(tools)+os.pathsep+os.environ.get('PATH','')
    print(out,flush=True)
    def run(label,argv,obs=None):
        r=execute(argv,ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),240 if obs is None else 30,observation=obs)
        r['label']=label;r['raw']=[item(out/(label+'-'+stream+'.log')) for stream in ('stdout','stderr')]
        commands.append(r);save_json(out/'commands.json',commands)
        if not owned_success(r):raise ValueError('command failed: '+label)
        return r
    try:
        producer=args.producer.resolve()
        run('producer-build',['cmake','--build',str(producer),'--config',configuration,'--parallel','4','--','/nr:false'])
        graph=read_json(producer/'ock-target-graph.json')
        if set(graph['targets'])!={'Foundation','CoreContracts','Runtime','Adapter::CpuPool'}:raise ValueError('not an Embedded producer')
        acquisition=read_json(producer/'dependency-acquisition.json')
        if set(acquisition['selected'])!={'expected','thread_pool'}:raise ValueError('dynamic dependencies acquired')
        run('architecture',[sys.executable,'-X','utf8',str(ROOT/'tools/architecture/check.py'),'--graph',str(producer/'ock-target-graph.json')])
        run('install',['cmake','--install',str(producer),'--config',configuration,'--prefix',str(prefix)])
        manifest=read_json(prefix/'share/ock/sdk_api_manifest.json')
        if manifest['installation_profile']!='Embedded' or manifest['sdk_version']!='0.1.0-dev.6':raise ValueError('wrong installed SDK')
        installed=[item(p) for p in sorted(prefix.rglob('*')) if p.is_file()];inventory.extend(installed)
        save_json(out/'installed.json',installed)
        for name in ('CMakeCache.txt','ock-target-graph.json','dependency-acquisition.json'):
            shutil.copy2(producer/name,out/('producer-'+name))
        for target in ('ock_Runtime','ock_Adapter_CpuPool'):
            traces=list((producer/(target+'.dir')/configuration).glob('*.tlog/CL.command.1.tlog'))
            if len(traces)!=1:raise ValueError('missing producer compiler trace')
            trace=traces[0].read_text(encoding='utf-16').lower()
            if set(re.findall(r'/(mdd|md|mtd|mt)\b',trace))!={'mdd' if configuration=='Debug' else 'md'} or ('/fsanitize=address' in trace)!=asan or re.search(r'/gl\b',trace):raise ValueError('producer CRT/ASan/LTO mismatch')
            shutil.copy2(traces[0],out/(target+'-CL.command.tlog'))
        run('configure',['cmake','-S',str(ROOT/'tools/footprint/embedded_consumer'),'-B',str(build),
            '-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',
            '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),'-DCMAKE_PREFIX_PATH='+str(prefix),
            '-DOCK_EMBEDDED_ALLOCATION='+('ON' if allocation else 'OFF'),'-DOCK_EMBEDDED_ASAN='+('ON' if asan else 'OFF')])
        run('build',['cmake','--build',str(build),'--config',configuration,'--parallel','4','--','/nr:false'])
        binaries={kind:build/configuration/('footprint_'+kind+'.exe') for kind in ('baseline','embedded')}
        for kind,binary in binaries.items():
            inventory.append(item(binary));shutil.copy2(binary,out/(kind+'.exe'))
            run(kind+'-imports',[str(tools/'dumpbin.exe'),'/imports',str(binary)])
            traces=list((build/('footprint_'+kind+'.dir')/configuration).glob('*.tlog/CL.command.1.tlog'))
            if len(traces)!=1:raise ValueError('missing actual compiler trace')
            trace=traces[0].read_text(encoding='utf-16').lower()
            if set(re.findall(r'/(mdd|md|mtd|mt)\b',trace))!={'mdd' if configuration=='Debug' else 'md'} or ('/fsanitize=address' in trace)!=asan or re.search(r'/gl\b',trace):raise ValueError('CRT/ASan/LTO mismatch')
            shutil.copy2(traces[0],out/(kind+'-CL.command.tlog'))
            shutil.copy2(build/('footprint_'+kind+'.vcxproj'),out/('footprint_'+kind+'.vcxproj'))
        shutil.copy2(build/'CMakeCache.txt',out/'consumer-CMakeCache.txt')
        for block in range(BLOCKS):
            for index,kind in enumerate(('baseline','embedded','embedded','baseline')):
                label=f'b{block}-{index}-{kind}';r=run(label,[str(binaries[kind])],observation);require_complete(r)
                if (out/(label+'-stderr.log')).stat().st_size:raise ValueError('consumer stderr is not empty')
                objects=[json.loads(line) for line in (out/(label+'-stdout.log')).read_text(encoding='utf-8').splitlines() if line.startswith('{')]
                consumer=next(o for o in objects if o.get('kind')==kind and 'construction_ticks' in o)
                if consumer['released'] is not True:raise ValueError('consumer ownership not released')
                if kind=='embedded' and any(consumer[k]!=v for k,v in {'workers':2,'warmup':4,'invoke':40,'submit':40,'resource_child_cancel':True,'default_memory_log':True}.items()):raise ValueError('complete Embedded fixture required')
                counted=allocations(objects,kind,profile) if allocation else None
                obs=r['observation'];samples=obs['samples'];boundary={s['reason']:s for s in samples if s['thread_ids'] is not None}
                before=set(boundary['HostConstructionBegin']['thread_ids']);ready=set(boundary['HostReady']['thread_ids'])
                added=ready-before
                if len(added)!=(3 if kind=='embedded' else 0):raise ValueError('Ready thread increment differs')
                if any(added&set(boundary[p]['thread_ids']) for p in ('ShutdownComplete','BoundReleased','SessionReleased','OwnersReleased','ExitPermitted')):raise ValueError('kernel threads survived shutdown')
                observed_peak=max(len(s['thread_ids']) for s in boundary.values())
                if observed_peak-len(before)>(3 if kind=='embedded' else 0):raise ValueError('sampled thread increment exceeds fixed construction bound')
                windows={}
                for begin,end in zip(obs['phases'],obs['phases'][1:]):
                    points=[s for s in samples if begin['parent_observed_ticks']<=s['ticks']<end['parent_observed_ticks']]
                    if not points:raise ValueError('missing phase memory window')
                    windows[begin['phase']]={key:statistics([p[key] for p in points]) for key in ('private_bytes','working_set_bytes')}
                modules=[item(p) for p in sorted({p for m in obs['modules'] for p in m['paths']})];inventory.extend(modules)
                idle=windows['IdleSamplingBegin'];host_ready=boundary['HostReady']
                rows.append({'run_id':str(r['pid'])+'-'+label+'-'+out.name,'block':block,'kind':'A' if kind=='baseline' else 'B','identity':method,
                    'private_peak':max(s['private_bytes'] for s in samples),'working_set_peak':max(s['working_set_bytes'] for s in samples),
                    'ready_private':idle['private_bytes']['max'],'ready_working_set':idle['working_set_bytes']['max'],
                    'ready_peak_commit':host_ready['peak_commit_bytes'],'ready_peak_working_set':host_ready['peak_working_set_bytes'],
                    'windows':windows,'modules':modules,'sample_count':len(samples),'missed_intervals':sum(s['missed_intervals'] for s in samples),
                    'sampled_thread_peak':observed_peak,'kernel_threads_at_ready':sorted(added),'threads':{k:v['thread_ids'] for k,v in boundary.items()},'allocation':counted})
                save_json(out/'samples.json',rows)
            print(profile,'block',block+1,'/',BLOCKS,flush=True)
        metrics={};stats={}
        for key in ('private_peak','working_set_peak','ready_private','ready_working_set','ready_peak_commit','ready_peak_working_set'):
            delta=paired_differences([{**r,'value':r[key]} for r in rows],BLOCKS)
            stats[key]={'embedded':statistics([r[key] for r in rows if r['kind']=='B']),'paired_increment':statistics(delta)}
            metrics[key+'_absolute_max']=stats[key]['embedded']['max'];metrics[key+'_paired_max']=stats[key]['paired_increment']['max']
        system=Path(os.environ['SystemRoot']).resolve();binary_paths={str(p.resolve()).casefold() for p in binaries.values()}
        extra=[]
        for block in range(BLOCKS):
            group=rows[block*4:block*4+4]
            for a,b in ((group[0],group[1]),(group[3],group[2])):
                baseline={m['path'].casefold() for m in a['modules']}
                additional=[m for m in b['modules'] if m['path'].casefold() not in baseline|binary_paths and
                    (not Path(m['path']).resolve().is_relative_to(system) or Path(m['path']).name.lower().startswith(('msvcp','vcruntime','concrt','clang_rt')))]
                extra.append({'baseline':a['run_id'],'embedded':b['run_id'],'new_distribution_modules':additional,'bytes':sum(m['bytes'] for m in additional)})
        metrics['distribution_increment_bytes']=binaries['embedded'].stat().st_size-binaries['baseline'].stat().st_size+max(x['bytes'] for x in extra)
        save_json(out/'distribution.json',{'exe':{k:item(v) for k,v in binaries.items()},'pairs':extra,'excluded_from_distribution':'OS-provided modules under SystemRoot except redistributable C++/sanitizer runtimes; all loaded modules retained in samples'})
        check_limits(metrics,budgets[keys[0]])
        if profile=='win-msvc-release':
            run('startup',[sys.executable,'-u','-X','utf8',str(ROOT/'tools/footprint/embedded_startup.py'),'--build',str(build/configuration),'--output',str(out/'startup'),'--blocks',str(BLOCKS)])
            startup=read_json(out/'startup/samples.json');startup_metrics={}
            for key in ('construction_to_ready_ms','create_to_ready_ms'):
                points=[{'run_id':str(i),'identity':method,'block':v['block'],'kind':'A' if v['kind']=='baseline' else 'B','value':v[key]} for i,v in enumerate(startup)]
                startup_metrics[key+'_absolute_max']=max(v['value'] for v in points if v['kind']=='B')
                startup_metrics[key+'_paired_max']=max(paired_differences(points,BLOCKS))
            check_limits(startup_metrics,budgets[keys[1]])
        else:startup_metrics=None
        verify(inventory)
        for command in commands:verify(command['raw'])
        if identity(profile)[0]!=method:raise ValueError('source inputs changed during formal measurement')
        for key in keys:validate_budget(budgets[key],method_digest=method,report_created=created,run_ids=[r['run_id'] for r in rows])
        save_json(out/'artifacts.json',inventory)
        save_json(out/'report.json',{'format':'ock.embedded-footprint-report/1','status':'Passed','created_at':created,'completed_at':now(),
            'profile':profile,'method_digest':method,'blocks':BLOCKS,'budget_keys':keys,'metrics':metrics,'statistics':stats,'startup_metrics':startup_metrics,
            'kernel_thread_structural_upper_bound':3,'thread_proof':'docs/contracts/embedded-footprint-method.md','allocation_zero_verified':allocation,
            'scope':'complete Embedded; Debug/ASan are allocation diagnostics; phase thread observations plus fixed creation bound, not continuous ETW; G3-C artifact only'})
        return 0
    except Exception as exc:
        save_json(out/'report.json',{'format':'ock.embedded-footprint-report/1','status':'Failed','created_at':created,'profile':profile,'error':str(exc),'completed_samples':len(rows)})
        print(str(exc),file=sys.stderr);return 1

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--profile',choices=tuple(PROFILES),required=True)
    parser.add_argument('--producer',type=Path);parser.add_argument('--identity',action='store_true');args=parser.parse_args()
    if args.identity:print(json.dumps({'method_digest':identity(args.profile)[0],'inputs':identity(args.profile)[1]},ensure_ascii=False));return 0
    if args.producer is None:parser.error('--producer is required')
    return collect(args)
if __name__=='__main__':sys.exit(main())
