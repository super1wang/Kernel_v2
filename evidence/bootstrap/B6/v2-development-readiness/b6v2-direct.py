from pathlib import Path
import os,sys,json,uuid
R=Path(__file__).resolve().parents[1];os.chdir(R);sys.path.insert(0,str(R))
from tools.evidence.process import execute
from tools.footprint.analyze import owned_success
os.environ['PATH']='E:/vs2022IDE/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin;'+os.environ['PATH']
out=R/'evidence/bootstrap/B6'/('v2-direct-'+sys.argv[1]+'-'+uuid.uuid4().hex[:8]);out.mkdir(parents=True);print(out,flush=True)
commands=[]
for label,args in [('build',['cmake','--build','build/b6-formal-debug','--config','Debug','--target','ock_state_runtime','--parallel','4','--','/nr:false']),('test',['ctest','--test-dir','build/b6-formal-debug','-C','Debug','-R','^T06.state.runtime_native$','--output-on-failure','--timeout','45'])]:
 print('START '+label,flush=True)
 r=execute(args,R,out/(label+'-stdout.log'),out/(label+'-stderr.log'),900);r['label']=label;commands.append(r)
 (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
 print((out/(label+'-stdout.log')).read_text(errors='replace')[-2500:],flush=True)
 if not owned_success(r):sys.exit(1)
