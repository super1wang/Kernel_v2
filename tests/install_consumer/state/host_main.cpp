#include <ock/runtime/host.hpp>
#include <ock/runtime/atomic.hpp>
#include <ock/state/provider.hpp>
#ifdef OCK_SETTINGS_MANAGED
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#endif
#include <ock/foundation/sdk_version.hpp>
#include <iostream>
#include <thread>
using namespace ock::contracts;
namespace runtime=ock::runtime;
namespace foundation=ock::foundation;
template<class T>T id(unsigned n=1){T value{};value.bytes[15]=static_cast<std::uint8_t>(n);return value;}
Name name(const char* value){return *Name::parse(value);}
OperationVersion version(){return *OperationVersion::parse("1.0.0",5);}
OperationKey operation(bool group=false){return{name(group?"installed.group":"installed.edit"),version()};}
PrincipalRef principal(){return{id<PrincipalId>()};}
AtomicDomainRef domain(){return{ProviderContract<ock::state::ObjectStateProvider>::key(),id<foundation::ObjectId>(),id<foundation::RegistryGeneration>()};}
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(false)
struct Value {int value;};
namespace ock::contracts {template<>struct TypeContract<Value>{
  static constexpr AsyncOwnership async_ownership=AsyncOwnership::Owning;
  static TypeIdentity identity(){return{::name("installed.value"),::version(),{}};}
  static Result<void> validate(const Value& v){return v.value>=0?Result<void>{}:reject(ContractsErrc::InvalidContract);}
};}
namespace ock::state {template<>struct RootContract<Value>{
  static Result<Value> freeze(const Value& v){return v;}static Result<std::size_t> bytes(const Value&){return sizeof(Value);}
};}
struct Clock final:runtime::policy::ClockPort {runtime::policy::TimePoint now()const noexcept override{return std::chrono::steady_clock::now();}};
static std::vector<runtime::policy::ScopeRule> rules(){
  using namespace runtime::policy;std::vector<ScopeRule> out;
  for(int use=0;use<=static_cast<int>(AccessUse::Subscribe);++use)for(bool group:{false,true})
    out.push_back({static_cast<AccessUse>(use),OperationSelector{operation(group),{}},{name("allow")},{domain().domain_id},{principal()},
      {SummaryField::Identity,SummaryField::Owner,SummaryField::Parent,SummaryField::Phase,SummaryField::Progress,SummaryField::Facts}});
  return out;
}
struct Auth final:runtime::policy::TrustedAuthenticationPort {
  Result<runtime::policy::AuthenticatedIdentity> authenticate(const runtime::policy::AuthenticationAttempt& attempt)override{
    if(attempt.credential!=std::vector<std::byte>{std::byte{7}})return make_unexpected(runtime::policy::policy_error(runtime::policy::PolicyErrc::AuthenticationFailed));
    auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(1);
    return runtime::policy::AuthenticatedIdentity{principal(),runtime::policy::PrincipalKind::User,{},{rules(),deadline,false},deadline};
  }
};
// Deterministic fixture authority; this local installed consumer has no remote input.
struct Digest final:runtime::policy::TrustedGroupDigestPort {
  Result<ContractDigest> fingerprint(const runtime::policy::GroupSnapshot& group)override{
    ContractDigest result{};std::uint64_t hash=1469598103934665603ULL;
    auto add=[&](std::string_view value){for(unsigned char byte:value)hash=(hash^byte)*1099511628211ULL;hash=(hash^0xff)*1099511628211ULL;};
    add(group.envelope().operation.name.view());add(group.envelope().operation.version.text());
    for(const auto& member:group.members()) {add(member.operation.operation.name.view());add(member.operation.operation.version.text());
      for(auto byte:member.operation.contract.bytes)hash=(hash^std::to_integer<unsigned>(byte))*1099511628211ULL;
      for(const auto& target:member.targets)for(auto byte:target.bytes)hash=(hash^byte)*1099511628211ULL;}
    for(unsigned i=0;i<32;++i){result.bytes[i]=std::byte(hash>>((i%8)*8));hash=(hash^i)*1099511628211ULL;}return result;
  }
};
struct Threads final:runtime::invocation::TrustedThreadPort {
  std::thread::id owner=std::this_thread::get_id();
  Result<runtime::invocation::ThreadObservation> current()const noexcept override{
    using namespace runtime::invocation;return ThreadObservation{owner==std::this_thread::get_id()?ThreadRole::Application:ThreadRole::Worker,name("app"),true};}
};
#ifdef OCK_SETTINGS_MANAGED
struct Factory final:runtime::host::HostExecutionFactoryPort {
  Result<std::shared_ptr<runtime::host::HostExecutionPort>> create(HostIncarnation host)override{
    auto made=ock::cpu_pool::Executor::create({2,2});if(!made)return make_unexpected(made.error());
    std::shared_ptr<ExecutorControlPort> executor=std::move(*made);runtime::host::ExecutionOptions options;
    options.subjects={{principal().principal_id}};options.slots={{"state",1,false}};
    options.resources={{{name("installed"),name("state")},{{"state",runtime::resources::Mode::Exclusive,1}}}};
    auto result=runtime::host::make_executions(host,executor,options);
    if(!result)(void)executor->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(3));return result;
  }
};
#endif
struct Lifecycle final:runtime::host::ModuleLifecyclePort {
  Result<void> start(const runtime::host::ModuleContext&)override{return{};}runtime::host::ModuleStopResult stop()override{return{true,{}};}
};
struct Lifetime final:PortLifetime {};
struct Executor final:ExecutorPort {Result<void> submit(std::unique_ptr<ReadyWork>)override{return reject(ContractsErrc::Rejected);}};
struct Reads final:ock::state::SnapshotAuthority {
  std::shared_ptr<CallerAuthorityPort> callers;
  Result<void> authorize(const CallerView& value,const AtomicDomainRef& d)const override{
    return callers&&d==domain()?validate_caller(*callers,value):reject(ContractsErrc::InvalidAuthority);}
};
static Result<Value> edit(const Value& input,EditView<ock::state::ObjectStateProvider>& view,WorkContext& work){
#ifdef OCK_SETTINGS_MANAGED
  if(work.granted_resources().size()!=1)return make_unexpected(error(ContractsErrc::InvalidContract));
#endif
  auto value=ock::state::ObjectValue::freeze(input,1024);if(!value)return make_unexpected(value.error());
  auto row=ock::state::ObjectRecord::create(id<foundation::ObjectId>(2),*value,{},0);if(!row)return make_unexpected(row.error());
  auto changed=view.edit().find(row->id())?view.edit().replace(*row):view.edit().create(*row);
  if(!changed)return make_unexpected(changed.error());return Value{input.value+1};
}
static Result<std::size_t> target(const Value&,std::span<foundation::ObjectId> output)noexcept{
  if(output.empty())return make_unexpected(error(ContractsErrc::BudgetExceeded));output[0]=domain().domain_id;return 1;
}
int main()try{
  runtime::policy::PolicyConfiguration config;config.principals={{principal(),rules()}};
  config.operations={{{operation(),{}},{name("allow")},rules(),false},{{operation(true),{}},{name("allow")},rules(),true}};
  for(int use=1;use<=static_cast<int>(runtime::policy::AccessUse::Subscribe);++use)config.uses.push_back({static_cast<runtime::policy::AccessUse>(use),{name("allow")},rules()});
  config.targets={{domain().domain_id,1,rules(),std::make_shared<Lifetime>()}};
  runtime::host::HostOptions options;options.enable_state=true;
  runtime::host::HostPorts ports{std::make_shared<Auth>(),std::make_shared<Clock>(),std::make_shared<Digest>(),std::make_shared<Threads>(),{}, {}};
#ifdef OCK_SETTINGS_MANAGED
  ports.execution_factory=std::make_shared<Factory>();
#endif
  auto host=runtime::host::NativeHost::create(options,config,ports);CHECK(host);
  struct Close{runtime::host::NativeHost& host;~Close(){(void)host.shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(3));}}close{**host};
  auto reads=std::make_shared<Reads>();auto state=ock::state::StateDomain<ock::state::ObjectRoot>::create(domain(),{},{65536,65536,4096,8,65536,8,8,1,2},reads);CHECK(state);
  auto provider=ock::state::ObjectMemoryProvider::create(*state,{65536,65536,128});CHECK(provider);
  runtime::registry::ModuleManifest manifest{name("installed"),version()};manifest.operations={operation(),operation(true)};
  manifest.providers={domain().provider};manifest.required_providers={{{name("installed"),domain().provider},CppTypeToken::of<ock::state::ObjectStateProvider>()}};
  manifest.executors={name("inline")};manifest.resources={name("state")};manifest.required_resources={{name("installed"),name("state")}};
  runtime::registry::ModuleInput module{manifest,{},{*runtime::registry::ProviderBinding::make<ock::state::ObjectStateProvider>(*provider)},{{name("state"),std::make_shared<ResourceLease>()}},{},{{name("inline"),name("app"),false,false,std::make_shared<Executor>()}}, {}};
#ifndef OCK_SETTINGS_MANAGED
  module.resources.clear();module.manifest.resources.clear();module.manifest.required_resources.clear();
#endif
  module.register_operations=[](runtime::registry::Registrar& registrar){
    DefinitionInput definition{operation(),{},{true,false,false,name("inline"),name("app")},AtomicMode::StateEdit,{name("allow")},"installed State"};
    runtime::registry::OperationOptions options{{},runtime::registry::ProviderRef{name("installed"),domain().provider},{name("installed"),name("inline")},{{name("installed"),name("state")}},false,runtime::registry::RevisionPolicy::RequireExplicitRevision};
#ifndef OCK_SETTINGS_MANAGED
    options.resources.clear();
#endif
    runtime::registry::SubmissionStorage<Value,Value> storage{1024,4096,[](const Value&)->Result<std::size_t>{return sizeof(Value);},[](const InvokeReply<Value>&)->Result<std::size_t>{return 1024;},[](const Value&)->Result<std::size_t>{return sizeof(Value);}};
    CHECK(registrar.state_edit(edit,definition,options,storage));definition.key=operation(true);
    CHECK(runtime::atomic::register_group<ock::state::ObjectStateProvider>(registrar,definition,options));
  };
  CHECK((*host)->add({module,std::make_shared<Lifecycle>()}));CHECK((*host)->start());CHECK((*host)->capabilities().state);
  auto session=(*host)->open({{std::byte{7}}},{rules(),std::chrono::steady_clock::now()+std::chrono::seconds(30),false});CHECK(session);
  auto context=session->catalog_context();CHECK(context);reads->callers=context->authorization->callers();
  auto caller=session->verify({principal(),{},{}});CHECK(caller);auto catalog=std::dynamic_pointer_cast<const runtime::registry::Catalog>(context->definitions);CHECK(catalog);
  auto step=runtime::registry::CandidateBindings::bind<ock::state::ObjectStateProvider,Value,Value>(catalog,operation(),{},AtomicMode::StateEdit,Value{7},domain(),std::array{domain().domain_id},target);CHECK(step);
  auto linked=runtime::registry::CandidateBindings::bind<ock::state::ObjectStateProvider,Value,Value>(catalog,operation(),{},AtomicMode::StateEdit,Value{7},domain(),std::array{domain().domain_id},target,0);CHECK(linked);
  using Input=runtime::atomic::Input<ock::state::ObjectStateProvider>;
  auto input=Input::create({*step,*linked});CHECK(input);
  auto bound=session->bind<Input,runtime::atomic::Results>(operation(true),{},Shape::StateEdit,*caller,std::array{domain().domain_id},runtime::atomic::target<ock::state::ObjectStateProvider>,name("installed.group"));CHECK(bound);
  runtime::invocation::InvokeOptions invoke{{},std::chrono::steady_clock::now()+std::chrono::seconds(5),100,PreparedBase{0,1}};
  std::shared_ptr<const InvokeReply<runtime::atomic::Results>> output;
#ifdef OCK_SETTINGS_MANAGED
  auto accepted=bound->submit(*input,invoke);CHECK(std::holds_alternative<Accepted>(accepted));auto ref=std::get<Accepted>(accepted).execution;
  auto wait=session->wait(**caller,ref,std::chrono::steady_clock::now()+std::chrono::seconds(5));CHECK(wait&&wait->state==runtime::host::ExecutionWaitState::Terminal);
  auto result=session->result<runtime::atomic::Results>(**caller,ref);CHECK(result);
  output=result->value;
#else
  output=std::make_shared<const InvokeReply<runtime::atomic::Results>>(bound->invoke(*input,invoke));
#endif
  const auto& committed=std::get<StateCommitted<runtime::atomic::Results>>(std::get<Completed<runtime::atomic::Results>>(*output).outcome.value());
  CHECK(committed.revision==1&&committed.result&&committed.result->values.size()==2&&committed.result->values[1].get<Value>()->value==9);
  auto snapshot=(*state)->snapshot((*caller)->view());CHECK(snapshot&&snapshot->revision()==1&&snapshot->value().find(id<foundation::ObjectId>(2))->value().get<Value>()->value==8);
#ifdef OCK_SETTINGS_MANAGED
  std::cout<<ock::sdk::version<<" State Host managed Atomic passed\n";
#else
  std::cout<<ock::sdk::version<<" State Host native Atomic passed\n";
#endif
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
