from pathlib import Path
import sys,json
root=Path(r"E:\VS2019Qt5.15\3D");sys.path.insert(0,str(root))
from tools.evidence.process import execute
out=Path(__file__).parent;b=root/'build'/('d1.05-bootstrap-'+out.name.rsplit('-',1)[1]);commands=[]
def run(name,args):
 r=execute(args,root,out/(name+'-stdout.log'),out/(name+'-stderr.log'),180);commands.append(r);(out/'commands.json').write_text(json.dumps(commands,indent=2));print(name,r['status'],r['exit_code'],flush=True)
 if r['status']!='Exited' or r['process_tree']['active_after']!=0:raise RuntimeError('unclean owned process')
 return r['exit_code']
assert run('configure',['cmake','-S',str(out),'-B',str(b),'-G','Visual Studio 17 2022','-A','x64','-T','v143,version=14.44.35207','-DCMAKE_SYSTEM_VERSION=10.0.26100.0',f'-DCMAKE_TOOLCHAIN_FILE={root}/cmake/LockedMSVC.cmake'])==0
assert run('positive',['cmake','--build',str(b),'--config','Debug','--target','positive','--','/nr:false'])==0
assert run('negative',['cmake','--build',str(b),'--config','Debug','--target','negative','--','/nr:false'])!=0
