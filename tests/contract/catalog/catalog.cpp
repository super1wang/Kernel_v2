#include "tests/contract/native/fixtures.hpp"
#include <fstream>
#include <ock/control/catalog_methods.hpp>
#include <ock/dynamic/catalog/catalog.hpp>
using namespace ock;
struct Value {
  std::int64_t amount;
};
struct Fields {
  static contracts::TypeIdentity identity() {
    return {name("catalog.value"), ver(), {}};
  }
  static auto fields() {
    auto f = binding::field<&Value::amount>("amount");
    f.minimum = 0;
    f.unit = "count";
    return std::tuple(f);
  }
};
namespace ock::contracts {
template <>
struct TypeContract<Value> : binding::TypeContract<Value, Fields> {};
} // namespace ock::contracts
Result<Value> compute(const Value &n, WorkContext &) { return n; }
int main(int argc, char **argv) try {
  CHECK(argc == 2);
  policy_test::Env env;
  auto input = ::input(AtomicMode::PureCompute);
  input.docs =
      R"({"format":"ock.command-docs/1","purpose":"Echo a nonnegative integer","counterexamples":["Negative input is invalid"],"coordinates":"not applicable","position_mode":"not applicable","impact_scope":"returned value only","id_sources":"query the current host; no stored IDs","cancellation":"cooperative deadline before call","retry":{"natural_idempotence":"yes","framework_deduplication":"not provided","device_deduplication":"not applicable"},"durability":"not provided","result_phases":"ReadCompleted or FailedBeforeApply","error_repair":"correct the input then retry"})";
  input.execution = {true, false, false, name("test"), name("app")};
  runtime::registry::ModuleManifest manifest{name("native"), ver()};
  manifest.operations.push_back(key());
  manifest.executors.push_back(name("test"));
  runtime::registry::ModuleInput module{
      manifest,
      {},
      {},
      {},
      {},
      {{name("test"), name("app"), false, false,
        std::make_shared<native_test::Executor>()}},
      {}};
  module.register_operations = [input](
                                   runtime::registry::Registrar &registrar) {
    CHECK(registrar.compute(
        compute, input, {{}, {}, {name("native"), name("test")}, {}, false}));
  };
  auto batch = runtime::registry::RegistrationBatch::create({});
  CHECK(batch);
  CHECK((*batch)->add(module));
  auto registry = (*batch)->publish();
  CHECK(registry);
  auto schema = catalog::TypeSchema::generated<Value>();
  CHECK(schema);
  auto catalog = catalog::Catalog::create(*registry, {*schema});
  CHECK(catalog);
  auto caller = env.session->verify({policy_test::principal(), {}, {}});
  CHECK(caller);
  auto page =
      catalog->search(*env.session, **caller, policy_test::target(), {});
  CHECK(page && page->items.size() == 1 && page->items[0].eligible &&
        page->fingerprint.size() == 64);
  CHECK(page->items[0].definition.get() ==
        (*registry)->describe(0).value().get());
  auto help = catalog::Catalog::help(page->items[0]);
  CHECK(help && help->find("x-unit=count") != std::string::npos &&
        help->find("device_deduplication") != std::string::npos);
  CHECK(!catalog::Catalog::help(page->items[0], 2));
  auto card = catalog::Catalog::command_card(page->items[0]);
  CHECK(card && card->view().at("docs").at("purpose").string() ==
                    "Echo a nonnegative integer");
  CHECK(card->view().at("atomic_mode").string() == "PureCompute");
  CHECK(card->view()
            .at("args_schema")
            .at("properties")
            .at("amount")
            .at("x-unit")
            .string() == "count");
  std::ifstream stream(argv[1]);
  std::string schema_text((std::istreambuf_iterator<char>(stream)), {});
  auto card_schema = binding::CompiledSchema::compile(schema_text);
  CHECK(card_schema && card_schema->validate(card->view()));
  auto small = data::Budget{};
  small.nodes = 2;
  CHECK(!catalog::Catalog::command_card(page->items[0], small));
  auto hidden = page->items[0];
  hidden.visible = false;
  CHECK(!catalog::Catalog::command_card(hidden));
  auto incomplete = page->items[0];
  incomplete.command_docs.reset();
  CHECK(!catalog::Catalog::command_card(incomplete));
  auto methods = control::catalog_methods(
      std::make_shared<const catalog::Catalog>(*catalog), env.session,
      policy_test::target());
  CHECK(methods);
  auto router = control::Router::create(
      {"test.app", std::string(32, '1'), std::string(32, '1')}, *caller,
      *methods);
  CHECK(router);
  auto send = [&](std::string_view json) {
    auto p = data::Payload::parse(json);
    CHECK(p);
    return router->dispatch(p->view());
  };
  CHECK(send(
      R"({"jsonrpc":"2.0","id":"h","method":"host.hello","params":{"api_version":"ock.control/1"}})"));
  auto search = send(
      R"({"jsonrpc":"2.0","id":"s","method":"capabilities.search","params":{}})");
  CHECK(search);
  auto wire = data::Payload::parse(search->json);
  CHECK(wire && wire->view().at("result").at("items").size() == 1);
  auto describe = send(
      R"({"jsonrpc":"2.0","id":"d","method":"capabilities.describe","params":{"name":"test.read","version":"1.0.0"}})");
  CHECK(describe);
  wire = data::Payload::parse(describe->json);
  CHECK(wire && card_schema->validate(wire->view().at("result")));
  auto missing = key();
  missing.name = name("not.installed");
  CHECK(!catalog->describe(*env.session, **caller, policy_test::target(),
                           missing));
  auto principal = policy_test::configuration().principals[0];
  std::erase_if(principal.rules, [](const auto &rule) {
    return rule.use == runtime::policy::AccessUse::Invoke;
  });
  CHECK(env.assembly.administration->replace_principal_policy(principal));
  page = catalog->search(*env.session, **caller, policy_test::target(), {});
  CHECK(page && page->items.size() == 1 && !page->items[0].eligible);
  principal.rules.clear();
  CHECK(env.assembly.administration->replace_principal_policy(principal));
  page = catalog->search(*env.session, **caller, policy_test::target(), {});
  CHECK(page && page->items.empty());
  CHECK(
      !catalog->describe(*env.session, **caller, policy_test::target(), key()));
  describe = send(
      R"({"jsonrpc":"2.0","id":"d2","method":"capabilities.describe","params":{"name":"test.read","version":"1.0.0"}})");
  CHECK(describe);
  wire = data::Payload::parse(describe->json);
  CHECK(wire && wire->view().at("error").at("code").int64() == -32010);
  auto unknown = send(
      R"({"jsonrpc":"2.0","id":"d3","method":"capabilities.describe","params":{"name":"not.installed","version":"1.0.0"}})");
  CHECK(unknown);
  auto unknown_wire = data::Payload::parse(unknown->json);
  CHECK(unknown_wire &&
        unknown_wire->view().at("error").at("code").int64() == -32010);
  CHECK(!catalog->search(*env.session, **caller, policy_test::target(),
                         {"", 0, 201}));
  input.docs = "{}";
  module.register_operations = [input](
                                   runtime::registry::Registrar &registrar) {
    CHECK(registrar.compute(
        compute, input, {{}, {}, {name("native"), name("test")}, {}, false}));
  };
  auto incomplete_batch = runtime::registry::RegistrationBatch::create({});
  CHECK(incomplete_batch);
  CHECK((*incomplete_batch)->add(module));
  auto incomplete_registry = (*incomplete_batch)->publish();
  CHECK(incomplete_registry);
  CHECK(!catalog::Catalog::create(*incomplete_registry, {*schema}));
  std::cout << "Catalog policy visibility and cold metadata passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
