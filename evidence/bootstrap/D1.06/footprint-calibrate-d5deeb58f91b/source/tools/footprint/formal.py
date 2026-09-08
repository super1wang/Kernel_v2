"""D1.06 固定 ABBA 入口。复用现有 owner、协议及计数校验；不自动批准预算。"""
import argparse
from dataclasses import replace
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import uuid

ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.footprint.windows_process import ObservationConfig, PHASES
from tools.footprint.analyze import (finite, owned_success, require_complete,
    require_sampling_v2, paired_differences, statistics, validate_budget)
from tools.footprint.pair_record import validate_record
from tools.footprint.allocation_record import validate_allocation
from tools.footprint.thread_record import validate_thread_record


def now(): return datetime.now(timezone.utc).isoformat()
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def save(p,v): Path(p).write_text(json.dumps(v,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
def load(p): return json.loads(Path(p).read_text(encoding='utf-8'))
def item(p): return {'path':str(Path(p).resolve()),'sha256':sha(p),'bytes':Path(p).stat().st_size}


def verify_inventory(rows):
    for r in rows:
        p=Path(r['path'])
        if not p.is_file() or sha(p)!=r['sha256'] or p.stat().st_size!=r['bytes']:
            raise ValueError('missing or changed material: '+str(p))


def bounded_metrics(metrics,limits):
    if set(metrics)!=set(limits) or any(not finite(v) or v<=0 for v in limits.values()):
        raise ValueError('exact finite budget metrics required')
    for k,v in metrics.items():
        if not finite(v) or v>limits[k]: raise ValueError('budget exceeded/invalid: '+k)


def identity(profile,mode):
    paths=[ROOT/'dependencies.lock',ROOT/'cmake/msvc-validation-tools.json',ROOT/'cmake/LockedMSVC.cmake']
    for base in ('packages','tools/footprint','tests/contract/native'):
        paths.extend(p for p in (ROOT/base).rglob('*') if p.is_file() and '__pycache__' not in p.parts)
    rows=[{'path':str(p.relative_to(ROOT)),'sha256':sha(p)} for p in sorted(set(paths))]
    config=replace(ObservationConfig(**load(ROOT/'tools/footprint/configuration-development.json')),mode=mode)
    value={'profile':profile,'mode':mode,'stage':'NativeSubset','architecture':'x64',
           'configuration':'Release' if profile=='win-msvc-release' else 'Debug',
           'asan':profile=='win-msvc-asan','lto':False,'logging':'default-memory',
           'counter_mode':'allocation' if mode=='allocation' else 'disabled',
           'observation':config.identity(),'sources':rows}
    return hashlib.sha256(json.dumps(value,sort_keys=True).encode()).hexdigest(),value,config


def measure(args):
    profile=args.profile; mode=args.mode; configuration='Release' if profile=='win-msvc-release' else 'Debug'
    asan=profile=='win-msvc-asan'; blocks=3 if args.pilot else 6
    task={'occupancy':'native_occupancy','allocation':'allocation_report','latency':'ready_latency'}[mode]
    out=ROOT/'evidence/G1'/profile/task/uuid.uuid4().hex;out.mkdir(parents=True)
    print(str(out),flush=True)
    method,inputs,observation=identity(profile,mode);save(out/'identity.json',inputs)
    started=now(); commands=[];artifacts=[];rows=[]
    budget=None
    if not args.pilot:
        budget=load(ROOT/'footprint-budgets.json')['budgets'][profile+'/'+mode]
        validate_budget(budget,method_digest=method,report_created=started,run_ids=[])
        save(out/'budget-binding.json',{'file':item(ROOT/'footprint-budgets.json'),'budget':budget})
    tools=load(ROOT/'cmake/msvc-validation-tools.json')['source_hint']
    os.environ['PATH']=tools+os.pathsep+os.environ.get('PATH','')

    def run(label,argv,obs=None,intentional=False):
        r=execute(argv,ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),240 if obs is None else 30,observation=obs)
        r['label']=label;r['raw']=[item(out/(label+'-'+s+'.log')) for s in ('stdout','stderr')]
        commands.append(r);save(out/'commands.json',commands)
        if not intentional and not owned_success(r): raise ValueError('command failed: '+label)
        return r

    try:
        prefix=out/'prefix'
        run('install',['cmake','--install',str(args.build.resolve()),'--config',configuration,'--prefix',str(prefix)])
        artifacts.extend(item(p) for p in prefix.rglob('*') if p.is_file())
        source=ROOT/'tools/footprint/consumer'; build=ROOT/'build'/('footprint-'+out.name)
        common=['-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207',
                '-DCMAKE_SYSTEM_VERSION=10.0.26100.0','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake'),
                '-DCMAKE_PREFIX_PATH='+str(prefix),'-DOCK_FOOTPRINT_MODE='+('allocation' if mode=='allocation' else 'occupancy'),
                '-DOCK_FOOTPRINT_ASAN='+('ON' if asan else 'OFF')]
        run('configure',['cmake','-S',str(source),'-B',str(build),*common])
        run('build',['cmake','--build',str(build),'--config',configuration,'--parallel','4','--','/nr:false'])
        binaries={k:build/configuration/('footprint_'+k+'.exe') for k in ('baseline','native')}
        for k,p in binaries.items():
            artifacts.append(item(p));shutil.copyfile(p,out/(k+'.exe'))
            run(k+'-imports',[str(Path(tools)/'dumpbin.exe'),'/imports',str(p)])
        for p in [build/'CMakeCache.txt',*build.glob('footprint_*.vcxproj')]:
            shutil.copyfile(p,out/p.name)
        # 分配注入是独立失败控制，不进入 ABBA 的成功样本。
        if mode=='allocation':
            r=run('injected',[str(build/configuration/'footprint_native_injected.exe')],observation,True)
            tree=r['process_tree']; obs=r.get('observation',{})
            if not (r['status']=='Exited' and r['exit_code']==1 and tree['assigned_before_resume'] and tree['active_after']==0 and not tree['terminated_owned_job'] and obs.get('status')=='Complete' and [p['phase'] for p in obs['phases']]==list(PHASES)):
                raise ValueError('allocation injection not detected with complete owned protocol')
            require_sampling_v2(obs)
            records=[json.loads(s) for s in (out/'injected-stdout.log').read_text().splitlines()]
            validate_record(records[0],'native','allocation');validate_allocation(records[1],'native',configuration,asan,injected=True)
        for b in range(blocks):
            for index,k in enumerate(('baseline','native','native','baseline')):
                label=f'b{b}-{index}-{k}';r=run(label,[str(binaries[k])],observation);require_complete(r)
                values=[json.loads(s) for s in (out/(label+'-stdout.log')).read_text().splitlines()]
                if len(values)!=(2 if mode=='allocation' else 1):raise ValueError('unexpected consumer records')
                validate_record(values[0],k,'allocation' if mode=='allocation' else 'disabled')
                if mode=='allocation':validate_allocation(values[1],k,configuration,asan)
                if (out/(label+'-stderr.log')).stat().st_size:raise ValueError('unexpected consumer stderr')
                obs=r['observation'];samples=obs['samples'];ready=next(s for s in samples if s['reason']=='HostReady')
                modules=[item(p) for p in sorted({p for m in obs['modules'] for p in m['paths']})]
                artifacts.extend(modules)
                rows.append({'run_id':str(r['pid'])+'-'+label+'-'+out.name,'command':label,'block':b,
                    'kind':'A' if k=='baseline' else 'B','identity':method,'modules':modules,
                    'private_peak':max(s['private_bytes'] for s in samples),
                    'working_set_peak':max(s['working_set_bytes'] for s in samples),
                    'ready_private':ready['private_bytes'],
                    'internal_ms':obs['ready']['internal_ticks']*1000/obs['qpc_frequency'],
                    'thread_peak':max(len(s['thread_ids']) for s in samples if s.get('thread_ids') is not None),
                    'allocation':values[1] if mode=='allocation' else None})
                save(out/'samples.json',rows)
            print(profile,mode,'block',b+1,'/',blocks,flush=True)
        # 一次独立诊断，区分系统入口与本消费者额外线程；不污染占用/时延样本。
        diagnostic=build/'thread-diagnostic'
        run('thread-configure',['cmake','-S',str(source),'-B',str(diagnostic),*common,
                               '-DOCK_FOOTPRINT_MODE=occupancy','-DOCK_FOOTPRINT_THREAD_DIAGNOSTIC=ON'])
        run('thread-build',['cmake','--build',str(diagnostic),'--config',configuration,'--parallel','4','--','/nr:false'])
        origins={}
        for kind in ('baseline','native','held_thread'):
            r=run('thread-'+kind,[str(diagnostic/configuration/('footprint_'+kind+'.exe'))],replace(observation,mode='latency'))
            require_complete(r);record=load(out/('thread-'+kind+'-stderr.log'))
            validate_thread_record(record,r['pid'],kind=='held_thread');origins[kind]=record
        # PSS 记录主线程之外的真实入口；Native 只允许 baseline 已有的系统入口。
        def signature(record):
            return sorted((Path(t['module']).name.lower(),t['rva']) for t in record['threads'] if t['tid']!=record['main_tid'])
        if signature(origins['native'])!=signature(origins['baseline']):raise ValueError('unattributed native thread origin')
        if len(origins['held_thread']['threads'])!=len(origins['baseline']['threads'])+1:raise ValueError('held thread positive control missing')
        save(out/'thread-origin.json',origins)
        metrics={}
        keys=('internal_ms',) if mode=='latency' else ('private_peak','working_set_peak','ready_private')
        stats={}
        for key in keys:
            differences=paired_differences([{**r,'value':r[key]} for r in rows],blocks)
            stats[key]={'native':statistics([r[key] for r in rows if r['kind']=='B']),
                        'paired_increment':statistics(differences)}
            metrics[key+'_absolute_max']=stats[key]['native']['max']
            metrics[key+'_paired_max']=stats[key]['paired_increment']['max']
        metrics['distribution_increment_bytes']=binaries['native'].stat().st_size-binaries['baseline'].stat().st_size
        if mode=='allocation':
            # validate_allocation 已逐次检查 40 个整窗、控制与全部寿命台账。
            stats['steady_allocation']='40 windows per process, exact zero; injection rejected'
        verify_inventory(artifacts)
        for r in commands:verify_inventory(r['raw'])
        if identity(profile,mode)[0]!=method:raise ValueError('measurement inputs changed during execution')
        if budget:
            validate_budget(budget,method_digest=method,report_created=started,run_ids=[r['run_id'] for r in rows])
            bounded_metrics(metrics,budget['limits'])
        save(out/'artifacts.json',artifacts)
        save(out/'report.json',{'format':'ock.native-footprint-report/1','status':'PilotComplete' if args.pilot else 'Passed',
            'created_at':started,'completed_at':now(),'method_digest':method,'profile':profile,'mode':mode,
            'blocks':blocks,'run_ids':[r['run_id'] for r in rows],'metrics':metrics,'statistics':stats,
            'budget_status':'NotApproved' if args.pilot else 'Approved','new_native_threads':0,
            'scope':'NativeSubset only; sampled memory, no claim of full Embedded or physical-machine approval'})
        return 0
    except Exception as exc:
        save(out/'report.json',{'status':'Failed','created_at':started,'error':str(exc),'profile':profile,'mode':mode,'completed_samples':len(rows)})
        print(str(exc),file=sys.stderr);return 1


def main():
    p=argparse.ArgumentParser();p.add_argument('--profile',choices=['win-msvc-debug','win-msvc-release','win-msvc-asan'],required=True)
    p.add_argument('--mode',choices=['occupancy','allocation','latency'],required=True)
    p.add_argument('--build',type=Path,required=True);p.add_argument('--pilot',action='store_true');args=p.parse_args()
    if args.mode=='latency' and args.profile!='win-msvc-release':p.error('formal latency requires Release')
    return measure(args)

if __name__=='__main__':sys.exit(main())
