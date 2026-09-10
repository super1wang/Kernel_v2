#include "fixtures.hpp"
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <ock/runtime/host.hpp>
namespace native_test {
namespace {
std::atomic<unsigned> session_entered=0;
std::atomic<bool> session_release=false;
std::shared_ptr<ExecutionScopePort> session_scope;
Result<int> session_work(const int& value,WorkContext& work) {
  if(value==0)session_scope=work.execution_scope();
  ++session_entered;
  auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
  if(value==0)while(!session_release&&std::chrono::steady_clock::now()<end)std::this_thread::yield();
  return value+2;
}
struct SessionThreads:TrustedThreadPort {
  std::thread::id application=std::this_thread::get_id();
  Result<ThreadObservation> current()const noexcept override {
    return ThreadObservation{std::this_thread::get_id()==application?ThreadRole::Application:ThreadRole::Worker,name("app"),true};
  }
};
struct SessionFactory:host::HostExecutionFactoryPort {
  Result<std::shared_ptr<host::HostExecutionPort>> create(HostIncarnation id)override {
    auto made=ock::cpu_pool::Executor::create({2,2});CHECK(made);
    std::shared_ptr<ExecutorControlPort> pool=std::move(*made);
    host::ExecutionOptions options;options.subjects={{policy_test::principal().principal_id,1,1}};
    auto result=host::make_executions(id,pool,options);
    if(!result)(void)pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));
    return result;
  }
};
struct SessionLifecycle:host::ModuleLifecyclePort {
  Result<void> start(const host::ModuleContext&)override{return {};}
  host::ModuleStopResult stop()override{return {true,{}};}
};
}
void host_cached_result_sessions() {
  session_entered=0;session_release=false;session_scope.reset();
  std::optional<registry::ModuleInput> registration;
  Env env(false,session_work,{},[&](registry::ModuleInput& module){
    module.register_operations=[](registry::Registrar& registrar){
      registry::SubmissionStorage<int,int> storage{sizeof(int),sizeof(InvokeReply<int>),
          [](const int&)->Result<std::size_t>{return sizeof(int);},
          [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
      CHECK(registrar.compute(session_work,{key(),{},{false,false,false,name("test"),name("app")},AtomicMode::PureCompute,{name("allow")},"session"},{{},{},{name("native"),name("test")},{},false},storage));
    };registration=module;
  });
  host::HostOptions limits;limits.policy.sessions=2;limits.policy.inline_bindings=2;
  auto made=host::NativeHost::create(limits,policy_test::configuration(),{env.policy.auth,env.policy.clock,env.policy.digest,std::make_shared<SessionThreads>(),{},std::make_shared<SessionFactory>()});CHECK(made);
  auto& host=*made;
  struct Close {host::NativeHost& host;~Close(){session_release=true;session_scope.reset();host.shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(3));}}close{*host};
  CHECK(host->add({*registration,std::make_shared<SessionLifecycle>()}));CHECK(host->start());
  auto open=[&]{return host->open({{std::byte{7}}},{policy_test::rules(),env.policy.auth->identity.deadline,false});};
  auto observer=open();CHECK(observer);auto who=observer->verify({policy_test::principal(),{},{}});CHECK(who);
  auto options=[&]{return invocation::InvokeOptions{{},std::chrono::steady_clock::now()+std::chrono::seconds(2),100};};
  auto accept=[](const SubmitReply& reply){CHECK(std::holds_alternative<Accepted>(reply));return std::get<Accepted>(reply).execution;};
  std::vector<std::shared_ptr<const InvokeReply<int>>> pinned;
  auto retain=[&](ExecutionRef ref,int expected){
    auto waited=observer->wait(**who,ref,std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(waited&&waited->state==host::ExecutionWaitState::Terminal);
    auto value=observer->result<int>(**who,ref);CHECK(value&&result(*value->value)==expected);pinned.push_back(value->value);
  };
  ExecutionRef first,second;
  {
    auto submitter=open();CHECK(submitter);auto caller=submitter->verify({policy_test::principal(),{},{}});CHECK(caller);
    auto a=submitter->bind<int,int>(key(),{},Shape::Read,*caller,std::array{policy_test::target()},targets,name("session.a"));CHECK(a);
    auto b=submitter->bind<int,int>(key(),{},Shape::Read,*caller,std::array{policy_test::target()},targets,name("session.b"));CHECK(b);
    first=accept(a->submit(0,options()));
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(session_entered!=1&&std::chrono::steady_clock::now()<end)std::this_thread::yield();CHECK(session_entered==1);
    second=accept(b->submit(1,options()));CHECK(session_entered==1);
  } // 丢弃连接侧 wrapper/绑定；队列中的调用仍拥有有限期限授权。
  session_release=true;retain(first,2);retain(second,3);
  for(int i=2;i<18;++i) {
    ExecutionRef ref;
    {
      auto submitter=open();CHECK(submitter);auto caller=submitter->verify({policy_test::principal(),{},{}});CHECK(caller);
      auto bound=submitter->bind<int,int>(key(),{},Shape::Read,*caller,std::array{policy_test::target()},targets,name("session.next"));CHECK(bound);
      auto budget=foundation::CheckedCount<std::uint64_t>::create(0,100);CHECK(budget);
      WorkContext stale({},options().deadline,*budget,name("stale"),BorrowedResourceViews{{}},session_scope);
      CHECK(std::holds_alternative<Rejected>(bound->submit_child(stale,i,options())));
      ref=accept(bound->submit(i,options()));
    }
    retain(ref,i+2);
  }
  CHECK(pinned.size()==18);for(unsigned i=0;i<pinned.size();++i)CHECK(result(*pinned[i])==static_cast<int>(i)+2);
  // 显式 close 与释放 wrapper 不同：关闭后的排队调用仍须被当前授权拒绝。
  session_entered=0;session_release=false;
  {
    auto submitter=open();CHECK(submitter);auto caller=submitter->verify({policy_test::principal(),{},{}});CHECK(caller);
    auto a=submitter->bind<int,int>(key(),{},Shape::Read,*caller,std::array{policy_test::target()},targets,name("close.a"));CHECK(a);
    auto b=submitter->bind<int,int>(key(),{},Shape::Read,*caller,std::array{policy_test::target()},targets,name("close.b"));CHECK(b);
    first=accept(a->submit(0,options()));
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(2);
    while(session_entered!=1&&std::chrono::steady_clock::now()<end)std::this_thread::yield();CHECK(session_entered==1);
    second=accept(b->submit(1,options()));CHECK(submitter->close());
  }
  session_release=true;retain(first,2);
  auto done=observer->wait(**who,second,std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(done&&done->state==host::ExecutionWaitState::Terminal);
  auto refused=observer->result<int>(**who,second);CHECK(refused&&std::holds_alternative<Completed<int>>(*refused->value));
  CHECK(session_entered==1);
}
}
#endif
