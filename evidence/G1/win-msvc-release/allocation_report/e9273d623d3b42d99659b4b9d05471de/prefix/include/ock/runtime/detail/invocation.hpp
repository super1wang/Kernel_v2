#pragma once
// NativeSubset 模板实现；应用通过 host.hpp 使用。
#include <atomic>
#include <ock/runtime/native_types.hpp>
#include <ock/contracts/operation.hpp>
#include <ock/runtime/policy.hpp>
#include <ock/runtime/detail/private_bridge.hpp>

namespace ock::runtime::invocation {
using namespace contracts;

namespace detail {
// 锁定 Result 后端的移动辅助函数为 noexcept；绑定前拒绝可能越过该边界的结果。
template<class R> inline constexpr bool transport_safe =
    // tl::expected<void> 的异常说明保守继承 void trait；无值分支只移动 Error。
    (std::is_void_v<R> && std::is_nothrow_move_constructible_v<Error>) ||
    (std::is_nothrow_move_constructible_v<R> &&
    std::is_nothrow_move_constructible_v<Result<R>> &&
    std::is_nothrow_move_constructible_v<Outcome<R>> &&
    std::is_nothrow_move_constructible_v<Result<Outcome<R>>> &&
    std::is_nothrow_move_constructible_v<InvokeReply<R>>);
struct EngineState;
struct CallSlot {
  std::atomic<bool> occupied{false};
  std::vector<foundation::ObjectId> targets;
};
struct BoundState {
  explicit BoundState(Name label) : trace(label) {}
  ~BoundState();
  std::shared_ptr<EngineState> engine;
  std::shared_ptr<const registry::Catalog> catalog;
  std::shared_ptr<policy::SessionAuthority> session;
  std::shared_ptr<const policy::InlineAuthorization> authorization;
  std::shared_ptr<TrustedThreadPort> thread;
  std::optional<NativeEntry> entry;
  std::vector<foundation::ObjectId> targets;
  std::shared_ptr<const KnownFacts> empty_facts;
  NativeBudget budget;
  Name trace;
  std::unique_ptr<CallSlot[]> slots;
  bool counted = false;
  std::optional<std::size_t> acquire() noexcept;
  void observe(InvocationRecordKind, std::optional<foundation::ErrorCode>) const noexcept;
};
struct CallLease {
  CallLease(std::shared_ptr<BoundState> state,std::size_t index) : owner(std::move(state)),slot(index) {}
  CallLease(const CallLease&)=delete;
  CallLease& operator=(const CallLease&)=delete;
  std::shared_ptr<BoundState> owner;
  std::size_t slot;
  ~CallLease() { owner->slots[slot].occupied.store(false, std::memory_order_release); }
};
struct FinalObservation {
  const BoundState& state;
  InvocationRecordKind kind=InvocationRecordKind::Rejected;
  std::optional<foundation::ErrorCode> error;
  ~FinalObservation() {state.observe(kind,error);}
};
bool same_targets(std::span<const foundation::ObjectId>,
                  std::span<const foundation::ObjectId>) noexcept;
Result<void> check_thread(const BoundState&) noexcept;
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
} // namespace detail

class NativeEngine;
template <ContractValue A, ContractResult R> class NativeBound final {
public:
  NativeBound(NativeBound&&) noexcept = default;
  NativeBound& operator=(NativeBound&&) noexcept = default;
  NativeBound(const NativeBound&) = delete;
  NativeBound& operator=(const NativeBound&) = delete;
  ~NativeBound() = default;

  InvokeReply<R> invoke(const A& args, const InvokeOptions& options) const {
    // 本次栈持有绑定，业务释放外部 Engine/Bound 后治理材料仍活到返回。
    auto state = state_;
    const auto projection = projection_;
    if (!state) return Rejected{invocation_error(InvocationErrc::InvalidBinding)};
    std::optional<detail::CallLease> lease;
    std::optional<policy::InlineAdmission> admission;
    detail::FinalObservation observation{*state};
    const auto rejected = [&](Error error) -> InvokeReply<R> {
      observation.kind=InvocationRecordKind::Rejected;observation.error=error.code();
      return Rejected{std::move(error)};
    };
    const auto failed = [&](Error error) -> InvokeReply<R> {
      auto outcome = detail::outcome_failure<R>(error, state->empty_facts);
      // 失败分支没有 R，不会再次执行可能抛出的 R 验证器。
      foundation::invariant(bool(outcome));
      observation.kind=InvocationRecordKind::FailedBeforeApply;observation.error=error.code();
      return Completed<R>{std::move(*outcome)};
    };
    auto slot = state->acquire();
    if (!slot) return rejected(invocation_error(InvocationErrc::Busy));
    lease.emplace(state,*slot);
    bool entered = false;
    try {
      auto current = NativeAccess::check(*state->catalog, *state->entry,
                                         CppTypeToken::of<A>(), CppTypeToken::of<R>());
      if (!current) return rejected(current.error());
      if (state->entry->shape != Shape::Read)
        return rejected(invocation_error(InvocationErrc::ProviderUnavailable));
      if (state->entry->resource_count)
        return rejected(invocation_error(InvocationErrc::ResourceUnavailable));
      auto thread = detail::check_thread(*state);
      if (!thread) return rejected(thread.error());
      auto valid = TypeContract<A>::validate(args);
      if (!valid) return rejected(valid.error());
      auto& actual = state->slots[*slot].targets;
      auto count = projection(args, actual);
      if (!count) return rejected(count.error());
      if (*count > actual.size() ||
          !detail::same_targets(state->targets, std::span(actual).first(*count)))
        return rejected(invocation_error(InvocationErrc::InvalidInput));
      if (!options.work_limit || options.work_limit > state->budget.work_units)
        return rejected(invocation_error(InvocationErrc::BudgetExceeded));
      if (options.stop.stop_requested())
        return rejected(invocation_error(InvocationErrc::Cancelled));
      auto admitted = state->authorization->admit(*state->entry->definition,
          std::span(actual).first(*count), options.stop, options.deadline);
      if (!admitted) return rejected(admitted.error());
      admission.emplace(std::move(*admitted));
      auto budget = foundation::CheckedCount<std::uint64_t>::create(0, options.work_limit);
      if (!budget) return rejected(budget.error());
      WorkContext work(options.stop, admission->deadline(), *budget, state->trace,
                       BorrowedResourceViews{std::span<const ResourceLease* const>{}});
      std::optional<Result<R>> result;
      entered = true;
      NativeAccess::dispatch(*state->catalog, *state->entry, &args, work, &result);
      if (!result) return failed(invocation_error(InvocationErrc::InvalidOutput));
      if (!*result) return failed(result->error());
      auto completed = detail::outcome_read<R>(std::move(*result), state->empty_facts);
      if (!completed) return failed(completed.error());
      InvokeReply<R> reply{Completed<R>{std::move(*completed)}};
      observation.kind=InvocationRecordKind::ReadCompleted;observation.error.reset();
      return reply;
    } catch (const std::bad_alloc&) {
      auto error = invocation_error(InvocationErrc::BudgetExceeded);
      return entered ? failed(error) : rejected(error);
    } catch (...) {
      auto error = invocation_error(InvocationErrc::HandlerException);
      return entered ? failed(error) : rejected(error);
    }
  }
private:
  friend class NativeEngine;
  NativeBound(std::shared_ptr<detail::BoundState> state, TargetProjection<A> projection)
      : state_(std::move(state)), projection_(projection) {}
  std::shared_ptr<detail::BoundState> state_;
  TargetProjection<A> projection_;
};
class NativeEngine final {
public:
  static Result<std::shared_ptr<NativeEngine>> create(
      std::shared_ptr<const registry::Catalog>, std::shared_ptr<policy::SessionAuthority>,
      std::shared_ptr<TrustedThreadPort>, NativeBudget);
  template <ContractValue A, ContractResult R>
  Result<NativeBound<A,R>> bind(const OperationKey& key, ContractDigest digest, Shape shape,
      std::shared_ptr<const policy::VerifiedCaller> caller,
      std::span<const foundation::ObjectId> targets, TargetProjection<A> projection, Name trace) {
    if constexpr (!detail::transport_safe<R>) {
      return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
    } else {
    if (!projection) return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
    try {
      // 使用原 typed bind 冻结 TypeIdentity；调用期只核对不可变快照和 token。
      auto catalog = catalog_owner();
      auto typed = BoundOperation<A,R>::bind(catalog, key, digest, shape);
      if (!typed) return make_unexpected(typed.error());
      auto state = bind_erased(key,digest,shape,std::move(caller),targets,trace,
                               CppTypeToken::of<A>(),CppTypeToken::of<R>());
      if (!state) return make_unexpected(state.error());
      return NativeBound<A,R>{std::move(*state),projection};
    } catch (const std::bad_alloc&) {
      return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
    } catch (...) {
      return make_unexpected(invocation_error(InvocationErrc::ContractMismatch));
    }
    }
  }
  InvocationSnapshot snapshot(std::span<InvocationRecord>) const noexcept;
private:
  explicit NativeEngine(std::shared_ptr<detail::EngineState> state) : state_(std::move(state)) {}
  std::shared_ptr<const registry::Catalog> catalog_owner() const noexcept;
  Result<std::shared_ptr<detail::BoundState>> bind_erased(
      const OperationKey&, ContractDigest, Shape, std::shared_ptr<const policy::VerifiedCaller>,
      std::span<const foundation::ObjectId>, Name, CppTypeToken, CppTypeToken);
  std::shared_ptr<detail::EngineState> state_;
};
} // namespace ock::runtime::invocation
