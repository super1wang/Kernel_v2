#pragma once
#include "execution_table.hpp"
#include "invocation_record.hpp"
#include "resource_wait_binding.hpp"
#include "managed_control.hpp"

namespace ock::runtime::executions::detail {
// Host 与内部 typed 适配共用的逐执行 owner；控制循环消费可靠 pending，wake 仅作唤醒。
class ManagedInvocation final : public ManagedControl, public contracts::ExecutionScopePort,
                                public std::enable_shared_from_this<ManagedInvocation> {
public:
  using Record=InvocationRecordBase;
  using Resolver=std::function<contracts::Result<std::vector<resources::Claim>>(
      std::span<const registry::ResourceRef>)>;
  // child 强持有父；父槽仅弱持有 child。失败创建与终态均显式释放槽。
  class ChildLink final {
    friend class ManagedInvocation;
  public:
    ~ChildLink() { settle(); }
    void settle() noexcept;
    void install(const std::shared_ptr<ManagedInvocation>&);
    contracts::ExecutionRef parent() const noexcept {return parent_->execution();}
    std::size_t depth() const noexcept {return parent_->depth_+1;}
  private:
    ChildLink(std::shared_ptr<ManagedInvocation> parent,std::size_t slot,std::uint64_t generation)
        :parent_(std::move(parent)),slot_(slot),generation_(generation) {}
    std::shared_ptr<ManagedInvocation> parent_;
    std::size_t slot_;
    std::uint64_t generation_;
    // shared_ptr 控制块分配失败会销毁刚 new 的对象；此时尚未占槽，
    // 不得从父锁内回调 settle。占槽全部提交后才激活回收责任。
    std::atomic<bool> settled_{true};
  };
  contracts::Result<std::shared_ptr<ChildLink>> reserve_child(
      const std::shared_ptr<ExecutionTable>& table,const Record& record,std::size_t max_depth) {
    auto parent=record_->material(),child=record.material();
    if(table_!=table || parent->session!=child->session ||
       parent->caller->view().description().principal!=child->caller->view().description().principal ||
       record.options().deadline>record_->options().deadline)
      return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::InvalidAuthority));
    auto current=parent->caller->view().revalidate();
    if(!current)return contracts::make_unexpected(current.error());
    std::lock_guard lock(lifetime_mutex_);
    if(!accept_children_||cancel_requested_||!next_child_generation_||depth_>=max_depth)return fail();
    for(std::size_t i=0;i<children_.size();++i)if(!children_[i].reserved) {
      auto link=std::shared_ptr<ChildLink>(new ChildLink(shared_from_this(),i,next_child_generation_));
      children_[i].reserved=true;children_[i].generation=next_child_generation_++;
      ++children_remaining_;link->settled_.store(false,std::memory_order_release);return link;
    }
    return fail();
  }
  static contracts::Result<std::shared_ptr<ManagedInvocation>> create(
      std::shared_ptr<ExecutionTable> table,std::shared_ptr<scheduler::Scheduler> scheduler,
      std::shared_ptr<resources::ResourceManager> resources,
      contracts::ExecutionRef identity,contracts::HostIncarnation host,
      std::shared_ptr<Record> record,const Resolver& resolve,std::function<void()> wake,
      std::shared_ptr<ChildLink> parent={},std::size_t children_limit=64) {
    if(!table||!scheduler||!record||!resolve||!children_limit||children_limit>256)return fail();
    auto options=record->options();
    std::shared_ptr<ManagedInvocation> owner;
    try {
      auto material=record->material();
      auto claims=resolve(material->entry->resources);
      if(!claims)return contracts::make_unexpected(claims.error());
      if(!claims->empty()&&!resources)return fail();
      owner=std::shared_ptr<ManagedInvocation>(new ManagedInvocation(
          table,scheduler,record,std::move(wake),std::move(parent),children_limit));
      contracts::SummaryInput summary{identity,material->entry->definition->description().key,
          material->caller->view().description().principal,
          owner->parent_?std::optional(owner->parent_->parent()):std::nullopt,contracts::ExecutionPhase::Queued,
          host,*contracts::ObservationVersion::create(1),{}, {}};
      auto entry=table->prepare(std::move(summary),record,record->result_type(),
          record->input_bytes(),record->reserved_reply_bytes(),material->targets,
          +[](const void* value) noexcept -> const void* {return static_cast<const Record*>(value)->reply_pointer();});
      if(!entry)return contracts::make_unexpected(entry.error());
      owner->entry_=*entry;
      std::weak_ptr<ManagedInvocation> weak=owner;
      owner->binding_=std::make_shared<ResourceWaitBinding>(scheduler,resources,std::move(*claims),
          [weak]{if(auto e=weak.lock())e->signal();});
      // 先装入父槽再 enqueue；父取消与安装共用父仲裁，不能漏掉早到取消。
      if(owner->parent_)owner->parent_->install(owner);
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
      owner->original_stop_.emplace(options.stop,OriginalStop{weak});
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
      bool completing;
      {std::lock_guard lock(lifetime_mutex_);completing=body_completed_;}
      // 锁外统一执行 continuation，不在 child 完成栈递归收尾父链。
      if(completing) {finish_if_ready();return;}
      if(!resources_announced_.exchange(true,std::memory_order_acq_rel))
        (void)table_->transition(entry_,contracts::ExecutionPhase::WaitingResources,conditions());
      binding_->drive();
    }
  }
  bool pending() const noexcept override {return pending_.load(std::memory_order_acquire);}
  contracts::Result<contracts::CancelDisposition> cancel() override {
    if(finished())return contracts::CancelDisposition::AlreadyTerminal;
    {std::lock_guard lock(lifetime_mutex_);cancel_requested_=true;}
    // 有限槽逐个锁外取消；安装看到 cancel_requested 后补同一取消意图。
    for(std::size_t i=0;i<children_.size();++i) {
      std::shared_ptr<ManagedInvocation> child;
      {std::lock_guard lock(lifetime_mutex_);child=children_[i].child.lock();}
      if(child)(void)child->cancel();
    }
    auto retired=binding_->cancel();
    if(!retired) {
      // Scheduler 的有限历史可能已淘汰；本执行的可靠完成事实仍归 owner。
      if(finished())return contracts::CancelDisposition::AlreadyTerminal;
      {std::lock_guard lock(lifetime_mutex_);if(body_completed_)return contracts::CancelDisposition::AlreadyClaimed;}
      return contracts::make_unexpected(retired.error());
    }
    switch(*retired) {
      case scheduler::Retirement::Retired:return contracts::CancelDisposition::Requested;
      case scheduler::Retirement::AlreadyStarted:return contracts::CancelDisposition::AlreadyClaimed;
      case scheduler::Retirement::AlreadyTerminal:return finished()?contracts::CancelDisposition::AlreadyTerminal:
                                                                    contracts::CancelDisposition::AlreadyClaimed;
    }
    return fail();
  }
  bool finished() const noexcept override {return finished_.load(std::memory_order_acquire);}
  contracts::ExecutionRef execution() const noexcept override {return entry_->execution();}
  const std::shared_ptr<ExecutionTable::Entry>& entry() const noexcept {return entry_;}
private:
  static foundation::Unexpected<contracts::Error> fail() {
    return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::BudgetExceeded));
  }
  ManagedInvocation(std::shared_ptr<ExecutionTable> table,std::shared_ptr<scheduler::Scheduler> scheduler,
      std::shared_ptr<Record> record,std::function<void()> wake,std::shared_ptr<ChildLink> parent,
      std::size_t children_limit)
      :table_(std::move(table)),scheduler_(std::move(scheduler)),record_(std::move(record)),wake_(std::move(wake)),
       parent_(std::move(parent)),children_(children_limit),depth_(parent_?parent_->depth():1) {}
  void signal() noexcept {
    pending_.store(true,std::memory_order_release);
    try {if(wake_)wake_();}catch(...){}
  }
  static contracts::PhaseConditions conditions(std::uint64_t children=0) {
    return {{contracts::RequiredRecordState::NotRequired,0,children},{},{},false,false};
  }
  contracts::Result<void> run() {
    auto lease=binding_->start_lease();
    if(!lease)return contracts::make_unexpected(lease.error());
    auto running=table_->transition(entry_,contracts::ExecutionPhase::Running,conditions());
    if(!running)return running;
    {std::lock_guard lock(lifetime_mutex_);accept_children_=!cancel_requested_;}
    struct CloseChildren {
      ManagedInvocation& owner;
      ~CloseChildren() {std::lock_guard lock(owner.lifetime_mutex_);owner.accept_children_=false;}
    } close{*this};
    const contracts::ResourceLease* raw=lease->get();
    const auto resources=raw?std::span<const contracts::ResourceLease* const>(&raw,1):
                            std::span<const contracts::ResourceLease* const>{};
    if(!record_->run_once(binding_->stop_token(),resources,shared_from_this()))return fail();
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
    {
      std::lock_guard lock(lifetime_mutex_);
      accept_children_=false;completion_fault_=fault;
      if(children_remaining_) {
        auto waiting=table_->transition(entry_,contracts::ExecutionPhase::WaitingChild,conditions(children_remaining_),fault);
        foundation::invariant(bool(waiting));
      }
      body_completed_=true;
    }
    finish_if_ready();
  }
  void finish_if_ready() {
    std::shared_ptr<ChildLink> parent;
    {
      std::lock_guard lock(lifetime_mutex_);
      if(!body_completed_||children_remaining_||finished())return;
      auto finalizing=table_->transition(entry_,contracts::ExecutionPhase::Finalizing,conditions(),completion_fault_);
      foundation::invariant(bool(finalizing));
      auto terminal=table_->transition(entry_,contracts::ExecutionPhase::Terminal,conditions());
      foundation::invariant(bool(terminal));
      pending_.store(false,std::memory_order_release);
      finished_.store(true,std::memory_order_release);
      parent=std::move(parent_);
    }
    if(parent)parent->settle();
    try {if(wake_)wake_();}catch(...){}
  }
  struct OriginalStop {
    std::weak_ptr<ManagedInvocation> owner;
    void operator()() const noexcept {if(auto current=owner.lock())(void)current->cancel();}
  };
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
  struct ChildSlot {bool reserved=false;std::uint64_t generation=0;std::weak_ptr<ManagedInvocation> child;};
  std::mutex lifetime_mutex_;
  std::shared_ptr<ChildLink> parent_;
  std::vector<ChildSlot> children_;
  const std::size_t depth_;
  std::size_t children_remaining_=0;
  std::uint64_t next_child_generation_=1;
  bool accept_children_=false,cancel_requested_=false,body_completed_=false;
  std::optional<contracts::Error> completion_fault_;
  std::optional<std::stop_callback<OriginalStop>> original_stop_;
};

inline void ManagedInvocation::ChildLink::settle() noexcept {
  if(settled_.exchange(true,std::memory_order_acq_rel))return;
  auto parent=parent_;bool resume=false;
  {
    std::lock_guard lock(parent->lifetime_mutex_);
    auto& slot=parent->children_[slot_];
    foundation::invariant(slot.reserved&&slot.generation==generation_);
    slot.child.reset();slot.reserved=false;--parent->children_remaining_;
    resume=parent->body_completed_&&!parent->children_remaining_;
  }
  if(resume)parent->signal();
}
inline void ManagedInvocation::ChildLink::install(const std::shared_ptr<ManagedInvocation>& child) {
  bool cancel;
  {
    std::lock_guard lock(parent_->lifetime_mutex_);
    auto& slot=parent_->children_[slot_];
    foundation::invariant(slot.reserved&&slot.generation==generation_);
    slot.child=child;cancel=parent_->cancel_requested_;
  }
  if(cancel)(void)child->cancel();
}

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
