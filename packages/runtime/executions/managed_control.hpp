#pragma once
#include <ock/contracts/outcome.hpp>
#include <chrono>
namespace ock::runtime::executions::detail {
class ManagedControl {
public:
  virtual ~ManagedControl()=default;
  virtual void drive()=0;
  virtual contracts::Result<contracts::CancelDisposition> cancel()=0;
  virtual bool pending() const noexcept=0;
  virtual bool finished() const noexcept=0;
  virtual contracts::ExecutionRef execution() const noexcept=0;
  virtual std::chrono::steady_clock::time_point deadline() const noexcept=0;
};
contracts::Result<contracts::ExecutionRef> new_execution_identity() noexcept;
}
