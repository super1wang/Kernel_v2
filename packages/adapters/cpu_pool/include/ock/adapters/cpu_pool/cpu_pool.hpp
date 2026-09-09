#pragma once
#include <ock/contracts/executor.hpp>
namespace ock::cpu_pool {
struct Options { std::size_t workers=2, max_inflight=16; };
class Executor final : public contracts::ExecutorControlPort {
public:
  static foundation::Result<std::unique_ptr<Executor>> create(Options = {});
  ~Executor();
  foundation::Result<void> submit(std::unique_ptr<contracts::ReadyWork>) override;
  bool in_worker() const noexcept override;
  foundation::Result<void> drain_until(contracts::ExecutorTime) override;
  foundation::Result<void> shutdown_until(contracts::ExecutorTime) override;
private:
  struct State;
  explicit Executor(std::unique_ptr<State>);
  std::unique_ptr<State> state_;
};
}
