// 仅使用公开 SDK 的 Embedded 验证消费者；认证数据来自本进程可信组合根。
#include <ock/runtime/host.hpp>
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace embedded {
struct Value { int number=0; };
}
namespace ock::contracts {
template<> struct TypeContract<embedded::Value> {
  static TypeIdentity identity() {
    return {*Name::parse("embedded.value"),*OperationVersion::parse("1.0.0",5),{}};
  }
  static Result<void> validate(const embedded::Value& value) {
    if(value.number<0||value.number>1000)return make_unexpected(error(ContractsErrc::InvalidContract));
    return {};
  }
  static constexpr auto async_ownership=AsyncOwnership::Owning;
};
}
namespace embedded {
using namespace ock;
using namespace contracts;
using namespace runtime;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
Name name(const char* value) {return *Name::parse(value);}
OperationVersion version() {return *OperationVersion::parse("1.0.0",5);}
OperationKey operation() {return {name("embedded.compute"),version()};}
template<class T> T id() {T value;value.bytes[0]=1;return value;}
PrincipalRef principal() {return {id<PrincipalId>()};}
foundation::ObjectId target() {return id<foundation::ObjectId>();}
std::vector<policy::ScopeRule> rules() {
  std::vector<policy::ScopeRule> out;
  for(auto use:{policy::AccessUse::Invoke,policy::AccessUse::GetSummary,policy::AccessUse::Wait,
      policy::AccessUse::ReadResult,policy::AccessUse::CancelExecution,policy::AccessUse::ListSummary,policy::AccessUse::Subscribe})
    out.push_back({use,policy::OperationSelector{operation(),{}},{name("embedded.use")},{target()},{principal()},
        {policy::SummaryField::Identity,policy::SummaryField::Owner,policy::SummaryField::Parent,
         policy::SummaryField::Phase,policy::SummaryField::Progress,policy::SummaryField::Facts}});
  return out;
}
struct Lifetime final:PortLifetime {};
policy::PolicyConfiguration configuration() {
  auto allow=rules();std::vector<policy::UsePolicyInput> uses;
  for(auto use:{policy::AccessUse::GetSummary,policy::AccessUse::Wait,policy::AccessUse::ReadResult,
      policy::AccessUse::CancelExecution,policy::AccessUse::ListSummary,policy::AccessUse::Subscribe})
    uses.push_back({use,{name("embedded.use")},allow});
  return {{{principal(),allow}},{{{operation(),{}},{name("embedded.use")},allow,true}},
      std::move(uses),{{target(),1,allow,std::make_shared<Lifetime>()}}};
}
struct Clock final:policy::ClockPort {
  policy::TimePoint now()const noexcept override{return std::chrono::steady_clock::now();}
};
struct Authentication final:policy::TrustedAuthenticationPort {
  Result<policy::AuthenticatedIdentity> authenticate(const policy::AuthenticationAttempt& attempt)override {
    if(attempt.credential!=std::vector<std::byte>{std::byte{7}})
      return foundation::make_unexpected(policy::policy_error(policy::PolicyErrc::AuthenticationFailed));
    auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(5);
    return policy::AuthenticatedIdentity{principal(),policy::PrincipalKind::Service,{},{rules(),deadline,false},deadline};
  }
};
struct Digest final:policy::TrustedGroupDigestPort {
  Result<ContractDigest> fingerprint(const policy::GroupSnapshot&)override {
    return foundation::make_unexpected(policy::policy_error(policy::PolicyErrc::InvalidGroup));
  }
};
struct Threads final:invocation::TrustedThreadPort {
  std::thread::id application=std::this_thread::get_id();
  std::weak_ptr<ExecutorControlPort> executor;
  Result<invocation::ThreadObservation> current()const noexcept override {
    if(auto pool=executor.lock();pool&&pool->in_worker())
      return invocation::ThreadObservation{invocation::ThreadRole::Worker,name("embedded"),true};
    if(std::this_thread::get_id()==application)
      return invocation::ThreadObservation{invocation::ThreadRole::Application,name("embedded"),true};
    return foundation::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::ThreadRejected));
  }
};
struct Factory final:host::HostExecutionFactoryPort {
  std::shared_ptr<Threads> threads;
  explicit Factory(std::shared_ptr<Threads> value):threads(std::move(value)){}
  Result<std::shared_ptr<host::HostExecutionPort>> create(HostIncarnation incarnation)override {
    auto made=cpu_pool::Executor::create({2,8});if(!made)return foundation::make_unexpected(made.error());
    std::shared_ptr<ExecutorControlPort> pool=std::move(*made);threads->executor=pool;
    host::ExecutionOptions options;options.active=8;options.subjects={{principal().principal_id,1,2}};
    options.limits.records=32;options.limits.terminal_records=16;
    options.limits.input_bytes=65536;options.limits.reply_bytes=65536;options.limits.terminal_bytes=65536;
    options.limits.waiters=8;options.limits.notice_entries=32;options.limits.observation_leases=8;
    auto result=host::make_executions(incarnation,pool,options);
    if(!result)(void)pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(5));
    return result;
  }
};
struct Executor final:ExecutorPort {
  Result<void> submit(std::unique_ptr<ReadyWork>)override {
    return foundation::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::SubmitRequired));
  }
};
struct Lifecycle final:host::ModuleLifecyclePort {
  bool stopped=false;
  Result<void> start(const host::ModuleContext&)override{return {};}
  host::ModuleStopResult stop()override{stopped=true;return {true,{}};}
};
Result<Value> compute(const Value& input,WorkContext&) {return Value{input.number+1};}
Result<std::size_t> targets(const Value&,std::span<foundation::ObjectId> out)noexcept {
  if(out.empty())return foundation::make_unexpected(error(ContractsErrc::BudgetExceeded));out[0]=target();return 1;
}
int number(const InvokeReply<Value>& reply) {
  const auto* completed=std::get_if<Completed<Value>>(&reply);require(completed,"completed reply");
  const auto* read=std::get_if<ReadCompleted<Value>>(&completed->outcome.value());
  require(read&&read->result,"typed result");return read->result->number;
}
void run() {
  auto threads=std::make_shared<Threads>();auto lifecycle=std::make_shared<Lifecycle>();
  auto made=host::NativeHost::create({},configuration(),{std::make_shared<Authentication>(),
      std::make_shared<Clock>(),std::make_shared<Digest>(),threads,{},std::make_shared<Factory>(threads)});
  require(bool(made),"Host create");auto owner=std::move(*made);
  registry::ModuleManifest manifest{name("embedded"),version()};
  manifest.operations.push_back(operation());manifest.executors.push_back(name("cpu"));
  registry::ModuleInput module{manifest,{}, {}, {}, {},{{name("cpu"),name("embedded"),false,false,std::make_shared<Executor>()}}, {}};
  module.register_operations=[](registry::Registrar& registrar) {
    registry::SubmissionStorage<Value,Value> storage{sizeof(Value),sizeof(InvokeReply<Value>),
      [](const Value&)->Result<std::size_t>{return sizeof(Value);},
      [](const InvokeReply<Value>&)->Result<std::size_t>{return sizeof(InvokeReply<Value>);}};
    DefinitionInput definition{operation(),{},{true,false,false,name("cpu"),name("embedded")},
        AtomicMode::PureCompute,{name("embedded.use")},"Embedded typed parity"};
    require(bool(registrar.compute(compute,definition,{{},{},{name("embedded"),name("cpu")},{},false},storage)),"registration");
  };
  require(bool(owner->add({std::move(module),lifecycle})),"Host add");require(bool(owner->start()),"Host start");
  const auto capabilities=owner->capabilities();
  require(capabilities.async_execution&&capabilities.execution_observation&&!capabilities.state&&!capabilities.storage,"assembled capabilities");
  auto session=owner->open({{std::byte{7}}},{rules(),std::chrono::steady_clock::now()+std::chrono::minutes(4),false});
  require(bool(session),"session");auto caller=session->verify({principal(),{},{}});require(bool(caller),"caller");
  auto binding=session->bind<Value,Value>(operation(),{},Shape::Read,*caller,std::array{target()},targets,name("embedded.call"));
  require(bool(binding),"binding");
  for(int i=0;i<40;++i) {
    invocation::InvokeOptions options{{},std::chrono::steady_clock::now()+std::chrono::seconds(3),100};
    const auto inline_reply=binding->invoke(Value{i},options);require(number(inline_reply)==i+1,"Invoke parity");
    Value input{i};auto submitted=binding->submit(input,options);input.number=900;
    auto accepted=std::get_if<Accepted>(&submitted);require(accepted,"Submit accepted");
    auto waited=session->wait(**caller,accepted->execution,std::chrono::steady_clock::now()+std::chrono::seconds(3));
    require(waited&&waited->state==host::ExecutionWaitState::Terminal,"wait terminal");
    auto result=session->result<Value>(**caller,accepted->execution);
    require(result&&number(*result->value)==i+1,"owned Submit input/result parity");
  }
  const auto report=owner->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(5));
  require(report.quiescent&&lifecycle->stopped,"Host drain");
  std::cout<<"Embedded public SDK: 40 Invoke/Submit parity cycles and quiescent shutdown passed\n";
}
}
int main() {
  try {embedded::run();return 0;}
  catch(const std::exception& failure){std::cerr<<failure.what()<<'\n';return 1;}
}
