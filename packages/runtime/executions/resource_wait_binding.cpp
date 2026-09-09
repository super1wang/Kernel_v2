#include "resource_wait_binding.hpp"
#include <limits>
namespace ock::runtime::executions::detail {
using foundation::make_unexpected;
ResourceWaitBinding::ResourceWaitBinding(std::shared_ptr<scheduler::Scheduler> scheduler,
    std::shared_ptr<resources::ResourceManager> resources,std::vector<resources::Claim> claims,std::function<void()> wake,Acquire acquire)
    :scheduler_(std::move(scheduler)),resources_(std::move(resources)),claims_(std::move(claims)),wake_(std::move(wake)),acquire_(std::move(acquire)){}
void ResourceWaitBinding::signal() noexcept {try{if(wake_)wake_();}catch(...){/* 控制 owner 的 pending 仍保留，不丢完成事实。 */}}
void ResourceWaitBinding::dependencies_ready() {
  auto owner=shared_from_this();bool notify=false;
  {std::lock_guard lock(mutex_);if(phase_==Phase::Terminal)return;dependencies_=true;
    if(accepted_){pending_=true;notify=true;}}
  if(notify)signal();
}
void ResourceWaitBinding::publish(scheduler::Ticket ticket) {
  auto owner=shared_from_this();bool notify=false;
  {std::lock_guard lock(mutex_);foundation::invariant(ticket && !ticket_);ticket_=ticket;accepted_=true;
    if(phase_!=Phase::Terminal && dependencies_){pending_=true;notify=true;}}
  if(stop_.stop_requested())cancel();
  if(notify)signal();
}
void ResourceWaitBinding::woke(std::uint64_t attempt,std::uint64_t waiter) {
  bool notify=false;
  {std::lock_guard lock(mutex_);
    if(attempt!=attempt_ || phase_==Phase::Terminal)return;
    if(phase_==Phase::Installing){wake_generation_=waiter;return;}
    if(phase_!=Phase::Waiting || waiter!=waiter_generation_)return;
    wake_generation_=waiter;pending_=true;phase_=Phase::Retrying;notify=true;
  }
  if(notify)signal();
}
void ResourceWaitBinding::drive() {
  auto owner=shared_from_this();std::uint64_t attempt;scheduler::Ticket ticket;
  std::unique_ptr<resources::ResourceManager::Waiter> old;
  bool exhausted=false;
  {
    std::lock_guard lock(mutex_);
    if(!pending_ || driving_ || !accepted_ || !dependencies_ || phase_==Phase::Terminal || phase_==Phase::Ready)return;
    pending_=false;driving_=true;ticket=ticket_;
    exhausted=attempt_==std::numeric_limits<std::uint64_t>::max();
    if(!exhausted)++attempt_;
    attempt=attempt_;phase_=Phase::Installing;wake_generation_=waiter_generation_=0;old=std::move(waiter_);
  }
  old.reset();
  if(exhausted) {
    {std::lock_guard lock(mutex_);driving_=false;}
    (void)scheduler_->retire(ticket,resources::error(resources::Errc::Overflow));return;
  }
  foundation::Result<resources::ResourceManager::Acquisition> acquired;
  try {
    if(!claims_.empty()) {
      auto weak=weak_from_this();
      auto wake=[weak,attempt](std::uint64_t waiter){if(auto current=weak.lock())current->woke(attempt,waiter);};
      acquired=acquire_?acquire_(claims_,wake):resources_->acquire(claims_,resources::Phase::Compute,std::move(wake));
    }
  }catch(...){acquired=make_unexpected(resources::error(resources::Errc::Full));}
  bool ready=false,retry=false;std::optional<foundation::Error> failure;
  {
    std::lock_guard lock(mutex_);driving_=false;
    if(phase_==Phase::Terminal || attempt!=attempt_)return; // 局部 Acquisition 在锁外销毁。
    if(!acquired)failure=acquired.error();
    else if(acquired->lease && acquired->waiter)failure=resources::error(resources::Errc::InvalidInput);
    else if(acquired->waiter) {
      waiter_generation_=acquired->waiter->generation();waiter_=std::move(acquired->waiter);
      retry=wake_generation_==waiter_generation_;
      phase_=retry?Phase::Retrying:Phase::Waiting;pending_=retry;
    } else if(acquired->lease || claims_.empty()) {
      lease_=std::move(acquired->lease);phase_=Phase::Ready;ready=true;
    } else failure=resources::error(resources::Errc::WouldBlock);
  }
  if(failure){(void)scheduler_->retire(ticket,*failure);return;}
  if(retry)signal();
  if(ready) {
    auto result=scheduler_->make_ready(ticket);
    if(!result)(void)scheduler_->retire(ticket,result.error());
  }
}
void ResourceWaitBinding::cancel() {
  auto owner=shared_from_this();stop_.request_stop();scheduler::Ticket ticket;
  {std::lock_guard lock(mutex_);ticket=ticket_;}
  // retire 与业务 start 共用 Scheduler 锁；AlreadyStarted 只保留协作意图。
  if(ticket)(void)scheduler_->retire(ticket);
}
void ResourceWaitBinding::terminal() {
  auto owner=shared_from_this();std::unique_ptr<resources::ResourceManager::Lease> lease;
  std::unique_ptr<resources::ResourceManager::Waiter> waiter;
  {std::lock_guard lock(mutex_);phase_=Phase::Terminal;pending_=false;lease=std::move(lease_);waiter=std::move(waiter_);}
  // 不修改已转给业务的 Lease；capture/资源 wake 析构可能重入。
}
foundation::Result<std::unique_ptr<resources::ResourceManager::Lease>> ResourceWaitBinding::start_lease() {
  std::lock_guard lock(mutex_);
  if(!accepted_ || phase_!=Phase::Ready || running_)return make_unexpected(resources::error(resources::Errc::PhaseViolation));
  running_=true;return std::move(lease_);
}
ResourceWaitBinding::Snapshot ResourceWaitBinding::snapshot()const {
  std::lock_guard lock(mutex_);return {phase_,accepted_,pending_,driving_,running_,attempt_,waiter_generation_};
}
}
