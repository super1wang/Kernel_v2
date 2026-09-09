#pragma once
// NativeSubset 模板实现；应用通过 host.hpp 使用。
#include <algorithm>
#include <atomic>
#include <ock/runtime/native_types.hpp>
#include <ock/contracts/operation.hpp>
#include <ock/runtime/policy.hpp>
#include <ock/runtime/detail/private_bridge.hpp>

namespace ock::runtime::host {
template<contracts::ContractValue A, contracts::ContractResult R> class HostBound;
}
namespace ock::runtime::executions::detail {
class InvocationAccess;
template<contracts::AsyncInput A, contracts::ContractResult R>
  requires (std::same_as<R,void> || contracts::AsyncInput<R>)
class InvocationRecord;
}

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
  std::shared_ptr<const policy::VerifiedCaller> caller;
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
Result<void> check_managed_thread(const BoundState&) noexcept;

// Submit 准入与开始时复用参数、目标、预算及当前授权管线。
template <ContractValue A>
Result<policy::InlineAdmission> validate_call(const BoundState& state,
    std::size_t slot, TargetProjection<A> projection, const A& args,
    const InvokeOptions& options) {
  auto valid = TypeContract<A>::validate(args);
  if (!valid) return make_unexpected(valid.error());
  auto& actual = state.slots[slot].targets;
  auto count = projection(args, actual);
  if (!count) return make_unexpected(count.error());
  if (*count > actual.size() ||
      !same_targets(state.targets, std::span(actual).first(*count)))
    return make_unexpected(invocation_error(InvocationErrc::InvalidInput));
  if (!options.work_limit || options.work_limit > state.budget.work_units)
    return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
  if (options.stop.stop_requested())
    return make_unexpected(invocation_error(InvocationErrc::Cancelled));
  if (!state.authorization)
    return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
  return state.authorization->admit(*state.entry->definition,
      std::span(actual).first(*count), options.stop, options.deadline);
}
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
    return run(args, options, false, {});
  }
private:
  friend class executions::detail::InvocationAccess;
  template<contracts::AsyncInput A2, contracts::ContractResult R2>
    requires (std::same_as<R2,void> || contracts::AsyncInput<R2>)
  friend class executions::detail::InvocationRecord;
  Result<void> prepare_submission(const A& args, const InvokeOptions& options,
                                 std::optional<std::size_t> reserved={}) const {
    auto state=state_;
    if (!state) return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
    auto slot=reserved ? reserved : state->acquire();
    if (!slot) return make_unexpected(invocation_error(InvocationErrc::Busy));
    std::optional<detail::CallLease> lease;
    if(!reserved) lease.emplace(state,*slot);
    try {
      auto current=NativeAccess::check(*state->catalog,*state->entry,
                                      CppTypeToken::of<A>(),CppTypeToken::of<R>());
      if (!current) return current;
      if (state->entry->shape!=Shape::Read || state->entry->execution.requires_external_wait)
        return make_unexpected(invocation_error(InvocationErrc::ProviderUnavailable));
      auto admission=detail::validate_call(*state,*slot,projection_,args,options);
      if (!admission) return make_unexpected(admission.error());
      return {};
    } catch (const std::bad_alloc&) {
      return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
    } catch (...) {
      return make_unexpected(invocation_error(InvocationErrc::HandlerException));
    }
  }
  InvokeReply<R> run(const A& args, const InvokeOptions& options, bool managed,
                    std::span<const ResourceLease* const> resources,
                    std::optional<std::size_t> reserved={},
                    std::shared_ptr<ExecutionScopePort> execution={}) const {
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
    auto slot = reserved ? reserved : state->acquire();
    if (!slot) return rejected(invocation_error(InvocationErrc::Busy));
    if(!reserved) lease.emplace(state,*slot);
    bool entered = false;
    try {
      auto current = NativeAccess::check(*state->catalog, *state->entry,
                                         CppTypeToken::of<A>(), CppTypeToken::of<R>());
      if (!current) return rejected(current.error());
      if (state->entry->shape != Shape::Read)
        return rejected(invocation_error(InvocationErrc::ProviderUnavailable));
      if (!managed && !state->entry->resources.empty())
        return rejected(invocation_error(InvocationErrc::ResourceUnavailable));
      if (managed && (state->entry->resources.empty()!=resources.empty() ||
          std::any_of(resources.begin(),resources.end(),[](auto p){return p==nullptr;})))
        return rejected(invocation_error(InvocationErrc::ResourceUnavailable));
      auto thread = managed ? detail::check_managed_thread(*state) : detail::check_thread(*state);
      if (!thread) return rejected(thread.error());
      auto admitted = detail::validate_call(*state,*slot,projection,args,options);
      if (!admitted) return rejected(admitted.error());
      admission.emplace(std::move(*admitted));
      auto budget = foundation::CheckedCount<std::uint64_t>::create(0, options.work_limit);
      if (!budget) return rejected(budget.error());
      WorkContext work(options.stop, admission->deadline(), *budget, state->trace,
                       BorrowedResourceViews{resources},std::move(execution));
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

namespace ock::runtime::executions::detail {

struct InvocationCompletion {
  std::array<contracts::FactSummary,8> facts{};
  std::size_t count=0;
  contracts::EvidenceState evidence=contracts::EvidenceState::Volatile;
  std::optional<contracts::Error> fault;
};
// 拥有型 typed 记录的窄执行接口；创建权保留在 HostBound 与内部适配器。
class InvocationRecordBase {
public:
  virtual ~InvocationRecordBase()=default;
  virtual bool run_once(std::stop_token,std::span<const contracts::ResourceLease* const>,
                        std::shared_ptr<contracts::ExecutionScopePort> = {})=0;
  virtual bool reject_before_start(contracts::Error)=0;
  virtual const void* reply_pointer() const noexcept=0;
  virtual contracts::CppTypeToken result_type() const noexcept=0;
  virtual InvocationCompletion completion() const=0;
  virtual std::size_t input_bytes() const noexcept=0;
  virtual std::size_t reserved_reply_bytes() const noexcept=0;
  virtual invocation::InvokeOptions options() const noexcept=0;
  virtual std::shared_ptr<const invocation::detail::BoundState> material() const noexcept=0;
};

// 由可信 typed 注册适配器提供；必须计入可变缓冲区，不能仅返回 sizeof(T)。
template <contracts::AsyncInput A, contracts::ContractResult R>
  requires (std::same_as<R,void> || contracts::AsyncInput<R>)
using InvocationStoragePolicy = registry::SubmissionStorage<A,R>;

// Execution 表将持有此记录。创建时拥有参数、预留调用槽和最小结果存储；
// run_once 只能由赢得 Scheduler start claim 的工作调用，完成后读取不再变动。
template <contracts::AsyncInput A, contracts::ContractResult R>
  requires (std::same_as<R,void> || contracts::AsyncInput<R>)
class InvocationRecord final : public InvocationRecordBase, public std::enable_shared_from_this<InvocationRecord<A,R>> {
public:
  using Policy=InvocationStoragePolicy<A,R>;
private:
  friend class InvocationAccess;
  template<contracts::ContractValue A2,contracts::ContractResult R2> friend class host::HostBound;
  static contracts::Result<std::shared_ptr<InvocationRecord>> create_registered(
      const invocation::NativeBound<A,R>& bound,A args,invocation::InvokeOptions options) {
    auto state=bound.state_;
    if(!state || !state->entry->submission_storage ||
       state->entry->submission_storage_type!=contracts::CppTypeToken::of<Policy>())
      return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::InvalidBinding));
    return create(bound,std::move(args),options,
        *std::static_pointer_cast<const Policy>(state->entry->submission_storage));
  }
  static contracts::Result<std::shared_ptr<InvocationRecord>> create(
      const invocation::NativeBound<A,R>& bound,A args,
      invocation::InvokeOptions options,Policy policy) {
    if(!policy.input_limit || !policy.reply_limit || !policy.input_bytes || !policy.reply_bytes)
      return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::InvalidBinding));
    try {
      auto record=std::shared_ptr<InvocationRecord>(new InvocationRecord(
          invocation::NativeBound<A,R>(bound.state_,bound.projection_),std::move(args),options,policy));
      auto state=record->bound_.state_;
      if(!state) return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::InvalidBinding));
      auto slot=state->acquire();
      if(!slot) return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::Busy));
      std::unique_ptr<invocation::detail::CallLease> reserved;
      try {reserved=std::make_unique<invocation::detail::CallLease>(state,*slot);}
      catch(...) {state->slots[*slot].occupied.store(false,std::memory_order_release);throw;}
      auto prepared=record->bound_.prepare_submission(*record->input_,options,*slot);
      if(!prepared)return contracts::make_unexpected(prepared.error());
      record->call_=std::move(reserved);
      auto bytes=policy.input_bytes(*record->input_);
      if(!bytes) return contracts::make_unexpected(bytes.error());
      if(*bytes>policy.input_limit)
        return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded));
      record->input_bytes_=*bytes;
      return record;
    } catch(...) {
      return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded));
    }
  }
public:
  bool run_once(std::stop_token stop,std::span<const contracts::ResourceLease* const> resources={},
                std::shared_ptr<contracts::ExecutionScopePort> execution={}) override {
    auto keep_alive=this->shared_from_this();
    if(claimed_.exchange(true,std::memory_order_acq_rel)) return false;
    const auto request=[&]{combined_.request_stop();};
    std::stop_callback original(options_.stop,request);
    std::stop_callback managed(stop,request);
    auto options=options_;options.stop=combined_.get_token();
    auto reply=bound_.run(*input_,options,true,resources,call_->slot,std::move(execution));
    store(std::move(reply));
    return true;
  }
  // 仅用于 Scheduler 已确认未开始的退役/拒绝；不能代替 start/cancel 权威仲裁。
  // 一次性 claim 只防御重复交付，取消请求本身不得调用此方法。
  bool reject_before_start(contracts::Error reason) {
    auto keep_alive=this->shared_from_this();
    if(claimed_.exchange(true,std::memory_order_acq_rel)) return false;
    store(contracts::Rejected{contracts::Error{reason.code()}});
    return true;
  }
  const contracts::InvokeReply<R>* reply() const noexcept {
    return completed_.load(std::memory_order_acquire) ? &*reply_ : nullptr;
  }
  std::size_t input_bytes() const noexcept {return input_bytes_;}
  std::size_t reserved_reply_bytes() const noexcept {return policy_.reply_limit;}
  std::size_t reply_bytes() const noexcept {
    return completed_.load(std::memory_order_acquire) ? reply_bytes_ : 0;
  }
  std::shared_ptr<const invocation::detail::BoundState> material() const noexcept override {return bound_.state_;}
  invocation::InvokeOptions options() const noexcept override {return options_;}
  const void* reply_pointer() const noexcept override {return reply();}
  contracts::CppTypeToken result_type() const noexcept override {return contracts::CppTypeToken::of<R>();}
  InvocationCompletion completion() const override {
    InvocationCompletion out;
    auto value=reply();if(!value)return out;
    if(const auto* rejected=std::get_if<contracts::Rejected>(value))out.fault=contracts::Error{rejected->reason.code()};
    if(const auto* completed=std::get_if<contracts::Completed<R>>(value)) {
      const auto& outcome=completed->outcome;out.evidence=outcome.evidence();
      for(const auto& fact:outcome.facts().values()) {
        foundation::invariant(out.count<out.facts.size());
        out.facts[out.count++]=contracts::summarize_fact(fact);
      }
      std::visit([&](const auto& result) {
        using V=std::decay_t<decltype(result)>;
        if constexpr(std::same_as<V,contracts::ReadCompleted<R>>) {
          if(!result.result)out.fault=contracts::Error{result.result.error().code()};
        } else if constexpr(std::same_as<V,contracts::FailedBeforeApply>||std::same_as<V,contracts::CancelledBeforeApply>)
          out.fault=contracts::Error{result.reason.code()};
      },outcome.value());
    }
    return out;
  }
private:
  InvocationRecord(invocation::NativeBound<A,R> bound,A args,
      invocation::InvokeOptions options,Policy policy)
      :bound_(std::move(bound)),input_(std::move(args)),options_(options),policy_(policy) {}
  void store(contracts::InvokeReply<R> reply) {
    // 测量失败或超额时保留固定错误回执；不让结果容量失败吞掉可靠完成。
    try {
      auto bytes=policy_.reply_bytes(reply);
      if(!bytes || *bytes>policy_.reply_limit) {
        reply=contracts::Rejected{invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded)};
      } else reply_bytes_=*bytes;
    } catch(...) {
      reply=contracts::Rejected{invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded)};
    }
    reply_.emplace(std::move(reply));
    input_.reset(); // 参数析构与调用槽释放均先于可靠完成发布。
    call_.reset();
    completed_.store(true,std::memory_order_release);
  }
  invocation::NativeBound<A,R> bound_;
  std::optional<A> input_;
  invocation::InvokeOptions options_;
  Policy policy_;
  std::unique_ptr<invocation::detail::CallLease> call_;
  std::optional<contracts::InvokeReply<R>> reply_;
  std::size_t input_bytes_=0,reply_bytes_=0;
  std::stop_source combined_; // stop-state 分配也在接受前完成。
  std::atomic<bool> claimed_{false},completed_{false};
};
}
