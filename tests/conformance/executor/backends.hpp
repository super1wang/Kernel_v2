#pragma once
#include <ock/adapters/cpu_pool/cpu_pool.hpp>
#include <condition_variable>
#include <deque>
#include <mutex>
namespace executor_test {
using namespace ock::contracts;
struct Frame {
  const void *owner;
  Frame *previous;
  inline static thread_local Frame *current=nullptr;
  explicit Frame(const void *p):owner(p),previous(current){current=this;}
  ~Frame(){current=previous;}
  static bool contains(const void *p){for(auto *f=current;f;f=f->previous)if(f->owner==p)return true;return false;}
};
class TestExecutor final : public ExecutorControlPort {
public:
  explicit TestExecutor(bool inline_mode):inline_(inline_mode){}
  ~TestExecutor(){ock::foundation::invariant(!in_worker());(void)shutdown_until(ExecutorTime::max());}
  Result<void> submit(std::unique_ptr<ReadyWork> work) override {
    if(!work)return make_unexpected(executor_error(ExecutorErrc::InvalidInput));
    {
      std::lock_guard lock(mutex_);
      if(closed_)return make_unexpected(executor_error(ExecutorErrc::Closed));
      if(active_+queue_.size()>=16)return make_unexpected(executor_error(ExecutorErrc::Full));
      if(!inline_){queue_.push_back(std::move(work));return {};}
      ++active_;
    }
    run(std::move(work));return {};
  }
  bool in_worker() const noexcept override{return Frame::contains(this);}
  bool run_one() {
    std::unique_ptr<ReadyWork> work;
    {std::lock_guard lock(mutex_);if(queue_.empty() || active_)return false;work=std::move(queue_.front());queue_.pop_front();++active_;}
    run(std::move(work));return true;
  }
  Result<void> drain_until(ExecutorTime deadline) override {
    if(in_worker())return make_unexpected(executor_error(ExecutorErrc::WorkerWait));
    for(;;) {
      while(run_one()){}
      std::unique_lock lock(mutex_);
      if(active_==0 && queue_.empty())return {};
      if(!changed_.wait_until(lock,deadline,[&]{return active_==0;}))return make_unexpected(executor_error(ExecutorErrc::Timeout));
    }
  }
  Result<void> shutdown_until(ExecutorTime deadline) override {
    if(in_worker())return make_unexpected(executor_error(ExecutorErrc::WorkerWait));
    {std::lock_guard lock(mutex_);closed_=true;}
    return drain_until(deadline);
  }
private:
  void run(std::unique_ptr<ReadyWork> work) {
    Frame frame(this);work->execute();work.reset();
    {std::lock_guard lock(mutex_);--active_;}changed_.notify_all();
  }
  bool inline_,closed_=false;
  std::mutex mutex_;
  std::condition_variable changed_;
  std::size_t active_=0;
  std::deque<std::unique_ptr<ReadyWork>> queue_;
};
inline std::unique_ptr<ExecutorControlPort> create(std::string_view backend) {
  if(backend=="cpu_pool") {auto p=ock::cpu_pool::Executor::create();if(!p)throw std::runtime_error("pool create");return std::move(*p);}
  if(backend=="inline")return std::make_unique<TestExecutor>(true);
  if(backend=="controlled")return std::make_unique<TestExecutor>(false);
  throw std::runtime_error("unknown backend");
}
}
