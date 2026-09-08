"""同一次 CTest 的八个方法职责共享一次真实控制；不跨轮复用成功。"""
import argparse
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT))
from tools.footprint.formal import save,load,item,identity,verify_inventory
from tools.footprint.analyze import owned_success
from tools.evidence.process import execute

CASES=('method_identity','module_accounting','memory_calibration','thread_clock_calibration',
       'phase_repetition','owned_process','raw_integrity','budget_provenance')


def main():
    p=argparse.ArgumentParser();p.add_argument('--case',choices=CASES,required=True)
    p.add_argument('--fixture',type=Path,required=True);args=p.parse_args();out=args.fixture.resolve()
    if not out.is_relative_to(ROOT/'build'):raise ValueError('fixture must be in workspace build')
    method=identity('win-msvc-debug','occupancy')[0]
    if args.case=='method_identity':
        out.mkdir(parents=True,exist_ok=False)
        commands=[];material=[];base=ROOT/'evidence/bootstrap/D1.06'
        for label,argv in [
            ('guards',[sys.executable,'-X','utf8','-m','unittest','discover','-s','tests/tools/footprint','-v']),
            ('calibration',[sys.executable,'-X','utf8','tools/footprint/run.py','--configuration','tools/footprint/configuration-development.json','--mode','calibrate']),
            ('owner',[sys.executable,'-X','utf8','tools/footprint/owner_controls.py','--mode','all'])]:
            previous=set(base.glob('footprint-*'))
            r=execute(argv,ROOT,out/(label+'-stdout.log'),out/(label+'-stderr.log'),600)
            commands.append(r);save(out/'commands.json',commands)
            if not owned_success(r):raise ValueError(label+' failed')
            material.extend([item(out/(label+'-stdout.log')),item(out/(label+'-stderr.log'))])
            if label!='guards':
                fresh=set(base.glob('footprint-*'))-previous
                if len(fresh)!=1:raise ValueError('ambiguous real control output')
                directory=fresh.pop();result=load(directory/'result.json')
                if result.get('status')!='Passed':raise ValueError('control aggregate failed')
                material.extend(item(f) for f in directory.rglob('*') if f.is_file() and '__pycache__' not in f.parts)
        if identity('win-msvc-debug','occupancy')[0]!=method:raise ValueError('method changed during controls')
        save(out/'complete.json',{'method':method,'fixture':str(out),'cases':CASES,'materials':material})
    ready=load(out/'complete.json')
    if ready['method']!=method or ready['fixture']!=str(out) or ready['cases']!=list(CASES):
        raise ValueError('stale/mismatched method controls')
    verify_inventory(ready['materials'])
    print(args.case+': real shared controls and material identity verified')
    return 0

if __name__=='__main__':sys.exit(main())
