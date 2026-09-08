#pragma once
// D1.06：有限内存管理与隔离 facade。
#include <ock/contracts/logging.hpp>
#include <memory>

namespace ock::runtime::observability {
struct FacadeCounters {
  std::uint64_t write_attempts, accepted, rejected, dropped;
  std::uint64_t write_backend_exceptions, write_protocol_failures, write_reentrant;
  std::uint64_t acceptance_unknown;
  std::uint64_t snapshot_attempts, snapshot_failures, snapshot_backend_exceptions;
  std::uint64_t snapshot_protocol_failures, snapshot_reentrant;
  std::uint64_t flush_attempts, flush_failures, flush_backend_exceptions;
  std::uint64_t flush_protocol_failures, flush_reentrant;
  std::uint64_t close_attempts, close_failures, close_backend_exceptions;
  std::uint64_t close_protocol_failures, close_reentrant;
  bool saturated;
};
class SafeLogger final {
public:
  static foundation::Result<SafeLogger> create(std::shared_ptr<contracts::LogPort>);
  SafeLogger(const SafeLogger&) noexcept;
  SafeLogger& operator=(const SafeLogger&) noexcept;
  SafeLogger(SafeLogger&&) noexcept;
  SafeLogger& operator=(SafeLogger&&) noexcept;
  ~SafeLogger();
  contracts::LogWriteResult try_write(const contracts::LogInput&) noexcept;
  foundation::Result<contracts::LogSnapshot> snapshot() noexcept;
  foundation::Result<contracts::LogFlushResult> flush(contracts::LogPosition) noexcept;
  foundation::Result<contracts::LogFlushResult> close() noexcept;
  foundation::Result<FacadeCounters> counters() const noexcept;
private:
  struct State;
  explicit SafeLogger(std::shared_ptr<State>) noexcept;
  std::shared_ptr<State> state_;
};
}

namespace ock::runtime::observability {
struct LogReadPage {
  std::uint32_t copied;
  contracts::LogPosition next, accepted_upper;
  std::uint64_t evicted_through;
  bool gap;
};
class MemoryDiagnostics : public contracts::PortLifetime {
public:
  virtual foundation::Result<LogReadPage>
  copy_records(contracts::LogPosition after,
               std::span<contracts::PublicLogRecord> out) = 0;
};
struct MemoryLoggingBundle {
  std::shared_ptr<contracts::LogPort> writer;
  std::shared_ptr<MemoryDiagnostics> diagnostics;
};
foundation::Result<MemoryLoggingBundle>
make_memory_logging(contracts::HostIncarnation host, std::uint64_t stream,
                    contracts::LogLimits limits = {});
}
