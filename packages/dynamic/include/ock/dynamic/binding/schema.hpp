#pragma once
#include <map>
#include <ock/data/payload.hpp>
namespace ock::binding {
// 资源包在注册前提供，解析器从不访问网络/文件系统。实例仍使用原 Payload DOM。
using SchemaResources = std::map<std::string, std::string, std::less<>>;
class CompiledSchema {
public:
  static foundation::Result<CompiledSchema>
  compile(std::string_view schema, const SchemaResources &resources = {});
  foundation::Result<void> validate(data::ValueView instance) const;
  // 不提供 Native 函数生成；复杂 Schema 必须由调用方标为 dynamic-only。
  std::string_view cache_key() const noexcept;

private:
  struct State;
  explicit CompiledSchema(std::shared_ptr<const State> state)
      : state_(std::move(state)) {}
  std::shared_ptr<const State> state_;
};
} // namespace ock::binding
