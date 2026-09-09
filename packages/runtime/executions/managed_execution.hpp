#pragma once
#include "execution_table.hpp"
#include "invocation_record.hpp"
#include "resource_wait_binding.hpp"
#include "managed_control.hpp"

namespace ock::runtime::executions::detail {
// 真实 typed 调用的逐执行接线；控制 owner 须保留对象并消费 pending，wake 仅作唤醒。
// 未安装为客户端能力；身份分配、当前观察授权及共享控制循环由上层 Runtime 服务提供。
class ManagedInvocation final : public ManagedControl,public std::enable_shared_from_this<ManagedInvocation> {
public:
  using Record=InvocationRecordBase;
  using Resolver=std::function<contracts::Result<std::vector<resources::Claim>>(
      std::span<const registry::ResourceRef>)>;
  static contracts::Result<std::shared_ptr<ManagedInvocation>> create(
      std::shared_ptr<ExecutionTable> table,std::shared_ptr<scheduler::Scheduler> scheduler,
      std::shared_ptr<resources::ResourceManager> resources,
      contracts::ExecutionRef identity,contracts::HostIncarnation host,
      std::shared_ptr<Record> record,const Resolver& resolve,std::function<void()> wake) {
    if(!table||!scheduler||!resources||!record||!resolve)return fail();
    auto options=record->options();
    std::shared_ptr<ManagedInvocation> owner;
    try {
      auto material=record->material();
      auto claims=resolve(material->entry->resources);
      if(!claims)return contracts::make_unexpected(claims.error());
      owner=std::shared_ptr<ManagedInvocation>(new ManagedInvocation(table,scheduler,record,std::move(wake)));
      contracts::SummaryInput summary{identity,material->entry->definition->description().key,
          material->caller->view().description().principal,{},contracts::ExecutionPhase::Queued,
          host,*contracts::ObservationVersion::create(1),{}, {}};
      auto entry=table->prepare(std::move(summary),record,record->result_type(),
          record->input_bytes(),record->reserved_reply_bytes(),material->targets,
          +[](const void* value) noexcept -> const void* {return static_cast<const Record*>(value)->reply_pointer();});
      if(!entry)return contracts::make_unexpected(entry.error());
      owner->entry_=*entry;
      std::weak_ptr<ManagedInvocation> weak=owner;
      owner->binding_=std::make_shared<ResourceWaitBinding>(scheduler,resources,std::move(*claims),
          [weak]{if(auto e=weak.lock())e->signal();});
      scheduler::Request request;
      request.principal=material->caller->view().description().principal.principal_id;
      request.deadline=options.deadline;request.resource_ready=false;
      request.work=[owner]{return owner->run();};
      request.completed=[owner](contracts::Result<void> status){owner->completed(std::move(status));};
      request.dependencies_ready=[binding=owner->binding_]{binding->dependencies_ready();};
      request.detach_waiter=[binding=owner->binding_]{binding->terminal();};
      auto ticket=scheduler->enqueue(std::move(request));
      if(!ticket) {
        owner->binding_->terminal();owner->record_->reject_before_start(ticket.error());
        table->abandon(owner->entry_);return contracts::make_unexpected(ticket.error());
      }
      owner->ticket_=*ticket;
      auto accepted=table->publish(owner->entry_);
      if(!accepted) {
        (void)scheduler->retire(*ticket,accepted.error());
        table->abandon(owner->entry_);return contracts::make_unexpected(accepted.error());
      }
      owner->accepted_=true;
      // 唯一接受线性化点在表内；直到此后才允许 ResourceManager/make_ready。
      owner->binding_->publish(*ticket);
      return owner;
    } catch(...) {
      if(owner&&owner->accepted_) {
        // 接受之后不能再返回无身份 Rejected；保留同一执行并可靠退役。
        (void)scheduler->retire(owner->ticket_,contracts::error(contracts::ContractsErrc::Rejected));
        return owner;
      }
      if(owner&&owner->entry_) {
        if(owner->ticket_)(void)scheduler->retire(owner->ticket_);
        table->abandon(owner->entry_);
      }
      return fail();
    }
  }
  contracts::Accepted accepted() const noexcept {
    return {entry_->execution(),contracts::AcceptanceGuarantee::Volatile};
  }
  // 控制循环每次最多一次资源 acquire；pending 与可靠完成均不依赖 lossy 通知。
  void drive() override {
    if(pending_.exchange(false,std::memory_order_acq_rel)) {
      if(!resources_announced_.exchange(true,std::memory_order_acq_rel))
        (void)table_->transition(entry_,contracts::ExecutionPhase::WaitingResources,conditions());
      binding_->drive();
    }
  }
  bool pending() const noexcept override {return pending_.load(std::memory_order_acquire);}
  void cancel() override {binding_->cancel();}
  bool finished() const noexcept override {return finished_.load(std::memory_order_acquire);}
  contracts::ExecutionRef execution() const noexcept override {return entry_->execution();}
  const std::shared_ptr<ExecutionTable::Entry>& entry() const noexcept {return entry_;}
private:
  static foundation::Unexpected<contracts::Error> fail() {
    return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::BudgetExceeded));
  }
  ManagedInvocation(std::shared_ptr<ExecutionTable> table,std::shared_ptr<scheduler::Scheduler> scheduler,
      std::shared_ptr<Record> record,std::function<void()> wake)
      :table_(std::move(table)),scheduler_(std::move(scheduler)),record_(std::move(record)),wake_(std::move(wake)) {}
  void signal() noexcept {
    pending_.store(true,std::memory_order_release);
    try {if(wake_)wake_();}catch(...){}
  }
  static contracts::PhaseConditions conditions() {
    return {{contracts::RequiredRecordState::NotRequired,0,0},{},{},false,false};
  }
  contracts::Result<void> run() {
    auto lease=binding_->start_lease();
    if(!lease)return contracts::make_unexpected(lease.error());
    auto running=table_->transition(entry_,contracts::ExecutionPhase::Running,conditions());
    if(!running)return running;
    const contracts::ResourceLease* raw=lease->get();
    const auto resources=raw?std::span<const contracts::ResourceLease* const>(&raw,1):
                            std::span<const contracts::ResourceLease* const>{};
    if(!record_->run_once(binding_->stop_token(),resources))return fail();
    // 同步访问栈退出后局部 lease 释放，Scheduler completed 才能发布 Terminal。
    return {};
  }
  void completed(contracts::Result<void> status) {
    binding_->terminal();
    if(!record_->reply_pointer())record_->reject_before_start(status?contracts::error(contracts::ContractsErrc::Rejected):status.error());
    std::optional<contracts::Error> fault;
    if(!status)fault=contracts::Error{status.error().code()};
    auto completion=record_->completion();
    if(completion.fault)fault=completion.fault;
    auto recorded=table_->complete_read(entry_,std::span(completion.facts).first(completion.count),completion.evidence,fault);
    foundation::invariant(bool(recorded));
    auto finalizing=table_->transition(entry_,contracts::ExecutionPhase::Finalizing,conditions(),fault);
    foundation::invariant(bool(finalizing));
    auto terminal=table_->transition(entry_,contracts::ExecutionPhase::Terminal,conditions());
    foundation::invariant(bool(terminal));
    pending_.store(false,std::memory_order_release);
    finished_.store(true,std::memory_order_release);
    try {if(wake_)wake_();}catch(...){}
  }
  std::shared_ptr<ExecutionTable> table_;
  std::shared_ptr<scheduler::Scheduler> scheduler_;
  std::shared_ptr<Record> record_;
  std::shared_ptr<ExecutionTable::Entry> entry_;
  std::shared_ptr<ResourceWaitBinding> binding_;
  std::function<void()> wake_;
  scheduler::Ticket ticket_=0;
  bool accepted_=false;
  std::atomic<bool> pending_{false};
  std::atomic<bool> resources_announced_{false};
  std::atomic<bool> finished_{false};
};

// 保留内部 typed 调试适配；Host 使用同一 erased owner，不生成第二条执行管线。
template<contracts::AsyncInput A,contracts::ContractResult R>
  requires (std::same_as<R,void> || contracts::AsyncInput<R>)
struct ManagedExecution {
  using Record=InvocationRecord<A,R>;
  using Resolver=ManagedInvocation::Resolver;
  static contracts::Result<std::shared_ptr<ManagedInvocation>> create(
      std::shared_ptr<ExecutionTable> table,std::shared_ptr<scheduler::Scheduler> scheduler,
      std::shared_ptr<resources::ResourceManager> resources,
      contracts::ExecutionRef identity,contracts::HostIncarnation host,
      const invocation::NativeBound<A,R>& bound,A args,invocation::InvokeOptions options,
      typename Record::Policy policy,const Resolver& resolve,std::function<void()> wake) {
    auto record=InvocationAccess::create_record(bound,std::move(args),options,policy);
    if(!record)return contracts::make_unexpected(record.error());
    return ManagedInvocation::create(std::move(table),std::move(scheduler),std::move(resources),
        identity,host,std::move(*record),resolve,std::move(wake));
  }
};
}
