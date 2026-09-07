#include "fixtures.hpp"
#include "packages/runtime/registry/registry.hpp"
#include "test_support.hpp"
#include <cstdlib>
#include <new>
thread_local bool fail_allocation = false;
void *operator new(std::size_t n) {
  if (fail_allocation)
    throw std::bad_alloc();
  if (auto p = std::malloc(n ? n : 1))
    return p;
  throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
struct Config {
  std::shared_ptr<int> value;
};
struct LongContractValue {
  int value;
};
std::weak_ptr<int> frozen_config;
namespace ock::contracts {
template <> struct TypeContract<LongContractValue> {
  static TypeIdentity identity() {
    const auto version = std::string(1024, '1') + ".0.0";
    return {name("long.contract"), *OperationVersion::parse(version, 2048), {}};
  }
  static Result<void> validate(const LongContractValue &) { return {}; }
};
template <> struct TypeContract<Config> {
  static TypeIdentity identity() { return {name("config"), ver(), {}}; }
  static Result<void> validate(const Config &c) {
    return c.value && *c.value >= 0 ? Result<void>{}
                                    : reject(ContractsErrc::InvalidContract);
  }
};
} // namespace ock::contracts
namespace ock::runtime::registry {
template <> struct ConfigurationSnapshot<Config> {
  static Result<Config> freeze(const Config &c) {
    if (!c.value)
      return make_unexpected(error(ContractsErrc::InvalidContract));
    Config copy{std::make_shared<int>(*c.value)};
    frozen_config = copy.value;
    return copy;
  }
};
} // namespace ock::runtime::registry
using namespace ock::runtime::registry;
Result<int> long_args_handler(const LongContractValue &a, WorkContext &) {
  return a.value;
}
Result<LongContractValue> long_result_handler(const int &a, WorkContext &) {
  return LongContractValue{a};
}
class Executor final : public ExecutorPort {
public:
  Result<void> submit(std::unique_ptr<ReadyWork>) override { return {}; }
};
ModuleInput module(const char *n = "module") {
  ModuleManifest m{name(n), ver()};
  m.executors.push_back(name("test"));
  return {
      m,
      {},
      {},
      {},
      {},
      {{name("test"), name("app"), false, false, std::make_shared<Executor>()}},
      {}};
}
OperationOptions options(const char *n = "module") {
  return {{}, {}, {name(n), name("test")}, {}, false};
}
std::unique_ptr<RegistrationBatch> batch(BatchBudget budget = {}) {
  auto b = RegistrationBatch::create(budget);
  CHECK(b);
  return std::move(*b);
}
void typed_binding_success() {
  auto b = batch();
  auto m = module();
  m.manifest.operations.push_back(key());
  m.register_operations = [](Registrar &r) {
    CHECK(
        r.compute(compute_handler, input(AtomicMode::PureCompute), options()));
  };
  CHECK(b->add(m));
  auto p = b->publish();
  CHECK(p);
  auto bound = bind<int, int>(*p, key(), {}, Shape::Read);
  CHECK(bound);
  CHECK(validate_inline_args(*bound, 2));
  CHECK(!validate_inline_args(*bound, -1));
}
void ignored_errors_sticky() {
  auto b = batch();
  auto m = module();
  m.manifest.operations.push_back(key());
  m.register_operations = [](Registrar &r) {
    (void)r.compute(compute_handler, input(), options());
    (void)r.compute(compute_handler, input(AtomicMode::PureCompute), options());
  };
  CHECK(b->add(m));
  CHECK(!b->publish());
  CHECK(b->failed());
  CHECK(!b->errors().empty());
}
void module_cycles_rejected() {
  auto b = batch();
  auto m = module();
  bool called = false;
  m.manifest.dependencies.push_back({m.manifest.name, ver()});
  m.register_operations = [&](Registrar &) { called = true; };
  CHECK(b->add(m));
  CHECK(!b->publish());
  CHECK(!called);
}
void publish_once_freeze() {
  {
    auto nested = batch();
    auto m = module();
    m.manifest.operations.push_back(key());
    auto owner = std::make_shared<Reader>();
    std::weak_ptr<Reader> lifetime = owner;
    bool alive_in_hook = false;
    m.manifest.services.push_back(name("s"));
    m.services.push_back(*ServiceBinding::make(name("s"), owner));
    m.register_operations = [&](Registrar &r) {
      CHECK(!nested->publish());
      CHECK(!nested->publish());
      alive_in_hook = !lifetime.expired();
      (void)r.compute(compute_handler, input(AtomicMode::PureCompute),
                      options());
    };
    CHECK(nested->add(m));
    owner.reset();
    m.services.clear();
    CHECK(!nested->publish());
    CHECK(nested->failed());
    CHECK(alive_in_hook);
    CHECK(lifetime.expired());
  }
  auto b = batch();
  CHECK(b->add(module()));
  auto p = b->publish();
  CHECK(p);
  CHECK(!b->publish());
  CHECK(!b->add(module("later")));
  CHECK((*p)->size() == 0);
}
ModuleInput operation_module(const char *n = "module") {
  auto m = module(n);
  m.manifest.operations.push_back(key());
  m.register_operations = [n](Registrar &r) {
    CHECK(
        r.compute(compute_handler, input(AtomicMode::PureCompute), options(n)));
  };
  return m;
}
std::shared_ptr<const Catalog> catalog() {
  auto b = batch();
  CHECK(b->add(operation_module()));
  auto c = b->publish();
  CHECK(c);
  return *c;
}
void module_dag_order() {
  auto b = batch();
  std::string order;
  auto a = module("a"), z = module("z"), c = module("c");
  a.manifest.dependencies.push_back({name("z"), ver()});
  c.manifest.dependencies.push_back({name("a"), ver()});
  a.register_operations = [&](Registrar &) { order += 'a'; };
  z.register_operations = [&](Registrar &) { order += 'z'; };
  c.register_operations = [&](Registrar &) { order += 'c'; };
  CHECK(b->add(c));
  CHECK(b->add(a));
  CHECK(b->add(z));
  CHECK(b->publish());
  CHECK(order == "zac");
  auto cycle = batch();
  a.manifest.dependencies = {{name("c"), ver()}};
  CHECK(cycle->add(a));
  CHECK(cycle->add(c));
  CHECK(!cycle->publish());
  CHECK(order == "zac");
}
void module_identity_versions() {
  for (int mode = 0; mode < 3; ++mode) {
    auto b = batch();
    auto a = module("a"), c = module("c");
    if (mode == 0)
      c.manifest.name = a.manifest.name;
    else
      c.manifest.dependencies.push_back(
          {name(mode == 1 ? "missing" : "a"), ver("2.0.0")});
    CHECK(b->add(a));
    CHECK(b->add(c));
    CHECK(!b->publish());
  }
}
void manifest_owned_budget() {
  for (bool result : {false, true}) {
    auto b = batch({128, 4096, 16384, 512, 128});
    auto m = operation_module();
    m.register_operations = [result](Registrar &r) {
      auto d = input(AtomicMode::PureCompute);
      d.docs.clear();
      if (result)
        (void)r.compute(long_result_handler, d, options());
      else
        (void)r.compute(long_args_handler, d, options());
    };
    CHECK(b->add(m));
    CHECK(!b->publish());
    CHECK(b->errors()[0].code == RegistryErrc::BudgetExceeded);
  }
  {
    auto b = batch();
    auto m = module();
    m.manifest.services.push_back(name("s"));
    m.services.push_back(
        *ServiceBinding::make(name("s"), std::make_shared<Reader>(Reader{11})));
    m.manifest.required_services.push_back(
        {{name("module"), name("s")}, CppTypeToken::of<Reader>()});
    m.register_operations = [](Registrar &r) {
      auto s = r.service<Reader>(name("module"), name("s"));
      CHECK(s);
      CHECK((*s)->value == 11);
    };
    auto *retained = &m.services[0];
    CHECK(b->add(std::move(m)));
    *retained =
        *ServiceBinding::make(name("s"), std::make_shared<Reader>(Reader{22}));
    CHECK(b->publish());
  }
  {
    auto limited = batch({128, 4096, 16384, 150, 128});
    auto m = operation_module();
    m.register_operations = [](Registrar &r) {
      auto d = input(AtomicMode::PureCompute);
      d.docs.clear();
      d.required_permissions = {
          name("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
               "aaaaaaaaaaaaaaaaaaaaaaaaaaa"),
          name("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"
               "bbbbbbbbbbbbbbbbbbbbbbbbbbb")};
      (void)r.compute(compute_handler, d, options());
    };
    CHECK(limited->add(m));
    CHECK(!limited->publish());
  }
  auto moved = batch();
  auto movable = operation_module();
  auto *retained = &movable.manifest.operations[0];
  CHECK(moved->add(std::move(movable)));
  retained->name = name("mutated");
  auto moved_catalog = moved->publish();
  CHECK(moved_catalog);
  CHECK((*moved_catalog)->find(key()));
  auto b = batch();
  auto m = operation_module();
  CHECK(b->add(m));
  m.manifest.operations.clear();
  m.manifest.name = name("changed");
  auto c = b->publish();
  CHECK(c);
  CHECK((*c)->find(key()));
  CHECK(!RegistrationBatch::create({0, 1, 1, 1, 1}));
  for (int mode = 0; mode < 4; ++mode) {
    BatchBudget budget;
    if (mode == 0)
      budget.modules = 1;
    if (mode == 1)
      budget.operations = 1;
    if (mode == 2)
      budget.text_bytes = 1;
    if (mode == 3)
      budget.declarations = 1;
    auto x = batch(budget);
    auto a = operation_module();
    if (mode == 1)
      a.manifest.operations.push_back({name("other"), ver()});
    if (mode == 3)
      a.manifest.services = {name("one"), name("two")};
    auto first = x->add(a);
    if (mode == 0) {
      CHECK(first);
      CHECK(!x->add(module("second")));
    } else
      CHECK(!first);
    CHECK(!x->publish());
  }
  Config cfg{std::make_shared<int>(7)};
  auto frozen = ConfigurationBinding::make(cfg);
  CHECK(frozen);
  auto held = frozen_config;
  *cfg.value = 9;
  cfg.value.reset();
  CHECK(*held.lock() == 7);
  auto cb = batch();
  auto cm = module();
  cm.manifest.required_configuration.push_back(
      TypeContract<Config>::identity());
  cm.configurations.push_back(std::move(*frozen));
  CHECK(cb->add(std::move(cm)));
  cm.configurations.clear();
  auto cc = cb->publish();
  CHECK(cc);
  CHECK(*held.lock() == 7);
  cc->reset();
  CHECK(held.expired());
  auto missing = batch();
  auto mm = module();
  mm.manifest.required_configuration.push_back(
      TypeContract<Config>::identity());
  CHECK(missing->add(mm));
  CHECK(!missing->publish());
  Config invalid{std::make_shared<int>(-1)};
  CHECK(!ConfigurationBinding::make(invalid));
}
void declared_operations_complete() {
  auto duplicate = batch();
  auto first = operation_module("a"), second = operation_module("b");
  second.register_operations = {};
  CHECK(duplicate->add(first));
  CHECK(duplicate->add(second));
  CHECK(!duplicate->publish());
  for (bool undeclared : {false, true}) {
    auto b = batch();
    auto m = operation_module();
    if (undeclared)
      m.manifest.operations.clear();
    else
      m.register_operations = {};
    CHECK(b->add(m));
    CHECK(!b->publish());
  }
}
void exact_operation_versions() {
  for (bool duplicate : {false, true}) {
    auto b = batch();
    auto m = operation_module();
    auto second = key();
    if (!duplicate)
      second.version = ver("2.0.0");
    m.manifest.operations.push_back(second);
    m.register_operations = [second](Registrar &r) {
      CHECK(r.compute(compute_handler, input(AtomicMode::PureCompute),
                      options()));
      auto d = input(AtomicMode::PureCompute);
      d.key = second;
      (void)r.compute(compute_handler, d, options());
    };
    CHECK(b->add(m));
    auto c = b->publish();
    if (duplicate)
      CHECK(!c);
    else {
      CHECK(c);
      CHECK((*c)->size() == 2);
      CHECK(bind<int, int>(*c, key(), {}, Shape::Read));
      CHECK(bind<int, int>(*c, second, {}, Shape::Read));
      auto absent = key();
      absent.version = ver("3.0.0");
      CHECK(!(*c)->find(absent));
    }
  }
  // 同一精确key不能由另一个模块重复发布。
  auto b = batch();
  CHECK(b->add(operation_module("a")));
  CHECK(b->add(operation_module("b")));
  CHECK(!b->publish());
}
void batch_k_failure_atomic() {
  auto b = batch();
  std::weak_ptr<Reader> w;
  {
    auto a = operation_module("a");
    auto p = std::make_shared<Reader>();
    w = p;
    a.manifest.services.push_back(name("s"));
    a.services.push_back(*ServiceBinding::make(name("s"), p));
    CHECK(b->add(std::move(a)));
  }
  auto z = module("z");
  z.manifest.operations.push_back({name("missing"), ver()});
  CHECK(b->add(z));
  CHECK(!b->publish());
  CHECK(w.expired());
  CHECK(!b->publish());
}
void callback_exception_cleanup() {
  auto b = batch();
  std::weak_ptr<Reader> w;
  {
    auto m = module();
    auto p = std::make_shared<Reader>();
    w = p;
    m.manifest.services.push_back(name("s"));
    m.services.push_back(*ServiceBinding::make(name("s"), p));
    m.register_operations = [](Registrar &) {
      throw std::runtime_error("fault");
    };
    CHECK(b->add(std::move(m)));
  }
  CHECK(!b->publish());
  CHECK(w.expired());
  CHECK(b->failed());
  CHECK(b->errors()[0].code == RegistryErrc::CallbackException);
}
void error_budget_sticky() {
  {
    auto b = batch();
    auto m = operation_module();
    bool caught = false;
    fail_allocation = true;
    try {
      (void)b->add(m);
    } catch (const std::bad_alloc &) {
      caught = true;
    }
    fail_allocation = false;
    CHECK(caught);
    CHECK(b->failed());
    CHECK(!b->publish());
  }
  auto b = batch({128, 4096, 16384, 1048576, 1});
  auto m = operation_module();
  m.register_operations = [](Registrar &r) {
    auto d = input();
    auto o = options();
    for (int i = 0; i < 5; ++i)
      (void)r.compute(compute_handler, d, o);
  };
  CHECK(b->add(m));
  CHECK(!b->publish());
  CHECK(b->errors().size() == 1);
  CHECK(b->diagnostics_truncated());
  for (bool diagnostic : {false, true}) {
    auto x = batch();
    auto a = operation_module();
    bool caught = false;
    a.register_operations = [&](Registrar &r) {
      auto d = input(AtomicMode::PureCompute);
      auto o = options();
      auto f = diagnostic
                   ? static_cast<Result<int> (*)(const int &, WorkContext &)>(
                         nullptr)
                   : compute_handler;
      fail_allocation = true;
      try {
        (void)r.compute(f, d, o);
      } catch (const std::bad_alloc &) {
        caught = true;
      }
      fail_allocation = false;
    };
    CHECK(x->add(a));
    CHECK(!x->publish());
    CHECK(x->failed());
    CHECK(x->diagnostics_truncated());
    CHECK(caught != diagnostic);
  }
}
void attach_service(ModuleInput &m, std::shared_ptr<Reader> p) {
  m.manifest.services.push_back(name("s"));
  m.services.push_back(*ServiceBinding::make(name("s"), p));
}
void service_binding() {
  CHECK(!ServiceBinding::make<Reader>(name("s"), {}));
  Reader borrowed;
  CHECK(!ServiceBinding::make(
      name("s"),
      std::shared_ptr<Reader>(std::shared_ptr<Reader>{}, &borrowed)));
  for (int mode = 0; mode < 5; ++mode) {
    auto b = batch();
    auto a = module("a"), z = module("z"), m = module();
    attach_service(a, std::make_shared<Reader>(Reader{11}));
    attach_service(z, std::make_shared<Reader>(Reader{22}));
    m.manifest.dependencies = {{name("a"), ver()}, {name("z"), ver()}};
    m.manifest.required_services.push_back(
        {{name("z"), name("s")}, CppTypeToken::of<Reader>()});
    m.manifest.operations.push_back(key());
    if (mode == 1)
      m.manifest.required_services[0].ref.name = name("missing");
    if (mode == 2)
      m.manifest.required_services[0].type = CppTypeToken::of<Other>();
    if (mode == 3)
      m.manifest.dependencies.pop_back();
    m.register_operations = [mode](Registrar &r) {
      auto o = options();
      o.read_service = ServiceRef{name(mode == 4 ? "a" : "z"), name("s")};
      if (mode == 0) {
        auto s = r.service<Reader>(name("z"), name("s"));
        CHECK(s);
        CHECK((*s)->value == 22);
      }
      (void)r.read(read_handler, input(), o);
    };
    CHECK(b->add(a));
    CHECK(b->add(z));
    CHECK(b->add(m));
    auto c = b->publish();
    CHECK(bool(c) == (mode == 0));
  }
  auto missing = batch();
  auto m = module();
  m.manifest.services.push_back(name("s"));
  CHECK(missing->add(m));
  CHECK(!missing->publish());
}
template <class P> class AtomicPort final : public AtomicProviderPort<P> {
public:
  Result<std::unique_ptr<typename P::Frame>>
  begin(const AtomicDomainRef &) override {
    return std::make_unique<typename P::Frame>();
  }
  Result<std::shared_ptr<const PreparedCommit>>
  prepare(typename P::Frame &, const PreparedIdentity &) override {
    return make_unexpected(error(ContractsErrc::Rejected));
  }
  Result<void> commit(std::shared_ptr<const PreparedCommit>,
                      std::shared_ptr<const ActionPermit>,
                      std::shared_ptr<CommitReceiver>) override {
    return {};
  }
};
template <class P> void attach_provider(ModuleInput &m) {
  m.manifest.providers.push_back(ProviderContract<P>::key());
  std::shared_ptr<AtomicProviderPort<P>> p = std::make_shared<AtomicPort<P>>();
  m.providers.push_back(*ProviderBinding::make<P>(p));
}
void provider_binding() {
  CHECK(!ProviderBinding::make<Provider>({}));
  AtomicPort<Provider> borrowed;
  CHECK(!ProviderBinding::make<Provider>(
      std::shared_ptr<AtomicProviderPort<Provider>>(
          std::shared_ptr<AtomicProviderPort<Provider>>{}, &borrowed)));
  for (int mode = 0; mode < 5; ++mode) {
    auto b = batch();
    auto a = module("a"), z = module("z"), m = module();
    attach_provider<Provider>(a);
    if (mode == 2)
      attach_provider<OtherProvider>(z);
    else
      attach_provider<Provider>(z);
    m.manifest.dependencies = {{name("a"), ver()}, {name("z"), ver()}};
    m.manifest.required_providers.push_back(
        {{name("z"), name("provider")}, CppTypeToken::of<Provider>()});
    m.manifest.operations.push_back(key());
    if (mode == 3)
      m.manifest.dependencies.pop_back();
    if (mode == 1)
      m.manifest.required_providers[0].ref.key = name("missing");
    m.register_operations = [mode](Registrar &r) {
      auto o = options();
      o.provider = ProviderRef{name(mode == 4 ? "a" : "z"), name("provider")};
      (void)r.state_edit(edit_handler, input(AtomicMode::StateEdit), o);
    };
    CHECK(b->add(a));
    CHECK(b->add(z));
    CHECK(b->add(m));
    CHECK(bool(b->publish()) == (mode == 0));
  }
}
void execution_capabilities() {
  for (bool resource : {false, true}) {
    auto b = batch();
    auto m = module();
    Executor e;
    ResourceLease r;
    if (resource) {
      m.manifest.resources.push_back(name("r"));
      m.resources.push_back(
          {name("r"), std::shared_ptr<ResourceLease>(
                          std::shared_ptr<ResourceLease>{}, &r)});
    } else
      m.executors[0].owner =
          std::shared_ptr<ExecutorPort>(std::shared_ptr<ExecutorPort>{}, &e);
    CHECK(b->add(m));
    CHECK(!b->publish());
  }
  for (int mode = 0; mode < 8; ++mode) {
    auto b = batch();
    auto a = module("a"), z = module("z"), m = operation_module();
    a.executors[0].affinity = name("wrong");
    z.manifest.resources.push_back(name("resource"));
    z.resources.push_back(
        {name("resource"), std::make_shared<ResourceLease>()});
    m.manifest.dependencies = {{name("a"), ver()}, {name("z"), ver()}};
    m.manifest.required_executors.push_back({name("z"), name("test")});
    m.manifest.required_resources.push_back({name("z"), name("resource")});
    if (mode == 1)
      z.executors[0].owner.reset();
    if (mode == 2)
      z.executors[0].affinity = name("wrong");
    if (mode == 3)
      m.manifest.dependencies.pop_back();
    if (mode == 4)
      z.resources[0].owner.reset();
    if (mode == 6)
      m.manifest.required_executors.clear();
    m.register_operations = [mode](Registrar &r) {
      auto d = input(AtomicMode::PureCompute);
      auto o = options();
      o.executor.module = name(mode == 5 ? "a" : "z");
      o.resources.push_back({name("z"), name("resource")});
      if (mode == 7)
        d.execution.requires_async_dispatch = true;
      (void)r.compute(compute_handler, d, o);
    };
    CHECK(b->add(a));
    CHECK(b->add(z));
    CHECK(b->add(m));
    CHECK(bool(b->publish()) == (mode == 0));
  }
}
void shape_compile_contract() {
  auto b = batch();
  auto m = module();
  attach_service(m, std::make_shared<Reader>());
  attach_provider<Provider>(m);
  m.manifest.required_services.push_back(
      {{name("module"), name("s")}, CppTypeToken::of<Reader>()});
  m.manifest.required_providers.push_back(
      {{name("module"), name("provider")}, CppTypeToken::of<Provider>()});
  for (auto n : {"read", "edit", "effect", "lifecycle"})
    m.manifest.operations.push_back({name(n), ver()});
  m.register_operations = [](Registrar &r) {
    auto d = input();
    auto o = options();
    d.key.name = name("read");
    o.read_service = ServiceRef{name("module"), name("s")};
    CHECK(r.read(read_handler, d, o));
    o.read_service.reset();
    o.provider = ProviderRef{name("module"), name("provider")};
    d.key.name = name("edit");
    d.atomic_mode = AtomicMode::StateEdit;
    CHECK(r.state_edit(edit_handler, d, o));
    o.provider.reset();
    d.atomic_mode = AtomicMode::Incompatible;
    d.key.name = name("effect");
    CHECK(r.external_effect(effect_handler, d, o));
    d.key.name = name("lifecycle");
    CHECK(r.lifecycle(transition_handler, d, o));
  };
  CHECK(b->add(m));
  auto c = b->publish();
  CHECK(c);
  CHECK((*c)->size() == 4);
}
void atomic_candidate_contract() {
  for (int mode = 0; mode < 4; ++mode) {
    auto b = batch();
    auto m = module();
    attach_service(m, std::make_shared<Reader>());
    attach_provider<Provider>(m);
    m.manifest.required_services.push_back(
        {{name("module"), name("s")}, CppTypeToken::of<Reader>()});
    m.manifest.required_providers.push_back(
        {{name("module"), name("provider")}, CppTypeToken::of<Provider>()});
    m.manifest.operations.push_back(key());
    m.register_operations = [mode](Registrar &r) {
      auto d = input();
      auto o = options();
      o.read_service = ServiceRef{name("module"), name("s")};
      o.provider = ProviderRef{name("module"), name("provider")};
      if (mode == 1)
        o.provider.reset();
      if (mode == 2)
        d.execution.requires_async_dispatch = true;
      auto fn = mode == 3
                    ? static_cast<Result<int> (*)(
                          const int &, const Provider::CandidateReadPort &,
                          WorkContext &)>(nullptr)
                    : candidate_handler;
      (void)r.candidate_read<int, int, Reader, Provider>(read_handler, fn, d,
                                                         o);
    };
    CHECK(b->add(m));
    CHECK(bool(b->publish()) == (mode == 0));
  }
}
void schema_capability_absent() {
  auto b = batch();
  auto m = operation_module();
  m.register_operations = [](Registrar &r) {
    auto o = options();
    o.requires_dynamic_schema = true;
    (void)r.compute(compute_handler, input(AtomicMode::PureCompute), o);
  };
  CHECK(b->add(m));
  CHECK(!b->publish());
  CHECK(b->errors()[0].code == RegistryErrc::UnsupportedSchema);
}
void exact_fingerprint() {
  auto c = catalog();
  CHECK(bind<int, int>(c, key(), {}, Shape::Read));
  CHECK(!bind<Other, Other>(c, key(), {}, Shape::Read));
  CHECK(!bind<int, int>(c, key(), {}, Shape::StateEdit));
  ContractDigest d{};
  d.bytes[0] = std::byte{1};
  CHECK(!bind<int, int>(c, key(), d, Shape::Read));
}
void registry_generation() {
  auto a = catalog(), b = catalog();
  CHECK(a->identity() != b->identity());
  auto h = *a->find(key());
  CHECK(foundation::resolve_slot(h, a->identity(), a->generation(), a->size()));
  CHECK(
      !foundation::resolve_slot(h, b->identity(), b->generation(), b->size()));
  h.generation.bytes[15] = 2;
  CHECK(
      !foundation::resolve_slot(h, a->identity(), a->generation(), a->size()));
  h = *a->find(key());
  h.slot = 99;
  CHECK(
      !foundation::resolve_slot(h, a->identity(), a->generation(), a->size()));
  CHECK(!a->describe(99));
}
void snapshot_lifetime() {
  std::weak_ptr<Reader> w;
  std::shared_ptr<const Catalog> c;
  {
    auto b = batch();
    auto m = operation_module();
    auto owner = std::make_shared<Reader>();
    w = owner;
    attach_service(m, owner);
    CHECK(b->add(m));
    c = *b->publish();
  }
  auto bound = bind<int, int>(c, key(), {}, Shape::Read);
  CHECK(bound);
  auto desc = c->describe(0);
  CHECK(desc);
  c.reset();
  CHECK(!w.expired());
  CHECK(bound->revalidate());
  bound = make_unexpected(error(ContractsErrc::Rejected));
  CHECK(w.expired());
  CHECK((*desc)->description().docs == "测试");
}
void failure_preserves_catalog() {
  auto old = catalog();
  auto h = old->find(key());
  auto b = batch();
  auto m = operation_module();
  m.register_operations = {};
  CHECK(b->add(m));
  CHECK(!b->publish());
  CHECK(old->find(key()));
  CHECK(old->size() == 1);
  CHECK(old->identity() == h->registry);
}
void cold_docs_separation() {
  auto c = catalog();
  auto d = c->describe(0);
  CHECK(d);
  CHECK((*d)->description().docs == "测试");
  CHECK((*d)->description().key == key());
  static_assert(!std::is_copy_constructible_v<Catalog>);
}
void handler_not_exposed() {
  auto c = catalog();
  CHECK(c->size() == 1);
  CHECK(c->describe(0));
  static_assert(
      !std::is_constructible_v<Registrar, RegistrationBatch &, std::size_t>);
}
void internal_component_boundary() {
  static_assert(std::derived_from<Catalog, BindingPort>);
  CHECK(catalog()->size() == 1);
}
int main(int argc, char **argv) {
  std::map<std::string, void (*)()> cases{
#define CASE(prefix, n) {prefix ".registration." #n, n}
      CASE("T02", module_dag_order),
      CASE("T02", module_cycles_rejected),
      CASE("T02", module_identity_versions),
      CASE("T02", manifest_owned_budget),
      CASE("T02", declared_operations_complete),
      CASE("T02", exact_operation_versions),
      CASE("T02", batch_k_failure_atomic),
      CASE("T02", ignored_errors_sticky),
      CASE("T02", callback_exception_cleanup),
      CASE("T02", error_budget_sticky),
      CASE("T02", service_binding),
      CASE("T02", provider_binding),
      CASE("T02", execution_capabilities),
      CASE("T02", shape_compile_contract),
      CASE("T02", atomic_candidate_contract),
      CASE("T02", schema_capability_absent),
      CASE("T05", publish_once_freeze),
      CASE("T05", typed_binding_success),
      CASE("T05", exact_fingerprint),
      CASE("T05", registry_generation),
      CASE("T05", snapshot_lifetime),
      CASE("T05", failure_preserves_catalog),
      CASE("T24", cold_docs_separation),
      CASE("T24", handler_not_exposed),
      CASE("T24", internal_component_boundary)
#undef CASE
  };
  if (argc == 2 && std::string(argv[1]) == "--list") {
    for (auto &[n, f] : cases)
      std::cout << n << '\n';
    return 0;
  }
  try {
    CHECK(argc == 2);
    auto i = cases.find(argv[1]);
    CHECK(i != cases.end());
    i->second();
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
