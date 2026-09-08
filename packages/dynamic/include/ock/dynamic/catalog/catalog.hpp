#pragma once
#include <ock/dynamic/binding/schema.hpp>
#include <ock/runtime/policy.hpp>
namespace ock::catalog {
using foundation::Result;
struct TypeSchema {
  contracts::TypeIdentity identity;
  data::SharedPayload schema;
};
struct Description {
  std::shared_ptr<const contracts::DefinitionSnapshot> definition;
  data::SharedPayload args_schema, result_schema;
  bool installed = true, visible = true, eligible = false;
};
struct SearchRequest {
  std::string prefix;
  std::size_t offset = 0, page_size = 50;
};
struct Page {
  std::vector<Description> items;
  std::optional<std::size_t> next_offset;
  std::string fingerprint;
};
class Catalog {
public:
  static Result<Catalog> create(std::shared_ptr<const contracts::BindingPort>,
                                std::vector<TypeSchema>);
  Result<Page> search(const runtime::policy::SessionAuthority &,
                      const runtime::policy::VerifiedCaller &,
                      foundation::ObjectId, const SearchRequest &) const;
  Result<Description> describe(const runtime::policy::SessionAuthority &,
                               const runtime::policy::VerifiedCaller &,
                               foundation::ObjectId,
                               const contracts::OperationKey &) const;
  static Result<std::string> help(const Description &,
                                  std::size_t max_bytes = 16384);

private:
  Catalog(std::shared_ptr<const contracts::BindingPort> registry,
          std::vector<TypeSchema> schemas)
      : registry_(std::move(registry)), schemas_(std::move(schemas)) {}
  Result<Description> entry(std::uint32_t) const;
  std::shared_ptr<const contracts::BindingPort> registry_;
  std::vector<TypeSchema> schemas_;
  std::vector<Description> entries_;
  std::vector<std::string> fingerprints_;
};
} // namespace ock::catalog
