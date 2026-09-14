#pragma once
#include <ock/contracts/ports.hpp>
#include <chrono>
#include <functional>
namespace ock::contracts {
enum class ExecutorErrc : std::uint32_t {
  InvalidInput=1, Closed, Full, WorkerWait, Timeout, SubmissionException,
  WorkException, CallbackException
};
inline constexpr foundation::ErrorDomain executor_domain{"ock.executor"};
inline Error executor_error(ExecutorErrc code) noexcept {
  return Error{foundation::ErrorCode::make<executor_domain>(static_cast<std::uint32_t>(code))};
}
using ExecutorTime = std::chrono::steady_clock::time_point;
// Control owner 必须活过在途工作；超时不回收。不得从所属 worker 等待或关闭。
class ExecutorControlPort : public ExecutorPort {
public:
  virtual bool in_worker() const noexcept = 0;
  virtual Result<void> drain_until(ExecutorTime) = 0;
  virtual Result<void> shutdown_until(ExecutorTime) = 0;
};
struct WorkReport {
  std::atomic<unsigned> executions{0}, duplicates{0}, work_exceptions{0}, callback_exceptions{0};
};
// 已就绪的函数适配，不包含 Task 身份、业务注册或终态历史。
class CallbackWork final : public ReadyWork {
public:
  using Function = std::function<Result<void>()>;
  using Completion = std::function<void(Result<void>)>;
  CallbackWork(Function work, Completion done, std::shared_ptr<WorkReport> report)
      : work_(std::move(work)), done_(std::move(done)), report_(std::move(report)) {
    foundation::invariant(bool(work_) && bool(done_) && bool(report_));
  }
  void execute() noexcept override {
    if(claimed_.exchange(true)) { ++report_->duplicates; return; }
    ++report_->executions;
    Result<void> status;
    try { status=work_(); }
    catch(...) { ++report_->work_exceptions; status=make_unexpected(executor_error(ExecutorErrc::WorkException)); }
    try { done_(std::move(status)); }
    catch(...) { ++report_->callback_exceptions; }
  }
private:
  Function work_;
  Completion done_;
  std::shared_ptr<WorkReport> report_;
  std::atomic<bool> claimed_{false};
};
}
