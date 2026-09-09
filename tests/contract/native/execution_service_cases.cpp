#include "fixtures.hpp"
#include "packages/runtime/executions/execution_service.hpp"
#ifdef OCK_NATIVE_EXECUTION_SERVICE_TESTS
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
namespace native_test {
inline std::atomic<bool> service_entered=false,service_release=false;
inline Result<int> service_handler(const int& value,WorkContext&) {
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
}
#endif
