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

def check_native(row):
    from fixture import good
    if not good(row):
        raise ValueError('native control failed')


def main():
    parser=argparse.ArgumentParser()
    from fixture import arguments
    arguments(parser)
    parser.add_argument('--case',required=True)
    a=parser.parse_args();runtime=Path(a.runtime_dir).resolve()
    if not (runtime/'cl.exe').is_file():raise ValueError('实际MSVC目录无效')
    os.environ['MSBUILDDISABLENODEREUSE']='1'
    out=Path(a.work)/uuid.uuid4().hex;out.mkdir(parents=True,exist_ok=False);commands=[]
    def command(name,argv):
        stdout=out/(name+'-stdout.log');stderr=out/(name+'-stderr.log')
        row=execute(argv,ROOT,stdout,stderr,180)
        row['raw']=[{'path':p.name,'sha256':sha_file(p),'size':p.stat().st_size} for p in (stdout,stderr)]
        commands.append(row);save_json(out/'commands.json',{'case':a.case,'commands':commands})
        return row,stdout.read_bytes()+stderr.read_bytes()
    from fixture import good
    row,_=command('native',[str(Path(a.binary).resolve()),a.case]);check_native(row)
    if a.case=='T06.contracts.executor_reject_exception_cleanup':
        def aborted(row,raw):return row['status'] in ('Exited','Crashed') and row.get('observed_exit_code')==3 and row['process_tree']['active_after']==0 and not row['process_tree']['terminated_owned_job'] and b'contract noexcept requested' in raw and b'contract noexcept returned' not in raw
        row,raw=command('return-control',[a.binary,'--noexcept-control'])
        if not (good(row) and b'contract noexcept returned' in raw and not aborted(row,raw)):
            raise ValueError('noexcept return control failed')
        row,raw=command('noexcept-fault',[a.binary,'--noexcept-fault'])
        if not aborted(row,raw): raise ValueError('noexcept fault control failed')
    else:
        from fixture import targets, build_target, check_execution, require_ready
        ready = require_ready(a)
        selected = [name for name, item in targets().items() if item['case'] == a.case.split('.')[-1]]
        if not selected: raise ValueError('unknown compile wrapper')
        observed = []
        for name in selected:
            build_target(a, name, command)
            observed.append(name)
        check_execution(selected, observed)
        save_json(out/'fixture-use.json', {'run_id':a.run_id,'fixture':a.fixture,
                  'identity_sha256':ready['identity_sha256'],'targets':observed})
    print(json.dumps({'case':a.case,'status':'Passed','evidence':str(out)},ensure_ascii=False))
if __name__=='__main__':main()
