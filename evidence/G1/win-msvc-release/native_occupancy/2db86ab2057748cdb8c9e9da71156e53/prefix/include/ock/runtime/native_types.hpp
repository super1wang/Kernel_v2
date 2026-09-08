#pragma once
#include <ock/runtime/policy.hpp>

namespace ock::runtime::invocation {
using namespace contracts;
enum class InvocationErrc : std::uint32_t {
  InvalidBinding = 1, InvalidInput, InvalidOutput, ProviderUnavailable,
  ResourceUnavailable, ThreadRejected, SubmitRequired, Cancelled, Expired,
  BudgetExceeded, Busy, HandlerException, ContractMismatch
};
inline constexpr foundation::ErrorDomain invocation_domain{"ock.invocation"};
inline Error invocation_error(InvocationErrc code) noexcept {
  return Error{foundation::ErrorCode::make<invocation_domain>(static_cast<std::uint32_t>(code))};
}
enum class ThreadRole { Application, Worker, Control, Domain, Database };
struct ThreadObservation { ThreadRole role; Name affinity; bool inline_allowed; };
class TrustedThreadPort : public PortLifetime {
public:
  virtual Result<ThreadObservation> current() const noexcept = 0;
};
struct NativeBudget {
  std::size_t bindings = 128, targets_per_binding = 16,
              resources_per_binding = 16, concurrent_calls_per_binding = 1,
              observation_capacity = 256;
  std::uint64_t work_units = 1024, observation_counter_limit =
      (std::numeric_limits<std::uint64_t>::max)();
};
struct InvokeOptions {
  std::stop_token stop;
  policy::TimePoint deadline;
  std::uint64_t work_limit;
};
template <class A>
using TargetProjection = Result<std::size_t> (*)(const A&,
    std::span<foundation::ObjectId>) noexcept;
enum class InvocationRecordKind { Rejected, ReadCompleted, FailedBeforeApply };
struct InvocationRecord {
  std::uint64_t sequence;
  Name static_trace_label;
  InvocationRecordKind kind;
  std::optional<foundation::ErrorCode> error;
};
struct InvocationSnapshot {
  std::size_t written = 0;
  std::uint64_t dropped = 0;
  bool dropped_saturated = false, sequence_exhausted = false;
};

} // namespace ock::runtime::invocation
