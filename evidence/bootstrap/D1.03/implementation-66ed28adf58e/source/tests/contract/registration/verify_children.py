"""固定用例内的实际同工具链编译正/反控制与结构边界核对；不增加CTest项。"""
import argparse,json,os,re,sys,uuid
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.evidence.common import save_json,sha_file
NEGATIVE={
 'shape_compile_contract':{
  'wrong_read':('void rejected(Registrar&r){r.read(edit_handler,input(),OperationOptions{{},{},{name("module"),name("test")},{},false});}',r'C2672|C2664|C2784'),
  'wrong_edit':('void rejected(Registrar&r){r.state_edit(compute_handler,input(),OperationOptions{{},{},{name("module"),name("test")},{},false});}',r'C2672|C2664|C2784'),
  'wrong_effect':('Result<int> bad(const int&,EffectContext&);void rejected(Registrar&r){r.external_effect(bad,input(),OperationOptions{{},{},{name("module"),name("test")},{},false});}',r'C2672|C2664|C2784'),
  'wrong_lifecycle':('Result<int> bad(const int&,TransitionView&);void rejected(Registrar&r){r.lifecycle(bad,input(),OperationOptions{{},{},{name("module"),name("test")},{},false});}',r'C2672|C2664|C2784')},
 'manifest_owned_budget':{'missing_freeze':('auto rejected=ConfigurationBinding::make(3);',r'C2672|C7602')},
 'handler_not_exposed':{
  'handler_extract':('void rejected(const Catalog&c){c.handler();}',r'C2039'),
  'hot_extract':('void rejected(const Catalog&c){auto x=c.hot_;}',r'C2248'),
  'identity_copy':('void rejected(const Catalog&c){Catalog copy(c);}',r'C2280'),
  'registrar_extract':('void rejected(Registrar&r){r.handler();}',r'C2039')},
 'internal_component_boundary':{'runtime_unavailable':('#include <ock/runtime/runtime.hpp>\n',r'C1083')},
 'cold_docs_separation':{}
}
def main():
 p=argparse.ArgumentParser()
 for n in ('case','binary','includes','generator','platform','toolset','sdk','config','work','runtime-dir'):p.add_argument('--'+n,required=True)
 a=p.parse_args();runtime=Path(a.runtime_dir);assert (runtime/'cl.exe').is_file()
 os.environ['PATH']=str(runtime)+os.pathsep+os.environ.get('PATH','');os.environ['MSBUILDDISABLENODEREUSE']='1'
 out=Path(a.work)/uuid.uuid4().hex;out.mkdir(parents=True,exist_ok=False);commands=[]
 def command(n,argv):
  stdout=out/(n+'-stdout.log');stderr=out/(n+'-stderr.log');row=execute(argv,ROOT,stdout,stderr,240)
  row['raw']=[{'path':x.name,'sha256':sha_file(x),'size':x.stat().st_size} for x in (stdout,stderr)];commands.append(row);save_json(out/'commands.json',{'case':a.case,'commands':commands});return row,stdout.read_bytes()+stderr.read_bytes()
 def good(r):return r['status']=='Exited' and r['exit_code']==0 and r['process_tree']['active_after']==0
 r,_=command('native',[a.binary,a.case]);assert good(r)
 short=a.case.split('.')[-1];header=(ROOT/'packages/runtime/registry/registry.hpp').read_text(encoding='utf-8-sig')
 checks={}
 if short=='cold_docs_separation':
  hot=header.split('struct HotEntry final {',1)[1].split('\n};',1)[0]
  assert not any(x in hot for x in ('Docs','docs','DefinitionSnapshot','Catalog','Name','string'))
  assert 'std::vector<detail::HotEntry> hot_' in header and 'std::vector<std::shared_ptr<const DefinitionSnapshot>> cold_' in header
  checks={'hot_structure':hot,'header_sha256':sha_file(ROOT/'packages/runtime/registry/registry.hpp')}
 if short=='internal_component_boundary':
  cmake=(ROOT/'tests/contract/registration/CMakeLists.txt').read_text();assert 'target_link_libraries(ock_registry_internal PRIVATE OCK::CoreContracts)' in cmake
  assert not re.search(r'install\s*\(',cmake)
  root_cmake=(ROOT/'CMakeLists.txt').read_text();deps=(ROOT/'cmake/TargetDependencies.cmake').read_text()
  checks={'registry_cmake_sha256':sha_file(ROOT/'tests/contract/registration/CMakeLists.txt'),'sdk_dependency_sha256':sha_file(ROOT/'cmake/TargetDependencies.cmake')}
  # 复用实际D1.02边界检查，核对生成SDK的Runtime状态与闭包。
  r,_=command('sdk-boundary',[sys.executable,'-X','utf8','-m','unittest','tests.architecture.test_contracts_stage']);assert good(r)
 save_json(out/'structure.json',checks)
 if short=='cold_docs_separation':print(json.dumps({'case':a.case,'status':'Passed','evidence':str(out)}));return
 prefix='#include "test_support.hpp"\n#include "tests/conformance/core_contracts/factories.hpp"\n#include "packages/runtime/registry/registry.hpp"\nusing namespace ock::runtime::registry;\n'
 positive='void accepted(Registrar&r){auto o=OperationOptions{{},{},{name("module"),name("test")},{},false};r.read(read_handler,input(),o);r.state_edit(edit_handler,input(AtomicMode::StateEdit),o);r.external_effect(effect_handler,input(),o);r.lifecycle(transition_handler,input(),o);}\n'
 sources={'positive':(positive,''),**NEGATIVE[short]};includes=[x for x in a.includes.split('|') if x]
 lines=['cmake_minimum_required(VERSION 3.25)','project(RegistryCompileContract LANGUAGES CXX)','set(CMAKE_CXX_STANDARD 20)','set(CMAKE_CXX_STANDARD_REQUIRED ON)']
 for name,(body,diagnostic) in sources.items():
  (out/(name+'.cpp')).write_text(prefix+body+'\n',encoding='utf-8')
  lines += ['add_library('+name+' OBJECT EXCLUDE_FROM_ALL '+name+'.cpp)','target_compile_options('+name+' PRIVATE /EHsc /utf-8 /Zc:__cplusplus /permissive-)','target_include_directories('+name+' PRIVATE '+' '.join('[==['+x.replace('\\','/')+']==]' for x in includes)+')']
 (out/'CMakeLists.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
 argv=['cmake','-S',str(out),'-B',str(out/'build'),'-G',a.generator,f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake','-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded']
 if a.platform:argv+=['-A',a.platform]
 if a.toolset:argv+=['-T',a.toolset]
 if a.sdk:argv+=['-DCMAKE_SYSTEM_VERSION='+a.sdk]
 r,_=command('configure',argv);assert good(r)
 for name,(body,diagnostic) in sources.items():
  r,raw=command(name,['cmake','--build',str(out/'build'),'--config',a.config,'--target',name,'--parallel','2','--','/nr:false'])
  if name=='positive':assert good(r)
  else:
   assert r['status']=='Exited' and r['exit_code']!=0 and r['process_tree']['active_after']==0
   text=raw.decode('utf-8',errors='replace');assert re.search(r'error (?:'+diagnostic+r')',text),text
   assert name+'.cpp' in text
   if name=='runtime_unavailable':assert 'ock/runtime/runtime.hpp' in text
 print(json.dumps({'case':a.case,'status':'Passed','evidence':str(out)}))
if __name__=='__main__':main()
