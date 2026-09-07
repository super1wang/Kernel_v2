#pragma once
// D1.05 内部 Native 调用；不安装为 OCK::Runtime SDK。
#include <array>
#include <atomic>
#include <ock/contracts/operation.hpp>
#include "packages/runtime/policy/policy.hpp"
#include "packages/runtime/registry/registry.hpp"
#include "packages/runtime/invocation/private_bridge.hpp"

namespace ock::runtime::invocation {
using namespace contracts;

enum class InvocationErrc : std::uint32_t {
  InvalidBinding = 1, InvalidInput, InvalidOutput, ProviderUnavailable,
  ResourceUnavailable, ThreadRejected, SubmitRequired, Cancelled, Expired,
  BudgetExceeded, Busy, HandlerException, ContractMismatch
};
inline constexpr foundation::ErrorDomain invocation_domain{"ock.invocation"};
inline Error invocation_error(InvocationErrc code) noexcept {
  return Error{foundation::ErrorCode::make<invocation_domain>(
      static_cast<std::uint32_t>(code))};
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
              observation_capacity = 64;
  std::uint64_t work_units = 1024, observation_counter_limit =
      (std::numeric_limits<std::uint64_t>::max)();
};
struct InvokeOptions {
  std::stop_token stop;
  policy::TimePoint deadline;
  std::uint64_t work_limit;
};
template <class A>
using TargetProjection = Result<std::size_t> (*)(const A &,
    std::span<foundation::ObjectId>) noexcept;
enum class InvocationRecordKind { Rejected, ReadCompleted, FailedBeforeApply };
struct InvocationRecord { Name trace; std::uint64_t sequence; InvocationRecordKind kind; foundation::ErrorCode error; };
struct InvocationSnapshot { std::size_t written = 0; std::uint64_t dropped = 0; bool dropped_saturated = false, sequence_exhausted = false; };

namespace detail {
struct BoundState {
  explicit BoundState(Name trace_label) : trace(trace_label) {}
  std::shared_ptr<const registry::Catalog> catalog;
  std::shared_ptr<policy::SessionAuthority> session;
  std::shared_ptr<const policy::VerifiedCaller> caller;
  std::shared_ptr<TrustedThreadPort> thread;
  std::optional<NativeEntry> entry;
  std::vector<foundation::ObjectId> targets;
  const void *projection = nullptr;
  std::shared_ptr<const KnownFacts> empty_facts;
  NativeBudget budget;
  Name trace;
  void *observer = nullptr;
  void (*record)(void *, InvocationRecordKind, foundation::ErrorCode, Name) noexcept = nullptr;
  std::atomic<std::size_t> active{0};
};
enum class ErasedKind { Rejected, Completed, Failed };
struct ErasedInvocation { ErasedKind kind; Error error; };
Result<ErasedInvocation> invoke_bound(BoundState &, const void *, CppTypeToken,
                                      CppTypeToken, void *,
                                      const InvokeOptions &) noexcept;
template <class R> Result<Outcome<R>> outcome_read(
    Result<R> result, std::shared_ptr<const KnownFacts> facts) {
  class NoPublication final : public PublicationAuthorityPort {
  public:
    Result<std::shared_ptr<const PublicationProof>> attest(const PublishedCommit &) override {
      return make_unexpected(invocation_error(InvocationErrc::InvalidOutput));
    }
    Result<void> validate(const PublicationProof &, const PublishedCommit &) const override {
      return make_unexpected(invocation_error(InvocationErrc::InvalidOutput));
    }
  };
  static NoPublication publication;
  if (!facts) return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  OutcomeConditions conditions{BeforeApplyDecision{true, ApplyDecision::NotReached, true}, {},
                               {RequiredRecordState::NotRequired, 0, 0}};
  OutcomeValidation validation{publication, {}, {0, 0, 0}};
  return Outcome<R>::validate(ReadCompleted<R>{std::move(result), ResultScope::ReadOnly},
                              std::move(facts), EvidenceState::Volatile, conditions, validation);
}
template <class R> Result<Outcome<R>> outcome_failure(
    Error error, std::shared_ptr<const KnownFacts> facts) {
  class NoPublication final : public PublicationAuthorityPort {
  public:
    Result<std::shared_ptr<const PublicationProof>> attest(const PublishedCommit &) override {
      return make_unexpected(invocation_error(InvocationErrc::InvalidOutput));
    }
    Result<void> validate(const PublicationProof &, const PublishedCommit &) const override {
      return make_unexpected(invocation_error(InvocationErrc::InvalidOutput));
    }
  };
  static NoPublication publication;
  if (!facts) return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  OutcomeConditions conditions{BeforeApplyDecision{true, ApplyDecision::NotReached, true}, {},
                               {RequiredRecordState::NotRequired, 0, 0}};
  OutcomeValidation validation{publication, {}, {0, 0, 0}};
  return Outcome<R>::validate(FailedBeforeApply{*Name::parse("native"), error}, std::move(facts),
                              EvidenceState::Volatile, conditions, validation);
}
}

class NativeEngine;
template <ContractValue A, ContractResult R> class NativeBound final {
public:
  NativeBound(NativeBound &&) noexcept = default;
  NativeBound &operator=(NativeBound &&) noexcept = default;
  NativeBound(const NativeBound &) = delete;
  NativeBound &operator=(const NativeBound &) = delete;
  ~NativeBound() = default;
  InvokeReply<R> invoke(const A &args, const InvokeOptions &options) const {
    if (!state_) return Rejected{invocation_error(InvocationErrc::InvalidBinding)};
    auto valid = TypeContract<A>::validate(args);
    if (!valid) return Rejected{valid.error()};
    if (!state_->projection || state_->targets.size() > 16)
      return Rejected{invocation_error(InvocationErrc::InvalidBinding)};
    std::array<foundation::ObjectId, 16> actual{};
    auto project = reinterpret_cast<TargetProjection<A>>(const_cast<void *>(state_->projection));
    auto target_count = project(args, actual);
    if (!target_count || *target_count != state_->targets.size() ||
        !std::equal(state_->targets.begin(), state_->targets.end(), actual.begin()))
      return Rejected{target_count ? invocation_error(InvocationErrc::InvalidInput)
                                   : target_count.error()};
    std::optional<Result<R>> result;
    auto invoked = detail::invoke_bound(*state_, &args, CppTypeToken::of<A>(),
                                        CppTypeToken::of<R>(), &result, options);
    if (!invoked || invoked->kind == detail::ErasedKind::Rejected)
      return Rejected{invoked ? invoked->error : invoked.error()};
    if (invoked->kind == detail::ErasedKind::Failed || !result || !*result) {
      auto failed = detail::outcome_failure<R>(invoked->error, state_->empty_facts);
      return failed ? InvokeReply<R>{Completed<R>{std::move(*failed)}}
                    : InvokeReply<R>{Rejected{failed.error()}};
    }
    auto output = TypeContract<R>::validate(**result);
    if (!output) {
      auto failed = detail::outcome_failure<R>(output.error(), state_->empty_facts);
      return failed ? InvokeReply<R>{Completed<R>{std::move(*failed)}}
                    : InvokeReply<R>{Rejected{failed.error()}};
    }
    auto completed = detail::outcome_read<R>(std::move(*result), state_->empty_facts);
    return completed ? InvokeReply<R>{Completed<R>{std::move(*completed)}}
                     : InvokeReply<R>{Rejected{completed.error()}};
  }
private:
  friend class NativeEngine;
  NativeBound(std::shared_ptr<detail::BoundState> state, Name trace)
      : state_(std::move(state)), trace_(trace) {}
  std::shared_ptr<detail::BoundState> state_;
  Name trace_;
};

class NativeEngine final {
public:
  static Result<std::shared_ptr<NativeEngine>> create(
      std::shared_ptr<const registry::Catalog>, std::shared_ptr<policy::SessionAuthority>,
      std::shared_ptr<TrustedThreadPort>, NativeBudget);
  template <ContractValue A, ContractResult R>
  Result<NativeBound<A, R>> bind(const OperationKey &key, ContractDigest digest,
                                  Shape shape,
                                  std::shared_ptr<const policy::VerifiedCaller> caller,
                                  std::span<const foundation::ObjectId> targets,
                                  TargetProjection<A> projection, Name trace) {
    return bind_impl<A, R>(key, digest, shape, std::move(caller), targets,
                           reinterpret_cast<const void *>(projection), trace);
  }
  InvocationSnapshot snapshot(std::span<InvocationRecord>) const noexcept;
private:
  struct State;
  static void record_observation(void *, InvocationRecordKind,
                                 foundation::ErrorCode, Name) noexcept;
  explicit NativeEngine(std::shared_ptr<State> state) : state_(std::move(state)) {}
  template <ContractValue A, ContractResult R>
  Result<NativeBound<A, R>> bind_impl(const OperationKey &, ContractDigest, Shape,
                                      std::shared_ptr<const policy::VerifiedCaller>,
                                      std::span<const foundation::ObjectId>, const void *, Name);
  Result<std::shared_ptr<detail::BoundState>> bind_erased(
      const OperationKey &, ContractDigest, Shape,
      std::shared_ptr<const policy::VerifiedCaller>,
      std::span<const foundation::ObjectId>, const void *, Name,
      CppTypeToken, CppTypeToken);
  std::shared_ptr<State> state_;
};

template <ContractValue A, ContractResult R>
Result<NativeBound<A, R>> NativeEngine::bind_impl(
    const OperationKey &key, ContractDigest digest, Shape shape,
    std::shared_ptr<const policy::VerifiedCaller> caller,
    std::span<const foundation::ObjectId> targets, const void *projection,
    Name trace) {
  auto state = bind_erased(key, digest, shape, std::move(caller), targets,
                            projection, trace, CppTypeToken::of<A>(),
                            CppTypeToken::of<R>());
  if (!state) return make_unexpected(state.error());
  return NativeBound<A, R>{std::move(*state), trace};
}
} // namespace ock::runtime::invocation
