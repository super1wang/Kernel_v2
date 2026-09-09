#pragma once
#include "execution_table.hpp"
#include <ock/runtime/registry.hpp>

namespace ock::runtime::executions::detail {
// 只交给可信组合根，由已有 Policy Subscription 验证范围与当前发送权限。
// 原始 get/list 不开放；可靠查询继续走 HostSession 的 Policy 入口。
class ExecutionObservation final : public host::ExecutionObservationPort {
  struct Ledger {
    std::mutex mutex;
    std::condition_variable drained;
    std::size_t used=0,limit=0;
  };
  struct Reservation {
    std::shared_ptr<Ledger> ledger;
    bool armed=false;
    ~Reservation() {
      if(!armed)return;
      std::lock_guard lock(ledger->mutex);--ledger->used;ledger->drained.notify_all();
    }
  };
  struct Watch {
    std::shared_ptr<Reservation> reservation;
    contracts::CallerView caller;
    contracts::ObservationFilter filter;
    std::shared_ptr<contracts::ObservationReceiver> receiver;
    std::uint64_t cursor=0,identity=0;
    bool active=true,busy=false,gap=false;
    Watch(std::shared_ptr<Reservation> charge,const contracts::CallerView& who,
        const contracts::ObservationFilter& selected,std::shared_ptr<contracts::ObservationReceiver> sink)
        :reservation(std::move(charge)),caller(who),filter(selected),receiver(std::move(sink)) {}
    // 成员倒序销毁：receiver/caller 先释放，reservation 最后归还额度。
  };
  struct State {
    std::shared_ptr<ExecutionTable> table;
    std::shared_ptr<Ledger> ledger=std::make_shared<Ledger>();
    std::mutex mutex;
    std::vector<std::shared_ptr<Watch>> slots;
    std::size_t next=0;
    std::uint64_t identity=0;
    bool accepting=true,closed=false;
    State(std::shared_ptr<ExecutionTable> source,std::size_t limit)
        :table(std::move(source)),slots(limit) {ledger->limit=limit;}
    void remove(std::size_t slot,std::uint64_t id) {
      registry::detail::ExecutionCallbackFrame frame;
      std::shared_ptr<Watch> retired;
      {
        std::lock_guard lock(mutex);auto& watch=slots[slot];
        if(!watch||watch->identity!=id)return;
        watch->active=false;if(!watch->busy)retired=std::move(watch);
      }
      retired.reset();
    }
    void close() {
      registry::detail::ExecutionCallbackFrame frame;
      {std::lock_guard lock(mutex);accepting=false;closed=true;}
      // 不复制订阅目录；逐槽释放，外部析构始终在锁外。
      for(std::size_t i=0;i<slots.size();++i) {
        std::shared_ptr<Watch> retired;
        {std::lock_guard lock(mutex);if(slots[i]) {
          slots[i]->active=false;if(!slots[i]->busy)retired=std::move(slots[i]);
        }}
        retired.reset();
      }
    }
  };
  class Lease final : public contracts::ObservationLease {
  public:
    Lease(std::weak_ptr<State> state,std::size_t slot,std::uint64_t id)
        :state_(std::move(state)),slot_(slot),id_(id) {}
    ~Lease() override {if(auto state=state_.lock())state->remove(slot_,id_);}
  private:
    std::weak_ptr<State> state_;
    std::size_t slot_;
    std::uint64_t id_;
  };
public:
  static contracts::Result<std::shared_ptr<ExecutionObservation>> create(
      std::shared_ptr<ExecutionTable> table,std::size_t leases) {
    if(!table||!leases)return fail();
    try {return std::shared_ptr<ExecutionObservation>(new ExecutionObservation(std::move(table),leases));}
    catch(...) {return fail();}
  }
  ~ExecutionObservation() override {close();}
  contracts::Result<std::shared_ptr<const contracts::ExecutionSummary>>
  get_summary(const contracts::CallerView&,contracts::ExecutionRef) override {return unsupported();}
  contracts::Result<contracts::ListPage>
  list_summaries(const contracts::CallerView&,const contracts::ListRequest&) override {return unsupported();}
  contracts::Result<std::unique_ptr<contracts::ObservationLease>> observe_changes(
      const contracts::CallerView& caller,const contracts::ObservationFilter& filter,
      std::shared_ptr<contracts::ObservationReceiver> receiver) override {
    if(registry::detail::execution_callback_active)return fail();
    registry::detail::ExecutionCallbackFrame frame;
    auto s=state_;
    try {
      if(!receiver||!contracts::validate_observation_filter(filter)||!caller.revalidate())return fail();
      auto charge=std::make_shared<Reservation>();charge->ledger=s->ledger;
      {
        std::lock_guard lock(s->mutex);std::lock_guard quota(s->ledger->mutex);
        if(!s->accepting||s->ledger->used>=s->ledger->limit)return fail();
        ++s->ledger->used;charge->armed=true;
      }
      auto watch=std::make_shared<Watch>(charge,caller,filter,std::move(receiver));
      // 起点先于注册发布；其后的变化在有限环内保留或明确 gap。
      watch->cursor=s->table->notice_cursor();
      std::unique_ptr<contracts::ObservationLease> lease;
      {
        std::lock_guard lock(s->mutex);
        if(!s->accepting||s->identity==(std::numeric_limits<std::uint64_t>::max)())return fail();
        auto found=std::find(s->slots.begin(),s->slots.end(),nullptr);
        if(found==s->slots.end())return fail();
        auto slot=static_cast<std::size_t>(found-s->slots.begin());watch->identity=++s->identity;
        // 句柄分配成功后才发布；失败不在源锁内析构已安装的 lease。
        lease=std::make_unique<Lease>(s,slot,watch->identity);*found=watch;
      }
      return lease;
    } catch(...) {return fail();}
  }
  contracts::Result<std::size_t> pump(std::size_t budget) override {
    if(!budget||budget>4096||registry::detail::execution_callback_active)return fail();
    registry::detail::ExecutionCallbackFrame frame;
    auto s=state_;std::size_t consumed=0;
    for(std::size_t attempt=0;attempt<budget;++attempt) {
      std::shared_ptr<Watch> watch;std::size_t slot=0;
      {
        std::lock_guard lock(s->mutex);if(s->closed)break;
        for(std::size_t examined=0;examined<s->slots.size();++examined) {
          slot=s->next;s->next=(s->next+1)%s->slots.size();
          if(s->slots[slot]&&s->slots[slot]->active&&!s->slots[slot]->busy) {
            watch=s->slots[slot];watch->busy=true;break;
          }
        }
      }
      if(!watch)break;
      bool retire=false,exhausted=false;
      try {
        if(!watch->caller.revalidate())retire=true;
        else {
          auto next=s->table->next_notice(watch->cursor);watch->gap|=next.gap;exhausted=next.exhausted;
          if(next.notice) {
            ++consumed;const auto& notice=*next.notice;
            if(std::find(watch->filter.topics.begin(),watch->filter.topics.end(),notice.topic)!=watch->filter.topics.end()) {
              auto entry=s->table->find(notice.execution);
              auto summary=entry?s->table->summary(entry):contracts::Result<std::shared_ptr<const contracts::ExecutionSummary>>(fail());
              entry.reset(); // 通知 owner 从不 pin 输入/结果。
              if(!summary)watch->gap=true;
              else if((watch->filter.owner&&(*summary)->value().owner==*watch->filter.owner)||
                  (!watch->filter.owner&&std::find(watch->filter.executions.begin(),watch->filter.executions.end(),notice.execution)!=watch->filter.executions.end())) {
                auto gap=watch->gap||(*summary)->value().version.value()!=notice.version;
                watch->gap=false;watch->receiver->changed({*summary,notice.topic,gap});
              }
            }
          }
        }
      } catch(...) {watch->gap=true;}
      std::shared_ptr<Watch> retired;
      {
        std::lock_guard lock(s->mutex);watch->busy=false;
        if(retire)watch->active=false;
        if(!watch->active)retired=std::move(s->slots[slot]);
      }
      watch.reset();retired.reset();
      if(exhausted){s->close();return fail();}
    }
    return consumed;
  }
  void close() {auto s=state_;s->close();}
  void stop_accepting() {auto s=state_;std::lock_guard lock(s->mutex);s->accepting=false;}
  contracts::Result<bool> finish_until(host::TimePoint deadline) {
    if(registry::detail::execution_callback_active)return fail();
    auto s=state_;s->close();std::unique_lock lock(s->ledger->mutex);
    return s->ledger->drained.wait_until(lock,deadline,[&]{return s->ledger->used==0;});
  }
  std::size_t used() const {auto s=state_;std::lock_guard lock(s->ledger->mutex);return s->ledger->used;}
private:
  static foundation::Unexpected<contracts::Error> fail() {return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::BudgetExceeded));}
  static foundation::Unexpected<contracts::Error> unsupported() {return contracts::make_unexpected(host::host_error(host::HostErrc::UnsupportedCapability));}
  ExecutionObservation(std::shared_ptr<ExecutionTable> table,std::size_t leases)
      :state_(std::make_shared<State>(std::move(table),leases)) {}
  std::shared_ptr<State> state_;
};
}
