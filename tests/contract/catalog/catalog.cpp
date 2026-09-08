#include "tests/contract/authorization/fixtures.hpp"
#include <ock/dynamic/catalog/catalog.hpp>
using namespace ock;
struct Registry final : contracts::BindingPort {
  std::shared_ptr<const contracts::DefinitionSnapshot> definition;
  foundation::RegistryId identity() const noexcept override {
    return id<foundation::RegistryId>();
  }
  foundation::RegistryGeneration generation() const noexcept override {
    return id<foundation::RegistryGeneration>();
  }
  std::size_t size() const noexcept override { return 1; }
  Result<contracts::OperationHandle>
  find(const contracts::OperationKey &) const override {
    return foundation::make_unexpected(
        contracts::error(contracts::ContractsErrc::InvalidContract));
  }
  Result<std::shared_ptr<const contracts::DefinitionSnapshot>>
  describe(std::uint32_t n) const override {
    if (n)
      return foundation::make_unexpected(
          contracts::error(contracts::ContractsErrc::InvalidContract));
    return definition;
  }
};
Result<int> compute(const int &n, WorkContext &) { return n; }
int main() try {
  policy_test::Env env;
  auto registry = std::make_shared<Registry>();
  auto input = ::input(AtomicMode::PureCompute);
  auto definition =
      contracts::OperationDefinition<int, int>::compute(compute, input);
  CHECK(definition);
  registry->definition = definition->snapshot();
  auto schema = data::Payload::parse(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"integer"})");
  CHECK(schema);
  auto catalog = catalog::Catalog::create(
      registry,
      {{contracts::TypeContract<int>::identity(), std::move(*schema).share()}});
  CHECK(catalog);
  auto caller = env.session->verify({policy_test::principal(), {}, {}});
  CHECK(caller);
  auto page =
      catalog->search(*env.session, **caller, policy_test::target(), {});
  CHECK(page && page->items.size() == 1 && page->items[0].eligible &&
        page->fingerprint.size() == 64);
  CHECK(page->items[0].definition.get() == registry->definition.get());
  CHECK(catalog::Catalog::help(page->items[0]));
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
  CHECK(!catalog->search(*env.session, **caller, policy_test::target(),
                         {"", 0, 201}));
  std::cout << "Catalog policy visibility and cold metadata passed\n";
} catch (const std::exception &e) {
  std::cerr << e.what() << "\n";
  return 1;
}
