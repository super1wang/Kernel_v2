#pragma once
// D1.06：普通日志的固定公共数据与同步端口。
#include <ock/contracts/identity.hpp>
#include <array>
#include <optional>
#include <span>

namespace ock::contracts {
inline constexpr foundation::ErrorDomain logging_domain{"ock.logging"};
enum class LogErrc : std::uint32_t {
  InvalidConfiguration=1, BudgetExceeded=2, AllocationFailure=3,
  InvalidPosition=4, Busy=5, BackendFailure=6, Reentrant=7,
  InvalidLogger=8, BackendProtocolFailure=9, InvalidReadPage=10,
  UnsupportedCapability=11
};
inline foundation::Error log_error(LogErrc code) noexcept {
  return foundation::Error{foundation::ErrorCode::make<logging_domain>(static_cast<std::uint32_t>(code))};
}

enum class LogLevel : std::uint8_t { Trace=0, Debug=1, Info=2, Warning=3, Error=4 };
enum class LogComponent : std::uint8_t { Host=0, Registry=1, Policy=2, Invocation=3 };
enum class LogEvent : std::uint16_t {
  Configured=1, Starting=2, ModuleStarted=3, Ready=4, StartFailed=5,
  StopAccepting=6, ModuleStopped=7, LogClosing=8, Diagnostic=9
};
enum class LogKey : std::uint16_t { Status=1, Count=2, Module=3, Detail=4 };
enum class LogValueClass : std::uint8_t { Redacted=0, PublicCode=1, PublicCount=2 };
struct LogField {
  LogKey key;
  LogValueClass visibility{LogValueClass::Redacted};
  std::uint64_t value{};
};
struct LogInput {
  LogLevel level;
  LogComponent component;
  LogEvent event;
  std::span<const LogField> fields;
};
struct LogPosition {
  HostIncarnation host;
  std::uint64_t stream;
  std::uint64_t accepted_sequence;
  bool operator==(const LogPosition&) const = default;
};
enum class LogDecision : std::uint8_t { Accepted=0, Rejected=1, Dropped=2 };
enum class LogReason : std::uint8_t {
  None=0, Filtered=1, InvalidRecord=2, Full=3, Busy=4, Closed=5,
  BackendFailure=6, SequenceExhausted=7, Reentrant=8,
  InvalidLogger=9, BackendProtocolFailure=10
};
struct LogWriteResult {
  LogDecision decision;
  LogReason reason;
  std::optional<LogPosition> accepted;
};
enum class LogOverflow : std::uint8_t { DropOldest=0, RejectNewest=1 };
struct LogLimits {
  std::uint32_t record_capacity{128};
  LogOverflow full{LogOverflow::DropOldest};
  LogLevel minimum_level{LogLevel::Info};
};
enum class LogState : std::uint8_t { Open=0, Closing=1, Closed=2 };
struct LogCounters {
  std::uint64_t accepted, rejected, dropped_before_accept, evicted_after_accept;
  std::uint64_t rejected_invalid, rejected_full, rejected_busy, rejected_closed;
  std::uint64_t rejected_backend, rejected_sequence;
  std::uint64_t dropped_filtered;
  std::uint64_t snapshot_failures, flush_failures, close_failures;
  bool saturated;
};
struct PublicLogRecord {
  LogPosition position;
  LogLevel level;
  std::uint16_t text_size;
  std::array<char,256> text;
};
struct LogSnapshot {
  LogPosition accepted_through;
  std::uint64_t evicted_through, retained_count;
  LogLimits limits;
  LogState state;
  LogCounters counters;
};
struct LogFlushResult {
  LogPosition covered_through;
  std::uint64_t evicted_through;
  bool volatile_only;
};
class LogPort : public PortLifetime {
public:
  virtual LogWriteResult try_write(const LogInput&) = 0;
  virtual foundation::Result<LogSnapshot> snapshot() = 0;
  virtual foundation::Result<LogFlushResult> flush(LogPosition through) = 0;
  virtual foundation::Result<LogFlushResult> close() = 0;
};
}
