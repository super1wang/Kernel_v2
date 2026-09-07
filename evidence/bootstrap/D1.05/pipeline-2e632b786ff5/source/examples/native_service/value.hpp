#pragma once
#include <ock/contracts/operation.hpp>

namespace native_service {
struct Value { int number; };
}
namespace ock::contracts {
template <> struct TypeContract<native_service::Value> {
  static TypeIdentity identity() {
    return {*Name::parse("native.value"), *OperationVersion::parse("1.0.0", 5), {}};
  }
  static Result<void> validate(const native_service::Value& v) {
    return v.number >= 0 && v.number <= 100 ? Result<void>{}
        : make_unexpected(error(ContractsErrc::InvalidContract));
  }
  static constexpr auto async_ownership = AsyncOwnership::Owning;
};
}
