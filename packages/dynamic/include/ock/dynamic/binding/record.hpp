#pragma once
#include <algorithm>
#include <cmath>
#include <limits>
#include <ock/data/payload.hpp>
#include <tuple>

namespace ock::binding {
using foundation::Result;
inline constexpr foundation::ErrorDomain binding_domain{"ock.binding"};
inline Result<void> reject() {
  return foundation::make_unexpected(
      foundation::Error{foundation::ErrorCode::make<binding_domain>(1)});
}
enum class PresenceState { Missing, Null, Value };
// 显式保留 Missing/null；普通成员总是存在，不能静默补默认值。
template <class T> struct Presence {
  PresenceState state = PresenceState::Missing;
  T value{};
};
namespace detail {
template <class T> struct Value {
  using type = T;
  static constexpr bool presence = false;
};
template <class T> struct Value<Presence<T>> {
  using type = T;
  static constexpr bool presence = true;
};
template <class T> struct Member;
template <class T, class V> struct Member<V T::*> {
  using value = V;
};
template <class T>
constexpr bool scalar =
    std::same_as<T, std::string> || std::same_as<T, bool> ||
    std::same_as<T, std::int64_t> || std::same_as<T, std::uint64_t> ||
    std::same_as<T, double>;
template <class T> Result<T> read(data::ValueView v) {
  if constexpr (std::same_as<T, std::string>) {
    auto s = v.string();
    if (s)
      return std::string(*s);
  } else if constexpr (std::same_as<T, bool>)
    return v.boolean();
  else if constexpr (std::same_as<T, std::int64_t>)
    return v.int64();
  else if constexpr (std::same_as<T, std::uint64_t>)
    return v.uint64();
  else if constexpr (std::same_as<T, double>)
    return v.number();
  return foundation::make_unexpected(reject().error());
}
template <class T> void write(data::PayloadBuilder &b, const T &v) {
  if constexpr (std::same_as<T, std::string>)
    (void)b.string(v);
  else if constexpr (std::same_as<T, bool>)
    (void)b.boolean(v);
  else if constexpr (std::same_as<T, std::int64_t>)
    (void)b.int64(v);
  else if constexpr (std::same_as<T, std::uint64_t>)
    (void)b.uint64(v);
  else if constexpr (std::same_as<T, double>)
    (void)b.number(v);
}
} // namespace detail
template <auto Pointer> struct Field {
  using Member = typename detail::Member<decltype(Pointer)>::value;
  using Value = typename detail::Value<Member>::type;
  static_assert(detail::scalar<Value>,
                "Only explicit owning scalar fields are supported");
  static constexpr auto pointer = Pointer;
  std::string name;
  bool required = true, nullable = false;
  std::optional<Value> minimum, maximum;
  std::size_t min_length = 0, max_length = 2 * 1024 * 1024;
  std::vector<Value> enumeration;
  std::string unit, description, format;
  bool (*explicit_validator)(const Value &) = nullptr;
  bool valid_definition() const {
    if (name.empty() || name.size() > 128 ||
        !foundation::detail::valid_utf8(name))
      return false;
    if constexpr (!detail::Value<Member>::presence)
      if (!required || nullable)
        return false;
    if constexpr (std::same_as<Value, std::string> ||
                  std::same_as<Value, bool>) {
      if (minimum || maximum)
        return false;
    }
    if constexpr (std::same_as<Value, double>) {
      if ((minimum && !std::isfinite(*minimum)) ||
          (maximum && !std::isfinite(*maximum)))
        return false;
    }
    return min_length <= max_length &&
           (!minimum || !maximum || *minimum <= *maximum);
  }
  Result<void> validate_value(const Value &v) const {
    if constexpr (std::same_as<Value, double>)
      if (!std::isfinite(v))
        return reject();
    if (minimum && v < *minimum)
      return reject();
    if (maximum && v > *maximum)
      return reject();
    if constexpr (std::same_as<Value, std::string>) {
      if (!foundation::detail::valid_utf8(v))
        return reject();
      std::size_t length = 0;
      for (unsigned char c : v)
        if ((c & 0xc0) != 0x80)
          ++length;
      if (length < min_length || length > max_length)
        return reject();
    }
    if (!enumeration.empty() &&
        std::find(enumeration.begin(), enumeration.end(), v) ==
            enumeration.end())
      return reject();
    if (explicit_validator && !explicit_validator(v))
      return reject();
    return {};
  }
  template <class T> Result<void> validate(const T &record) const {
    const auto &member = record.*Pointer;
    if constexpr (detail::Value<Member>::presence) {
      if (member.state == PresenceState::Missing)
        return required ? reject() : Result<void>{};
      if (member.state == PresenceState::Null)
        return nullable ? Result<void>{} : reject();
      if (member.state != PresenceState::Value)
        return reject();
      return validate_value(member.value);
    } else
      return validate_value(member);
  }
  template <class T>
  Result<void> decode(data::ValueView input, T &record) const {
    auto v = input.at(name);
    if constexpr (detail::Value<Member>::presence) {
      auto &m = record.*Pointer;
      m.state = v.missing()                    ? PresenceState::Missing
                : v.kind() == data::Kind::Null ? PresenceState::Null
                                               : PresenceState::Value;
      if (m.state != PresenceState::Value)
        return validate(record);
      auto value = detail::read<Value>(v);
      if (!value)
        return reject();
      m.value = std::move(*value);
    } else {
      auto value = detail::read<Value>(v);
      if (!value)
        return reject();
      record.*Pointer = std::move(*value);
    }
    return validate(record);
  }
  template <class T>
  void encode(data::PayloadBuilder &b, const T &record) const {
    const auto &m = record.*Pointer;
    if constexpr (detail::Value<Member>::presence) {
      if (m.state == PresenceState::Missing)
        return;
      (void)b.key(name);
      if (m.state == PresenceState::Null)
        (void)b.null();
      else
        detail::write(b, m.value);
    } else {
      (void)b.key(name);
      detail::write(b, m);
    }
  }
  void schema(data::PayloadBuilder &b) const {
    (void)b.key(name);
    (void)b.begin_object();
    (void)b.key("type");
    std::string_view type = "integer";
    if constexpr (std::same_as<Value, std::string>)
      type = "string";
    if constexpr (std::same_as<Value, bool>)
      type = "boolean";
    if constexpr (std::same_as<Value, double>)
      type = "number";
    if (nullable) {
      (void)b.begin_array();
      (void)b.string(type);
      (void)b.string("null");
      (void)b.end_array();
    } else
      (void)b.string(type);
    if constexpr (std::same_as<Value, std::int64_t> ||
                  std::same_as<Value, std::uint64_t>) {
      (void)b.key("minimum");
      detail::write(b, minimum.value_or((std::numeric_limits<Value>::min)()));
      (void)b.key("maximum");
      detail::write(b, maximum.value_or((std::numeric_limits<Value>::max)()));
    } else if constexpr (std::same_as<Value, double>) {
      if (minimum) {
        (void)b.key("minimum");
        detail::write(b, *minimum);
      }
      if (maximum) {
        (void)b.key("maximum");
        detail::write(b, *maximum);
      }
    }
    if constexpr (std::same_as<Value, std::string>) {
      (void)b.key("minLength");
      (void)b.uint64(min_length);
      (void)b.key("maxLength");
      (void)b.uint64(max_length);
    }
    if (!enumeration.empty()) {
      (void)b.key("enum");
      (void)b.begin_array();
      for (const auto &v : enumeration)
        detail::write(b, v);
      if (nullable)
        (void)b.null();
      (void)b.end_array();
    }
    for (const auto &[key, value] :
         {std::pair{"description", description}, std::pair{"x-unit", unit},
          std::pair{"format", format}})
      if (!value.empty()) {
        (void)b.key(key);
        (void)b.string(value);
      }
    if (explicit_validator) {
      (void)b.key("x-explicit-validator");
      (void)b.boolean(true);
    }
    (void)b.end_object();
  }
};
template <auto Pointer> auto field(std::string name) {
  Field<Pointer> f;
  f.name = std::move(name);
  return f;
}

// Spec::fields 为唯一字段来源；同一个 validate 供 TypeContract 和动态绑定调用。
// Spec 可由 TypeContract 继承/转发，身份仍由 CoreContracts 的 TypeIdentity
// 提供。
template <class T, class Spec> class Record {
  static const auto &fields() {
    static const auto value = Spec::fields();
    return value;
  }
  template <class F> static Result<void> each(F &&f) {
    Result<void> result;
    std::apply(
        [&](const auto &...field) {
          ((result ? void(result = f(field)) : void()), ...);
        },
        fields());
    return result;
  }

public:
  static Result<void> definition() {
    std::vector<std::string_view> names;
    return each([&](const auto &f) -> Result<void> {
      if (!f.valid_definition() ||
          std::find(names.begin(), names.end(), f.name) != names.end())
        return reject();
      names.push_back(f.name);
      return {};
    });
  }
  static Result<void> validate(const T &value) {
    static const bool valid = bool(definition());
    if (!valid)
      return reject();
    auto r = each([&](const auto &f) { return f.validate(value); });
    if (!r)
      return r;
    if constexpr (requires { Spec::validate(value); })
      return Spec::validate(value);
    return {};
  }
  static Result<T> decode(data::ValueView input) {
    if (input.kind() != data::Kind::Object)
      return foundation::make_unexpected(reject().error());
    for (std::size_t i = 0; i < input.size(); ++i) {
      auto key = input.key_at(i);
      bool found = false;
      std::apply([&](const auto &...f) { found = ((f.name == *key) || ...); },
                 fields());
      if (!found)
        return foundation::make_unexpected(reject().error());
    }
    T value{};
    auto r = each([&](const auto &f) { return f.decode(input, value); });
    if (r)
      r = validate(value);
    if (!r)
      return foundation::make_unexpected(r.error());
    return value;
  }
  static Result<data::Payload> encode(const T &value,
                                      data::Budget budget = {}) {
    auto r = validate(value);
    if (!r)
      return foundation::make_unexpected(r.error());
    data::PayloadBuilder b(budget);
    (void)b.begin_object();
    std::apply([&](const auto &...f) { (f.encode(b, value), ...); }, fields());
    (void)b.end_object();
    return b.freeze();
  }
  static Result<data::Payload> schema(data::Budget budget = {}) {
    auto r = definition();
    if (!r)
      return foundation::make_unexpected(r.error());
    data::PayloadBuilder b(budget);
    (void)b.begin_object();
    (void)b.key("$schema");
    (void)b.string("https://json-schema.org/draft/2020-12/schema");
    (void)b.key("type");
    (void)b.string("object");
    (void)b.key("additionalProperties");
    (void)b.boolean(false);
    (void)b.key("properties");
    (void)b.begin_object();
    std::apply([&](const auto &...f) { (f.schema(b), ...); }, fields());
    (void)b.end_object();
    (void)b.key("required");
    (void)b.begin_array();
    std::apply(
        [&](const auto &...f) {
          ((f.required ? void(b.string(f.name)) : void()), ...);
        },
        fields());
    (void)b.end_array();
    // 非标准 typed 跨字段/格式规则明确披露，Schema 注释不能替代它们。
    if constexpr (requires(const T &value) { Spec::validate(value); }) {
      (void)b.key("x-typed-validator");
      (void)b.boolean(true);
    }
    (void)b.end_object();
    return b.freeze();
  }
};
} // namespace ock::binding
