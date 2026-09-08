#pragma once
#include <ock/dynamic/binding/record.hpp>
#include <ock/dynamic/binding/schema.hpp>
#include <ock/dynamic/binding/type_contract.hpp>
namespace ock::binding {
// 注册时创建一次并随定义持有；调用期不重新生成或编译 Schema。
template <class T, class Spec> class RegisteredRecord {
  static_assert(std::same_as<typename contracts::TypeContract<T>::FieldSpec,Spec>,
                "Native and Dynamic must use the same generated TypeContract");
public:
  static constexpr SchemaSemantics semantics() noexcept { return SchemaSemantics::SharedTypeContract; }
  static Result<RegisteredRecord> create() {
    auto schema = Record<T, Spec>::schema();
    if (!schema)
      return foundation::make_unexpected(schema.error());
    auto text = schema->encode();
    if (!text)
      return foundation::make_unexpected(text.error());
    auto compiled = CompiledSchema::compile(*text);
    if (!compiled)
      return foundation::make_unexpected(compiled.error());
    return RegisteredRecord(std::move(*schema).share(), std::move(*compiled));
  }
  Result<T> decode(data::ValueView value) const {
    auto check = compiled_.validate(value);
    if (!check)
      return foundation::make_unexpected(check.error());
    return Record<T, Spec>::decode(value);
  }
  Result<void> validate(const T &value) const {
    return Record<T, Spec>::validate(value);
  }
  Result<data::Payload> encode(const T &value) const {
    return Record<T, Spec>::encode(value);
  }
  data::ValueView schema() const & noexcept { return schema_.view(); }
  data::ValueView schema() const && = delete;

private:
  RegisteredRecord(data::SharedPayload schema, CompiledSchema compiled)
      : schema_(std::move(schema)), compiled_(std::move(compiled)) {}
  data::SharedPayload schema_;
  CompiledSchema compiled_;
};
} // namespace ock::binding
