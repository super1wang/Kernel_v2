#include "fixtures.hpp"
#include "packages/runtime/executions/execution_service.hpp"
#include "packages/runtime/executions/host_execution.hpp"
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
namespace native_test {
inline std::atomic<bool> service_entered=false,service_release=false;
inline std::atomic<bool> service_stopped=false;
inline host::NativeHost* service_host=nullptr;
inline std::atomic<bool> service_host_reentrant=false;
inline Result<int> service_handler(const int& value,WorkContext& work) {
  if(service_host)service_host_reentrant=service_host->shutdown_until(std::chrono::steady_clock::now()).disposition==host::ShutdownDisposition::Reentrant;
  service_entered.store(true);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!service_release.load()&&std::chrono::steady_clock::now()<deadline) {
    if(work.stop_requested())service_stopped=true;
    std::this_thread::yield();
  }
  return value+2;
}
void execution_service_control() {
  using Service=executions::detail::ExecutionService;
  using Table=executions::detail::ExecutionTable;
  using Record=executions::detail::InvocationRecord<int,int>;
  service_entered=false;service_release=false;service_stopped=false;
  Env env(false,service_handler);env.threads->any_thread=true;env.threads->role=ThreadRole::Worker;
  auto bound=env.bind();CHECK(bound);
  auto made_pool=ock::cpu_pool::Executor::create({2,2});CHECK(made_pool);
  std::shared_ptr<ock::cpu_pool::Executor> pool=std::move(*made_pool);
  HostIncarnation host;host.bytes[0]=101;auto table=Table::create({},host);CHECK(table);
  auto made_resources=resources::ResourceManager::create({{"unused",1,false}});CHECK(made_resources);
  std::shared_ptr<resources::ResourceManager> manager=std::move(*made_resources);
  auto service=Service::create(*table,pool,manager,{{env.policy.caller->view().description().principal.principal_id}}, {}, {1,1});CHECK(service);
  Record::Policy storage{4,sizeof(InvokeReply<int>),
      [](const int&)->Result<std::size_t>{return 4;},
      [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
  auto resolver=[](std::span<const registry::ResourceRef> refs)->Result<std::vector<resources::Claim>> {CHECK(refs.empty());return std::vector<resources::Claim>{};};
  auto options=env.options();options.deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(200);
  auto reply=(*service)->submit(*bound,4,options,storage,resolver);CHECK(std::holds_alternative<Accepted>(reply));
  auto ref=std::get<Accepted>(reply).execution;CHECK(!ref.execution_id.empty());
  const auto start_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!service_entered.load()&&std::chrono::steady_clock::now()<start_deadline)std::this_thread::yield();
  CHECK(service_entered.load());CHECK((*service)->active()==1);
  while(!service_stopped.load()&&std::chrono::steady_clock::now()<start_deadline)std::this_thread::yield();
  CHECK(service_stopped.load());CHECK((*service)->active()==1);
  auto second_bound=env.bind();CHECK(second_bound); // 独立调用槽，明确验证服务容量拒绝。
  CHECK(std::holds_alternative<Rejected>((*service)->submit(*second_bound,5,env.options(),storage,resolver)));
  auto source=std::make_shared<executions::detail::ExecutionSource>(*table);
  auto assembly=policy::PolicyStore::create({},policy_test::configuration(),env.policy.auth,env.policy.clock,env.policy.digest,source);CHECK(assembly);
  auto session=assembly->store->open({{std::byte{7}}},{policy_test::rules(),env.policy.auth->identity.deadline,false});CHECK(session);
  auto caller=(*session)->verify({policy_test::principal(),{}, {}});CHECK(caller);
  auto cancellation=(*service)->cancel(**caller,*session,ref);CHECK(cancellation&&*cancellation==CancelDisposition::AlreadyClaimed);
  // 开始后的取消是协作意图；尚未退出的业务使 shutdown 超时并保持 owner。
  auto timeout=(*service)->shutdown_until(std::chrono::steady_clock::now());CHECK(timeout&&!*timeout);
  CHECK((*service)->active()==1);
  service_release=true;
  auto drained=(*service)->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(drained&&*drained);
  auto summary=(*table)->access_find(ref);CHECK(summary&&summary->summary->value().phase==ExecutionPhase::Terminal);
  CHECK((*service)->active()==0&&(*service)->scheduler_snapshot().worker_delivery==0);
  CHECK(std::holds_alternative<Rejected>((*service)->submit(*bound,6,env.options(),storage,resolver)));
  CHECK(pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)));
}
void host_execution_lifecycle() {
  using Hosted=executions::detail::HostedExecutions;
  struct Factory final : host::HostExecutionFactoryPort {
    std::shared_ptr<Hosted> owner;
    bool foreign=false;
    Result<std::shared_ptr<host::HostExecutionPort>> create(HostIncarnation id) override {
      if(foreign)id.bytes[0]^=0x80;
      auto pool=ock::cpu_pool::Executor::create({2,2});CHECK(pool);
      std::shared_ptr<ExecutorControlPort> executor=std::move(*pool);
      host::ExecutionOptions options;options.subjects={{policy_test::principal().principal_id}};options.limits.waiters=1;
      auto made=host::make_executions(id,executor,options);
      if(!made){CHECK(executor->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)));return make_unexpected(made.error());}
      owner=std::dynamic_pointer_cast<Hosted>(*made);CHECK(owner);return *made;
    }
  };
  service_entered=false;service_release=false;service_host_reentrant=false;
  registry::SubmissionStorage<int,int> storage{4,sizeof(InvokeReply<int>),
      [](const int&)->Result<std::size_t>{return 4;},
      [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
  std::optional<registry::ModuleInput> registration;
  Env env(false,service_handler,{},[&](registry::ModuleInput& module) {
    module.register_operations=[storage](registry::Registrar& registrar) {
      DefinitionInput d{key(), {}, {true,false,false,name("test"),name("app")},
          AtomicMode::PureCompute,{name("allow")},"native"};
      registry::OperationOptions o{{},{},{name("native"),name("test")},{},false};
      CHECK(registrar.compute(service_handler,d,o,storage));
    };
    registration=module;
  });
  env.threads->any_thread=true;env.threads->role=ThreadRole::Worker;
  struct Lifecycle final : host::ModuleLifecyclePort {
    Result<void> start(const host::ModuleContext&) override {return {};}
    host::ModuleStopResult stop() override {return {true,{}};}
  };
  struct HostThreads final : TrustedThreadPort {
    std::thread::id application=std::this_thread::get_id();
    ThreadRole main_role=ThreadRole::Worker;
    Result<ThreadObservation> current() const noexcept override {
      return ThreadObservation{std::this_thread::get_id()==application?main_role:ThreadRole::Worker,name("app"),true};
    }
  };
  auto host_threads=std::make_shared<HostThreads>();
  auto factory=std::make_shared<Factory>();
  host::HostPorts ports{env.policy.auth,env.policy.clock,env.policy.digest,host_threads,{},factory};
  auto made=host::NativeHost::create({},policy_test::configuration(),ports);CHECK(made);
  auto& host=*made;CHECK(!factory->owner);
  CHECK(!host->capabilities().async_execution&&!host->capabilities().execution_observation);
  CHECK(host->add({*registration,std::make_shared<Lifecycle>()}));
  CHECK(host->start());CHECK(factory->owner);
  CHECK(host->capabilities().async_execution&&host->capabilities().execution_observation);
  CHECK(factory->owner->table()->host()==host->incarnation());
  auto session=host->open({{std::byte{7}}},{policy_test::rules(),env.policy.auth->identity.deadline,false});CHECK(session);
  auto caller=session->verify({policy_test::principal(),{}, {}});CHECK(caller);
  auto context=session->catalog_context();CHECK(context);
  auto bound=session->bind<int,int>(key(),{},Shape::Read,*caller,
      std::array{policy_test::target()},targets,name("host.submit"));CHECK(bound);
  // 公开绑定提交、等待和结果均使用同一 Host 会话及身份。
  service_release=true;
  auto completed=bound->submit(7,env.options());CHECK(std::holds_alternative<Accepted>(completed));
  auto completed_ref=std::get<Accepted>(completed).execution;
  CHECK(!session->wait(**caller,completed_ref,std::chrono::steady_clock::now()+std::chrono::seconds(2)));
  host_threads->main_role=ThreadRole::Application;
  auto waited=session->wait(**caller,completed_ref,std::chrono::steady_clock::now()+std::chrono::seconds(2));
  CHECK(waited&&waited->state==host::ExecutionWaitState::Terminal);
  host_threads->main_role=ThreadRole::Control;
  auto polling=session->prepare_wait(*caller,completed_ref,std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(polling);
  auto polled=(*polling)->poll();CHECK(polled&&*polled&&(**polled).state==host::ExecutionWaitState::Terminal);
  CHECK((**polled).observed.response);CHECK(!(*polling)->poll());
  CHECK(factory->owner->table()->usage().waiters==0);
  auto value=session->result<int>(**caller,completed_ref);CHECK(value&&result(*value->value)==9);
  CHECK(!session->result<void>(**caller,completed_ref));
  auto terminal_cancel=session->cancel(**caller,completed_ref);CHECK(terminal_cancel&&*terminal_cancel==CancelDisposition::AlreadyTerminal);
  service_entered=false;service_release=false;host_threads->main_role=ThreadRole::Worker;
  service_host=host.get();
  auto reply=bound->submit(4,env.options());
  CHECK(std::holds_alternative<Accepted>(reply));auto ref=std::get<Accepted>(reply).execution;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!service_entered.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  CHECK(service_entered.load()&&service_host_reentrant.load());
  auto observed=context->authorization->observations()->get(**caller,ref,policy::AccessUse::GetSummary);CHECK(observed);
  host_threads->main_role=ThreadRole::Control;
  polling=session->prepare_wait(*caller,ref,std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(polling);
  polled=(*polling)->poll();CHECK(polled&&!*polled);
  CHECK(factory->owner->table()->usage().waiters==1);
  CHECK(!session->prepare_wait(*caller,ref,std::chrono::steady_clock::now()));
  host_threads->main_role=ThreadRole::Application;
  CHECK(!session->wait(**caller,ref,std::chrono::steady_clock::now()));
  polling->reset();CHECK(factory->owner->table()->usage().waiters==0);
  polling=session->prepare_wait(*caller,ref,std::chrono::steady_clock::now());CHECK(polling);
  polled=(*polling)->poll();CHECK(polled&&*polled&&(**polled).state==host::ExecutionWaitState::Timeout);
  std::stop_source polling_stop;
  polling=session->prepare_wait(*caller,ref,std::chrono::steady_clock::now()+std::chrono::seconds(2),polling_stop.get_token());CHECK(polling);
  polling_stop.request_stop();polled=(*polling)->poll();
  CHECK(polled&&*polled&&(**polled).state==host::ExecutionWaitState::Cancelled);
  CHECK(factory->owner->table()->usage().waiters==0);
  auto observer_session=host->open({{std::byte{7}}},{policy_test::rules(),env.policy.auth->identity.deadline,false});CHECK(observer_session);
  auto observer_caller=observer_session->verify({policy_test::principal(),{}, {}});CHECK(observer_caller);
  polling=observer_session->prepare_wait(*observer_caller,ref,std::chrono::steady_clock::now()+std::chrono::seconds(2));CHECK(polling);
  CHECK(observer_session->close());CHECK(!(*polling)->poll());
  CHECK(factory->owner->table()->usage().waiters==0);
  host_threads->main_role=ThreadRole::Application;
  auto wait_timeout=session->wait(**caller,ref,std::chrono::steady_clock::now());
  CHECK(wait_timeout&&wait_timeout->state==host::ExecutionWaitState::Timeout);
  std::stop_source stop_wait;stop_wait.request_stop();
  auto wait_cancel=session->wait(**caller,ref,std::chrono::steady_clock::now()+std::chrono::seconds(2),stop_wait.get_token());
  CHECK(wait_cancel&&wait_cancel->state==host::ExecutionWaitState::Cancelled);
  CHECK(factory->owner->service().active()>=1); // 等待退出没有取消/终止正在运行的业务。
  auto running_cancel=session->cancel(**caller,ref);CHECK(running_cancel&&*running_cancel==CancelDisposition::AlreadyClaimed);
  std::array<host::PendingCleanup,8> pending;
  auto timeout=host->shutdown_until(std::chrono::steady_clock::now(),pending);
  CHECK(timeout.disposition==host::ShutdownDisposition::DeadlineExceeded&&!timeout.quiescent);
  CHECK(std::any_of(pending.begin(),pending.begin()+timeout.pending_written,[](const auto& p){return p.kind==host::PendingKind::Executions;}));
  CHECK(std::any_of(pending.begin(),pending.begin()+timeout.pending_written,[](const auto& p){return p.kind==host::PendingKind::Executors;}));
  CHECK(factory->owner->service().active()>=1);
  service_release=true;
  auto drained=host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2),pending);
  CHECK(drained.quiescent&&drained.pending_total==0);
  CHECK(host->capabilities().async_execution&&host->capabilities().execution_observation);
  CHECK(factory->owner->service().scheduler_snapshot().worker_delivery==0);
  CHECK(factory->owner->table()->access_find(ref));
  CHECK(!context->authorization->observations()->get(**caller,ref,policy::AccessUse::GetSummary));
  CHECK(!session->result<int>(**caller,completed_ref));
  CHECK(!session->cancel(**caller,completed_ref));
  CHECK(!session->wait(**caller,completed_ref,std::chrono::steady_clock::now()));
  CHECK(!session->prepare_wait(*caller,completed_ref,std::chrono::steady_clock::now()));
  CHECK(result(*value->value)==9); // 已授权取得的结果 owner 在 Host 关闭后仍保活。
  service_host=nullptr;
  // 工厂已交付真实线程后发现源世代错误：失败路径也履行排空责任。
  auto foreign=std::make_shared<Factory>();foreign->foreign=true;ports.execution_factory=foreign;
  auto invalid=host::NativeHost::create({},policy_test::configuration(),ports);CHECK(invalid);
  CHECK(!(*invalid)->start());CHECK(foreign->owner);
  CHECK(!(*invalid)->capabilities().async_execution&&!(*invalid)->capabilities().execution_observation);
  CHECK((*invalid)->snapshot({}).quiescent);
  CHECK(foreign->owner->service().scheduler_snapshot().worker_delivery==0);
}

inline std::atomic<unsigned> hosted_resource_calls=0;
inline Result<int> hosted_resource_handler(const int& value,WorkContext& context) {
  CHECK(context.granted_resources().size()==1&&context.granted_resources()[0]);
  ++hosted_resource_calls;
  return service_handler(value,context);
}
void host_execution_resources() {
  struct Factory final : host::HostExecutionFactoryPort {
    host::ExecutionOptions options;
    Result<std::shared_ptr<host::HostExecutionPort>> create(HostIncarnation id) override {
      auto made_pool=ock::cpu_pool::Executor::create({2,2});CHECK(made_pool);
      std::shared_ptr<ExecutorControlPort> pool=std::move(*made_pool);
      auto made=host::make_executions(id,pool,options);
      if(!made)CHECK(pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)));
      return made;
    }
  };
  struct Lifecycle final : host::ModuleLifecyclePort {
    Result<void> start(const host::ModuleContext&) override {return {};}
    host::ModuleStopResult stop() override {return {true,{}};}
  };
  struct HostThreads final : TrustedThreadPort {
    std::thread::id application=std::this_thread::get_id();
    Result<ThreadObservation> current() const noexcept override {
      return ThreadObservation{std::this_thread::get_id()==application?ThreadRole::Application:ThreadRole::Worker,name("app"),true};
    }
  };
  service_host=nullptr;service_entered=false;service_release=false;hosted_resource_calls=0;
  std::optional<registry::ModuleInput> registration;
  Env env(false,hosted_resource_handler,{},[&](registry::ModuleInput& module) {
    module.manifest.resources.push_back(name("declared"));
    module.manifest.required_resources.push_back({name("native"),name("declared")});
    module.resources.push_back({name("declared"),std::make_shared<ResourceLease>()});
    module.register_operations=[](registry::Registrar& registrar) {
      DefinitionInput d{key(), {}, {false,false,false,name("test"),name("app")},
          AtomicMode::PureCompute,{name("allow")},"host.resource"};
      registry::OperationOptions o{{},{},{name("native"),name("test")},{{name("native"),name("declared")}},false};
      registry::SubmissionStorage<int,int> storage{4,sizeof(InvokeReply<int>),
          [](const int&)->Result<std::size_t>{return 4;},
          [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
      CHECK(registrar.compute(hosted_resource_handler,d,o,storage));
    };
    registration=module;
  });
  auto factory=std::make_shared<Factory>();
  factory->options.subjects={{policy_test::principal().principal_id}};
  factory->options.slots={{"shared.slot",1,false}};
  factory->options.resources={{{name("native"),name("declared")},{{"shared.slot",resources::Mode::Exclusive,1}}}};
  host::HostPorts ports{env.policy.auth,env.policy.clock,env.policy.digest,std::make_shared<HostThreads>(),{},factory};
  auto made=host::NativeHost::create({},policy_test::configuration(),ports);CHECK(made);auto& host=*made;
  CHECK(host->add({*registration,std::make_shared<Lifecycle>()}));CHECK(host->start());
  factory->options.resources.clear(); // 接受后的可信映射独立拥有，不再借用配置容器。
  auto session=host->open({{std::byte{7}}},{policy_test::rules(),env.policy.auth->identity.deadline,false});CHECK(session);
  auto caller=session->verify({policy_test::principal(),{}, {}});CHECK(caller);
  auto bind=[&]{return session->bind<int,int>(key(),{},Shape::Read,*caller,
      std::array{policy_test::target()},targets,name("host.resource"));};
  auto first=bind(),second=bind();CHECK(first&&second);
  CHECK(std::holds_alternative<Rejected>(first->invoke(1,env.options())));
  auto one=first->submit(1,env.options());CHECK(std::holds_alternative<Accepted>(one));
  auto one_ref=std::get<Accepted>(one).execution;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!service_entered.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  CHECK(service_entered&&hosted_resource_calls==1);
  auto two=second->submit(2,env.options());CHECK(std::holds_alternative<Accepted>(two));
  auto two_ref=std::get<Accepted>(two).execution;
  auto context=session->catalog_context();CHECK(context);
  bool waiting=false;
  while(std::chrono::steady_clock::now()<deadline) {
    auto observed=context->authorization->observations()->get(**caller,two_ref,policy::AccessUse::GetSummary);CHECK(observed);
    if(observed->summary->value().phase==ExecutionPhase::WaitingResources){waiting=true;break;}
    std::this_thread::yield();
  }
  CHECK(waiting&&hosted_resource_calls==1);
  auto cancelled=session->cancel(**caller,two_ref);CHECK(cancelled&&*cancelled==CancelDisposition::Requested);
  auto timeout=host->shutdown_until(std::chrono::steady_clock::now());CHECK(!timeout.quiescent);
  service_release=true;
  CHECK(host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)).quiescent);
  CHECK(hosted_resource_calls==1);
  factory->options.resources={{{name("native"),name("declared")},{{"missing.slot",resources::Mode::Exclusive,1}}}};
  CHECK(!factory->create(host->incarnation())); // 启动前拒绝未知映射，工厂排空临时 pool。
  factory->options.resources[0].claims[0].key="shared.slot";
  factory->options.resources.push_back(factory->options.resources.front());
  CHECK(!factory->create(host->incarnation())); // 同一声明不能有两份可漂移的 claims。
}
}
#endif
