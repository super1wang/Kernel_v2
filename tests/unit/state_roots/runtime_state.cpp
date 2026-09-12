#include "tests/contract/native/fixtures.hpp"
#include "packages/runtime/executions/invocation_access.hpp"
#include <ock/state/provider.hpp>

using namespace ock::contracts;using namespace ock::runtime;
struct StateItem {int value;};
template<> struct ock::state::RootContract<StateItem>{static Result<StateItem> freeze(const StateItem& v){return v;}static Result<std::size_t> bytes(const StateItem&){return sizeof(StateItem);}};
template<> struct ock::contracts::TypeContract<StateItem>{static TypeIdentity identity(){return{name("runtime.state.item"),ver(),{}};}static Result<void> validate(const StateItem&){return{};}};
static AtomicDomainRef state_domain(){return{ProviderContract<ock::state::ObjectStateProvider>::key(),policy_test::target(),id<foundation::RegistryGeneration>()};}
struct StateReads final:ock::state::SnapshotAuthority {
  std::shared_ptr<CallerAuthorityPort> callers;explicit StateReads(std::shared_ptr<CallerAuthorityPort> v):callers(std::move(v)){}
  Result<void> authorize(const CallerView& caller,const AtomicDomainRef& domain)const override {
    if(domain!=state_domain())return reject(ContractsErrc::InvalidGrant);return validate_caller(*callers,caller);}
};
static std::atomic<unsigned> state_entries=0;
static Result<int> state_edit(const int& input,EditView<ock::state::ObjectStateProvider>& view,WorkContext&) {
  ++state_entries;auto value=ock::state::ObjectValue::freeze(StateItem{input},1024);if(!value)return make_unexpected(value.error());
  auto record=ock::state::ObjectRecord::create(policy_test::target(2),*value,{},4);if(!record)return make_unexpected(record.error());
  auto result=view.edit().find(record->id())?view.edit().replace(*record):view.edit().create(*record);
  if(!result)return make_unexpected(result.error());return input+1;
}
struct Executor final:ExecutorPort{Result<void> submit(std::unique_ptr<ReadyWork>)override{return reject(ContractsErrc::Rejected);}};
static Result<std::size_t> state_target(const int&,std::span<foundation::ObjectId> out)noexcept {
  if(out.empty())return make_unexpected(error(ContractsErrc::BudgetExceeded));out[0]=policy_test::target();return 1;
}
static Result<std::size_t> input_bytes(const int&){return sizeof(int);}
static std::atomic<bool> reject_reply_bytes=false,reject_result_bytes=false;
static Result<std::size_t> reply_bytes(const InvokeReply<int>&){
  if(reject_reply_bytes.load())return make_unexpected(error(ContractsErrc::BudgetExceeded));return 512;}
static Result<std::size_t> result_bytes(const int&){
  if(reject_result_bytes.load())return make_unexpected(error(ContractsErrc::BudgetExceeded));return sizeof(int);}
int main() try {
  policy_test::Env policy;
  ock::state::DomainOptions domain_options{.root_bytes=1024*1024,.candidate_bytes=1024*1024,.result_bytes=4096,
    .history_entries=8,.history_bytes=65536,.snapshot_pins=16,.history_pins=4,.inflight_commits=1,.reclaim_batch=2};
  auto state=ock::state::StateDomain<ock::state::ObjectRoot>::create(state_domain(),{},domain_options,
      std::make_shared<StateReads>(policy.session->callers()));CHECK(state);
  auto provider=ock::state::ObjectMemoryProvider::create(*state,{1024*1024,1024*1024,128});CHECK(provider);
  registry::ModuleManifest manifest{name("state.runtime"),ver()};
  manifest.operations={key()};manifest.providers={ProviderContract<ock::state::ObjectStateProvider>::key()};manifest.executors={name("inline")};
  manifest.required_providers.push_back({{name("state.runtime"),ProviderContract<ock::state::ObjectStateProvider>::key()},CppTypeToken::of<ock::state::ObjectStateProvider>()});
  registry::ModuleInput module{manifest,{}, {},{},{},{{name("inline"),name("app"),false,false,std::make_shared<Executor>()}}, {}};
  auto binding=registry::ProviderBinding::make<ock::state::ObjectStateProvider>(*provider);CHECK(binding);module.providers.push_back(*binding);
  module.register_operations=[](registry::Registrar& registrar){
    DefinitionInput definition{key(),{}, {true,false,false,name("inline"),name("app")},AtomicMode::StateEdit,{name("allow")},"Runtime StateEdit"};
    registry::OperationOptions options{{},{registry::ProviderRef{name("state.runtime"),ProviderContract<ock::state::ObjectStateProvider>::key()}},
      {name("state.runtime"),name("inline")},{},false};
    registry::SubmissionStorage<int,int> storage{64,2048,input_bytes,reply_bytes,result_bytes};
    CHECK(registrar.state_edit(state_edit,definition,options,storage));
  };
  auto batch=registry::RegistrationBatch::create({});CHECK(batch);CHECK((*batch)->add(module));auto catalog=(*batch)->publish();CHECK(catalog);
  auto threads=std::make_shared<native_test::Threads>();auto engine=invocation::NativeEngine::create(*catalog,policy.session,threads,{});CHECK(engine);
  auto bound=(*engine)->bind<int,int>(key(),{},Shape::StateEdit,policy.caller,std::array{policy_test::target()},state_target,name("state.native"));CHECK(bound);
  invocation::InvokeOptions options{{},policy.clock->now()+std::chrono::seconds(5),100};
  auto reply=bound->invoke(41,options);CHECK(std::holds_alternative<Completed<int>>(reply));
  const auto& outcome=std::get<Completed<int>>(reply).outcome;CHECK(std::holds_alternative<StateCommitted<int>>(outcome.value()));
  const auto& committed=std::get<StateCommitted<int>>(outcome.value());CHECK(committed.result&&*committed.result==42&&committed.revision==1);
  CHECK(outcome.facts().values().size()==2&&state_entries==1);
  auto snapshot=(*state)->snapshot(policy.caller->view());CHECK(snapshot&&snapshot->revision()==1);
  auto record=snapshot->value().find(policy_test::target(2));CHECK(record&&record->value().get<StateItem>()->value==41);
  reject_result_bytes=true;auto rejected=bound->invoke(51,options);reject_result_bytes=false;
  CHECK(std::holds_alternative<Completed<int>>(rejected));
  CHECK(std::holds_alternative<FailedBeforeApply>(std::get<Completed<int>>(rejected).outcome.value()));
  auto unchanged=(*state)->snapshot(policy.caller->view());CHECK(unchanged&&unchanged->revision()==1&&state_entries==2);
  auto managed=executions::detail::InvocationAccess::registered_record(*bound,52,options);CHECK(managed);
  threads->role=invocation::ThreadRole::Worker;
  reject_reply_bytes=true;
  CHECK((*managed)->run_once({}));CHECK((*managed)->reply());
  reject_reply_bytes=false;
  const auto& managed_reply=*(*managed)->reply();CHECK(std::holds_alternative<Completed<int>>(managed_reply));
  const auto& managed_outcome=std::get<Completed<int>>(managed_reply).outcome;
  if(!std::holds_alternative<StateCommitted<int>>(managed_outcome.value()))
    std::visit([&](const auto& value){using V=std::decay_t<decltype(value)>;
      std::cerr<<"managed outcome index="<<managed_outcome.value().index()<<" facts="<<managed_outcome.facts().values().size();
      if constexpr(std::same_as<V,FailedBeforeApply>||std::same_as<V,CancelledBeforeApply>)std::cerr<<" error="<<value.reason.code().value();
      std::cerr<<'\n';},managed_outcome.value());
  CHECK(std::holds_alternative<StateCommitted<int>>(managed_outcome.value()));
  CHECK(*std::get<StateCommitted<int>>(managed_outcome.value()).result==53);
  CHECK((*managed)->reply_bytes()==2048);
  auto managed_snapshot=(*state)->snapshot(policy.caller->view());CHECK(managed_snapshot&&managed_snapshot->revision()==2);
  CHECK(managed_snapshot->value().find(policy_test::target(2))->value().get<StateItem>()->value==52&&state_entries==3);
  return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
