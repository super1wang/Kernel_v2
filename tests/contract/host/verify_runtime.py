"""Host真实子进程退出与分配正负控制；不用任意非零充当拒绝。"""
import argparse
import json
import os
from pathlib import Path
import sys
import uuid

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.evidence.common import save_json,sha_file
from tools.evidence.process import execute


def require(condition,message):
    if not condition: raise ValueError(message)


def main():
    p=argparse.ArgumentParser()
    for name in ('binary','case','work','runtime-dir'): p.add_argument('--'+name,required=True)
    p.add_argument('--fault-binary')
    p.add_argument('--configuration',choices=['Debug','Release'])
    a=p.parse_args()
    fault_cases={'T03.host.configuration':'create','T03.host.start_failures':'start','T22.host.shutdown_errors':'internal'}
    require(a.case in ('T22.host.destructor_guard','T23.host.allocation_controls') or a.case in fault_cases,'unknown Host control')
    binary=Path(a.binary).resolve()
    os.environ['PATH']=a.runtime_dir+os.pathsep+os.environ.get('PATH','')
    out=Path(a.work)/uuid.uuid4().hex;out.mkdir(parents=True)
    records=[]
    def run(label,argument,code=0,executable=binary):
        streams=[out/(label+'-'+s+'.log') for s in ('stdout','stderr')]
        row=execute([str(executable),argument],ROOT,*streams,30)
        row['binary_sha256']=sha_file(executable)
        row['raw']=[{'path':f.name,'sha256':sha_file(f),'size':f.stat().st_size} for f in streams]
        records.append(row)
        save_json(out/'commands.json',{'case':a.case,'binary_sha256':sha_file(binary),'commands':records})
        tree=row['process_tree']
        require(row['status']=='Exited' and row['exit_code']==code and row.get('observed_exit_code')==code and tree['assigned_before_resume']
                and tree['active_after']==0 and not tree['terminated_owned_job'],label+' unexpected exit/tree')
        return streams[0].read_bytes(),streams[1].read_bytes()
    if a.case in fault_cases:
        require(a.fault_binary is not None,'missing actual fault executable')
        require(a.configuration in ('Debug','Release'),'explicit fault configuration required')
        run('public-case',a.case)
        controls=[(fault_cases[a.case],0,None)]
        if a.case=='T03.host.configuration':
            controls=([('create-recoverable',0,None),('create-ordinal-2',86,b'host_fault_terminate ordinal=2 size=16 calls=2'),
                       ('stl-default-vector',86,b'host_fault_terminate ordinal=1 size=16 calls=1')]
                      if a.configuration=='Debug' else [('create-full',0,None),('stl-default-vector',0,None)])
        for mode,code,marker in controls:
            stdout,stderr=run(mode,mode,code,Path(a.fault_binary).resolve())
            if marker:
                require(stderr in (marker+b'\n',marker+b'\r\n')
                        and b'host_fault_controls_checked' not in stdout,
                        'unexpected termination diagnostic: '+mode)
            else:
                lines=stdout.splitlines()
                require(not stderr and lines and lines[-1]==b'host_fault_controls_checked'
                        and lines.count(b'host_fault_controls_checked')==1,
                        'unexpected success diagnostic: '+mode)
    elif a.case=='T22.host.destructor_guard':
        _,raw=run('safe','--host-destructor-safe')
        require(b'host_destructor_safe' in raw,'missing destructor positive control')
        for mode in ('ready','active','failed'):
            _,raw=run(mode,'--host-destructor-'+mode,3)
            require(('host_destructor_probe '+mode).encode() in raw,'missing intended destructor boundary')
    else:
        stdout,_=run('calibration',a.case)
        report=json.loads(stdout)
        require(report['format']=='ock.native-allocation/1' and report['case']=='T23.native.allocation_probe',
                'not the shared D1.05 calibration')
        entries=['new','new_array','aligned_new','aligned_array','nothrow_new','nothrow_array',
                 'nothrow_aligned','nothrow_aligned_array','malloc','calloc','realloc','aligned_malloc']
        require([s['name'] for s in report['samples'][:12]]==entries,'missing independent allocation entries')
        require(all(v is True for v in report['checks'].values()),'invalid calibration')
        coverage=report['coverage']
        for sample in report['samples'][:12]:
            if coverage['asan_allocator_hooks']:
                require(sample['asan_allocations']==1,'missing ASan positive entry')
            elif coverage['debug_crt']:
                require(sample['crt']>0,'missing CRT positive entry')
        if coverage['asan_allocator_hooks']:
            _,raw=run('registration-failure','--asan-hook-registration-failure',87)
            require(b'native_asan_hook_registration_failed' in raw,'missing ASan registration failure')
        stdout,_=run('injected','--allocation-injected-red',1)
        injected=json.loads(stdout)
        require(injected['case']=='--allocation-injected-red' and injected['checks']['verified'] is False,
                'injected allocation was not rejected')
        require(any(s['name']=='intentional_single_allocation' and s['cpp']>0 for s in injected['samples']),
                'missing actual injected new')
        save_json(out/'calibration.json',{'scope':'本Host可执行文件复用D1.05计数校准，不是Host稳态结果',
                                         'report':report,'injected':injected})
    save_json(out/'result.json',{'case':a.case,'status':'Passed','commands':len(records)})
    print(json.dumps({'case':a.case,'evidence':str(out),'status':'Passed'}))
    return 0


if __name__=='__main__':
    try: sys.exit(main())
    except (ValueError,OSError,KeyError) as error:
        print('Host control failed: '+str(error),file=sys.stderr);sys.exit(1)
