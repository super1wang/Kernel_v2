"""D1.02编译反例与noexcept故障：同工具链正控制，原始Job输出逐轮保留。"""
import argparse
import json
import os
from pathlib import Path
import re
import sys
import uuid
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT))
from tools.evidence.process import execute
from tools.evidence.common import save_json,sha_file

NEGATIVE={
 'type_contract_constraints':{
  'missing_contract':'Result<double> f(const double&,WorkContext&); auto rejected=make_compute_definition(f,input(AtomicMode::PureCompute));',
  'nested_borrow':'void test(const BoundOperation<Borrowed,int>& b,Borrowed a){auto rejected=prepare_submit_args(b,std::move(a));}',
  'standard_borrow':'namespace ock::contracts {template<>struct TypeContract<std::string_view>{static TypeIdentity identity(){return TypeContract<int>::identity();}static Result<void> validate(const std::string_view&){return {};}static constexpr auto async_ownership=AsyncOwnership::Owning;};} void test(const BoundOperation<std::string_view,int>& b,std::string_view a){auto rejected=prepare_submit_args(b,std::move(a));}',
  'bad_validator':'struct Bad{}; namespace ock::contracts {template<>struct TypeContract<Bad>{static TypeIdentity identity(){return TypeContract<int>::identity();}static bool validate(const Bad&){return true;}};} Result<int> f(const Bad&,WorkContext&);auto rejected=make_compute_definition(f,input(AtomicMode::PureCompute));'},
 'typed_value_validation':{'bad_validator_call':'void f(const BoundOperation<int,int>& b){auto rejected=validate_inline_args(b,Other{1});}'},
 'read_shape':{'wrong_context':'auto rejected=make_read_definition(edit_handler,input());','mutable_args':'Result<int> f(int&,WorkContext&);auto rejected=make_compute_definition(f,input(AtomicMode::PureCompute));'},
 'state_edit_shape':{'wrong_provider':'auto rejected=make_state_edit_definition<int,int,OtherProvider>(edit_handler,input(AtomicMode::StateEdit));','missing_edit':'auto rejected=make_state_edit_definition(compute_handler,input(AtomicMode::StateEdit));'},
 'effect_shape':{'loses_fact':'Result<int> f(const int&,EffectContext&);auto rejected=make_effect_definition(f,input());'},
 'lifecycle_shape':{'loses_transition':'Result<int> f(const int&,TransitionView&);auto rejected=make_lifecycle_definition(f,input());'},
 'context_capability_boundary':{'edit_commit':'void test(EditView<Provider>& view){view.commit();}','host_lookup':'void test(WorkContext& context){context.host();}','service_lookup':'void test(WorkContext& context){context.get_service<Reader>();}','direct_sql':'void test(WorkContext& context){context.sql();}','mutable_reader':'void test(ReadServices<Reader>& services){services.reader().value=9;}'},
 'atomic_read_adapters':{'wrong_args':'Result<int> adapter(const Other&,const Provider::CandidateReadPort&,WorkContext&);void test(const OperationDefinition<int,int>& d){auto rejected=with_candidate_read<Provider>(d,adapter,name("provider"));}','active_read':'Result<int> adapter(const int&,ReadServices<Reader>&,WorkContext&);void test(const OperationDefinition<int,int>& d){auto rejected=with_candidate_read<Provider>(d,adapter,name("provider"));}'},
 'atomic_async_rejected':{'no_dispatch':'void test(EditView<Provider>& view){view.submit();}'},
 'typed_binding_fingerprint':{'token_ctor':'auto forged=CppTypeToken(static_cast<const unsigned char*>(nullptr));','frozen_definition':'void test(DefinitionSnapshot& a,const DefinitionSnapshot& b){a=b;}','raw_handler':'void test(const BoundOperation<int,int>& b){b.handler();}','false_cpp_type':'auto rejected=make_compute_definition<Other,Other>(compute_handler,input(AtomicMode::PureCompute));'},
}

def main():
    parser=argparse.ArgumentParser()
    for name in ('case','binary','includes','generator','platform','toolset','sdk','config','work','runtime-dir'):
        parser.add_argument('--'+name,required=True)
    a=parser.parse_args();runtime=Path(a.runtime_dir).resolve()
    if not (runtime/'cl.exe').is_file():raise ValueError('实际MSVC目录无效')
    os.environ['MSBUILDDISABLENODEREUSE']='1'
    os.environ['PATH']=str(runtime)+os.pathsep+os.environ.get('PATH','')
    out=Path(a.work)/uuid.uuid4().hex;out.mkdir(parents=True,exist_ok=False);commands=[]
    def command(name,argv):
        stdout=out/(name+'-stdout.log');stderr=out/(name+'-stderr.log')
        row=execute(argv,ROOT,stdout,stderr,180)
        row['raw']=[{'path':p.name,'sha256':sha_file(p),'size':p.stat().st_size} for p in (stdout,stderr)]
        commands.append(row);save_json(out/'commands.json',{'case':a.case,'commands':commands})
        return row,stdout.read_bytes()+stderr.read_bytes()
    def good(row):return row['status']=='Exited' and row['exit_code']==0 and row['process_tree']['active_after']==0
    row,_=command('native',[str(Path(a.binary).resolve()),a.case]);assert good(row)
    if a.case=='T06.contracts.executor_reject_exception_cleanup':
        def aborted(row,raw):return row['status'] in ('Exited','Crashed') and row.get('observed_exit_code')==3 and row['process_tree']['active_after']==0 and not row['process_tree']['terminated_owned_job'] and b'contract noexcept requested' in raw and b'contract noexcept returned' not in raw
        row,raw=command('return-control',[a.binary,'--noexcept-control']);assert good(row) and b'contract noexcept returned' in raw and not aborted(row,raw)
        row,raw=command('noexcept-fault',[a.binary,'--noexcept-fault']);assert aborted(row,raw)
    else:
        sources={'positive':'auto accepted=make_compute_definition(compute_handler,input(AtomicMode::PureCompute));',**NEGATIVE[a.case.split('.')[-1]]}
        includes=[p for p in a.includes.split('|') if p]+[str(Path(__file__).parent)]
        lines=['cmake_minimum_required(VERSION 3.25)','project(CoreContractRejection LANGUAGES CXX)','set(CMAKE_CXX_STANDARD 20)','set(CMAKE_CXX_STANDARD_REQUIRED ON)']
        for name,body in sources.items():
            (out/(name+'.cpp')).write_text('#include "test_support.hpp"\n'+body+'\n',encoding='utf-8',newline='\n')
            lines+=['add_library('+name+' OBJECT EXCLUDE_FROM_ALL '+name+'.cpp)','target_compile_options('+name+' PRIVATE /EHsc /utf-8 /Zc:__cplusplus /permissive-)','target_include_directories('+name+' PRIVATE '+' '.join('[==['+p.replace('\\','/')+']==]' for p in includes)+')']
        (out/'CMakeLists.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')
        argv=['cmake','-S',str(out),'-B',str(out/'build'),'-G',a.generator,f'-DCMAKE_TOOLCHAIN_FILE={ROOT}/cmake/LockedMSVC.cmake','-DCMAKE_MSVC_DEBUG_INFORMATION_FORMAT=Embedded']
        if a.platform:argv+=['-A',a.platform]
        if a.toolset:argv+=['-T',a.toolset]
        if a.sdk:argv+=['-DCMAKE_SYSTEM_VERSION='+a.sdk]
        row,_=command('configure',argv);assert good(row)
        row,_=command('positive',['cmake','--build',str(out/'build'),'--config',a.config,'--target','positive','--parallel','2','--','/nr:false']);assert good(row)
        for name in sources:
            if name=='positive':continue
            row,raw=command(name,['cmake','--build',str(out/'build'),'--config',a.config,'--target',name,'--parallel','2','--','/nr:false'])
            assert row['status']=='Exited' and row['exit_code']!=0 and row['process_tree']['active_after']==0
            assert re.search(rb'error C(?:2248|2672|2664|2280|2039|3892|2440|2783|2784|7602)',raw),'必须是目标C++合同拒绝诊断'
    print(json.dumps({'case':a.case,'status':'Passed','evidence':str(out)},ensure_ascii=False))
if __name__=='__main__':main()
