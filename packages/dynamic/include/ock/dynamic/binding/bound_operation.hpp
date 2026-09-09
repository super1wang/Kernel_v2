#pragma once
#include <ock/dynamic/binding/registered_record.hpp>
#include <ock/runtime/host.hpp>
namespace ock::binding {
// 动态入口仅负责共有字段解码；治理、线程、目标和每次授权继续走 HostBound。
template <contracts::ContractValue A, contracts::ContractResult R>
class BoundOperation {
  using Arguments =
      RegisteredRecord<A, typename contracts::TypeContract<A>::FieldSpec>;

public:
  static Result<BoundOperation>
  create(runtime::host::HostSession &session,
         const Arguments &arguments,
         const contracts::OperationKey &key, contracts::ContractDigest digest,
         contracts::Shape shape,
         std::shared_ptr<const runtime::policy::VerifiedCaller> caller,
         std::span<const foundation::ObjectId> targets,
         runtime::invocation::TargetProjection<A> projection,
         foundation::Name trace) {
    auto bound = session.template bind<A, R>(
        key, digest, shape, std::move(caller), targets, projection, trace);
    if (!bound)
      return foundation::make_unexpected(bound.error());
    return BoundOperation(arguments, std::move(*bound));
  }
  contracts::InvokeReply<R>
  invoke(data::ValueView input,
         const runtime::invocation::InvokeOptions &options) const {
    auto args = arguments_.decode(input);
    if (!args)
      return contracts::Rejected{args.error()};
    return bound_.invoke(*args, options);
  }
  contracts::SubmitReply submit(data::ValueView input,
      const runtime::invocation::InvokeOptions& options) const
    requires (contracts::AsyncInput<A> && (std::same_as<R,void> || contracts::AsyncInput<R>)) {
    auto args=arguments_.decode(input);
    if(!args)return contracts::Rejected{args.error()};
    return bound_.submit(std::move(*args),options);
  }
  data::ValueView argument_schema() const & noexcept {
    return arguments_.schema();
  }
  data::ValueView argument_schema() const && = delete;

private:
  BoundOperation(Arguments arguments, runtime::host::HostBound<A, R> bound)
      : arguments_(std::move(arguments)), bound_(std::move(bound)) {}
  Arguments arguments_;
  runtime::host::HostBound<A, R> bound_;
};
} // namespace ock::binding
