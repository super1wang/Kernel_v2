#pragma once
#include <ock/control/catalog_methods.hpp>
#include <ock/control/execution_method.hpp>
#include <ock/control/observation_transport.hpp>
#include <ock/local_ipc/pipe.hpp>
namespace managed {
using namespace ock;
using foundation::Result;
inline foundation::Name name(std::string_view value){return *foundation::Name::parse(value);}
inline contracts::OperationVersion version(){return *contracts::OperationVersion::parse("1.0.0",128);}
template<class Id> Id id(){Id value{};value.bytes[15]=1;return value;}
inline contracts::PrincipalRef principal(){return {id<contracts::PrincipalId>()};}
inline foundation::ObjectId target(){return id<foundation::ObjectId>();}
inline contracts::OperationKey operation(){return {name("sample.compute"),version()};}
struct Input {std::int64_t amount{},delay_ms{};std::string text;};
struct Output {std::int64_t amount{};std::string text;bool stop_observed=false;};
struct InputFields {
  static contracts::TypeIdentity identity(){return {name("managed.input"),version(),{}};}
  static auto fields(){
    auto amount=binding::field<&Input::amount>("amount");amount.minimum=0;amount.maximum=1000000;
    auto delay=binding::field<&Input::delay_ms>("delay_ms");delay.minimum=0;delay.maximum=10000;
    auto text=binding::field<&Input::text>("text");text.max_length=2048;
    return std::tuple(amount,delay,text);
  }
};
struct OutputFields {
  static contracts::TypeIdentity identity(){return {name("managed.output"),version(),{}};}
  static auto fields(){
    auto amount=binding::field<&Output::amount>("amount");amount.minimum=1;amount.maximum=1000001;
    auto text=binding::field<&Output::text>("text");text.max_length=2048;
    return std::tuple(amount,text,binding::field<&Output::stop_observed>("stop_observed"));
  }
};
}
namespace ock::contracts {
template<>struct TypeContract<managed::Input>:binding::TypeContract<managed::Input,managed::InputFields>{};
template<>struct TypeContract<managed::Output>:binding::TypeContract<managed::Output,managed::OutputFields>{};
}
namespace managed {
class Service {
public:
  static Result<std::unique_ptr<Service>> create(std::string allowed_sid);
  ~Service();
  Result<std::unique_ptr<local_ipc::Server>> listen(std::string instance);
  Result<void> shutdown();
  struct ObservationDiagnostics {std::size_t sessions=0,queued_bytes=0;std::uint64_t started=0;};
  ObservationDiagnostics observation_diagnostics() const;
  Result<std::size_t> restrict_observers();
private:
  struct State;
  explicit Service(std::shared_ptr<State> state):state_(std::move(state)){}
  std::shared_ptr<State> state_;
};
}
