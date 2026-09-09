"""G3-C 无阶段握手的真实创建/构造至 Ready；无预算通过推断。"""
import argparse,hashlib,json,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.footprint.analyze import statistics

def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def save(p,value):p.write_text(json.dumps(value,ensure_ascii=False,indent=2,allow_nan=False)+'\n',encoding='utf-8')
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--blocks',type=int,default=3)
    args=parser.parse_args()
    if not 1<=args.blocks<=100:parser.error('blocks must be 1..100')
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
    binaries={kind:args.build.resolve()/('footprint_'+kind+'.exe') for kind in ('baseline','embedded')}
    inputs={kind:{'path':str(p),'sha256':digest(p),'bytes':p.stat().st_size} for kind,p in binaries.items()}
    inputs['source']={p:digest(ROOT/p) for p in ('tools/footprint/embedded_startup.py','tools/evidence/process.py','tools/footprint/embedded_consumer/main.cpp','examples/embedded_service/fixture.hpp')}
    save(out/'inputs.json',inputs);rows=[]
    for block in range(args.blocks):
        for index,kind in enumerate(('baseline','embedded','embedded','baseline')):
            label=f'{block}-{index}-{kind}';binary=binaries[kind]
            assert digest(binary)==inputs[kind]['sha256']
            record=execute([str(binary),'--startup','1'],ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),30)
            save(out/(label+'-command.json'),record)
            assert record['exit_code']==0 and record['status']=='Exited' and record['process_tree']['active_after']==0
            objects=[json.loads(s) for s in (out/(label+'-stdout.log')).read_text(encoding='utf-8').splitlines() if s.startswith('{')]
            consumer=next(r for r in objects if r.get('kind')==kind)
            ready=next(r for r in objects if r.get('format')=='ock.embedded-startup/1')
            assert consumer['released'] is True
            if kind=='embedded':
                assert all(consumer[k]==v for k,v in {'workers':2,'warmup':4,'invoke':40,'submit':40,'resource_child_cancel':True,'default_memory_log':True}.items())
            clock=record['creation_clock'];frequency=clock['qpc_frequency']
            assert frequency>0 and frequency==ready['qpc_frequency']==consumer['qpc_frequency']
            elapsed=ready['ready_ticks']-clock['qpc_ticks']
            assert 0<=consumer['construction_ticks']<=elapsed
            rows.append({'block':block,'index':index,'kind':kind,'cache_condition':'first launch; OS cache uncontrolled' if block==0 and index in (0,1) else 'subsequent launch; OS cache uncontrolled',
                'create_to_ready_ms':elapsed*1000/frequency,'construction_to_ready_ms':consumer['construction_ticks']*1000/frequency,'consumer':consumer})
            save(out/'samples.json',rows)
    for kind,p in binaries.items():assert digest(p)==inputs[kind]['sha256']
    assert all(digest(ROOT/p)==value for p,value in inputs['source'].items())
    summary={kind:{metric:statistics([r[metric] for r in rows if r['kind']==kind]) for metric in ('create_to_ready_ms','construction_to_ready_ms')} for kind in binaries}
    save(out/'summary.json',{'status':'Collected','budget_status':'NotEvaluated','blocks':args.blocks,'summary':summary,
        'method':'QPC before CreateProcess to child actual Ready; includes owned Job assignment/resume, no stage handshake or allocation instrumentation; cold cache not forced'})
    print(json.dumps(summary),flush=True)
if __name__=='__main__':main()
