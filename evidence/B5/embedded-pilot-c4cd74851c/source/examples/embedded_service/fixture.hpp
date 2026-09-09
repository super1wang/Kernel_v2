#pragma once
// 仅使用公开 SDK 的 Embedded 验证消费者；认证数据来自本进程可信组合根。
#include <ock/runtime/host.hpp>
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>
#include <functional>

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
OperationKey structured_operation() {return {name("embedded.structured"),version()};}
template<class T> T id() {T value;value.bytes[0]=1;return value;}
PrincipalRef principal() {return {id<PrincipalId>()};}
foundation::ObjectId target() {return id<foundation::ObjectId>();}
std::vector<policy::ScopeRule> rules() {
  std::vector<policy::ScopeRule> out;
  for(const auto& key:{operation(),structured_operation()})
  for(auto use:{policy::AccessUse::Invoke,policy::AccessUse::GetSummary,policy::AccessUse::Wait,
      policy::AccessUse::ReadResult,policy::AccessUse::CancelExecution,policy::AccessUse::ListSummary,policy::AccessUse::Subscribe})
    out.push_back({use,policy::OperationSelector{key,{}},{name("embedded.use")},{target()},{principal()},
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
  return {{{principal(),allow}},{{{operation(),{}},{name("embedded.use")},allow,true},
      {{structured_operation(),{}},{name("embedded.use")},allow,true}},
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
    options.children_per_execution=2;options.max_depth=4;
    options.slots={{"embedded.slot",1,false}};
    options.resources={{{name("embedded"),name("shared")},{{"embedded.slot",resources::Mode::Exclusive,1}}}};
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
struct StructuredState {
  std::atomic<bool> parent_entered=false,parent_release=false,child_entered=false,child_release=false,child_stopped=false;
  std::function<SubmitReply(const WorkContext&)> submit_child;
  std::optional<SubmitReply> child_reply;
};
// 消费者只有一次结构化场景；owner 在 Host 排空后才释放。
std::shared_ptr<StructuredState> structured;
Result<Value> structured_compute(const Value& input,WorkContext& work) {
  auto state=structured;
  require(state&&work.execution_scope()&&work.granted_resources().size()==1,"structured scope/resource");
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  if(input.number==0) {
    state->child_reply=state->submit_child(work);state->parent_entered=true;
    while(!state->parent_release&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
    require(state->parent_release,"parent release deadline");
  } else {
    state->child_entered=true;
    while(!state->child_release&&std::chrono::steady_clock::now()<end) {
      if(work.stop_requested())state->child_stopped=true;
      std::this_thread::yield();
    }
    require(state->child_release,"child release deadline");
  }
  return Value{input.number+10};
}
template<class Predicate> void until(Predicate predicate,const char* message) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!predicate()&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
  require(predicate(),message);
}
Result<std::size_t> targets(const Value&,std::span<foundation::ObjectId> out)noexcept {
  if(out.empty())return foundation::make_unexpected(error(ContractsErrc::BudgetExceeded));out[0]=target();return 1;
}
int number(const InvokeReply<Value>& reply) {
  const auto* completed=std::get_if<Completed<Value>>(&reply);require(completed,"completed reply");
  const auto* read=std::get_if<ReadCompleted<Value>>(&completed->outcome.value());
  require(read&&read->result,"typed result");return read->result->number;
}
template<class Hooks> void run(Hooks& hooks) {
  hooks.stage(1,0,0);
  {
  auto threads=std::make_shared<Threads>();auto lifecycle=std::make_shared<Lifecycle>();
  std::weak_ptr<Lifecycle> released_lifecycle=lifecycle;
  auto made=host::NativeHost::create({},configuration(),{std::make_shared<Authentication>(),
      std::make_shared<Clock>(),std::make_shared<Digest>(),threads,{},std::make_shared<Factory>(threads)});
  require(bool(made),"Host create");auto owner=std::move(*made);
  registry::ModuleManifest manifest{name("embedded"),version()};
  manifest.operations.push_back(operation());manifest.executors.push_back(name("cpu"));
  manifest.operations.push_back(structured_operation());manifest.resources.push_back(name("shared"));
  registry::ModuleInput module{manifest,{}, {}, {}, {},{{name("cpu"),name("embedded"),false,false,std::make_shared<Executor>()}}, {}};
  module.resources.push_back({name("shared"),std::make_shared<ResourceLease>()});
  module.register_operations=[](registry::Registrar& registrar) {
    registry::SubmissionStorage<Value,Value> storage{sizeof(Value),sizeof(InvokeReply<Value>),
      [](const Value&)->Result<std::size_t>{return sizeof(Value);},
      [](const InvokeReply<Value>&)->Result<std::size_t>{return sizeof(InvokeReply<Value>);}};
    DefinitionInput definition{operation(),{},{true,false,false,name("cpu"),name("embedded")},
        AtomicMode::PureCompute,{name("embedded.use")},"Embedded typed parity"};
    require(bool(registrar.compute(compute,definition,{{},{},{name("embedded"),name("cpu")},{},false},storage)),"registration");
    definition.key=structured_operation();
    definition.execution.inline_safe=false;
    require(bool(registrar.compute(structured_compute,definition,
      {{},{},{name("embedded"),name("cpu")},{{name("embedded"),name("shared")}},false},storage)),"structured registration");
  };
  require(bool(owner->add({std::move(module),lifecycle})),"Host add");require(bool(owner->start()),"Host start");
  const auto capabilities=owner->capabilities();
  require(capabilities.async_execution&&capabilities.execution_observation&&!capabilities.state&&!capabilities.storage,"assembled capabilities");
  hooks.stage(2,12,0);
  std::array<PublicLogRecord,8> logs;
  auto page=owner->copy_logs({owner->incarnation(),1,0},logs);
  require(page&&page->copied>0&&page->accepted_upper.accepted_sequence>0,"default memory log read");
  {
  auto session=owner->open({{std::byte{7}}},{rules(),std::chrono::steady_clock::now()+std::chrono::minutes(4),false});
  require(bool(session),"session");auto caller=session->verify({principal(),{},{}});require(bool(caller),"caller");
  {
  auto binding=session->bind<Value,Value>(operation(),{},Shape::Read,*caller,std::array{target()},targets,name("embedded.call"));
  require(bool(binding),"binding");
  for(int i=0;i<4;++i) {
    hooks.measure("warmup",i,false,[&]{
      require(number(binding->invoke(Value{i},{{},std::chrono::steady_clock::now()+std::chrono::seconds(3),100}))==i+1,"warmup parity");
    });
  }
  hooks.stage(3,15,0);hooks.stage(4,15,0);
  for(int i=0;i<40;++i) {
    invocation::InvokeOptions options{{},std::chrono::steady_clock::now()+std::chrono::seconds(3),100};
    hooks.measure("invoke",i,true,[&]{
      const auto inline_reply=binding->invoke(Value{i},options);require(number(inline_reply)==i+1,"Invoke parity");
    });
    hooks.measure("submit_wait_result",i,false,[&]{
    Value input{i};auto submitted=binding->submit(input,options);input.number=900;
    auto accepted=std::get_if<Accepted>(&submitted);require(accepted,"Submit accepted");
    auto waited=session->wait(**caller,accepted->execution,std::chrono::steady_clock::now()+std::chrono::seconds(3));
    require(waited&&waited->state==host::ExecutionWaitState::Terminal,"wait terminal");
    auto result=session->result<Value>(**caller,accepted->execution);
    require(result&&number(*result->value)==i+1,"owned Submit input/result parity");
    });
  }
  hooks.measure("resource_child_cancel",0,false,[&]{
    auto parent=session->bind<Value,Value>(structured_operation(),{},Shape::Read,*caller,std::array{target()},targets,name("embedded.parent"));
    auto child=session->bind<Value,Value>(structured_operation(),{},Shape::Read,*caller,std::array{target()},targets,name("embedded.child"));
    require(parent&&child,"structured bindings");
    auto state=std::make_shared<StructuredState>();structured=state;
    struct Release {std::shared_ptr<StructuredState> state;~Release(){state->parent_release=true;state->child_release=true;}} release{state};
    state->submit_child=[child_owner=std::make_shared<host::HostBound<Value,Value>>(std::move(*child))](const WorkContext& work) {
      return child_owner->submit_child(work,Value{1},{{},work.deadline(),100});
    };
    auto submitted=parent->submit(Value{0},{{},std::chrono::steady_clock::now()+std::chrono::seconds(5),100});
    require(std::holds_alternative<Accepted>(submitted),"parent accepted");
    const auto parent_ref=std::get<Accepted>(submitted).execution;
    until([&]{return state->parent_entered.load();},"parent entered");
    require(state->child_reply&&std::holds_alternative<Accepted>(*state->child_reply),"child accepted");
    const auto child_ref=std::get<Accepted>(*state->child_reply).execution;
    require(parent_ref!=child_ref,"independent child identity");
    auto context=session->catalog_context();require(bool(context),"observation context");
    auto phase=[&](ExecutionRef ref) {
      auto observed=context->authorization->observations()->get(**caller,ref,policy::AccessUse::GetSummary);
      require(bool(observed),"authorized phase");return observed->summary->value().phase;
    };
    until([&]{return phase(child_ref)==ExecutionPhase::WaitingResources;},"child resource wait");
    require(!state->child_entered,"exclusive resource held by parent");
    state->parent_release=true;
    until([&]{return state->child_entered.load()&&phase(parent_ref)==ExecutionPhase::WaitingChild;},"parent waits for child");
    auto cancelled=session->cancel(**caller,parent_ref);
    require(cancelled&&*cancelled==CancelDisposition::AlreadyClaimed,"parent cancel");
    until([&]{return state->child_stopped.load();},"child cancel propagation");
    require(phase(parent_ref)==ExecutionPhase::WaitingChild,"parent not early terminal");
    state->child_release=true;
    auto waited=session->wait(**caller,parent_ref,std::chrono::steady_clock::now()+std::chrono::seconds(3));
    require(waited&&waited->state==host::ExecutionWaitState::Terminal,"parent terminal after child");
    require(phase(child_ref)==ExecutionPhase::Terminal,"child terminal");
    auto parent_result=session->result<Value>(**caller,parent_ref),child_result=session->result<Value>(**caller,child_ref);
    require(parent_result&&number(*parent_result->value)==10&&child_result&&number(*child_result->value)==11,"structured results");
    state->submit_child={};structured.reset();
  });
  hooks.stage(5,15,0);
  const auto report=owner->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(5));
  require(report.quiescent&&lifecycle->stopped,"Host drain");
  hooks.stage(6,15,0);
  }
  hooks.stage(7,14,0);
  require(bool(session->close()),"session close");
  }
  hooks.stage(8,12,0);
  owner.reset();lifecycle.reset();threads.reset();
  require(released_lifecycle.expired(),"last lifecycle owner released");
  }
  hooks.stage(9,0,1);hooks.stage(10,0,1);
  std::cout<<"Embedded public SDK: 40 Invoke/Submit parity cycles, resource/child/cancel, memory log and quiescent shutdown passed\n";
}
}
