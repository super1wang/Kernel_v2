#pragma once
// Runtime 内部 Submit 接线，不安装为独立 SDK 入口。
#include <ock/runtime/detail/invocation.hpp>

namespace ock::runtime::executions::detail {
class InvocationAccess final {
public:
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
