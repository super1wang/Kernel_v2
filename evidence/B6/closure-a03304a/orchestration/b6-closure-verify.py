from pathlib import Path
import os,sys,json,subprocess,uuid
sys.path.insert(0,str(Path.cwd()))
from tools.evidence.process import execute
from tools.evidence.common import save_json,sha_file,now
root=Path.cwd();mode=sys.argv[1];out=root/'evidence/bootstrap/B6'/('closure-'+mode+'-'+uuid.uuid4().hex[:10]);out.mkdir(parents=True);print(out,flush=True)
os.environ['PATH']='E:/vs2022IDE/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin;'+os.environ['PATH'];commands=[]
def run(label,args,timeout=2400):
 print('START '+label,flush=True)
 r=execute(args,root,out/(label+'-stdout.log'),out/(label+'-stderr.log'),timeout);r['label']=label;r['raw']=[{'path':str(p),'sha256':sha_file(p)} for p in [out/(label+'-stdout.log'),out/(label+'-stderr.log')]];commands.append(r);save_json(out/'commands.json',commands)
 print((out/(label+'-stdout.log')).read_text(errors='replace')[-1600:],flush=True)
 if r['status']!='Exited' or r['exit_code']!=0 or r['process_tree']['active_after']:raise RuntimeError('command failed: '+label)
 print('PASS '+label,flush=True)
try:
 if mode=='matrix':
  for p in ['debug','release','asan']:run(p,[sys.executable,'-X','utf8','tools/evidence/run.py','tests/runs/b6-closure-win-msvc-'+p+'.json'])
 elif mode=='native':
  for p in ['release','asan']:run('producer-'+p,[sys.executable,'-X','utf8','build/b6-closure-projections.py',p])
  for key in json.loads(Path('footprint-budgets.json').read_text())['budgets']:
   if key=='win-msvc-debug/allocation':continue
   p,m=key.split('/');run(p+'-'+m,[sys.executable,'-X','utf8','tools/footprint/formal.py','--profile',p,'--mode',m,'--build','build/b6-native-'+p.rsplit('-',1)[-1]])
 elif mode=='embedded':
  for p in ['debug','release','asan']:
   producer='build/b6-embedded-'+p
   run('configure-'+p,['cmake','--preset','win-msvc-'+p,'-B',producer,'-DOCK_BUILD_COMPONENTS=Embedded','-DOCK_DEPENDENCY_COMPONENTS=Foundation;CpuPool','-DOCK_DEPENDENCIES_OFFLINE=ON','-DBUILD_TESTING=OFF','-DOCK_BUILD_DEPENDENCY_PROBES=OFF'])
   run('measure-'+p,[sys.executable,'-X','utf8','tools/footprint/embedded_formal.py','--profile','win-msvc-'+p,'--producer',producer])
 else:raise ValueError('mode')
 save_json(out/'result.json',{'status':'Passed','mode':mode,'commit':subprocess.check_output(['git','rev-parse','HEAD']).decode().strip(),'completed_at':now()})
except Exception as e:
 save_json(out/'result.json',{'status':'Failed','mode':mode,'error':str(e),'completed_at':now()});print(str(e),flush=True);sys.exit(1)
