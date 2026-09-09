#pragma once
#include <ock/contracts/executor.hpp>
namespace ock::runtime::scheduler {
using foundation::Result;
using Time = contracts::ExecutorTime;
using Ticket = std::uint64_t; // Runtime 内部调度标识；不是 ExecutionRef。
enum class Errc : std::uint32_t { InvalidInput=1, Full, Closed, UnknownTicket, DependencyFailed, ExpiredBeforeDispatch, ExecutorRejected, ExecutorViolation, WorkException, CancelledBeforeStart };
enum class Retirement { Retired, AlreadyStarted, AlreadyTerminal };
inline constexpr foundation::ErrorDomain domain{"ock.scheduler"};
inline foundation::Error error(Errc code) noexcept {return foundation::Error{foundation::ErrorCode::make<domain>(static_cast<std::uint32_t>(code))};}
struct Subject {
  contracts::PrincipalId principal;
  unsigned weight=1;
  std::size_t max_inflight=2;
};
struct Options {
  std::size_t global_queued=4096, subject_queued=256, max_inflight=16,
              history=10000, max_dependencies=64, control_slots=128, worker_delivery=16;
};
struct Request {
  contracts::PrincipalId principal;
  unsigned priority=0; // 0..7；主体/并发配额不可被优先级绕过。
  std::vector<Ticket> dependencies;
  Time deadline=Time::max();
  bool resource_ready=true;
  contracts::CallbackWork::Function work;
  contracts::CallbackWork::Completion completed;
  std::function<void()> detach_waiter; // 锁外摘除 resource waiter；先于 completed。
  // 依赖全部成功后至多一次锁外通知，可能早于 enqueue 返回。
  // 仅表示依赖满足；接收者仍须仲裁接受发布、取消及资源准入。
  std::function<void()> dependencies_ready;
};
struct Snapshot {
  std::size_t active=0, queued=0, inflight=0, history=0, controls=0, worker_delivery=0, dependency_edges=0;
  std::uint64_t dispatched=0, subject_inspections=0, executor_violations=0, callback_errors=0;
};
class Scheduler {
public:
  // 控制 owner 保留 Executor 至排空；这里只保存弱引用，worker 完成不会析构池。
  static Result<std::unique_ptr<Scheduler>> create(std::shared_ptr<contracts::ExecutorPort>,std::vector<Subject>,Options = {},std::function<void()> wake = {});
  ~Scheduler();
  Result<Ticket> enqueue(Request);
  Result<void> make_ready(Ticket);
  // 与 start claim 共用仲裁；AlreadyStarted 时调用者只能协作取消，不能释放运行期资源。
  Result<Retirement> retire(Ticket,foundation::Error reason=error(Errc::CancelledBeforeStart));
  Result<void> post_control(std::function<void()>);
  // 由拥有者的控制循环调用；只扫描活跃主体/到期期限，不扫描终态历史。
  std::size_t pump(Time now=std::chrono::steady_clock::now(),std::size_t limit=64);
  std::optional<Time> next_deadline() const;
  Snapshot snapshot() const;
  void close(); // 拒绝新工作、结束尚未投递项；不抢占已开始的 C++。
private:
  struct State;
  explicit Scheduler(std::shared_ptr<State> state):state_(std::move(state)){}
  std::shared_ptr<State> state_;
};
}
