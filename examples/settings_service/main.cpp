#include "packages/runtime/invocation/invocation.hpp"
#include "packages/runtime/executions/invocation_access.hpp"
#include <ock/state/provider.hpp>
#include <iostream>
#include <thread>

namespace settings_service {
using namespace ock::contracts;using namespace ock::runtime;using namespace policy;namespace foundation=ock::foundation;
Name name(const char* s){return *Name::parse(s);} OperationVersion version(){return *OperationVersion::parse("1.0.0",5);}
OperationKey operation(){return{name("settings.set"),version()};}
template<class T>T id(unsigned n=1){T value{};value.bytes[15]=static_cast<std::uint8_t>(n);return value;}
PrincipalRef principal(){return{id<PrincipalId>()};} foundation::ObjectId target(){return id<foundation::ObjectId>();}
struct Put{int value;};struct Ack{int value;};struct Setting{int value;};
std::vector<ScopeRule> rules(){return{{AccessUse::Invoke,OperationSelector{operation(),{}},{name("write")},{target()},{principal()},{}}};}
struct Clock final:ClockPort{TimePoint now()const noexcept override{return std::chrono::steady_clock::now();}};
struct Authentication final:TrustedAuthenticationPort{
  TimePoint deadline;explicit Authentication(TimePoint d):deadline(d){}
  Result<AuthenticatedIdentity> authenticate(const AuthenticationAttempt& a)override{
    if(a.credential!=std::vector<std::byte>{std::byte{7}})return make_unexpected(policy_error(PolicyErrc::AuthenticationFailed));
    return AuthenticatedIdentity{principal(),PrincipalKind::Service,{}, {rules(),deadline,false},deadline};}
};
struct Digest final:TrustedGroupDigestPort{Result<ContractDigest> fingerprint(const GroupSnapshot& group)override{
  ContractDigest value{};value.bytes[0]=std::byte{0x61};value.bytes[1]=std::byte(group.members().size());return value;}};
struct Source final:ExecutionAccessSourcePort{
  ObservationSourceIdentity identity()const noexcept override{return{id<HostIncarnation>(),RestoreMode::Absent};}
  Result<ExecutionAccessInput> find(ExecutionRef)override{return make_unexpected(policy_error(PolicyErrc::TargetUnavailable));}
  Result<AccessScanPage> scan(const AccessScanRequest&)override{return AccessScanPage{{},{},identity().host,name("settings")};}
};
struct Lifetime final:PortLifetime{};
struct Threads final:invocation::TrustedThreadPort{
  invocation::ThreadRole role=invocation::ThreadRole::Application;
  Result<invocation::ThreadObservation> current()const noexcept override{return invocation::ThreadObservation{role,name("app"),true};}
};
struct Executor final:ExecutorPort{Result<void> submit(std::unique_ptr<ReadyWork>)override{return reject(ContractsErrc::Rejected);}};
AtomicDomainRef domain(){return{ProviderContract<ock::state::ObjectStateProvider>::key(),target(),id<foundation::RegistryGeneration>()};}
struct Reads final:ock::state::SnapshotAuthority{
  std::shared_ptr<CallerAuthorityPort> callers;explicit Reads(std::shared_ptr<CallerAuthorityPort> v):callers(std::move(v)){}
  Result<void> authorize(const CallerView& caller,const AtomicDomainRef& value)const override{
    if(value!=domain())return reject(ContractsErrc::InvalidGrant);return validate_caller(*callers,caller);}
};
}
namespace ock::contracts {
template<>struct TypeContract<settings_service::Put>{static TypeIdentity identity(){return{*Name::parse("settings.put"),settings_service::version(),{}};}static Result<void> validate(const settings_service::Put&){return{};}static constexpr auto async_ownership=AsyncOwnership::Owning;};
template<>struct TypeContract<settings_service::Ack>{static TypeIdentity identity(){return{*Name::parse("settings.ack"),settings_service::version(),{}};}static Result<void> validate(const settings_service::Ack&){return{};}static constexpr auto async_ownership=AsyncOwnership::Owning;};
template<>struct TypeContract<settings_service::Setting>{static TypeIdentity identity(){return{*Name::parse("settings.value"),settings_service::version(),{}};}static Result<void> validate(const settings_service::Setting&){return{};}};
}
namespace ock::state {
template<>struct RootContract<settings_service::Setting>{static foundation::Result<settings_service::Setting> freeze(const settings_service::Setting& v){return v;}static foundation::Result<std::size_t> bytes(const settings_service::Setting&){return sizeof(settings_service::Setting);}};
}
namespace settings_service {
Result<Ack> set(const Put& input,EditView<ock::state::ObjectStateProvider>& view,WorkContext&){
  auto value=ock::state::ObjectValue::freeze(Setting{input.value},1024);if(!value)return make_unexpected(value.error());
  auto record=ock::state::ObjectRecord::create(id<foundation::ObjectId>(2),*value,{},4);if(!record)return make_unexpected(record.error());
  auto changed=view.edit().find(record->id())?view.edit().replace(*record):view.edit().create(*record);
  if(!changed)return make_unexpected(changed.error());return Ack{input.value};
}
Result<std::size_t> targets(const Put&,std::span<foundation::ObjectId> out)noexcept{
  if(out.empty())return make_unexpected(error(ContractsErrc::BudgetExceeded));out[0]=target();return 1;}
Result<std::size_t> input_bytes(const Put&){return sizeof(Put);} Result<std::size_t> reply_bytes(const InvokeReply<Ack>&){return 512;}
Result<std::size_t> result_bytes(const Ack&){return sizeof(Ack);}
void require(bool value){if(!value)throw std::runtime_error("Settings State consumer failed");}
}
int main()try{
  using namespace settings_service;
  auto clock=std::make_shared<Clock>();auto auth=std::make_shared<Authentication>(clock->now()+std::chrono::hours(1));
  PolicyConfiguration config;config.principals={{principal(),rules()}};config.operations={{OperationSelector{operation(),{}},{name("write")},rules(),false}};
  config.targets={{target(),1,rules(),std::make_shared<Lifetime>()}};
  auto assembly=PolicyStore::create({},config,auth,clock,std::make_shared<Digest>(),std::make_shared<Source>());require(bool(assembly));
  auto opened=assembly->store->open({{std::byte{7}}},{rules(),auth->deadline,false});require(bool(opened));auto session=*opened;
  auto caller=session->verify({principal(),{}, {}});require(bool(caller));
  ock::state::DomainOptions state_options{.root_bytes=1024*1024,.candidate_bytes=1024*1024,.result_bytes=4096,.history_entries=8,
    .history_bytes=65536,.snapshot_pins=16,.history_pins=4,.inflight_commits=1,.reclaim_batch=2};
  auto state=ock::state::StateDomain<ock::state::ObjectRoot>::create(domain(),{},state_options,std::make_shared<Reads>(session->callers()));require(bool(state));
  auto provider=ock::state::ObjectMemoryProvider::create(*state,{1024*1024,1024*1024,128});require(bool(provider));
  registry::ModuleManifest manifest{name("settings"),version()};manifest.operations={operation()};
  manifest.providers={ProviderContract<ock::state::ObjectStateProvider>::key()};manifest.executors={name("inline")};
  manifest.required_providers.push_back({{name("settings"),ProviderContract<ock::state::ObjectStateProvider>::key()},CppTypeToken::of<ock::state::ObjectStateProvider>()});
  registry::ModuleInput module{manifest,{}, {},{},{},{{name("inline"),name("app"),false,false,std::make_shared<Executor>()}}, {}};
  module.providers.push_back(*registry::ProviderBinding::make<ock::state::ObjectStateProvider>(*provider));
  module.register_operations=[](registry::Registrar& registrar){
    DefinitionInput definition{operation(),{}, {true,false,false,name("inline"),name("app")},AtomicMode::StateEdit,{name("write")},"Settings State"};
    registry::OperationOptions options{{},{registry::ProviderRef{name("settings"),ProviderContract<ock::state::ObjectStateProvider>::key()}},
      {name("settings"),name("inline")},{},false};
    registry::SubmissionStorage<Put,Ack> storage{64,2048,input_bytes,reply_bytes,result_bytes};
    require(bool(registrar.state_edit(set,definition,options,storage)));
  };
  auto batch=registry::RegistrationBatch::create({});require(bool(batch));require(bool((*batch)->add(module)));auto catalog=(*batch)->publish();require(bool(catalog));
  auto threads=std::make_shared<Threads>();auto engine=invocation::NativeEngine::create(*catalog,session,threads,{});require(bool(engine));
  auto bound=(*engine)->bind<Put,Ack>(operation(),{},Shape::StateEdit,*caller,std::array{target()},targets,name("settings.native"));require(bool(bound));
  invocation::InvokeOptions options{{},clock->now()+std::chrono::seconds(5),100};auto native=bound->invoke(Put{7},options);
  require(std::holds_alternative<Completed<Ack>>(native));auto& n=std::get<Completed<Ack>>(native).outcome;
  require(std::holds_alternative<StateCommitted<Ack>>(n.value())&&n.facts().values().size()==2);
  auto managed=executions::detail::InvocationAccess::registered_record(*bound,Put{9},options);require(bool(managed));threads->role=invocation::ThreadRole::Worker;
  require((*managed)->run_once({})&&(*managed)->reply());auto& m=std::get<Completed<Ack>>(*(*managed)->reply()).outcome;
  require(std::holds_alternative<StateCommitted<Ack>>(m.value())&&m.facts().values().size()==2);
  auto snapshot=(*state)->snapshot((*caller)->view());require(bool(snapshot)&&snapshot->revision()==2);
  auto row=snapshot->value().find(id<foundation::ObjectId>(2));require(row&&row->value().get<Setting>()->value==9);
  std::cout<<R"({"checks":{"native_state":true,"managed_state":true,"publication_proof":true,"revision":2}})"<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
