#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <BS_thread_pool.hpp>
#include <condition_variable>
#include <mutex>
namespace ock::cpu_pool {
using namespace contracts;
struct Executor::State {
  std::mutex mutex;
  std::timed_mutex control;
  std::condition_variable changed;
  std::size_t inflight=0, limit;
  bool closing=false;
  std::unique_ptr<BS::wdc_thread_pool> pool;
  BS::wdc_thread_pool* const identity;
  std::atomic<bool> workers{true};
  explicit State(Options options):limit(options.max_inflight),pool(std::make_unique<BS::wdc_thread_pool>(options.workers)),identity(pool.get()){}
  // control 持有者独占 pool 寿命；submit 由 closing 与 mutex 仲裁。
  Result<void> drain(ExecutorTime deadline) {
    if(!pool)return {};
    std::unique_lock lock(mutex);
    if(!changed.wait_until(lock,deadline,[&]{return inflight==0;}))
      return make_unexpected(executor_error(ExecutorErrc::Timeout));
    lock.unlock();
    if(!pool->wait_until(deadline))return make_unexpected(executor_error(ExecutorErrc::Timeout));
    return {};
  }
};
Executor::Executor(std::unique_ptr<State> state):state_(std::move(state)){}
foundation::Result<std::unique_ptr<Executor>> Executor::create(Options options) {
  if(!options.workers || options.workers>64 || !options.max_inflight || options.max_inflight>4096)
    return make_unexpected(executor_error(ExecutorErrc::InvalidInput));
  try { return std::unique_ptr<Executor>(new Executor(std::make_unique<State>(options))); }
  catch(...) { return make_unexpected(executor_error(ExecutorErrc::SubmissionException)); }
}
Executor::~Executor() {
  foundation::invariant(!in_worker());
  { std::lock_guard lock(state_->mutex); state_->closing=true; }
  if(state_->pool)state_->pool->wait(); // 最终 owner 在控制线程排空；绝不 detach。
}
bool Executor::in_worker() const noexcept { return state_->workers.load()&&BS::this_thread::get_pool()==state_->identity; }
Result<void> Executor::submit(std::unique_ptr<ReadyWork> work) {
  if(!work) return make_unexpected(executor_error(ExecutorErrc::InvalidInput));
  std::shared_ptr<std::unique_ptr<ReadyWork>> owned;
  try { owned=std::make_shared<std::unique_ptr<ReadyWork>>(std::move(work)); }
  catch(...) { return make_unexpected(executor_error(ExecutorErrc::SubmissionException)); }
  std::unique_lock lock(state_->mutex);
  if(state_->closing) return make_unexpected(executor_error(ExecutorErrc::Closed));
  if(state_->inflight==state_->limit) return make_unexpected(executor_error(ExecutorErrc::Full));
  ++state_->inflight;
  try {
    // BS C++20 的任务容器需要可复制 callable；只有此共享句柄拥有工作。
    auto *state=state_.get();
    state_->pool->detach_task([state,owned]() mutable {
      auto work=std::move(*owned);
      work->execute();
      { std::lock_guard lock(state->mutex); --state->inflight; }
      // 先归还适配器容量，再析构 work 触发上游投递槽唤醒；BS wait 仍覆盖析构。
      work.reset();
      state->changed.notify_all();
    });
  } catch(...) {
    --state_->inflight;
    lock.unlock(); state_->changed.notify_all();
    return make_unexpected(executor_error(ExecutorErrc::SubmissionException));
  }
  return {};
}
Result<void> Executor::drain_until(ExecutorTime deadline) {
  if(in_worker()) return make_unexpected(executor_error(ExecutorErrc::WorkerWait));
  std::unique_lock lock(state_->control,std::defer_lock);
  if(!lock.try_lock_until(deadline))
    return make_unexpected(executor_error(ExecutorErrc::Timeout));
  return state_->drain(deadline);
}
Result<void> Executor::shutdown_until(ExecutorTime deadline) {
  if(in_worker()) return make_unexpected(executor_error(ExecutorErrc::WorkerWait));
  { std::lock_guard lock(state_->mutex); state_->closing=true; }
  std::unique_lock lock(state_->control,std::defer_lock);
  if(!lock.try_lock_until(deadline))return make_unexpected(executor_error(ExecutorErrc::Timeout));
  auto drained=state_->drain(deadline);if(!drained)return drained;
  // 工作和 capture 析构均已退出；BS 析构结束空闲 workers 并 join。
  // 不能等最后一个外部 Executor owner 释放才结束内核线程。
  state_->workers=false;
  state_->pool.reset();
  return {};
}
}
