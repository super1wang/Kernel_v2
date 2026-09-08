#pragma once
#include <ock/dynamic/binding/bound_operation.hpp>
#include <ock/control_protocol/outcome_wire.hpp>
#include <ock/control/router.hpp>
#include <stdexcept>
namespace ock::control {
namespace invoke_detail {
template<class R,bool = std::is_void_v<R>> struct Output {
  binding::RegisteredRecord<R,typename contracts::TypeContract<R>::FieldSpec> record;
  static Result<Output> create(){auto r=decltype(record)::create();if(!r)return foundation::make_unexpected(r.error());return Output{std::move(*r)};}
  Result<data::Payload> encode(const R &value)const{return record.encode(value);}
};
template<class R> struct Output<R,true> {
  static Result<Output> create(){return Output{};}
  template<class T> Result<data::Payload> encode(const T &)const{return data::Payload::parse("null");}
};
}
class InvocationBinding {
  struct Invoker {
    virtual ~Invoker()=default;
    virtual data::Payload run(const runtime::policy::VerifiedCaller &,data::ValueView,
                              const runtime::invocation::InvokeOptions &)const=0;
  };
  template<class A,class R> struct Typed final:Invoker {
    binding::BoundOperation<A,R> bound;
    invoke_detail::Output<R> output;
    std::shared_ptr<const runtime::policy::VerifiedCaller> caller;
    Typed(binding::BoundOperation<A,R> b,invoke_detail::Output<R> o,
          std::shared_ptr<const runtime::policy::VerifiedCaller> c)
        :bound(std::move(b)),output(std::move(o)),caller(std::move(c)){}
    data::Payload run(const runtime::policy::VerifiedCaller &c,data::ValueView args,
                      const runtime::invocation::InvokeOptions &options)const override {
      contracts::InvokeReply<R> reply = &c==caller.get()
          ? bound.invoke(args,options)
          : contracts::InvokeReply<R>{contracts::Rejected{runtime::policy::policy_error(runtime::policy::PolicyErrc::InvalidAuthority)}};
      auto wire=encode_invoke<R>(reply,[&](const auto &value){return output.encode(value);});
      if(!wire)throw std::runtime_error("Cannot encode completed invocation");
      return std::move(*wire);
    }
  };
  runtime::policy::OperationSelector selector_;
  std::shared_ptr<const Invoker> invoker_;
  InvocationBinding(runtime::policy::OperationSelector selector,std::shared_ptr<const Invoker> invoker)
      :selector_(std::move(selector)),invoker_(std::move(invoker)){}
  friend class InvokeMethod;
public:
  template<contracts::ContractValue A,contracts::ContractResult R>
  static Result<InvocationBinding> bind(runtime::host::HostSession &session,
      const contracts::OperationKey &key,contracts::ContractDigest digest,contracts::Shape shape,
      std::shared_ptr<const runtime::policy::VerifiedCaller> caller,
      std::span<const foundation::ObjectId> targets,
      runtime::invocation::TargetProjection<A> projection,foundation::Name trace) {
    auto output=invoke_detail::Output<R>::create();
    if(!output)return foundation::make_unexpected(output.error());
    auto bound=binding::BoundOperation<A,R>::create(session,key,digest,shape,caller,targets,projection,trace);
    if(!bound)return foundation::make_unexpected(bound.error());
    return InvocationBinding({key,digest},std::make_shared<Typed<A,R>>(std::move(*bound),std::move(*output),std::move(caller)));
  }
};
class InvokeMethod final:public MethodPort {
public:
  static Result<Method> create(std::vector<InvocationBinding>,
      std::shared_ptr<runtime::policy::ClockPort>,
      std::chrono::milliseconds timeout=std::chrono::milliseconds(1000),
      std::uint64_t work_limit=1024);
  Result<data::Payload> call(const runtime::policy::VerifiedCaller &,data::ValueView)override;
  void disconnect()noexcept override{stop_.request_stop();}
private:
  InvokeMethod(std::vector<InvocationBinding> bindings,std::shared_ptr<runtime::policy::ClockPort> clock,
      std::chrono::milliseconds timeout,std::uint64_t work):bindings_(std::move(bindings)),clock_(std::move(clock)),timeout_(timeout),work_(work){}
  std::vector<InvocationBinding> bindings_;
  std::shared_ptr<runtime::policy::ClockPort> clock_;
  std::chrono::milliseconds timeout_;
  std::uint64_t work_;
  std::stop_source stop_;
};
} // namespace ock::control
