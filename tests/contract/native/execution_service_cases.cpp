#include "fixtures.hpp"
#include "packages/runtime/executions/execution_service.hpp"
#include "packages/runtime/executions/host_execution.hpp"
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
namespace native_test {
inline std::atomic<bool> service_entered=false,service_release=false;
inline host::NativeHost* service_host=nullptr;
inline std::atomic<bool> service_host_reentrant=false;
inline Result<int> service_handler(const int& value,WorkContext&) {
  if(service_host)service_host_reentrant=service_host->shutdown_until(std::chrono::steady_clock::now()).disposition==host::ShutdownDisposition::Reentrant;
  service_entered.store(true);
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
  while(!service_release.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  return value+2;
}
void execution_service_control() {
  using Service=executions::detail::ExecutionService;
  using Table=executions::detail::ExecutionTable;
  using Record=executions::detail::InvocationRecord<int,int>;
  service_entered=false;service_release=false;
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
  auto reply=(*service)->submit(*bound,4,env.options(),storage,resolver);CHECK(std::holds_alternative<Accepted>(reply));
  auto ref=std::get<Accepted>(reply).execution;CHECK(!ref.execution_id.empty());
  const auto start_deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!service_entered.load()&&std::chrono::steady_clock::now()<start_deadline)std::this_thread::yield();
  CHECK(service_entered.load());CHECK((*service)->active()==1);
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
      auto resources=resources::ResourceManager::create({{"unused",1,false}});CHECK(resources);
      std::shared_ptr<resources::ResourceManager> manager=std::move(*resources);
      auto made=Hosted::create(id,executor,manager,{{policy_test::principal().principal_id}});
      if(!made){CHECK(executor->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)));return make_unexpected(made.error());}
      owner=*made;return std::shared_ptr<host::HostExecutionPort>(owner);
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
  CHECK(host->add({*registration,std::make_shared<Lifecycle>()}));
  CHECK(host->start());CHECK(factory->owner);
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
  CHECK(factory->owner->service().scheduler_snapshot().worker_delivery==0);
  CHECK(factory->owner->table()->access_find(ref));
  CHECK(!context->authorization->observations()->get(**caller,ref,policy::AccessUse::GetSummary));
  CHECK(!session->result<int>(**caller,completed_ref));
  CHECK(!session->cancel(**caller,completed_ref));
  CHECK(!session->wait(**caller,completed_ref,std::chrono::steady_clock::now()));
  CHECK(result(*value->value)==9); // 已授权取得的结果 owner 在 Host 关闭后仍保活。
  service_host=nullptr;
  // 工厂已交付真实线程后发现源世代错误：失败路径也履行排空责任。
  auto foreign=std::make_shared<Factory>();foreign->foreign=true;ports.execution_factory=foreign;
  auto invalid=host::NativeHost::create({},policy_test::configuration(),ports);CHECK(invalid);
  CHECK(!(*invalid)->start());CHECK(foreign->owner);
  CHECK((*invalid)->snapshot({}).quiescent);
  CHECK(foreign->owner->service().scheduler_snapshot().worker_delivery==0);
}
}
#endif
