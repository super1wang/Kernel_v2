#pragma once
// NativeSubset 模板实现；应用通过 host.hpp 使用。
#include <algorithm>
#include <atomic>
#include <ock/runtime/native_types.hpp>
#include <ock/contracts/operation.hpp>
#include <ock/runtime/policy.hpp>
#include <ock/runtime/resources.hpp>
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
    Result<R> result, std::shared_ptr<const KnownFacts> facts,bool accepted=false) {
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
  OutcomeConditions conditions{BeforeApplyDecision{true, ApplyDecision::NotReached, true,accepted}, {},
                               {RequiredRecordState::NotRequired, 0, 0}};
  OutcomeValidation validation{publication, {}, {0, 0, 0}};
  return Outcome<R>::validate(ReadCompleted<R>{std::move(result), ResultScope::ReadOnly},
                              std::move(facts), EvidenceState::Volatile, conditions, validation);
}
template <class R> Result<Outcome<R>> outcome_failure(
    Error error, std::shared_ptr<const KnownFacts> facts,bool entered=true,bool accepted=false,bool cancelled=false) {
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
  OutcomeConditions conditions{BeforeApplyDecision{entered, cancelled?ApplyDecision::CancelWon:ApplyDecision::NotReached, true,accepted}, {},
                               {RequiredRecordState::NotRequired, 0, 0}};
  OutcomeValidation validation{publication, {}, {0, 0, 0}};
  typename Outcome<R>::Candidate candidate=cancelled
      ?typename Outcome<R>::Candidate{CancelledBeforeApply{error}}
      :typename Outcome<R>::Candidate{FailedBeforeApply{*Name::parse(accepted?(entered?"managed.result":"managed.dispatch"):"native"), error}};
  return Outcome<R>::validate(std::move(candidate), std::move(facts),
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
                                 std::optional<std::size_t> reserved={},
                                 std::chrono::steady_clock::time_point* deadline=nullptr) const {
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
      if (state->entry->shape!=Shape::Read ||
          (state->entry->execution.requires_external_wait&&!state->entry->asynchronous_read))
        return make_unexpected(invocation_error(InvocationErrc::ProviderUnavailable));
      auto admission=detail::validate_call(*state,*slot,projection_,args,options);
      if (!admission) return make_unexpected(admission.error());
      if(deadline)*deadline=admission->deadline();
      return {};
    } catch (const std::bad_alloc&) {
      return make_unexpected(invocation_error(InvocationErrc::BudgetExceeded));
    } catch (...) {
      return make_unexpected(invocation_error(InvocationErrc::HandlerException));
    }
  }
  Result<std::shared_ptr<registry::detail::AsyncDispatchPort>> prepare_async(std::shared_ptr<PortLifetime> source) const {
    return NativeAccess::prepare_async(*state_->catalog,*state_->entry,std::move(source));
  }
  Result<policy::InlineAdmission> admit_async(const A& args,const InvokeOptions& options,
      std::size_t slot,std::span<const ResourceLease* const> resources) const {
    auto state=state_;
    auto current=NativeAccess::check(*state->catalog,*state->entry,CppTypeToken::of<A>(),CppTypeToken::of<R>());
    if(!current)return make_unexpected(current.error());
    if(!state->entry->asynchronous_read||state->entry->shape!=Shape::Read)
      return make_unexpected(invocation_error(InvocationErrc::InvalidBinding));
    if(state->entry->resources.empty()!=resources.empty()||
       std::any_of(resources.begin(),resources.end(),[](auto p){return p==nullptr;}))
      return make_unexpected(invocation_error(InvocationErrc::ResourceUnavailable));
    auto thread=detail::check_managed_thread(*state);
    if(!thread)return make_unexpected(thread.error());
    return detail::validate_call(*state,slot,projection_,args,options);
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
      auto outcome = detail::outcome_failure<R>(error, state->empty_facts,true,managed);
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
      if(state->entry->asynchronous_read)
        return rejected(invocation_error(InvocationErrc::SubmitRequired));
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
      if(auto scope=std::dynamic_pointer_cast<registry::detail::InvocationScopePort>(execution))
        scope->admitted(admission->deadline());
      auto budget = foundation::CheckedCount<std::uint64_t>::create(0, options.work_limit);
      if (!budget) return rejected(budget.error());
      WorkContext work(options.stop, admission->deadline(), *budget, state->trace,
                       BorrowedResourceViews{resources},std::move(execution));
      std::optional<Result<R>> result;
      entered = true;
      NativeAccess::dispatch(*state->catalog, *state->entry, &args, work, &result);
      if (!result) return failed(invocation_error(InvocationErrc::InvalidOutput));
      if (!*result) return failed(result->error());
      auto completed = detail::outcome_read<R>(std::move(*result), state->empty_facts,managed);
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
                        std::shared_ptr<contracts::ExecutionScopePort> = {},
                        std::unique_ptr<contracts::ResourceLease> = {})=0;
  virtual bool discard_before_accept(contracts::Error)=0;
  virtual bool complete_before_start(contracts::Error,bool cancelled=false)=0;
  virtual const void* reply_pointer() const noexcept=0;
  virtual contracts::CppTypeToken result_type() const noexcept=0;
  virtual InvocationCompletion completion() const=0;
  virtual std::size_t input_bytes() const noexcept=0;
  virtual std::size_t reserved_reply_bytes() const noexcept=0;
  virtual invocation::InvokeOptions options() const noexcept=0;
  virtual std::shared_ptr<const invocation::detail::BoundState> material() const noexcept=0;
  virtual bool asynchronous() const noexcept=0;
  virtual bool settled() const noexcept=0;
  virtual bool accepts_children() const noexcept=0;
  virtual void set_wake(std::function<void()>)=0;
  virtual contracts::Result<void> before_child_wait(std::span<const resources::Claim>) const=0;
  virtual void finalize_required_record(contracts::RequiredRecordState,std::optional<contracts::RecordFailure>)=0;
  virtual void retire_ownership() noexcept=0;
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
  class AsyncCall final : public registry::detail::AsyncSource<A,R>,public std::enable_shared_from_this<AsyncCall> {
  public:
    explicit AsyncCall(std::weak_ptr<InvocationRecord> record):weak_(std::move(record)) {}
    const A& input() const noexcept override {return *record_->input_;}
    contracts::WorkContext& work() noexcept override {return *work_;}
    contracts::Result<void> complete(contracts::Result<R> result) noexcept override {
      auto self=this->shared_from_this();
      registry::detail::ExecutionCallbackFrame frame;
      return record_->complete_async(std::move(result));
    }
    void activate(policy::InlineAdmission admission,std::stop_token stop,
        std::shared_ptr<contracts::ExecutionScopePort> scope,std::unique_ptr<contracts::ResourceLease> lease) {
      record_=weak_.lock();foundation::invariant(bool(record_));
      admission_.emplace(std::move(admission));lease_=std::move(lease);view_=lease_.get();
      if(auto current=std::dynamic_pointer_cast<registry::detail::InvocationScopePort>(scope))
        current->admitted(admission_->deadline());
      auto budget=foundation::CheckedCount<std::uint64_t>::create(0,record_->options_.work_limit);
      foundation::invariant(bool(budget));
      work_.emplace(record_->combined_.get_token(),admission_->deadline(),*budget,record_->bound_.state_->trace,
          contracts::BorrowedResourceViews{view_?std::span<const contracts::ResourceLease* const>(&view_,1):
                                              std::span<const contracts::ResourceLease* const>{}},std::move(scope));
      original_.emplace(record_->options_.stop,Relay{record_->combined_});
      managed_.emplace(stop,Relay{record_->combined_});active=true;
    }
    contracts::Result<void> before_child_wait(std::span<const resources::Claim> claims) const {
      if(!lease_||claims.empty())return {};
      auto lease=dynamic_cast<const resources::ResourceManager::Lease*>(lease_.get());
      if(!lease)return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::InvalidAuthority));
      return lease->before_child_wait(claims);
    }
    bool active=false;
  private:
    struct Relay {
      std::stop_source source;
      void operator()() noexcept {auto keep=source;keep.request_stop();}
    };
    std::weak_ptr<InvocationRecord> weak_;
    std::shared_ptr<InvocationRecord> record_;
    std::optional<policy::InlineAdmission> admission_;
    std::unique_ptr<contracts::ResourceLease> lease_;
    const contracts::ResourceLease* view_=nullptr;
    std::optional<contracts::WorkContext> work_;
    std::optional<std::stop_callback<Relay>> original_,managed_;
  };
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
      auto prepared=record->bound_.prepare_submission(*record->input_,options,*slot,&record->options_.deadline);
      if(!prepared)return contracts::make_unexpected(prepared.error());
      record->call_=std::move(reserved);
      auto bytes=policy.input_bytes(*record->input_);
      if(!bytes) return contracts::make_unexpected(bytes.error());
      if(*bytes>policy.input_limit)
        return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded));
      record->input_bytes_=*bytes;
      if(state->entry->asynchronous_read) {
        std::weak_ptr<InvocationRecord> weak=record;
        auto source=std::shared_ptr<AsyncCall>(new AsyncCall(weak),[weak](AsyncCall* call) noexcept {
          registry::detail::ExecutionCallbackFrame frame;
          auto record=weak.lock();const bool active=call->active;
          delete call; // 先实际销毁 context/stop callbacks/Lease/admission，再通知 Runtime。
          if(active&&record)record->async_drained();
        });
        auto dispatch=record->bound_.prepare_async(source);
        if(!dispatch)return contracts::make_unexpected(dispatch.error());
        record->async_source_=source;record->async_dispatch_=std::move(*dispatch);
      }
      return record;
    } catch(...) {
      return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded));
    }
  }
public:
  bool run_once(std::stop_token stop,std::span<const contracts::ResourceLease* const> resources={},
                std::shared_ptr<contracts::ExecutionScopePort> execution={},
                std::unique_ptr<contracts::ResourceLease> lease={}) override {
    auto keep_alive=this->shared_from_this();
    if(claimed_.exchange(true,std::memory_order_acq_rel)) return false;
    if(asynchronous())return start_async(stop,resources,std::move(execution),std::move(lease));
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
  bool discard_before_accept(contracts::Error reason) override {
    auto keep_alive=this->shared_from_this();
    if(claimed_.exchange(true,std::memory_order_acq_rel)) return false;
    async_dispatch_.reset();reply_.emplace(contracts::Rejected{contracts::Error{reason.code()}});
    input_.reset();call_.reset();settled_.store(true,std::memory_order_release);
    completed_.store(true,std::memory_order_release);return true;
  }
  bool complete_before_start(contracts::Error reason,bool cancelled=false) override {
    auto keep_alive=this->shared_from_this();
    if(claimed_.exchange(true,std::memory_order_acq_rel)) return false;
    async_dispatch_.reset();
    store(failure_reply(reason,false,cancelled||cancel_before_start(reason)));
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
  bool asynchronous() const noexcept override {return asynchronous_;}
  bool settled() const noexcept override {return settled_.load(std::memory_order_acquire);}
  bool accepts_children() const noexcept override {
    return !completed_.load(std::memory_order_acquire)&&
        (!asynchronous()||(!candidate_claimed_.load(std::memory_order_acquire)&&!async_source_.expired()));
  }
  void set_wake(std::function<void()> wake) override {wake_=std::move(wake);}
  contracts::Result<void> before_child_wait(std::span<const resources::Claim> claims) const override {
    if(!asynchronous())return {};
    auto source=async_source_.lock();
    if(!source)return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::InvalidPhase));
    return source->before_child_wait(claims);
  }
  contracts::CppTypeToken result_type() const noexcept override {return contracts::CppTypeToken::of<R>();}
  void retire_ownership() noexcept override {
    registry::detail::ExecutionCallbackFrame frame;
    foundation::invariant(settled()&&reply());
    bound_.state_.reset();options_.stop={};
  }
  void finalize_required_record(contracts::RequiredRecordState state,std::optional<contracts::RecordFailure> failure) override {
    registry::detail::ExecutionCallbackFrame frame;
    foundation::invariant(settled()&&reply());
    // 尚未发布 Terminal，没有外部结果引用；保留业务候选值，只追加记录事实。
    foundation::invariant(std::holds_alternative<contracts::Completed<R>>(*reply_));
    auto pending=std::move(std::get<contracts::Completed<R>>(*reply_).outcome)
        .with_required_record(contracts::RequiredRecordState::Pending);
    foundation::invariant(bool(pending));
    auto final=std::move(*pending).with_required_record(state,std::move(failure));
    foundation::invariant(bool(final));reply_.emplace(contracts::Completed<R>{std::move(*final)});
  }
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
  bool start_async(std::stop_token stop,std::span<const contracts::ResourceLease* const> resources,
      std::shared_ptr<contracts::ExecutionScopePort> scope,std::unique_ptr<contracts::ResourceLease> lease) {
    auto dispatch=std::move(async_dispatch_);auto source=async_source_.lock();
    try {
      auto admission=bound_.admit_async(*input_,options_,call_->slot,resources);
      if(!admission||(!resources.empty()&&!lease)) {
        store(contracts::Rejected{admission?invocation::invocation_error(invocation::InvocationErrc::ResourceUnavailable):admission.error()});
        return true;
      }
      foundation::invariant(bool(source)&&bool(dispatch));
      source->activate(std::move(*admission),stop,std::move(scope),std::move(lease));
      auto started=dispatch->start();
      if(!started)(void)complete_async(contracts::make_unexpected(started.error()));
    } catch(const std::bad_alloc&) {
      if(source&&source->active)(void)complete_async(contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded)));
      else store(contracts::Rejected{invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded)});
    } catch(...) {
      if(source&&source->active)(void)complete_async(contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::HandlerException)));
      else store(contracts::Rejected{invocation::invocation_error(invocation::InvocationErrc::HandlerException)});
    }
    return true;
  }
  contracts::Result<void> complete_async(contracts::Result<R> result) noexcept {
    registry::detail::ExecutionCallbackFrame frame;
    if(candidate_claimed_.exchange(true,std::memory_order_acq_rel))
      return contracts::make_unexpected(contracts::error(contracts::ContractsErrc::DuplicateCompletion));
    auto state=bound_.state_;
    auto kind=invocation::InvocationRecordKind::ReadCompleted;
    std::optional<foundation::ErrorCode> fault;
    try {
      auto outcome=[&] {
        if(!result) {
          fault=result.error().code();kind=invocation::InvocationRecordKind::FailedBeforeApply;
          return invocation::detail::outcome_failure<R>(result.error(),state->empty_facts,true,true);
        }
        return invocation::detail::outcome_read<R>(std::move(result),state->empty_facts,true);
      }();
      if(!outcome) {
        fault=outcome.error().code();kind=invocation::InvocationRecordKind::FailedBeforeApply;
        auto failed=invocation::detail::outcome_failure<R>(outcome.error(),state->empty_facts,true,true);
        foundation::invariant(bool(failed));
        store(contracts::Completed<R>{std::move(*failed)},false);
      } else store(contracts::Completed<R>{std::move(*outcome)},false);
    } catch(const std::bad_alloc&) {
      fault=invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded).code();
      kind=invocation::InvocationRecordKind::FailedBeforeApply;
      store(failure_reply(contracts::Error{*fault},true),false);
    } catch(...) {
      fault=invocation::invocation_error(invocation::InvocationErrc::HandlerException).code();
      kind=invocation::InvocationRecordKind::FailedBeforeApply;
      store(failure_reply(contracts::Error{*fault},true),false);
    }
    state->observe(kind,fault);signal();return {};
  }
  void async_drained() noexcept {
    if(!completed_.load(std::memory_order_acquire))
      (void)complete_async(contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::InvalidOutput)));
    input_.reset();call_.reset();settled_.store(true,std::memory_order_release);signal();
  }
  void signal() noexcept {try {if(wake_)wake_();}catch(...){}}
  InvocationRecord(invocation::NativeBound<A,R> bound,A args,
      invocation::InvokeOptions options,Policy policy)
      :catalog_(bound.state_?bound.state_->catalog:nullptr),
       asynchronous_(bound.state_&&bound.state_->entry->asynchronous_read),
       bound_(std::move(bound)),input_(std::move(args)),options_(options),policy_(policy) {}
  void store(contracts::InvokeReply<R> reply,bool release=true) {
    // 此路径仅完成受管理执行；接受前丢弃独立处理。不会把已接受结果降为 Rejected。
    if(auto rejected=std::get_if<contracts::Rejected>(&reply))
      reply.template emplace<contracts::Completed<R>>(failure_reply(rejected->reason,false,cancel_before_start(rejected->reason)));
    const auto fail_measurement=[&] {
      const auto& outcome=std::get<contracts::Completed<R>>(reply).outcome;
      // 原始失败/取消已有固定最小回执，容量诊断不能覆盖其仲裁事实。
      if(std::holds_alternative<contracts::ReadCompleted<R>>(outcome.value()))
        reply.template emplace<contracts::Completed<R>>(failure_reply(invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded),true));
    };
    try {
      auto bytes=policy_.reply_bytes(reply);
      if(!bytes || *bytes>policy_.reply_limit) {
        fail_measurement();
      } else reply_bytes_=*bytes;
    } catch(...) {
      fail_measurement();
    }
    reply_.emplace(std::move(reply));
    if(release) {input_.reset();call_.reset();settled_.store(true,std::memory_order_release);}
    completed_.store(true,std::memory_order_release);
  }
  static bool cancel_before_start(const contracts::Error& reason) noexcept {
    return reason.code()==invocation::invocation_error(invocation::InvocationErrc::Cancelled).code()||
        reason.code()==invocation::invocation_error(invocation::InvocationErrc::Expired).code()||
        reason.code()==policy::policy_error(policy::PolicyErrc::Cancelled).code()||
        reason.code()==policy::policy_error(policy::PolicyErrc::Expired).code();
  }
  contracts::Completed<R> failure_reply(contracts::Error reason,bool entered,bool cancelled=false) const {
    auto outcome=invocation::detail::outcome_failure<R>(contracts::Error{reason.code()},bound_.state_->empty_facts,entered,true,cancelled);
    foundation::invariant(bool(outcome));return {std::move(*outcome)};
  }
  // 最后释放注册代码/服务 owner；结果 pin 不需要保留调用授权与 Engine。
  std::shared_ptr<const registry::Catalog> catalog_;
  const bool asynchronous_;
  invocation::NativeBound<A,R> bound_;
  std::optional<A> input_;
  invocation::InvokeOptions options_;
  Policy policy_;
  std::unique_ptr<invocation::detail::CallLease> call_;
  std::optional<contracts::InvokeReply<R>> reply_;
  std::size_t input_bytes_=0,reply_bytes_=0;
  std::stop_source combined_; // stop-state 分配也在接受前完成。
  std::atomic<bool> claimed_{false},completed_{false},settled_{false},candidate_claimed_{false};
  std::weak_ptr<AsyncCall> async_source_;
  std::shared_ptr<registry::detail::AsyncDispatchPort> async_dispatch_;
  std::function<void()> wake_;
};
}
