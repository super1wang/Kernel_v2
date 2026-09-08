// C-A NativeSubset 安装消费者；固定可信端口仅服务本进程。
#include "value.hpp"
#include <ock/runtime/host.hpp>
#include <ock/foundation/sdk_version.hpp>
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
  for (auto operation : {"native.compute", "native.read"})
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
unsigned entered = 0;
Result<Value> compute(const Value& value, WorkContext&) { ++entered; return Value{value.number + 2}; }
Result<Value> read(const Value& value, WorkContext&, ReadServices<Reader>& service) {
  ++entered; return Value{value.number + service.reader().read()};
}
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
struct Lifecycle final : host::ModuleLifecyclePort {
  unsigned starts=0, stops=0;
  Result<void> start(const host::ModuleContext&) override { ++starts; return {}; }
  host::ModuleStopResult stop() override { ++stops; return {true,{}}; }
};
struct StopGuard {
  host::NativeHost& owner;
  ~StopGuard() { (void)owner.shutdown_until(std::chrono::steady_clock::now()+std::chrono::seconds(2)); }
};
}
int main() {
  using namespace native_service;
  try {
    static_assert(ock::sdk::version == "0.1.0-dev.3");
    static_assert(ock::sdk::runtime_available && ock::sdk::implementation_stage == "B2Subset");
    auto clock=std::make_shared<Clock>();
    auto auth=std::make_shared<Authentication>(clock->now()+std::chrono::hours(1));
    PolicyConfiguration config;
    config.principals.push_back({principal(),rules()});
    for(auto operation:{"native.compute","native.read"})
      config.operations.push_back({{key(operation),{}},{name("allow")},rules(),false});
    config.targets.push_back({target(),1,rules(),std::make_shared<Lifetime>()});
    auto made=host::NativeHost::create({},config,{auth,clock,std::make_shared<Digest>(),std::make_shared<Threads>(),{}});
    require(bool(made));
    auto& runtime=**made;
    StopGuard stop{runtime};
    require(!runtime.incarnation().empty());
    const auto capabilities=runtime.capabilities();
    require(capabilities.native_read && capabilities.native_compute && capabilities.ordinary_memory_logging);
    require(!capabilities.async_execution && !capabilities.execution_observation && !capabilities.state && !capabilities.storage && !capabilities.restore);
    registry::ModuleManifest manifest{name("native"),*OperationVersion::parse("1.0.0",5)};
    manifest.operations={key("native.compute"),key("native.read")};
    manifest.services={name("reader")};
    manifest.executors={name("inline")};
    manifest.required_services.push_back({{name("native"),name("reader")},CppTypeToken::of<Reader>()});
    registry::ModuleInput module{manifest,{},{},{},{},
      {{name("inline"),name("app"),false,false,std::make_shared<Executor>()}},{}};
    auto service=registry::ServiceBinding::make(name("reader"),std::make_shared<Reader>());
    require(bool(service));module.services.push_back(*service);
    module.register_operations=[](registry::Registrar& registrar) {
      DefinitionInput definition{key("native.compute"),{},
        {true,false,false,name("inline"),name("app")},AtomicMode::PureCompute,{name("allow")},"NativeSubset 安装消费者"};
      registry::OperationOptions options{{},{},{name("native"),name("inline")},{},false};
      require(bool(registrar.compute(compute,definition,options)));
      definition.key=key("native.read");definition.atomic_mode=AtomicMode::Incompatible;
      options.read_service=registry::ServiceRef{name("native"),name("reader")};
      require(bool(registrar.read(read,definition,options)));
    };
    auto lifecycle=std::make_shared<Lifecycle>();
    require(bool(runtime.add({module,lifecycle})));
    require(!runtime.open({{std::byte{7}}},{rules(),auth->deadline,false}));
    require(bool(runtime.start()));require(lifecycle->starts==1);
    require(runtime.snapshot({}).phase==host::HostPhase::Ready);
    auto session=runtime.open({{std::byte{7}}},{rules(),auth->deadline,false});require(bool(session));
    auto caller=session->verify({principal(),{},{}});require(bool(caller));
    auto computing=session->bind<Value,Value>(key("native.compute"),{},Shape::Read,*caller,std::array{target()},targets,name("consumer.compute"));
    auto reading=session->bind<Value,Value>(key("native.read"),{},Shape::Read,*caller,std::array{target()},targets,name("consumer.read"));
    require(bool(computing)&&bool(reading));
    InvokeOptions options{{},clock->now()+std::chrono::seconds(10),100};
    require(result(computing->invoke(Value{5},options))==7);
    require(result(reading->invoke(Value{5},options))==12);
    auto before=entered;
    auto invalid=computing->invoke(Value{-1},options);
    require(std::holds_alternative<Rejected>(invalid)&&entered==before);
    auto closed=runtime.shutdown_until(clock->now()+std::chrono::seconds(2));
    require(closed.quiescent && closed.phase==host::HostPhase::Stopped && lifecycle->stops==1);
    auto stopped=computing->invoke(Value{5},options);
    require(std::holds_alternative<Rejected>(stopped)&&entered==before);
    require(!runtime.open({{std::byte{7}}},{rules(),auth->deadline,false}));
    std::cout << "{\"sdk\":\"" << ock::sdk::version << "\",\"stage\":\"" << ock::sdk::implementation_stage << R"(","checks":{"read":true,"compute":true,"invalid_input":true,"ready_gate":true,"shutdown":true}})" << '\n';
    return 0;
  } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
