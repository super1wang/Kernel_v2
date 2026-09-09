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
  CHECK((*service)->cancel(**caller,*session,ref));
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
  Env env(false,service_handler);env.threads->any_thread=true;env.threads->role=ThreadRole::Worker;
  auto factory=std::make_shared<Factory>();
  host::HostPorts ports{env.policy.auth,env.policy.clock,env.policy.digest,env.threads,{},factory};
  auto made=host::NativeHost::create({},policy_test::configuration(),ports);CHECK(made);
  auto& host=*made;CHECK(!factory->owner);CHECK(host->start());CHECK(factory->owner);
  CHECK(factory->owner->table()->host()==host->incarnation());
  auto session=host->open({{std::byte{7}}},{policy_test::rules(),env.policy.auth->identity.deadline,false});CHECK(session);
  auto caller=session->verify({policy_test::principal(),{}, {}});CHECK(caller);
  auto context=session->catalog_context();CHECK(context);
  auto bound=env.bind();CHECK(bound);
  executions::detail::InvocationRecord<int,int>::Policy storage{4,sizeof(InvokeReply<int>),
      [](const int&)->Result<std::size_t>{return 4;},
      [](const InvokeReply<int>&)->Result<std::size_t>{return sizeof(InvokeReply<int>);}};
  auto resolver=[](std::span<const registry::ResourceRef> refs)->Result<std::vector<resources::Claim>> {CHECK(refs.empty());return std::vector<resources::Claim>{};};
  service_host=host.get();
  auto reply=factory->owner->service().submit(*bound,4,env.options(),storage,resolver);
  CHECK(std::holds_alternative<Accepted>(reply));auto ref=std::get<Accepted>(reply).execution;
  const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(2);
  while(!service_entered.load()&&std::chrono::steady_clock::now()<deadline)std::this_thread::yield();
  CHECK(service_entered.load()&&service_host_reentrant.load());
  auto observed=context->authorization->observations()->get(**caller,ref,policy::AccessUse::GetSummary);CHECK(observed);
  std::array<host::PendingCleanup,8> pending;
  auto timeout=host->shutdown_until(std::chrono::steady_clock::now(),pending);
  CHECK(timeout.disposition==host::ShutdownDisposition::DeadlineExceeded&&!timeout.quiescent);
  CHECK(std::any_of(pending.begin(),pending.begin()+timeout.pending_written,[](const auto& p){return p.kind==host::PendingKind::Executions;}));
  CHECK(std::any_of(pending.begin(),pending.begin()+timeout.pending_written,[](const auto& p){return p.kind==host::PendingKind::Executors;}));
  CHECK(factory->owner->service().active()==1);
  service_release=true;
  auto drained=host->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2),pending);
  CHECK(drained.quiescent&&drained.pending_total==0);
  CHECK(factory->owner->service().scheduler_snapshot().worker_delivery==0);
  CHECK(factory->owner->table()->access_find(ref));
  CHECK(!context->authorization->observations()->get(**caller,ref,policy::AccessUse::GetSummary));
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
