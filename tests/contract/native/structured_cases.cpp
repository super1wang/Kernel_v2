#include "fixtures.hpp"
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <ock/runtime/host.hpp>
namespace native_test {
namespace {
std::atomic<bool> parent_entered=false,parent_release=false,child_entered=false,child_release=false,child_stopped=false;
std::function<SubmitReply(const WorkContext&)> create_child;
std::optional<SubmitReply> child_reply;
std::shared_ptr<ExecutionScopePort> saved_scope;
std::chrono::steady_clock::time_point saved_deadline;
Result<int> structured_handler(const int& value,WorkContext& work) {
  CHECK(work.execution_scope());CHECK(work.granted_resources().size()==1);
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  if(value==0) {
    saved_scope=work.execution_scope();
    saved_deadline=work.deadline();
    child_reply=create_child(work);
    parent_entered=true;
    while(!parent_release&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
  } else {
    child_entered=true;
    while(!child_release&&std::chrono::steady_clock::now()<end) {
      if(work.stop_requested())child_stopped=true;
      std::this_thread::yield();
    }
  }
  return value+10;
}
template<class Predicate> void until(Predicate predicate) {
  const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!predicate()&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
  CHECK(predicate());
}
struct StructuredHost {
  struct Factory final : host::HostExecutionFactoryPort {
    host::ExecutionOptions options;
    Result<std::shared_ptr<host::HostExecutionPort>> create(HostIncarnation id) override {
      auto made=ock::cpu_pool::Executor::create({2,2});CHECK(made);
      std::shared_ptr<ExecutorControlPort> pool=std::move(*made);
      auto backend=host::make_executions(id,pool,options);
      if(!backend)CHECK(pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)));
      return backend;
    }
  };
  struct Lifecycle final : host::ModuleLifecyclePort {
    Result<void> start(const host::ModuleContext&) override {return {};}
    host::ModuleStopResult stop() override {return {true,{}};}
  };
  struct Threads final : TrustedThreadPort {
    std::thread::id application=std::this_thread::get_id();
    Result<ThreadObservation> current() const noexcept override {
      return ThreadObservation{std::this_thread::get_id()==application?ThreadRole::Application:ThreadRole::Worker,name("app"),true};
    }
  };
  std::optional<registry::ModuleInput> registration;
  Env env;
  std::unique_ptr<host::NativeHost> host;
  std::optional<host::HostSession> session;
  std::shared_ptr<const policy::VerifiedCaller> caller;
  explicit StructuredHost(std::size_t depth=32):env(false,structured_handler,{},[&](registry::ModuleInput& module) {
    module.manifest.resources.push_back(name("declared"));
    module.manifest.required_resources.push_back({name("native"),name("declared")});
    module.resources.push_back({name("declared"),std::make_shared<ResourceLease>()});
    module.register_operations=[](registry::Registrar& registrar) {
      DefinitionInput d{key(),{}, {false,false,false,name("test"),name("app")},
          AtomicMode::PureCompute,{name("allow")},"structured"};
      registry::OperationOptions o{{},{},{name("native"),name("test")},{{name("native"),name("declared")}},false};
      registry::SubmissionStorage<int,int> storage{4,sizeof(InvokeReply<int>),
          [](const int&)->Result<std::size_t>{return 4;},
          [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
      CHECK(registrar.compute(structured_handler,d,o,storage));
    };
    registration=module;
  }) {
    auto factory=std::make_shared<Factory>();
    factory->options.subjects={{policy_test::principal().principal_id}};
    factory->options.children_per_execution=1;factory->options.max_depth=depth;
    factory->options.slots={{"shared.slot",1,false}};
    factory->options.resources={{{name("native"),name("declared")},{{"shared.slot",resources::Mode::Exclusive,1}}}};
    host::HostPorts ports{env.policy.auth,env.policy.clock,env.policy.digest,std::make_shared<Threads>(),{},factory};
    auto made=host::NativeHost::create({},policy_test::configuration(),ports);CHECK(made);host=std::move(*made);
    CHECK(host->add({*registration,std::make_shared<Lifecycle>()}));CHECK(host->start());
    auto opened=host->open({{std::byte{7}}},{policy_test::rules(),env.policy.auth->identity.deadline,false});CHECK(opened);
    session.emplace(std::move(*opened));
    auto verified=session->verify({policy_test::principal(),{}, {}});CHECK(verified);caller=*verified;
  }
  auto bind() {return session->bind<int,int>(key(),{},Shape::Read,caller,
      std::array{policy_test::target()},targets,name("structured"));}
  auto options() {return invocation::InvokeOptions{{},env.policy.clock->now()+std::chrono::seconds(4),100};}
  ExecutionPhase phase(ExecutionRef ref) {
    auto context=session->catalog_context();CHECK(context);
    auto observed=context->authorization->observations()->get(*caller,ref,policy::AccessUse::GetSummary);CHECK(observed);
    return observed->summary->value().phase;
  }
  ~StructuredHost() {
    parent_release=true;child_release=true;
    if(host)(void)host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));
  }
};
void reset_structured() {
  parent_entered=false;parent_release=false;child_entered=false;child_release=false;child_stopped=false;
  child_reply.reset();saved_scope.reset();create_child={};
}
}
void host_structured_lifetime() {
  reset_structured();StructuredHost fixture;
  auto parent=fixture.bind(),child=fixture.bind(),extra=fixture.bind();CHECK(parent&&child&&extra);
  std::atomic<bool> full_rejected=false;
  create_child=[&](const WorkContext& work) {
    auto reply=child->submit_child(work,1,fixture.options());
    full_rejected=std::holds_alternative<Rejected>(extra->submit_child(work,2,fixture.options()));
    return reply;
  };
  auto submitted=parent->submit(0,fixture.options());CHECK(std::holds_alternative<Accepted>(submitted));
  const auto parent_ref=std::get<Accepted>(submitted).execution;
  until([]{return parent_entered.load();});CHECK(full_rejected);
  CHECK(child_reply&&std::holds_alternative<Accepted>(*child_reply));
  const auto child_ref=std::get<Accepted>(*child_reply).execution;CHECK(child_ref!=parent_ref);
  until([&]{return fixture.phase(child_ref)==ExecutionPhase::WaitingResources;});CHECK(!child_entered);
  auto context=fixture.session->catalog_context();CHECK(context);
  auto observed=context->authorization->observations()->get(*fixture.caller,child_ref,policy::AccessUse::GetSummary);CHECK(observed);
  CHECK(observed->summary->value().parent==parent_ref);
  CHECK(observed->summary->value().owner==fixture.caller->view().description().principal);
  parent_release=true;until([]{return child_entered.load();});
  until([&]{return fixture.phase(parent_ref)==ExecutionPhase::WaitingChild;});
  CHECK(!fixture.session->result<int>(*fixture.caller,parent_ref));
  auto waited=fixture.session->wait(*fixture.caller,parent_ref,std::chrono::steady_clock::now(),{});
  CHECK(waited&&waited->state==host::ExecutionWaitState::Timeout);
  auto cancelled=fixture.session->cancel(*fixture.caller,parent_ref);
  CHECK(cancelled&&*cancelled==CancelDisposition::AlreadyClaimed);
  until([]{return child_stopped.load();});CHECK(fixture.phase(parent_ref)==ExecutionPhase::WaitingChild);
  child_release=true;
  auto done=fixture.session->wait(*fixture.caller,parent_ref,std::chrono::steady_clock::now()+std::chrono::seconds(2),{});
  CHECK(done&&done->state==host::ExecutionWaitState::Terminal);
  CHECK(fixture.phase(child_ref)==ExecutionPhase::Terminal);
  auto parent_result=fixture.session->result<int>(*fixture.caller,parent_ref);CHECK(parent_result&&result(*parent_result->value)==10);
  auto child_result=fixture.session->result<int>(*fixture.caller,child_ref);CHECK(child_result&&result(*child_result->value)==11);
  // 保存已结束 scope 不得重新打开父寿命。
  auto budget=foundation::CheckedCount<std::uint64_t>::create(0,100);CHECK(budget);
  WorkContext stale({},fixture.options().deadline,*budget,name("stale"),BorrowedResourceViews{{}},saved_scope);
  CHECK(std::holds_alternative<Rejected>(extra->submit_child(stale,2,fixture.options())));
  // 新的一组父子在关闭超时后仍保活；默认取消不会伪造业务退出。
  reset_structured();
  create_child=[&](const WorkContext& work){return child->submit_child(work,1,fixture.options());};
  CHECK(std::holds_alternative<Accepted>(parent->submit(0,fixture.options())));
  until([]{return parent_entered.load();});parent_release=true;until([]{return child_entered.load();});
  CHECK(!fixture.host->shutdown_until(std::chrono::steady_clock::now()).quiescent);
  until([]{return child_stopped.load();});child_release=true;
  CHECK(fixture.host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)).quiescent);
  saved_scope.reset();create_child={};
}
void host_structured_admission() {
  {
    reset_structured();StructuredHost fixture(1);
    auto parent=fixture.bind(),child=fixture.bind();CHECK(parent&&child);
    create_child=[&](const WorkContext& work){return child->submit_child(work,1,fixture.options());};
    auto reply=parent->submit(0,fixture.options());CHECK(std::holds_alternative<Accepted>(reply));
    until([]{return parent_entered.load();});CHECK(child_reply&&std::holds_alternative<Rejected>(*child_reply));
    CHECK(!child_entered);parent_release=true;
    auto waited=fixture.session->wait(*fixture.caller,std::get<Accepted>(reply).execution,
        std::chrono::steady_clock::now()+std::chrono::seconds(2),{});
    CHECK(waited&&waited->state==host::ExecutionWaitState::Terminal);
    struct Forged final : ExecutionScopePort {ExecutionRef execution() const noexcept override {return {};}};
    auto budget=foundation::CheckedCount<std::uint64_t>::create(0,100);CHECK(budget);
    WorkContext fake({},fixture.options().deadline,*budget,name("fake"),BorrowedResourceViews{{}},std::make_shared<Forged>());
    CHECK(std::holds_alternative<Rejected>(child->submit_child(fake,1,fixture.options())));
    saved_scope.reset();create_child={};
  }
  {
    reset_structured();StructuredHost fixture,foreign;
    auto parent=fixture.bind(),child=fixture.bind(),other=foreign.bind();CHECK(parent&&child&&other);
    create_child=[](const WorkContext&)->SubmitReply {return Rejected{error(ContractsErrc::Rejected)};};
    auto reply=parent->submit(0,fixture.options());CHECK(std::holds_alternative<Accepted>(reply));
    until([]{return parent_entered.load();});
    auto budget=foundation::CheckedCount<std::uint64_t>::create(0,100);CHECK(budget);
    WorkContext current({},saved_deadline,*budget,name("current"),BorrowedResourceViews{{}},saved_scope);
    auto foreign_reply=other->submit_child(current,1,foreign.options());
    CHECK(std::holds_alternative<Rejected>(foreign_reply));
    CHECK(std::get<Rejected>(foreign_reply).reason.code()==error(ContractsErrc::InvalidAuthority).code());
    auto other_session=fixture.host->open({{std::byte{7}}},
        {policy_test::rules(),fixture.env.policy.auth->identity.deadline,false});CHECK(other_session);
    auto other_caller=other_session->verify({policy_test::principal(),{}, {}});CHECK(other_caller);
    auto other_binding=other_session->bind<int,int>(key(),{},Shape::Read,*other_caller,
        std::array{policy_test::target()},targets,name("other.session"));CHECK(other_binding);
    auto other_reply=other_binding->submit_child(current,1,fixture.options());
    CHECK(std::holds_alternative<Rejected>(other_reply));
    CHECK(std::get<Rejected>(other_reply).reason.code()==error(ContractsErrc::InvalidAuthority).code());
    auto accepted=child->submit_child(current,1,fixture.options());CHECK(std::holds_alternative<Accepted>(accepted));
    auto child_ref=std::get<Accepted>(accepted).execution;
    until([&]{return fixture.phase(child_ref)==ExecutionPhase::WaitingResources;});
    auto cancelled=fixture.session->cancel(*fixture.caller,std::get<Accepted>(reply).execution);
    CHECK(cancelled&&*cancelled==CancelDisposition::AlreadyClaimed);
    until([&]{return fixture.phase(child_ref)==ExecutionPhase::Terminal;});CHECK(!child_entered);
    CHECK(std::holds_alternative<Rejected>(child->submit_child(current,1,fixture.options())));
    parent_release=true;
    auto done=fixture.session->wait(*fixture.caller,std::get<Accepted>(reply).execution,
        std::chrono::steady_clock::now()+std::chrono::seconds(2),{});
    CHECK(done&&done->state==host::ExecutionWaitState::Terminal);
    auto rejected=fixture.session->result<int>(*fixture.caller,child_ref);
    CHECK(rejected&&std::holds_alternative<Completed<int>>(*rejected->value));
    const auto& outcome=std::get<Completed<int>>(*rejected->value).outcome;
    CHECK(std::holds_alternative<CancelledBeforeApply>(outcome.value()));
    CHECK(outcome.conditions().before_apply->execution_accepted&&!outcome.conditions().before_apply->business_entered);
    saved_scope.reset();create_child={};
  }
}
}
#endif
