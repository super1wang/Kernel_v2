#pragma once
#include <ock/runtime/resources.hpp>
#include <ock/runtime/scheduler.hpp>
#include <mutex>
#include <stop_token>

namespace ock::runtime::executions::detail {
// Runtime Execution 独占的资源准入协议；wake 仅调度有限控制步，不能直接递归 drive。
class ResourceWaitBinding final : public std::enable_shared_from_this<ResourceWaitBinding> {
public:
  using Acquire=std::function<foundation::Result<resources::ResourceManager::Acquisition>(
      std::span<const resources::Claim>,std::function<void(std::uint64_t)>)>;
  enum class Phase {WaitingDependencies, Installing, Waiting, Retrying, Ready, Terminal};
  struct Snapshot {Phase phase; bool accepted, pending, driving, running; std::uint64_t attempt, waiter_generation;};
  ResourceWaitBinding(std::shared_ptr<scheduler::Scheduler>,std::shared_ptr<resources::ResourceManager>,
                      std::vector<resources::Claim>,std::function<void()> wake,Acquire acquire = {});
  void dependencies_ready();
  // 仅在 Execution 的 Volatile Accepted 内存发布之后调用。允许早到 terminal。
  void publish(scheduler::Ticket);
  void drive(); // 一次最多一个 acquire；可靠 pending 位由 Execution 控制 owner 消费。
  void cancel();
  // 仅由 Scheduler 的 completed（或 enqueue 拒绝）调用，不能由普通运行期 cancel 调用。
  void terminal();
  // 仅从已赢得 Scheduler start claim 的 work 内调用；释放权唯一转移给实际业务栈。
  foundation::Result<std::unique_ptr<resources::ResourceManager::Lease>> start_lease();
  std::stop_token stop_token() const noexcept {return stop_.get_token();}
  Snapshot snapshot() const;
private:
  void woke(std::uint64_t attempt,std::uint64_t waiter);
  void signal() noexcept;
  mutable std::mutex mutex_;
  std::shared_ptr<scheduler::Scheduler> scheduler_;
  std::shared_ptr<resources::ResourceManager> resources_;
  const std::vector<resources::Claim> claims_;
  const std::function<void()> wake_;
  const Acquire acquire_; // 可选受控调用边界；生产默认直接使用唯一 ResourceManager。
  std::stop_source stop_;
  scheduler::Ticket ticket_=0;
  Phase phase_=Phase::WaitingDependencies;
  bool accepted_=false,dependencies_=false,pending_=false,driving_=false,running_=false;
  std::uint64_t attempt_=0,waiter_generation_=0,wake_generation_=0;
  std::unique_ptr<resources::ResourceManager::Lease> lease_;
  std::unique_ptr<resources::ResourceManager::Waiter> waiter_;
};
}
