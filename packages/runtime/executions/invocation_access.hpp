#pragma once
// Runtime 内部 Submit 接线，不安装为独立 SDK 入口。
#include <ock/runtime/detail/invocation.hpp>

namespace ock::runtime::executions::detail {
class InvocationAccess final {
public:
  template<contracts::AsyncInput A,contracts::ContractResult R>
    requires (std::same_as<R,void> || contracts::AsyncInput<R>)
  static auto create_record(const invocation::NativeBound<A,R>& bound,A args,
      invocation::InvokeOptions options,registry::SubmissionStorage<A,R> storage) {
    return InvocationRecord<A,R>::create(bound,std::move(args),options,storage);
  }
  template<contracts::AsyncInput A,contracts::ContractResult R>
    requires (std::same_as<R,void> || contracts::AsyncInput<R>)
  static auto registered_record(const invocation::NativeBound<A,R>& bound,A args,
      invocation::InvokeOptions options) {
    return InvocationRecord<A,R>::create_registered(bound,std::move(args),options);
  }
  template <contracts::AsyncInput A, contracts::ContractResult R>
    requires (std::same_as<R,void> || contracts::AsyncInput<R>)
  static contracts::Result<std::unique_ptr<invocation::detail::CallLease>> reserve(
      const invocation::NativeBound<A,R>& bound,const A& args,
      const invocation::InvokeOptions& options) {
    auto state=bound.state_;
    if(!state) return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::InvalidBinding));
    auto slot=state->acquire();
    if(!slot) return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::Busy));
    std::unique_ptr<invocation::detail::CallLease> lease;
    try {
      // 在本对象分配失败时也必须归还已占用的 slot。
      lease=std::make_unique<invocation::detail::CallLease>(state,*slot);
    } catch(...) {
      state->slots[*slot].occupied.store(false,std::memory_order_release);
      return contracts::make_unexpected(invocation::invocation_error(invocation::InvocationErrc::BudgetExceeded));
    }
    auto prepared=bound.prepare_submission(args,options,*slot);
    if(!prepared) return contracts::make_unexpected(prepared.error());
    return lease;
  }
  template <contracts::ContractValue A, contracts::ContractResult R>
  static invocation::NativeBound<A,R> retain(const invocation::NativeBound<A,R>& bound) {
    return invocation::NativeBound<A,R>(bound.state_,bound.projection_);
  }
  template <contracts::AsyncInput A, contracts::ContractResult R>
    requires (std::same_as<R,void> || contracts::AsyncInput<R>)
  static contracts::InvokeReply<R> run_reserved(const invocation::NativeBound<A,R>& bound,
      const invocation::detail::CallLease& lease,const A& args,
      const invocation::InvokeOptions& options,
      std::span<const contracts::ResourceLease* const> resources={}) {
    if(bound.state_!=lease.owner)
      return contracts::Rejected{invocation::invocation_error(invocation::InvocationErrc::InvalidBinding)};
    return bound.run(args,options,true,resources,lease.slot);
  }
  template <contracts::AsyncInput A, contracts::ContractResult R>
    requires (std::same_as<R,void> || contracts::AsyncInput<R>)
  static contracts::Result<void> prepare(const invocation::NativeBound<A,R>& bound,
      const A& args, const invocation::InvokeOptions& options) {
    return bound.prepare_submission(args,options);
  }
  template <contracts::AsyncInput A, contracts::ContractResult R>
    requires (std::same_as<R,void> || contracts::AsyncInput<R>)
  static contracts::InvokeReply<R> run(const invocation::NativeBound<A,R>& bound,
      const A& args, const invocation::InvokeOptions& options,
      std::span<const contracts::ResourceLease* const> resources={}) {
    return bound.run(args,options,true,resources);
  }
  template <contracts::ContractValue A, contracts::ContractResult R>
  static std::shared_ptr<const invocation::detail::BoundState>
  material(const invocation::NativeBound<A,R>& bound) noexcept {
    return bound.state_;
  }
};
}
