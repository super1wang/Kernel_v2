#pragma once
#include "execution_service.hpp"
#include "execution_observation.hpp"
#include <ock/runtime/host.hpp>

namespace ock::runtime::executions::detail {
// Runtime 内部装配；只依赖 CoreContracts executor，不引入 CpuPool 反向依赖。
class HostedExecutions final : public host::HostExecutionPort {
public:
  static contracts::Result<std::shared_ptr<HostedExecutions>> create(
      contracts::HostIncarnation host,std::shared_ptr<contracts::ExecutorControlPort> executor,
      std::shared_ptr<resources::ResourceManager> resources,std::vector<scheduler::Subject> subjects,
      ExecutionTable::Limits limits={},scheduler::Options scheduling={},
      ExecutionService::Options options={},ManagedInvocation::Resolver resolve={}) {
    if(!executor||executor.use_count()==0)return contracts::make_unexpected(host::host_error(host::HostErrc::InvalidOwner));
    try {
      auto table=ExecutionTable::create(limits,host);if(!table)return contracts::make_unexpected(table.error());
      auto owner=std::shared_ptr<HostedExecutions>(new HostedExecutions(*table,executor));
      auto events=ExecutionObservation::create(*table,limits.observation_leases);
      if(!events)return contracts::make_unexpected(events.error());owner->events_=std::move(*events);
      owner->resolve_=std::move(resolve);
      if(!owner->resolve_)owner->resolve_=[](std::span<const registry::ResourceRef> refs)
          -> contracts::Result<std::vector<resources::Claim>> {
        if(!refs.empty())return contracts::make_unexpected(host::host_error(host::HostErrc::UnsupportedCapability));
        return std::vector<resources::Claim>{};
      };
      auto service=ExecutionService::create(*table,executor,std::move(resources),std::move(subjects),scheduling,options);
      if(!service)return contracts::make_unexpected(service.error());
      owner->service_=std::move(*service);return owner;
    } catch(...) {return contracts::make_unexpected(host::host_error(host::HostErrc::BudgetExceeded));}
  }
  std::shared_ptr<policy::ExecutionAccessSourcePort> observations() const override {return source_;}
  std::shared_ptr<host::ExecutionObservationPort> observation_events() const override {return events_;}
  contracts::SubmitReply submit(std::shared_ptr<InvocationRecordBase> record) override {
    return service_->submit(std::move(record),resolve_);
  }
  contracts::SubmitReply submit_child(std::shared_ptr<InvocationRecordBase> record,
      std::shared_ptr<contracts::ExecutionScopePort> scope) override {
    if(!scope)return contracts::Rejected{contracts::error(contracts::ContractsErrc::InvalidAuthority)};
    return service_->submit(std::move(record),resolve_,std::move(scope));
  }
  contracts::Result<host::ErasedExecutionResult> result(const policy::VerifiedCaller& caller,
      std::shared_ptr<policy::SessionAuthority> session,contracts::ExecutionRef ref,
      contracts::CppTypeToken type) override {
    if(!session)return contracts::make_unexpected(host::host_error(host::HostErrc::InvalidSession));
    auto allowed=session->observations()->get(caller,ref,policy::AccessUse::ReadResult);
    if(!allowed)return contracts::make_unexpected(allowed.error());
    auto value=table_->result_erased(ref,type);if(!value)return contracts::make_unexpected(value.error());
    allowed=session->observations()->get(caller,ref,policy::AccessUse::ReadResult);
    if(!allowed)return contracts::make_unexpected(allowed.error());
    return host::ErasedExecutionResult{*value,allowed->response};
  }
  contracts::Result<host::ExecutionWaitReply> wait(const policy::VerifiedCaller& caller,
      std::shared_ptr<policy::SessionAuthority> session,std::shared_ptr<invocation::TrustedThreadPort> threads,
      contracts::ExecutionRef ref,host::TimePoint deadline,std::stop_token stop) override {
    if(!session||!threads)return contracts::make_unexpected(host::host_error(host::HostErrc::InvalidSession));
    auto waited=ExecutionQueries(table_,std::move(session),std::move(threads)).wait(caller,ref,deadline,stop);
    if(!waited)return contracts::make_unexpected(waited.error());
    auto state=host::ExecutionWaitState::Terminal;
    if(waited->state==ExecutionTable::WaitState::Timeout)state=host::ExecutionWaitState::Timeout;
    else if(waited->state==ExecutionTable::WaitState::Cancelled)state=host::ExecutionWaitState::Cancelled;
    return host::ExecutionWaitReply{state,std::move(waited->observed)};
  }
  contracts::Result<contracts::CancelDisposition> cancel(const policy::VerifiedCaller& caller,
      std::shared_ptr<policy::SessionAuthority> session,contracts::ExecutionRef ref) override {
    return service_->cancel(caller,session,ref);
  }
  contracts::Result<std::unique_ptr<host::ExecutionWaitPort>> prepare_wait(
      std::shared_ptr<const policy::VerifiedCaller> caller,std::shared_ptr<policy::SessionAuthority> session,
      contracts::ExecutionRef ref,host::TimePoint deadline,std::stop_token stop) override {
    return PollingWait::create(table_,std::move(caller),std::move(session),ref,deadline,stop);
  }
  bool in_execution_thread() const noexcept override {return service_->in_execution_thread();}
  void stop_accepting() override {events_->stop_accepting();service_->close();}
  contracts::Result<bool> finish_until(host::TimePoint deadline) override {
    if(in_execution_thread())return contracts::make_unexpected(host::host_error(host::HostErrc::InvalidOwner));
    auto service=service_->shutdown_until(deadline);if(!service)return contracts::make_unexpected(service.error());
    if(!*service)return false;
    auto events=events_->finish_until(deadline);if(!events)return contracts::make_unexpected(events.error());
    return *service&&*events;
  }
  contracts::Result<bool> drain_executors_until(host::TimePoint deadline) override {
    auto result=executor_->shutdown_until(deadline);
    if(result)return true;
    if(result.error().code()==contracts::executor_error(contracts::ExecutorErrc::Timeout).code())return false;
    return contracts::make_unexpected(result.error());
  }
  ExecutionService& service() const noexcept {return *service_;}
  const std::shared_ptr<ExecutionTable>& table() const noexcept {return table_;}
private:
  class PollingWait final : public host::ExecutionWaitPort {
  public:
    static contracts::Result<std::unique_ptr<host::ExecutionWaitPort>> create(
        std::shared_ptr<ExecutionTable> table,std::shared_ptr<const policy::VerifiedCaller> caller,
        std::shared_ptr<policy::SessionAuthority> session,contracts::ExecutionRef ref,
        host::TimePoint deadline,std::stop_token stop) {
      if(!session||!caller)return contracts::make_unexpected(host::host_error(host::HostErrc::InvalidSession));
      auto allowed=session->observations()->get(*caller,ref,policy::AccessUse::Wait);
      if(!allowed)return contracts::make_unexpected(allowed.error());
      auto ticket=table->prepare_wait(ref);if(!ticket)return contracts::make_unexpected(ticket.error());
      try {
        return std::unique_ptr<host::ExecutionWaitPort>(new PollingWait(std::move(table),std::move(caller),
            std::move(session),ref,std::min(deadline,allowed->response->deadline()),stop,std::move(*ticket)));
      } catch(...) {return contracts::make_unexpected(host::host_error(host::HostErrc::BudgetExceeded));}
    }
    contracts::Result<std::optional<host::ExecutionWaitReply>> poll() override {
      if(!ticket_)return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::Rejected));
      auto allowed=session_->observations()->get(*caller_,ref_,policy::AccessUse::Wait);
      if(!allowed){ticket_.reset();return contracts::make_unexpected(allowed.error());}
      deadline_=std::min(deadline_,allowed->response->deadline());
      auto ready=table_->poll_wait(*ticket_,deadline_,stop_);
      if(!ready){ticket_.reset();return contracts::make_unexpected(ready.error());}
      if(!*ready)return std::optional<host::ExecutionWaitReply>{};
      allowed=session_->observations()->get(*caller_,ref_,policy::AccessUse::Wait);
      ticket_.reset();
      if(!allowed)return contracts::make_unexpected(allowed.error());
      auto state=host::ExecutionWaitState::Terminal;
      if(**ready==ExecutionTable::WaitState::Timeout)state=host::ExecutionWaitState::Timeout;
      else if(**ready==ExecutionTable::WaitState::Cancelled)state=host::ExecutionWaitState::Cancelled;
      return std::optional<host::ExecutionWaitReply>{host::ExecutionWaitReply{state,std::move(*allowed)}};
    }
  private:
    PollingWait(std::shared_ptr<ExecutionTable> table,std::shared_ptr<const policy::VerifiedCaller> caller,
        std::shared_ptr<policy::SessionAuthority> session,contracts::ExecutionRef ref,
        host::TimePoint deadline,std::stop_token stop,std::unique_ptr<ExecutionTable::WaitTicket> ticket)
        :table_(std::move(table)),caller_(std::move(caller)),session_(std::move(session)),ref_(ref),
         deadline_(deadline),stop_(stop),ticket_(std::move(ticket)) {}
    std::shared_ptr<ExecutionTable> table_;
    std::shared_ptr<const policy::VerifiedCaller> caller_;
    std::shared_ptr<policy::SessionAuthority> session_;
    contracts::ExecutionRef ref_;
    host::TimePoint deadline_;
    std::stop_token stop_;
    std::unique_ptr<ExecutionTable::WaitTicket> ticket_;
  };
  HostedExecutions(std::shared_ptr<ExecutionTable> table,std::shared_ptr<contracts::ExecutorControlPort> executor)
      :table_(std::move(table)),source_(std::make_shared<ExecutionSource>(table_)),executor_(std::move(executor)) {}
  std::shared_ptr<ExecutionTable> table_;
  std::shared_ptr<ExecutionSource> source_;
  std::shared_ptr<ExecutionObservation> events_;
  std::shared_ptr<contracts::ExecutorControlPort> executor_;
  std::unique_ptr<ExecutionService> service_;
  ManagedInvocation::Resolver resolve_;
};
}
