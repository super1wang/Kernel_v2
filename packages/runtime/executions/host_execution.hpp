#pragma once
#include "execution_service.hpp"
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
  contracts::SubmitReply submit(std::shared_ptr<InvocationRecordBase> record) override {
    return service_->submit(std::move(record),resolve_);
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
  bool in_execution_thread() const noexcept override {return service_->in_execution_thread();}
  void stop_accepting() override {service_->close();}
  contracts::Result<bool> finish_until(host::TimePoint deadline) override {return service_->shutdown_until(deadline);}
  contracts::Result<bool> drain_executors_until(host::TimePoint deadline) override {
    auto result=executor_->shutdown_until(deadline);
    if(result)return true;
    if(result.error().code()==contracts::executor_error(contracts::ExecutorErrc::Timeout).code())return false;
    return contracts::make_unexpected(result.error());
  }
  ExecutionService& service() const noexcept {return *service_;}
  const std::shared_ptr<ExecutionTable>& table() const noexcept {return table_;}
private:
  HostedExecutions(std::shared_ptr<ExecutionTable> table,std::shared_ptr<contracts::ExecutorControlPort> executor)
      :table_(std::move(table)),source_(std::make_shared<ExecutionSource>(table_)),executor_(std::move(executor)) {}
  std::shared_ptr<ExecutionTable> table_;
  std::shared_ptr<ExecutionSource> source_;
  std::shared_ptr<contracts::ExecutorControlPort> executor_;
  std::unique_ptr<ExecutionService> service_;
  ManagedInvocation::Resolver resolve_;
};
}
