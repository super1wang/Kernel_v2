#include "packages/dynamic/data/backend.hpp"
#include <jsoncons_ext/jsonschema/draft202012/schema_builder_202012.hpp>
#include <mutex>
#include <ock/dynamic/binding/record.hpp>
#include <ock/dynamic/binding/schema.hpp>

namespace ock::binding {
using Json = data::detail::Json;
namespace {
constexpr std::string_view dialect =
    "https://json-schema.org/draft/2020-12/schema";
// default 会生成变更 patch；此只读入口不提供默认值注入。
bool supported(data::ValueView v) {
  if (v.kind() != data::Kind::Object) return true;
  if (!v.at("default").missing()) return false;
  // Only schema locations are traversed; property names and const/enum data
  // are not schema keywords.
  for (auto key : {"properties", "patternProperties", "$defs", "dependentSchemas"}) {
    auto map = v.at(key);
    if (map.kind() == data::Kind::Object)
      for (std::size_t i = 0; i < map.size(); ++i)
        if (!supported(map.at(*map.key_at(i)))) return false;
  }
  for (auto key : {"allOf", "anyOf", "oneOf", "prefixItems"}) {
    auto array = v.at(key);
    if (array.kind() == data::Kind::Array)
      for (std::size_t i = 0; i < array.size(); ++i)
        if (!supported(array.at(i))) return false;
  }
  for (auto key : {"not", "if", "then", "else", "items", "contains",
                   "additionalProperties", "propertyNames", "unevaluatedItems",
                   "unevaluatedProperties", "contentSchema"})
    if (!supported(v.at(key))) return false;
  return true;
}
void append(std::string &key, std::string_view value) {
  key += std::to_string(value.size());
  key += ':';
  key.append(value);
}
// jsoncons 0.178.0 会将无 default 的对象 Schema 也暴露为 optional(null)，
// 导致只读校验积累无用 patch。包装节点关闭默认值投影，不改变验证规则。
namespace js = jsoncons::jsonschema;
class ReadOnlyValidator final : public js::schema_validator<Json> {
  std::unique_ptr<js::schema_validator<Json>> inner_;

public:
  explicit ReadOnlyValidator(std::unique_ptr<js::schema_validator<Json>> p)
      : inner_(std::move(p)) {}
  bool always_fails() const override { return inner_->always_fails(); }
  bool always_succeeds() const override { return inner_->always_succeeds(); }
  jsoncons::optional<Json> get_default_value() const override { return {}; }
  bool recursive_anchor() const override { return inner_->recursive_anchor(); }
  const jsoncons::optional<jsoncons::uri> &id() const override {
    return inner_->id();
  }
  const jsoncons::optional<jsoncons::uri> &dynamic_anchor() const override {
    return inner_->dynamic_anchor();
  }
  const js::schema_validator<Json> *
  get_schema_for_dynamic_anchor(const std::string &a) const override {
    return inner_->get_schema_for_dynamic_anchor(a);
  }
  const jsoncons::uri &schema_location() const override {
    return inner_->schema_location();
  }

private:
  js::walk_result do_validate(const js::evaluation_context<Json> &c,
                              const Json &v,
                              const jsoncons::jsonpointer::json_pointer &p,
                              js::evaluation_results &r, js::error_reporter &e,
                              Json &patch) const override {
    return inner_->validate(c, v, p, r, e, patch);
  }
  js::walk_result do_walk(const js::evaluation_context<Json> &c, const Json &v,
                          const jsoncons::jsonpointer::json_pointer &p,
                          const walk_reporter_type &r) const override {
    return inner_->walk(c, v, p, r);
  }
};
class ReadOnlyBuilder final
    : public js::draft202012::schema_builder_202012<Json> {
  using Base = js::draft202012::schema_builder_202012<Json>;
  schema_store_type *store_;

public:
  template <class Factory>
  ReadOnlyBuilder(Json source, const Factory &factory,
                  const js::evaluation_options &options,
                  schema_store_type *store,
                  const std::vector<js::schema_resolver<Json>> &resolvers,
                  const std::unordered_map<std::string, bool> &vocabulary)
      : Base(std::move(source), factory, options, store, resolvers, vocabulary),
        store_(store) {}
  schema_validator_type
  make_schema_validator(const js::compilation_context &context,
                        const Json &schema,
                        jsoncons::span<const std::string> keys,
                        anchor_uri_map_type &anchors) override {
    auto inner = Base::make_schema_validator(context, schema, keys, anchors);
    auto *old = inner.get();
    auto wrapped = std::make_unique<ReadOnlyValidator>(std::move(inner));
    for (auto &[uri, pointer] : *store_)
      if (pointer == old)
        pointer = wrapped.get();
    return wrapped;
  }
};
// 只实例化已锁定的 2020-12；旧 draft 工厂不支持此私有预算 allocator。
struct Factory {
  std::unique_ptr<jsoncons::jsonschema::schema_builder<Json>> operator()(
      Json source, const jsoncons::jsonschema::evaluation_options &options,
      std::map<jsoncons::uri, jsoncons::jsonschema::schema_validator<Json> *>
          *store,
      const std::vector<jsoncons::jsonschema::schema_resolver<Json>> &resolvers,
      const std::unordered_map<std::string, bool> &vocabulary) const {
    if (!source.is_object() && !source.is_bool())
      throw std::invalid_argument("Invalid schema type");
    if (source.is_object() && source.contains("$schema") &&
        source.at("$schema").as_string_view() != dialect)
      throw std::invalid_argument("Unsupported dialect");
    return std::make_unique<ReadOnlyBuilder>(std::move(source), *this, options,
                                             store, resolvers, vocabulary);
  }
};
} // namespace
struct CompiledSchema::State {
  // 编译器持有的 JSON 常量先销毁，再释放其分配账户。
  data::detail::Account account;
  std::string key;
  std::vector<data::Payload> resources;
  std::unique_ptr<jsoncons::jsonschema::json_schema<Json>> compiled;
  explicit State(std::string k)
      : account([] {
          data::Budget b;
          b.allocation_bytes = 64 * 1024 * 1024;
          return b;
        }()),
        key(std::move(k)) {}
};
foundation::Result<CompiledSchema>
CompiledSchema::compile(std::string_view text,
                        const SchemaResources &resources) {
  if (text.size() > 16384 || resources.size() > 16)
    return foundation::make_unexpected(reject().error());
  std::size_t total = text.size();
  std::string key;
  append(key, dialect);
  append(key, text);
  for (const auto &[uri, value] : resources) {
    if (uri.empty() || uri.size() > 256 || value.size() > 16384 ||
        total > 262144 - value.size())
      return foundation::make_unexpected(reject().error());
    total += value.size();
    append(key, uri);
    append(key, value);
  }
  // 有界弱缓存，键完整包含 dialect/内容/有序依赖，不能只靠冲突未检查的散列。
  static std::mutex mutex;
  static std::map<std::string, std::weak_ptr<const State>> cache;
  std::lock_guard lock(mutex);
  if (auto it = cache.find(key); it != cache.end())
    if (auto state = it->second.lock())
      return CompiledSchema(std::move(state));
  try {
    auto root = data::Payload::parse(text);
    if (!root || !supported(root->view()) ||
        root->view().at("$schema").string() != dialect)
      return foundation::make_unexpected(reject().error());
    auto state = std::make_shared<State>(std::move(key));
    data::detail::Scope scope(state->account);
    state->account.reserve_proxies(64 + total * 4);
    auto resolver = [&](const auto &input) -> Json {
      std::string uri;
      if constexpr (std::same_as<std::remove_cvref_t<decltype(input)>,
                                 std::string>)
        uri = input;
      else
        uri = input.string();
      auto i = resources.find(uri);
      if (i == resources.end())
        throw std::invalid_argument("Unregistered schema reference");
      auto p = data::Payload::parse(i->second);
      if (!p || !supported(p->view()))
        throw std::invalid_argument("Invalid schema resource");
      // jsoncons 元数据的嵌套 allocator 保留来源；随编译结果保留资源 owner。
      state->resources.push_back(std::move(*p));
      return Json(
          *data::detail::BackendAccess::node(state->resources.back().view()),
          data::detail::JsonAlloc(&state->account));
    };
    state->resources.push_back(std::move(*root));
    Json source(
        *data::detail::BackendAccess::node(state->resources.back().view()),
        data::detail::JsonAlloc(&state->account));
    jsoncons::jsonschema::evaluation_options options;
    options.default_version(std::string(dialect));
    options.require_format_validation(false);
    std::map<jsoncons::uri, jsoncons::jsonschema::schema_validator<Json> *>
        store;
    std::vector<jsoncons::jsonschema::schema_resolver<Json>> resolvers{
        resolver};
    auto builder = Factory{}(std::move(source), options, &store, resolvers, {});
    builder->build_schema();
    state->compiled = std::make_unique<jsoncons::jsonschema::json_schema<Json>>(
        builder->get_schema_validator());
    for (auto it = cache.begin(); it != cache.end();)
      if (it->second.expired())
        it = cache.erase(it);
      else
        ++it;
    if (cache.size() < 64)
      cache.emplace(state->key, state);
    return CompiledSchema(std::move(state));
  } catch (const data::detail::Failure &e) {
    return foundation::make_unexpected(data::error(e.code));
  } catch (const jsoncons::jsonschema::schema_error &) {
    return foundation::make_unexpected(reject().error());
  } catch (const std::invalid_argument &) {
    return foundation::make_unexpected(reject().error());
  }
}
foundation::Result<void>
CompiledSchema::validate(data::ValueView instance) const {
  if (instance.missing())
    return reject();
  try {
    data::detail::Account scratch(data::Budget{});
    data::detail::Scope scope(scratch);
    scratch.reserve_proxies(256);
    return state_->compiled->is_valid(
               *data::detail::BackendAccess::node(instance))
               ? foundation::Result<void>{}
               : reject();
  } catch (const data::detail::Failure &e) {
    return foundation::make_unexpected(data::error(e.code));
  }
}
std::string_view CompiledSchema::cache_key() const noexcept {
  return state_->key;
}
} // namespace ock::binding
