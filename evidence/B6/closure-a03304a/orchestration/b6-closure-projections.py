import json,subprocess,sys,os,uuid
from pathlib import Path
sys.path.insert(0,str(Path.cwd()))
from tools.evidence.process import execute
from tools.evidence.common import save_json,sha_file,now
root=Path.cwd();out=root/'evidence/bootstrap/B6'/('closure-projections-'+uuid.uuid4().hex[:10]);out.mkdir(parents=True)
print(out,flush=True);commands=[]
os.environ['PATH']='E:/vs2022IDE/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin;'+os.environ['PATH']
def run(label,args,timeout=600):
 r=execute(args,root,out/(label+'-stdout.log'),out/(label+'-stderr.log'),timeout);r['label']=label;r['raw']=[{'path':str(p),'sha256':sha_file(p)} for p in [out/(label+'-stdout.log'),out/(label+'-stderr.log')]];commands.append(r);save_json(out/'commands.json',commands)
 if r['status']!='Exited' or r['exit_code']!=0 or r['process_tree']['active_after']:raise RuntimeError('command failed: '+label)
 print(label+' Passed',flush=True)
mode=sys.argv[1]
try:
 if mode=='state':
  producer='build/b6-state-final'
  run('state-configure',['cmake','--preset','win-msvc-debug','-B',producer,'-DOCK_BUILD_COMPONENTS=StateNative','-DOCK_DEPENDENCY_COMPONENTS=State','-DOCK_DEPENDENCIES_OFFLINE=ON','-DOCK_BUILD_STATE_TESTS=ON','-DPython3_EXECUTABLE='+sys.executable])
  run('state-build',['cmake','--build',producer,'--config','Debug','--target','ock_settings_service','ock_state_roots','ock_state_snapshots','ock_state_commit','ock_state_atomic','ock_state_runtime','ock_state_objects','ock_state_cost','--parallel','4','--','/nr:false'])
  run('state-test',['ctest','--test-dir',producer,'-C','Debug','-R',r'^T(24.state.(settings_service|installed_consumer)|08.state.(frozen_roots|snapshot_ownership)|09.state.(memory_commit|object_references)|10.state.atomic_group|06.state.runtime_native|23.state.structural_sharing_cost)$','--no-tests=error','--output-on-failure','--output-junit',str(out/'state.xml')])
 else:
  profile=mode;producer='build/b6-native-'+profile;config='Release' if profile=='release' else 'Debug'
  run('native-configure',['cmake','--preset','win-msvc-'+profile,'-B',producer,'-DOCK_BUILD_COMPONENTS=Runtime','-DOCK_DEPENDENCY_COMPONENTS=Foundation','-DOCK_DEPENDENCIES_OFFLINE=ON','-DBUILD_TESTING=OFF','-DOCK_BUILD_DEPENDENCY_PROBES=OFF'])
  run('native-build',['cmake','--build',producer,'--config',config,'--parallel','4','--','/nr:false'])
  if profile=='debug':run('runtime-install',[sys.executable,'-X','utf8','tests/install_consumer/verify_native.py','--build',producer,'--config',config,'--case','installed_host','--evidence-task','B2'])
 save_json(out/'result.json',{'status':'Passed','mode':mode,'commit':subprocess.check_output(['git','rev-parse','HEAD']).decode().strip(),'completed_at':now(),'scope':'B6 closure projection; not Gate approval'})
except Exception as e:
 save_json(out/'result.json',{'status':'Failed','mode':mode,'error':str(e),'completed_at':now()});print(str(e),flush=True);sys.exit(1)
