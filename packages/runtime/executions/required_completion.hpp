#pragma once
#include "execution_table.hpp"
#include <ock/runtime/registry.hpp>

namespace ock::runtime::executions::detail {
// 一个预留的收尾事件；记录的是身份/收尾，不宣称业务返回值已经持久化。
class RequiredCompletion final : public std::enable_shared_from_this<RequiredCompletion> {
public:
  struct Snapshot {
    contracts::RequiredRecordState state;
    bool drained;
    std::optional<contracts::RecordFailure> failure;
  };
private:
  class Receiver final : public contracts::RecordReceiver,public std::enable_shared_from_this<Receiver> {
  public:
    void completed(contracts::RecordReport report) noexcept override {
      auto keep=shared_from_this();registry::detail::ExecutionCallbackFrame frame;
      owner->receive(std::move(report));
    }
    std::shared_ptr<RequiredCompletion> owner;
  };
public:
  ~RequiredCompletion() {
    registry::detail::ExecutionCallbackFrame frame;
    prepared_.reset();reservation_.reset();port_.reset();
  }
  static contracts::Result<std::shared_ptr<RequiredCompletion>> create(
      contracts::ExecutionRef identity,std::shared_ptr<contracts::RequiredRecordPort> port,
      std::shared_ptr<ExecutionTable> table,std::function<void()> wake) {
    try {
      auto owner=std::shared_ptr<RequiredCompletion>(new RequiredCompletion(std::move(port),std::move(table),std::move(wake)));
      contracts::RecordRequest request{identity,{},{},*contracts::Name::parse("read.finalization"),{},256};
      auto snapshot=contracts::RecordRequestSnapshot::create(request,256);
      if(!snapshot)return contracts::make_unexpected(snapshot.error());
      owner->request_=std::move(*snapshot);
      registry::detail::ExecutionCallbackFrame frame;
      auto reservation=owner->port_->reserve(request);
      if(!reservation)return contracts::make_unexpected(reservation.error());
      if(!*reservation)return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::InvalidContract));
      owner->reservation_=std::move(*reservation);
      std::weak_ptr<RequiredCompletion> weak=owner;
      owner->prepared_=std::shared_ptr<Receiver>(new Receiver,[weak](Receiver* receiver) noexcept {
        registry::detail::ExecutionCallbackFrame frame;
        auto current=weak.lock();const bool active=bool(receiver->owner);
        delete receiver;
        if(active&&current)current->drained();
      });
      return owner;
    } catch(...) {return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::BudgetExceeded));}
  }
  // 只在共享控制循环驱动；锁外调用外部端口，最多一次尝试。
  void start() noexcept {
    std::shared_ptr<Receiver> receiver;
    std::unique_ptr<contracts::RecordReservation> reservation;
    {std::lock_guard lock(mutex_);if(started_)return;started_=true;
      receiver=std::move(prepared_);reservation=std::move(reservation_);}
    registry::detail::ExecutionCallbackFrame frame;
    receiver->owner=shared_from_this();
    try {
      auto status=port_->record(std::move(reservation),request_,receiver);
      if(!status)fail(status.error());
    } catch(...) {fail(contracts::error(contracts::ContractsErrc::InvalidContract));}
    // 本地 receiver 保活至 record() 实际返回；回调报告不提前归还 owner。
  }
  Snapshot snapshot() const {
    std::lock_guard lock(mutex_);return {state_,drained_,failure_};
  }
private:
  RequiredCompletion(std::shared_ptr<contracts::RequiredRecordPort> port,
      std::shared_ptr<ExecutionTable> table,std::function<void()> wake)
      :port_(std::move(port)),table_(std::move(table)),wake_(std::move(wake)) {}
  void receive(contracts::RecordReport report) noexcept {
    {
      std::lock_guard lock(mutex_);if(state_!=contracts::RequiredRecordState::Pending)return;
      auto checked=contracts::validate_record_report(request_->value(),state_,report);
      // 当前 Read 无 commit/effect，故修复只能归 ManualReview。
      if(!checked||(report.failure&&report.failure->repair!=contracts::RepairKind::ManualReview)) {
        failure_locked(contracts::error(contracts::ContractsErrc::InvalidFact));
      } else if(report.state==contracts::RequiredRecordState::Failed)failure_locked(report.failure->reason);
      else state_=contracts::RequiredRecordState::Recorded;
    }
    signal();
  }
  void fail(contracts::Error error) noexcept {
    {std::lock_guard lock(mutex_);if(state_!=contracts::RequiredRecordState::Pending)return;failure_locked(error);}
    signal();
  }
  void failure_locked(contracts::Error error) {
    // 本服务当前只接受 Read；统一关闭其新准入，在发布封锁事实前先执行门控。
    table_->close_admission();
    if(!error.code().value())error=contracts::error(contracts::ContractsErrc::InvalidFact);
    failure_=contracts::RecordFailure{contracts::Error{error.code()},true,contracts::RepairKind::ManualReview};
    state_=contracts::RequiredRecordState::Failed;
  }
  void drained() noexcept {
    {std::lock_guard lock(mutex_);
      if(state_==contracts::RequiredRecordState::Pending)failure_locked(contracts::error(contracts::ContractsErrc::InvalidFact));
      drained_=true;
    }
    signal();
  }
  void signal() noexcept {try {if(wake_)wake_();}catch(...){}}
  std::shared_ptr<contracts::RequiredRecordPort> port_;
  std::shared_ptr<ExecutionTable> table_;
  std::function<void()> wake_;
  std::shared_ptr<const contracts::RecordRequestSnapshot> request_;
  std::unique_ptr<contracts::RecordReservation> reservation_;
  std::shared_ptr<Receiver> prepared_;
  mutable std::mutex mutex_;
  bool started_=false,drained_=false;
  contracts::RequiredRecordState state_=contracts::RequiredRecordState::Pending;
  std::optional<contracts::RecordFailure> failure_;
};
}
