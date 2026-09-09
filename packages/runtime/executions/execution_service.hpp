#pragma once
#include "managed_execution.hpp"
#include "execution_queries.hpp"
#include <thread>
namespace ock::runtime::executions::detail {
// 显式启用的共享执行服务；NativeSubset 不创建此对象，因此不新增线程。
// 生命周期由 Host 控制 owner 管理，业务不得在所属 worker 销毁控制 owner。
class ExecutionService final {
public:
  struct Options {std::size_t active=256,control_batch=64;};
private:
  struct Wake {
    std::mutex mutex;std::condition_variable changed;bool dirty=false;
    void signal() {std::lock_guard lock(mutex);dirty=true;changed.notify_one();}
    bool consume() {std::lock_guard lock(mutex);return std::exchange(dirty,false);}
    void wait(scheduler::Time deadline) {
      std::unique_lock lock(mutex);changed.wait_until(lock,deadline,[&]{return dirty;});
    }
  };
  struct Slot {bool reserved=false;std::shared_ptr<ManagedControl> execution;};
  struct State {
    std::shared_ptr<ExecutionTable> table;
    std::shared_ptr<scheduler::Scheduler> scheduler;
    std::shared_ptr<resources::ResourceManager> resources;
    std::shared_ptr<contracts::ExecutorPort> executor;
    std::shared_ptr<Wake> wake=std::make_shared<Wake>();
    Options options;
    std::mutex mutex;std::condition_variable stopped;
    std::vector<Slot> slots;
    std::size_t used=0,cursor=0;
    bool closing=false,exited=false;
    std::thread::id control_id;
  };
public:
  static contracts::Result<std::unique_ptr<ExecutionService>> create(
      std::shared_ptr<ExecutionTable> table,std::shared_ptr<contracts::ExecutorPort> executor,
      std::shared_ptr<resources::ResourceManager> resources,std::vector<scheduler::Subject> subjects,
      scheduler::Options scheduling,Options options) {
    if(!table||!executor||!resources||!options.active||!options.control_batch)return fail();
    try {
      auto state=std::make_shared<State>();state->table=std::move(table);state->executor=executor;
      state->resources=std::move(resources);state->options=options;state->slots.resize(options.active);
      auto schedule=scheduler::Scheduler::create(executor,std::move(subjects),scheduling,
          [wake=state->wake]{wake->signal();});
      if(!schedule)return contracts::make_unexpected(schedule.error());
      state->scheduler=std::move(*schedule);
      auto service=std::unique_ptr<ExecutionService>(new ExecutionService(state));
      service->thread_=std::thread([state]{run(state);});
      {std::lock_guard lock(state->mutex);state->control_id=service->thread_.get_id();}
      return service;
    } catch(...) {return fail();}
  }
  ~ExecutionService() {
    if(!thread_.joinable())return;
    foundation::invariant(std::this_thread::get_id()!=thread_.get_id());
    auto control=std::dynamic_pointer_cast<contracts::ExecutorControlPort>(state_->executor);
    foundation::invariant(!control||!control->in_worker());
    close();thread_.join(); // 不 detach；所有在途 owner 保留到实际排空。
  }
  template<contracts::AsyncInput A,contracts::ContractResult R>
    requires(std::same_as<R,void>||contracts::AsyncInput<R>)
  contracts::SubmitReply submit(const invocation::NativeBound<A,R>& bound,const A& args,
      invocation::InvokeOptions options,typename InvocationRecord<A,R>::Policy policy,
      const typename ManagedExecution<A,R>::Resolver& resolver) {
    auto record=InvocationAccess::create_record(bound,args,options,policy);
    if(!record)return contracts::Rejected{record.error()};
    return submit(std::move(*record),resolver);
  }
  contracts::SubmitReply submit(std::shared_ptr<InvocationRecordBase> record,
      const ManagedInvocation::Resolver& resolver) {
    if(!record)return contracts::Rejected{contracts::error(contracts::ContractsErrc::Rejected)};
    auto s=state_;std::size_t slot=s->slots.size();
    {
      std::lock_guard lock(s->mutex);
      if(s->closing||s->used==s->slots.size())return contracts::Rejected{contracts::error(contracts::ContractsErrc::BudgetExceeded)};
      for(std::size_t i=0;i<s->slots.size();++i)if(!s->slots[i].reserved){slot=i;break;}
      foundation::invariant(slot<s->slots.size());s->slots[slot].reserved=true;++s->used;
    }
    struct Reservation {
      std::shared_ptr<State> state;std::size_t slot;bool installed=false;
      ~Reservation() {
        if(installed)return;
        {std::lock_guard lock(state->mutex);state->slots[slot].reserved=false;--state->used;}
        state->wake->signal();
      }
    } reservation{s,slot};
    std::shared_ptr<ManagedInvocation> accepted;
    try {
      auto identity=new_execution_identity();if(!identity)return contracts::Rejected{identity.error()};
      auto execution=ManagedInvocation::create(s->table,s->scheduler,s->resources,*identity,s->table->host(),
          std::move(record),resolver,[wake=s->wake]{wake->signal();});
      if(!execution)return contracts::Rejected{execution.error()};
      accepted=*execution;
      bool closing;
      {
        std::lock_guard lock(s->mutex);s->slots[slot].execution=*execution;
        reservation.installed=true;closing=s->closing;
      }
      if(closing)(*execution)->cancel();
      s->wake->signal();
      return (*execution)->accepted();
    } catch(...) {
      // 接受后关闭/唤醒的异常不能把同一执行改报成无身份拒绝。
      if(accepted) {
        foundation::invariant(reservation.installed);
        return accepted->accepted();
      }
      return contracts::Rejected{contracts::error(contracts::ContractsErrc::BudgetExceeded)};
    }
  }
  // 授权仍在当前 Policy 中完成；true 表示提交取消意图，不声称已停止。
  contracts::Result<bool> cancel(const policy::VerifiedCaller& caller,const std::shared_ptr<policy::SessionAuthority>& session,
      contracts::ExecutionRef ref) {
    if(!session)return fail();
    auto allowed=session->observations()->get(caller,ref,policy::AccessUse::CancelExecution);
    if(!allowed)return contracts::make_unexpected(allowed.error());
    std::shared_ptr<ManagedControl> found;
    {std::lock_guard lock(state_->mutex);for(const auto& slot:state_->slots)
      if(slot.execution&&slot.execution->execution()==ref){found=slot.execution;break;}}
    if(found) {if(found->finished())return false;found->cancel();return true;}
    auto summary=state_->table->access_find(ref);
    if(summary&&summary->summary->value().phase==contracts::ExecutionPhase::Terminal)return false;
    return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::Rejected));
  }
  void close() {
    auto s=state_;s->table->close_admission();
    {std::lock_guard lock(s->mutex);s->closing=true;}
    // 没有容量依赖 post_control，也不在服务锁下调用 Scheduler/业务回调。
    for(std::size_t i=0;i<s->slots.size();++i) {
      std::shared_ptr<ManagedControl> entry;
      {std::lock_guard lock(s->mutex);entry=s->slots[i].execution;}
      if(entry)entry->cancel();
    }
    s->scheduler->close();s->wake->signal();
  }
  contracts::Result<bool> shutdown_until(scheduler::Time deadline) {
    if(in_execution_thread())return fail();
    std::unique_lock shutdown(shutdown_mutex_,std::defer_lock);
    if(!shutdown.try_lock_until(deadline))return false;
    close();std::unique_lock lock(state_->mutex);
    if(!state_->stopped.wait_until(lock,deadline,[&]{return state_->exited;}))return false;
    lock.unlock();if(thread_.joinable())thread_.join();return true;
  }
  std::size_t active() const {std::lock_guard lock(state_->mutex);return state_->used;}
  bool in_execution_thread() const noexcept {
    {std::lock_guard lock(state_->mutex);if(std::this_thread::get_id()==state_->control_id)return true;}
    auto control=std::dynamic_pointer_cast<contracts::ExecutorControlPort>(state_->executor);
    return control&&control->in_worker();
  }
  scheduler::Snapshot scheduler_snapshot() const {return state_->scheduler->snapshot();}
private:
  static foundation::Unexpected<contracts::Error> fail() {return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::BudgetExceeded));}
  explicit ExecutionService(std::shared_ptr<State> state):state_(std::move(state)){}
  static void run(const std::shared_ptr<State>& s) {
    std::size_t remaining=0;
    for(;;) {
      if(s->wake->consume())remaining=s->slots.size();
      for(std::size_t n=0;n<s->options.control_batch&&remaining;++n,--remaining) {
        std::shared_ptr<ManagedControl> entry;std::size_t slot;
        {std::lock_guard lock(s->mutex);slot=s->cursor;s->cursor=(s->cursor+1)%s->slots.size();entry=s->slots[slot].execution;}
        if(!entry)continue;
        if(entry->pending())entry->drive();
        if(entry->finished()) {
          std::shared_ptr<ManagedControl> retired;
          {std::lock_guard lock(s->mutex);if(s->slots[slot].execution==entry) {
            retired=std::move(s->slots[slot].execution);s->slots[slot].reserved=false;--s->used;
          }}
        }
      }
      s->scheduler->pump(std::chrono::steady_clock::now(),s->options.control_batch);
      s->table->trim(s->options.control_batch);
      const auto schedule=s->scheduler->snapshot();
      {std::lock_guard lock(s->mutex);if(s->closing&&!s->used&&!schedule.active&&!schedule.worker_delivery) {
        s->exited=true;s->stopped.notify_all();return;
      }}
      if(remaining)continue;
      s->wake->wait(s->scheduler->next_deadline().value_or(scheduler::Time::max()));
    }
  }
  std::shared_ptr<State> state_;
  std::thread thread_;
  std::timed_mutex shutdown_mutex_;
};
}
