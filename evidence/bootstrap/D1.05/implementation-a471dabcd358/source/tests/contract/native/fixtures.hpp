#pragma once
#include "packages/runtime/invocation/invocation.hpp"
#include "tests/contract/authorization/fixtures.hpp"
namespace native_test {
using namespace ock::runtime;
using namespace invocation;
inline std::atomic<unsigned> entered{0};
inline Result<int> compute(const int& a, WorkContext&) { ++entered; return a + 2; }
inline Result<int> read(const int& a, WorkContext&, ReadServices<Reader>& service) { ++entered; return a + service.reader().read(); }
struct Threads final : TrustedThreadPort {
  std::thread::id owner = std::this_thread::get_id();
  Result<ThreadObservation> current() const noexcept override {
    if (owner != std::this_thread::get_id()) return make_unexpected(invocation_error(InvocationErrc::ThreadRejected));
    return ThreadObservation{ThreadRole::Application, name("app"), true};
  }
};
struct Executor final : ExecutorPort { Result<void> submit(std::unique_ptr<ReadyWork>) override { throw std::runtime_error("unexpected submit"); } };
inline Result<std::size_t> targets(const int&, std::span<foundation::ObjectId> out) noexcept {
  if (out.empty()) return make_unexpected(error(ContractsErrc::BudgetExceeded));
  out[0] = policy_test::target(); return 1;
}
struct Env {
  policy_test::Env policy;
  std::shared_ptr<Reader> reader = std::make_shared<Reader>();
  std::shared_ptr<const registry::Catalog> catalog;
  std::shared_ptr<NativeEngine> engine;
  explicit Env(bool reading=false) {
    registry::ModuleManifest manifest{name("native"), ver()};
    manifest.operations.push_back(key()); manifest.executors.push_back(name("test"));
    registry::ModuleInput module{manifest, {}, {}, {}, {}, {{name("test"), name("app"), false, false, std::make_shared<Executor>()}}, {}};
    if (reading) { module.manifest.services.push_back(name("reader")); module.services.push_back(*registry::ServiceBinding::make(name("reader"), reader)); }
    module.register_operations = [reading](registry::Registrar& registrar) {
      DefinitionInput d{key(), {}, {true,false,false,name("test"),name("app")}, reading?AtomicMode::Incompatible:AtomicMode::PureCompute, {name("allow")}, "native"};
      registry::OperationOptions o{{}, {}, {name("native"),name("test")}, {}, false};
      if (reading) { o.read_service = registry::ServiceRef{name("native"),name("reader")}; CHECK(registrar.read(read,d,o)); }
      else CHECK(registrar.compute(compute,d,o));
    };
    auto batch = registry::RegistrationBatch::create({}); CHECK(batch); CHECK((*batch)->add(module));
    auto published=(*batch)->publish(); CHECK(published);catalog=*published;
    auto built=NativeEngine::create(catalog,policy.session,std::make_shared<Threads>(),{});CHECK(built);engine=*built;
  }
  auto bind() { return engine->bind<int,int>(key(),{},Shape::Read,policy.caller,std::array{policy_test::target()},targets,name("native.call")); }
  InvokeOptions options() { return {{},policy.clock->now()+std::chrono::seconds(1),100}; }
};
inline int result(const InvokeReply<int>& reply) {
  CHECK(std::holds_alternative<Completed<int>>(reply));
  const auto& outcome=std::get<Completed<int>>(reply).outcome;
  CHECK(std::holds_alternative<ReadCompleted<int>>(outcome.value()));
  return *std::get<ReadCompleted<int>>(outcome.value()).result;
}
}
