#pragma once
#include <ock/control/invoke_method.hpp>
#include <ock/control/subscription.hpp>
namespace ock::control {
// 冻结真实返回类型；读取仍通过 Host 的当前 ReadResult 授权及类型核对。
class ExecutionResultBinding {
public:
  struct Encoded {
    data::Payload value;
    std::shared_ptr<const runtime::policy::ResponseAuthorization> response;
  };
  template<contracts::ContractResult R>
  static ExecutionResultBinding create(contracts::OperationKey key,invoke_detail::Output<R> output) {
    return ExecutionResultBinding(std::move(key),std::make_shared<Typed<R>>(std::move(output)));
  }
private:
  struct Reader {
    virtual ~Reader()=default;
    virtual Result<Encoded> read(const runtime::host::HostSession&,const runtime::policy::VerifiedCaller&,
        contracts::ExecutionRef) const=0;
    virtual bool available(const runtime::host::HostSession&,const runtime::policy::VerifiedCaller&,
        contracts::ExecutionRef) const=0;
  };
  template<class R> struct Typed final:Reader {
    invoke_detail::Output<R> output;
    explicit Typed(invoke_detail::Output<R> o):output(std::move(o)) {}
    Result<Encoded> read(const runtime::host::HostSession& session,const runtime::policy::VerifiedCaller& caller,
        contracts::ExecutionRef ref) const override {
      auto actual=session.result<R>(caller,ref);if(!actual)return foundation::make_unexpected(actual.error());
      auto encoded=encode_invoke<R>(*actual->value,[&](const auto& value){return output.encode(value);});
      if(!encoded)return foundation::make_unexpected(encoded.error());
      return Encoded{std::move(*encoded),std::move(actual->response)};
    }
    bool available(const runtime::host::HostSession& session,const runtime::policy::VerifiedCaller& caller,
        contracts::ExecutionRef ref) const override {return bool(session.result<R>(caller,ref));}
  };
  ExecutionResultBinding(contracts::OperationKey key,std::shared_ptr<const Reader> reader)
      :key_(std::move(key)),reader_(std::move(reader)) {}
  contracts::OperationKey key_;
  std::shared_ptr<const Reader> reader_;
  friend class ExecutionMethod;
};
class ExecutionMethod final:public MethodPort {
public:
  enum class Kind {Get,Wait,Cancel,Result};
  static Result<std::shared_ptr<ExecutionMethod>> create(Kind,
      std::shared_ptr<runtime::host::HostSession>,std::shared_ptr<const runtime::policy::VerifiedCaller>,
      std::shared_ptr<ObservationTransport>,std::vector<ExecutionResultBinding>,
      std::shared_ptr<runtime::policy::ClockPort>,std::chrono::milliseconds maximum_wait=std::chrono::milliseconds(30000));
  static Result<binding::CompiledSchema> parameters(Kind);
  static std::string_view name(Kind) noexcept;
  Result<std::optional<data::Payload>> dispatch(const runtime::policy::VerifiedCaller&,
      std::string_view,data::ValueView) override;
  Result<runtime::policy::StartResult> pump();
  void disconnect() noexcept override;
  ~ExecutionMethod() override;
private:
  struct State;
  explicit ExecutionMethod(std::shared_ptr<State> state):state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};
}
