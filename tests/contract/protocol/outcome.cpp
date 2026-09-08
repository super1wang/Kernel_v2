#include "tests/compile/contracts/test_support.hpp"
#include <ock/control_protocol/outcome_wire.hpp>
#include <ock/dynamic/binding/schema.hpp>
#include <fstream>
#include <crtdbg.h>
#define NOMINMAX
#include <Windows.h>

#include <DbgHelp.h>
using namespace ock;
int main(int argc,char **argv)try{
  _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
  _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
  std::set_terminate([] {
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    void *frames[32];
    auto count = CaptureStackBackTrace(0, 32, frames, nullptr);
    for (unsigned i = 0; i < count; ++i) {
      alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 512]{};
      auto *symbol = reinterpret_cast<SYMBOL_INFO *>(storage);
      symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
      symbol->MaxNameLen = 511;
      DWORD64 displacement = 0;
      if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(frames[i]),
                      &displacement, symbol))
        std::cerr << symbol->Name << '\n';
    }
    std::abort();
  });
  CHECK(argc==2);std::ifstream file(argv[1]);std::string schema_text(std::istreambuf_iterator<char>(file),{});
  auto schema=binding::CompiledSchema::compile(schema_text);CHECK(schema);
  std::cerr<<"schema compiled\n";
  Publication publication_port;
  OutcomeValidation validation{publication_port,{},fact_budget()};
  auto unknown=Outcome<int>::validate(Indeterminate{{id<FactId>()}},
      facts({UnknownFact{id<FactId>(),UnknownBoundary::ExternalEffect,id<EffectId>(7),"receipt.lookup"},
             UnknownFact{id<FactId>(2),UnknownBoundary::ExternalEffect,id<EffectId>(8),"receipt.lookup"},
             ResolutionRecord{id<FactId>(3),id<FactId>(2),Determination::NotApplied,"receipt.absent"}}),
      EvidenceState::PersistenceUncertain,conditions(),validation);
  CHECK(unknown);
  std::cerr<<"unknown validated\n";
  auto encoder=[](const int &v){return data::Payload::parse(std::to_string(v));};
  auto wire=control::encode_invoke<int>(Completed<int>{*unknown},encoder);CHECK(wire);
  std::cerr<<"unknown encoded\n";
  CHECK(schema->validate(wire->view()));
  std::cerr<<"unknown schema valid\n";
  auto view=wire->view();CHECK(view.at("kind").string()=="Completed");
  auto body=view.at("outcome");CHECK(body.at("kind").string()=="Indeterminate");
  CHECK(body.at("known_facts").at(0).at("reference_id").string()==control::wire_text(id<EffectId>(7)));
  CHECK(body.at("evidence").string()=="PersistenceUncertain");
  CHECK(body.at("known_facts").at(2).at("kind").string()=="ResolutionRecord");
  auto read=Outcome<int>::validate(ReadCompleted<int>{3,ResultScope::ReadOnly},facts(),EvidenceState::Volatile,conditions(),validation);CHECK(read);
  auto failed_encoder=[](const int &)->Result<data::Payload>{return foundation::make_unexpected(data::error(data::DataErrc::BudgetExceeded));};
  auto failed_wire=control::encode_invoke<int>(Completed<int>{*read},failed_encoder);CHECK(failed_wire);
  CHECK(schema->validate(failed_wire->view()));
  CHECK(failed_wire->view().at("kind").string()=="Completed");
  CHECK(failed_wire->view().at("outcome").at("kind").string()=="ReadCompleted");
  CHECK(!failed_wire->view().at("outcome").at("result_error").missing());
  publication_port.publish(publication());
  auto proof=publication_port.attest(publication());CHECK(proof);
  std::vector<std::shared_ptr<const PublicationProof>> proofs{*proof};
  OutcomeValidation full_validation{publication_port,proofs,fact_budget()};
  auto verify=[&](Outcome<int>::Candidate candidate,std::shared_ptr<const KnownFacts> fs,OutcomeConditions c=conditions()){
    std::cerr<<"outcome variant "<<candidate.index()<<'\n';
    auto outcome=Outcome<int>::validate(std::move(candidate),std::move(fs),EvidenceState::Volatile,c,full_validation);CHECK(outcome);
    auto normal=control::encode_invoke<int>(Completed<int>{*outcome},encoder);CHECK(normal && schema->validate(normal->view()));
    auto failed=control::encode_invoke<int>(Completed<int>{*outcome},failed_encoder);CHECK(failed && schema->validate(failed->view()));
    CHECK(failed->view().at("outcome").at("known_facts").size()==outcome->facts().values().size());
    return normal;
  };
  verify(ReadCompleted<int>{4,ResultScope::Candidate},facts());
  auto state=verify(StateCommitted<int>{id<CommitId>(),domain(),1,1,2},facts({commit(),published()}));
  CHECK(state->view().at("outcome").at("domain").at("generation").string()==control::wire_text(domain().generation));
  EffectFact effect{id<FactId>(),id<EffectId>(),Application::Applied};
  verify(EffectResolved<int>{{id<EffectId>(),Application::Applied,BusinessStatus::Succeeded,{"receipt"},4,""}},facts({effect}));
  LifecycleFact lifecycle{id<FactId>(),id<TransitionId>(),name("before"),name("after"),1};
  verify(LifecycleResolved<int>{{id<TransitionId>(),id<foundation::ObjectId>(),name("before"),name("after"),1,BusinessStatus::Succeeded,4}},facts({lifecycle}));
  verify(PlanCompleted<int>{3,{{name("step"),StepStatus::Succeeded,true,ChildResult::Succeeded,true}}},facts());
  auto plan=Outcome<int>::validate(PlanCompleted<int>{3,{{name("step"),StepStatus::Succeeded,true,ChildResult::Succeeded,true}}},facts(),EvidenceState::Volatile,conditions(),full_validation);CHECK(plan);
  auto plan_wire=control::encode_invoke<int>(Completed<int>{*plan},[](const int &n){return data::Payload::parse("{\"value\":"+std::to_string(n)+"}");});
  CHECK(plan_wire && schema->validate(plan_wire->view()));
  CHECK(plan_wire->view().at("outcome").at("exports").at("value").int64()==3);
  auto before=conditions();before.before_apply=BeforeApplyDecision{true,ApplyDecision::NotReached,true};
  verify(FailedBeforeApply{name("run"),contracts::error(ContractsErrc::Rejected)},facts(),before);
  before.before_apply->decision=ApplyDecision::CancelWon;
  verify(CancelledBeforeApply{contracts::error(ContractsErrc::Rejected)},facts(),before);
  verify(PartialCompletion{{{name("step"),StepStatus::Failed,true,ChildResult::Failed,true}}},facts({effect}));
  std::cout<<"Outcome wire preserves unknown facts and result encoding failure\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
