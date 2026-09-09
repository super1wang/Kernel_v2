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
      ExecutionService::Options options={}) {
    if(!executor||executor.use_count()==0)return contracts::make_unexpected(host::host_error(host::HostErrc::InvalidOwner));
    try {
      auto table=ExecutionTable::create(limits,host);if(!table)return contracts::make_unexpected(table.error());
      auto owner=std::shared_ptr<HostedExecutions>(new HostedExecutions(*table,executor));
      auto service=ExecutionService::create(*table,executor,std::move(resources),std::move(subjects),scheduling,options);
      if(!service)return contracts::make_unexpected(service.error());
      owner->service_=std::move(*service);return owner;
    } catch(...) {return contracts::make_unexpected(host::host_error(host::HostErrc::BudgetExceeded));}
  }
  std::shared_ptr<policy::ExecutionAccessSourcePort> observations() const override {return source_;}
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
};
}
