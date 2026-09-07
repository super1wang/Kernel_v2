import sys, json, hashlib, shutil
from pathlib import Path
from datetime import datetime, timezone
ROOT=Path('E:/VS2019Qt5.15/3D')
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
source=ROOT/'build/d1.02-review-probes-green'
evidence=ROOT/'evidence/bootstrap/D1.02'/('independent-fix-verification-'+datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ'))
evidence.mkdir()
inputs=[*ROOT.glob('packages/contracts/include/ock/contracts/*.hpp'),*ROOT.glob('tests/conformance/core_contracts/*.hpp'),ROOT/'tests/compile/contracts/test_support.hpp',ROOT/'cmake/LockedMSVC.cmake',*source.glob('*.cpp'),source/'common.hpp',source/'CMakeLists.txt',source/'run.py']
(evidence/'source-inputs.json').write_text(json.dumps([{'path':str(p.relative_to(ROOT)), 'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in inputs],indent=2),encoding='utf-8')
for p in source.iterdir():
 if p.is_file(): shutil.copy2(p,evidence/p.name)
commands=[]
def run(name,args):
 r=execute(args,ROOT,evidence/(name+'-stdout.log'),evidence/(name+'-stderr.log'),120)
 commands.append({'name':name,**r})
 (evidence/'commands.json').write_text(json.dumps(commands,indent=2),encoding='utf-8')
 print(name,r['status'],r['exit_code'],flush=True)
 return r
cmake=shutil.which('cmake')
out=source/'out'
r=run('configure',[cmake,'-S',str(source),'-B',str(out),'-G','Visual Studio 17 2022','-A','x64','-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/LockedMSVC.cmake')])
if r['exit_code']!=0: sys.exit(1)
r=run('build-control',[cmake,'--build',str(out),'--config','Debug','--target','runtime','--','/m:1','/nr:false'])
if r['exit_code']!=0: sys.exit(2)
exe=str(out/'Debug/runtime.exe')
for mode in ['control','effect','transition']: run(mode,[exe,mode])
run('private-issuance-green',[cmake,'--build',str(out),'--config','Debug','--target','private_issuance','--','/m:1','/nr:false'])
print(str(evidence),flush=True)
