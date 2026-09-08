#pragma once
#include <map>
#include <ock/data/payload.hpp>
namespace ock::binding {
// 资源包在注册前提供，解析器从不访问网络/文件系统。实例仍使用原 Payload DOM。
enum class SchemaSemantics { DynamicOnly, SharedTypeContract };
using SchemaResources = std::map<std::string, std::string, std::less<>>;
class CompiledSchema {
public:
  static foundation::Result<CompiledSchema>
  compile(std::string_view schema, const SchemaResources &resources = {});
  foundation::Result<void> validate(data::ValueView instance) const;
  // 任意 Schema 只保证动态校验，不提供或声称少检查的 Native 等价入口。
  static constexpr SchemaSemantics semantics() noexcept { return SchemaSemantics::DynamicOnly; }
  std::string_view cache_key() const noexcept;

private:
  struct State;
  explicit CompiledSchema(std::shared_ptr<const State> state)
      : state_(std::move(state)) {}
  std::shared_ptr<const State> state_;
};
} // namespace ock::binding
