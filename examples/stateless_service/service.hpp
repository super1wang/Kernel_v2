#pragma once
#include <ock/control/catalog_methods.hpp>
#include <ock/control/invoke_method.hpp>
#include <ock/local_ipc/pipe.hpp>

namespace stateless {
using namespace ock;
using foundation::Result;
inline foundation::Name name(std::string_view text) { return *foundation::Name::parse(text); }
inline contracts::OperationVersion version() { return *contracts::OperationVersion::parse("1.0.0",128); }
template<class Id> Id id(unsigned value = 1) { Id result{}; result.bytes[15] = static_cast<std::uint8_t>(value); return result; }
inline contracts::PrincipalRef principal() { return {id<contracts::PrincipalId>()}; }
inline foundation::ObjectId target() { return id<foundation::ObjectId>(); }
inline contracts::OperationKey operation() { return {name("sample.increment"),version()}; }
struct Value { std::int64_t amount{}; std::string text; };
struct Fields {
  static contracts::TypeIdentity identity() { return {name("sample.value"),version(),{}}; }
  static auto fields() {
    auto amount = binding::field<&Value::amount>("amount"); amount.minimum = 0; amount.maximum = 100;
    auto text = binding::field<&Value::text>("text"); text.max_length = 256;
    return std::tuple(amount,text);
  }
};
}
namespace ock::contracts {
template<> struct TypeContract<stateless::Value> : binding::TypeContract<stateless::Value,stateless::Fields> {};
}
namespace stateless {
class Service {
public:
  class MeasurementPort {
  public:
    virtual ~MeasurementPort() = default;
    virtual void begin(std::string_view entry) = 0;
    virtual void end() = 0;
  };
  static foundation::Result<std::unique_ptr<Service>> create(std::string allowed_sid);
  ~Service();
  foundation::Result<std::unique_ptr<local_ipc::Server>> listen(std::string instance);
  foundation::Result<data::Payload> native(const Value &);
  foundation::Result<void> measure(MeasurementPort &);
  void close_connections();
private:
  struct State;
  explicit Service(std::shared_ptr<State> state) : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};
}
