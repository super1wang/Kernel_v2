#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <ock/runtime/scheduler.hpp>
#include <ock/runtime/resources.hpp>
#include <ock/foundation/sdk_version.hpp>
#include <iostream>
int main() {
  auto made=ock::cpu_pool::Executor::create();if(!made)return 1;
  std::shared_ptr<ock::contracts::ExecutorControlPort> pool=std::move(*made);
  auto resources=ock::runtime::resources::ResourceManager::create({{"cpu-budget",1}});if(!resources)return 2;
  std::vector<ock::runtime::resources::Claim> claims={{"cpu-budget"}};
  auto acquired=(*resources)->acquire(claims,ock::runtime::resources::Phase::Compute);if(!acquired)return 3;
  std::shared_ptr<ock::runtime::resources::ResourceManager::Lease> lease=std::move(acquired->lease);
  ock::contracts::PrincipalId p;p.bytes[0]=1;
  auto scheduler=ock::runtime::scheduler::Scheduler::create(pool,{{p}});if(!scheduler)return 4;
  std::atomic<unsigned> done=0;
  ock::runtime::scheduler::Request r;r.principal=p;r.work=[lease]{return ock::foundation::Result<void>{};};r.completed=[&](auto result){if(result)++done;};
  if(!(*scheduler)->enqueue(std::move(r)))return 5;lease.reset();(*scheduler)->pump();
  if(!pool->shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(5)))return 6;
  if(done!=1 || (*resources)->used("cpu-budget") || (*scheduler)->snapshot().worker_delivery)return 7;
  std::cout<<ock::sdk::version<<" B4Subset executor/scheduler/resources passed\n";
}
