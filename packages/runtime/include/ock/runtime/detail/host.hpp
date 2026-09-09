#pragma once
#include <ock/runtime/detail/invocation.hpp>

namespace ock::runtime::host::detail {
SubmitReply submit(const std::shared_ptr<HostControl>&,
    std::shared_ptr<executions::detail::InvocationRecordBase>);
SubmitReply submit_child(const std::shared_ptr<HostControl>&,
    std::shared_ptr<executions::detail::InvocationRecordBase>,std::shared_ptr<ExecutionScopePort>);
struct AdmissionFrame {
  const HostControl* host = nullptr;
  AdmissionFrame* previous = nullptr;
};
class HostAdmission final {
public:
  explicit HostAdmission(std::shared_ptr<HostControl>);
  ~HostAdmission();
  HostAdmission(const HostAdmission&) = delete;
  HostAdmission& operator=(const HostAdmission&) = delete;
  explicit operator bool() const noexcept { return !error_; }
  const Error& error() const noexcept { return *error_; }
private:
  std::shared_ptr<HostControl> owner_;
  std::optional<Error> error_;
  AdmissionFrame frame_;
};
struct SessionState {
  std::shared_ptr<HostControl> host;
  std::shared_ptr<policy::SessionAuthority> authority;
  std::shared_ptr<invocation::NativeEngine> engine;
};
template<class A,class R> struct HostedBinding {
  std::shared_ptr<HostControl> host;
  invocation::NativeBound<A,R> native;
};
}

namespace ock::runtime::host {
template<ContractResult R>
Result<ExecutionResult<R>> HostSession::result(const policy::VerifiedCaller& caller,ExecutionRef ref) const {
  auto reply=result_erased(caller,ref,CppTypeToken::of<R>());
  if(!reply)return make_unexpected(reply.error());
  return ExecutionResult<R>{std::static_pointer_cast<const InvokeReply<R>>(reply->value),reply->response};
}
template<ContractValue A,ContractResult R>
Result<HostBound<A,R>> HostSession::bind(const OperationKey& key,ContractDigest digest,Shape shape,
    std::shared_ptr<const policy::VerifiedCaller> caller,
    std::span<const foundation::ObjectId> targets,invocation::TargetProjection<A> projection,Name trace) {
  auto state=state_;
  if(!state) return make_unexpected(host_error(HostErrc::InvalidSession));
  detail::HostAdmission admission(state->host);
  if(!admission) return make_unexpected(admission.error());
  try {
    auto bound=state->engine->bind<A,R>(key,digest,shape,std::move(caller),targets,projection,trace);
    if(!bound) return make_unexpected(bound.error());
    auto hosted=std::make_shared<detail::HostedBinding<A,R>>(
        detail::HostedBinding<A,R>{state->host,std::move(*bound)});
    return HostBound<A,R>{std::move(hosted)};
  } catch(const std::bad_alloc&) {
    return make_unexpected(host_error(HostErrc::BudgetExceeded));
  } catch(...) {
    return make_unexpected(host_error(HostErrc::CallbackException));
  }
}

template<ContractValue A,ContractResult R>
HostBound<A,R>::HostBound(std::shared_ptr<detail::HostedBinding<A,R>> state) noexcept :state_(std::move(state)) {}
template<ContractValue A,ContractResult R>
HostBound<A,R>::HostBound(HostBound&&) noexcept=default;
template<ContractValue A,ContractResult R>
HostBound<A,R>& HostBound<A,R>::operator=(HostBound&&) noexcept=default;
template<ContractValue A,ContractResult R>
HostBound<A,R>::~HostBound()=default;
template<ContractValue A,ContractResult R>
InvokeReply<R> HostBound<A,R>::invoke(const A& args,const invocation::InvokeOptions& options) const {
  // 本地owner与准入均活到最终返回对象构造；回调释放外壳不影响返回。
  auto state=state_;
  if(!state) return Rejected{invocation::invocation_error(invocation::InvocationErrc::InvalidBinding)};
  detail::HostAdmission admission(state->host);
  if(!admission) return Rejected{admission.error()};
  return state->native.invoke(args,options);
}
template<ContractValue A,ContractResult R>
SubmitReply HostBound<A,R>::submit(A args,const invocation::InvokeOptions& options) const
    requires (AsyncInput<A> && (std::same_as<R,void> || AsyncInput<R>)) {
  auto state=state_;
  if(!state) return Rejected{invocation::invocation_error(invocation::InvocationErrc::InvalidBinding)};
  detail::HostAdmission admission(state->host);
  if(!admission) return Rejected{admission.error()};
  auto record=executions::detail::InvocationRecord<A,R>::create_registered(
      state->native,std::move(args),options);
  if(!record)return Rejected{record.error()};
  return detail::submit(state->host,std::move(*record));
}
template<ContractValue A,ContractResult R>
SubmitReply HostBound<A,R>::submit_child(const WorkContext& work,A args,
    const invocation::InvokeOptions& options) const
    requires (AsyncInput<A> && (std::same_as<R,void> || AsyncInput<R>)) {
  auto state=state_;
  if(!state||!work.execution_scope())
    return Rejected{invocation::invocation_error(invocation::InvocationErrc::InvalidBinding)};
  detail::HostAdmission admission(state->host);
  if(!admission)return Rejected{admission.error()};
  auto child_options=options;
  child_options.deadline=std::min(child_options.deadline,work.deadline());
  auto record=executions::detail::InvocationRecord<A,R>::create_registered(
      state->native,std::move(args),child_options);
  if(!record)return Rejected{record.error()};
  return detail::submit_child(state->host,std::move(*record),work.execution_scope());
}
}
