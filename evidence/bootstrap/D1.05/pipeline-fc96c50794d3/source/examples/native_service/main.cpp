// D1.05 独立验证消费者。固定可信端口仅服务本进程演示。
#include "value.hpp"
#include "packages/runtime/invocation/invocation.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace native_service {
using namespace ock::contracts;
using namespace ock::runtime;
using namespace invocation;
using namespace policy;
namespace foundation = ock::foundation;
void require(bool value) { if (!value) throw std::runtime_error("Native consumer contract failed"); }
Name name(const char* text) { return *Name::parse(text); }
OperationKey key(const char* text) { return {name(text), *OperationVersion::parse("1.0.0", 5)}; }
template <class T> T identity() { T value{}; value.bytes[15] = 1; return value; }
PrincipalRef principal() { return {identity<PrincipalId>()}; }
foundation::ObjectId target() { return identity<foundation::ObjectId>(); }
std::vector<ScopeRule> rules() {
  std::vector<ScopeRule> result;
  for (auto operation : {"native.compute", "native.read", "native.edit"})
    result.push_back({AccessUse::Invoke, OperationSelector{key(operation), {}},
                      {name("allow")}, {target()}, {principal()}, {}});
  return result;
}
struct Clock final : ClockPort {
  TimePoint now() const noexcept override { return std::chrono::steady_clock::now(); }
};
struct Authentication final : TrustedAuthenticationPort {
  TimePoint deadline;
  explicit Authentication(TimePoint end) : deadline(end) {}
  Result<AuthenticatedIdentity> authenticate(const AuthenticationAttempt& input) override {
    if (input.credential != std::vector<std::byte>{std::byte{7}})
      return make_unexpected(policy_error(PolicyErrc::AuthenticationFailed));
    return AuthenticatedIdentity{principal(), PrincipalKind::Service, {}, {rules(), deadline, false}, deadline};
  }
};
struct Digest final : TrustedGroupDigestPort {
  Result<ContractDigest> fingerprint(const GroupSnapshot&) override {
    // 本消费者关闭 group；意外调用明确拒绝。
    return make_unexpected(policy_error(PolicyErrc::TargetUnavailable));
  }
};
struct Source final : ExecutionAccessSourcePort {
  ObservationSourceIdentity identity() const noexcept override {
    return {native_service::identity<HostIncarnation>(), RestoreMode::Absent};
  }
  Result<ExecutionAccessInput> find(ExecutionRef) override {
    return make_unexpected(policy_error(PolicyErrc::TargetUnavailable));
  }
  Result<AccessScanPage> scan(const AccessScanRequest&) override {
    return AccessScanPage{{}, {}, identity().host, name("retained")};
  }
};
struct Lifetime final : PortLifetime {};
struct Threads final : TrustedThreadPort {
  std::thread::id owner = std::this_thread::get_id();
  Result<ThreadObservation> current() const noexcept override {
    if (owner != std::this_thread::get_id())
      return make_unexpected(invocation_error(InvocationErrc::ThreadRejected));
    return ThreadObservation{ThreadRole::Application, name("app"), true};
  }
};
struct Executor final : ExecutorPort {
  Result<void> submit(std::unique_ptr<ReadyWork>) override {
    throw std::runtime_error("Native consumer unexpectedly created submitted work");
  }
};
struct Reader { int read() const { return 7; } };
struct Provider {
  struct EditPort { int value = 0; };
  struct CandidateReadPort { int value = 0; };
  struct Frame { EditPort edit; CandidateReadPort read; };
};
unsigned entered = 0;
Result<Value> compute(const Value& value, WorkContext&) { ++entered; return Value{value.number + 2}; }
Result<Value> read(const Value& value, WorkContext&, ReadServices<Reader>& service) {
  ++entered; return Value{value.number + service.reader().read()};
}
Result<Value> edit(const Value& value, EditView<Provider>&, WorkContext&) { ++entered; return value; }
Result<std::size_t> targets(const Value&, std::span<foundation::ObjectId> output) noexcept {
  if (output.empty()) return make_unexpected(error(ContractsErrc::BudgetExceeded));
  output[0] = target(); return 1;
}
int result(const InvokeReply<Value>& reply) {
  require(std::holds_alternative<Completed<Value>>(reply));
  const auto& value = std::get<Completed<Value>>(reply).outcome.value();
  require(std::holds_alternative<ReadCompleted<Value>>(value));
  const auto& result = std::get<ReadCompleted<Value>>(value).result;
  require(bool(result)); return result->number;
}
}
namespace ock::contracts {
template <> struct ProviderContract<native_service::Provider> {
  static AtomicProviderKey key() { return *Name::parse("provider"); }
};
}
int main() {
  using namespace native_service;
  try {
    auto clock = std::make_shared<Clock>();
    auto auth = std::make_shared<Authentication>(clock->now() + std::chrono::hours(1));
    PolicyConfiguration config;
    config.principals.push_back({principal(), rules()});
    for (auto operation : {"native.compute", "native.read", "native.edit"})
      config.operations.push_back({{key(operation), {}}, {name("allow")}, rules(), false});
    config.targets.push_back({target(), 1, rules(), std::make_shared<Lifetime>()});
    auto assembly = PolicyStore::create({}, config, auth, clock, std::make_shared<Digest>(), std::make_shared<Source>());
    require(bool(assembly));
    auto opened = assembly->store->open({{std::byte{7}}}, {rules(), auth->deadline, false});
    require(bool(opened));
    auto session = *opened;
    auto caller = session->verify({principal(), {}, {}});
    require(bool(caller));
    registry::ModuleManifest manifest{name("native"), *OperationVersion::parse("1.0.0", 5)};
    manifest.operations = {key("native.compute"), key("native.read"), key("native.edit")};
    manifest.services = {name("reader")};
    manifest.providers = {name("provider")};
    manifest.executors = {name("inline")};
    manifest.required_services.push_back({{name("native"), name("reader")}, CppTypeToken::of<Reader>()});
    manifest.required_providers.push_back({{name("native"), name("provider")}, CppTypeToken::of<Provider>()});
    registry::ModuleInput module{manifest, {}, {}, {}, {},
        {{name("inline"), name("app"), false, false, std::make_shared<Executor>()}}, {}};
    auto service = registry::ServiceBinding::make(name("reader"), std::make_shared<Reader>());
    auto provider = registry::ProviderBinding::make(std::make_shared<Provider>());
    require(bool(service) && bool(provider));
    module.services.push_back(*service);
    module.providers.push_back(*provider);
    module.register_operations = [](registry::Registrar& registrar) {
      DefinitionInput definition{key("native.compute"), {},
          {true, false, false, name("inline"), name("app")}, AtomicMode::PureCompute, {name("allow")}, "Native 验证消费者"};
      registry::OperationOptions options{{}, {}, {name("native"), name("inline")}, {}, false};
      require(bool(registrar.compute(compute, definition, options)));
      definition.key = key("native.read");
      definition.atomic_mode = AtomicMode::Incompatible;
      options.read_service = registry::ServiceRef{name("native"), name("reader")};
      require(bool(registrar.read(read, definition, options)));
      definition.key = key("native.edit");
      definition.atomic_mode = AtomicMode::StateEdit;
      options.read_service.reset();
      options.provider = registry::ProviderRef{name("native"), name("provider")};
      require(bool(registrar.state_edit(edit, definition, options)));
    };
    auto batch = registry::RegistrationBatch::create({});
    require(bool(batch)); require(bool((*batch)->add(module)));
    auto catalog = (*batch)->publish(); require(bool(catalog));
    auto engine = NativeEngine::create(*catalog, session, std::make_shared<Threads>(), {});
    require(bool(engine));
    auto bind = [&](const char* operation, Shape shape) {
      return (*engine)->bind<Value, Value>(key(operation), {}, shape, *caller,
          std::array{target()}, targets, name("native.consumer"));
    };
    auto computing = bind("native.compute", Shape::Read);
    auto reading = bind("native.read", Shape::Read);
    auto editing = bind("native.edit", Shape::StateEdit);
    require(bool(computing) && bool(reading) && bool(editing));
    InvokeOptions options{{}, clock->now() + std::chrono::seconds(10), 100};
    require(result(computing->invoke(Value{5}, options)) == 7);
    require(result(reading->invoke(Value{5}, options)) == 12);
    const auto before = entered;
    auto invalid = computing->invoke(Value{-1}, options);
    require(std::holds_alternative<Rejected>(invalid));
    require(std::get<Rejected>(invalid).reason.code() == error(ContractsErrc::InvalidContract).code());
    require(entered == before);
    auto unavailable = editing->invoke(Value{5}, options);
    require(std::holds_alternative<Rejected>(unavailable));
    require(std::get<Rejected>(unavailable).reason.code() == invocation_error(InvocationErrc::ProviderUnavailable).code());
    require(entered == before);
    std::cout << R"({"checks":{"compute":true,"read":true,"invalid_input":true,"provider_unavailable":true}})" << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
