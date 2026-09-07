#pragma once
// D1.05 私有分派：所有可触达真实 Catalog hot 条目的方法均为 private。
#include <ock/contracts/operation.hpp>
#include "packages/runtime/registry/registry.hpp"

namespace ock::runtime::invocation {
struct NativeEntry final {
  contracts::OperationHandle handle;
  std::shared_ptr<const contracts::DefinitionSnapshot> definition;
  contracts::Shape shape;
  contracts::ExecutionRequirements execution;
  std::size_t resource_count;
};
class NativeEngine;
template <contracts::ContractValue A, contracts::ContractResult R> class NativeBound;
class NativeAccess final {
  friend class NativeEngine;
  template <contracts::ContractValue A, contracts::ContractResult R> friend class NativeBound;
  static contracts::Result<NativeEntry> inspect(
      const registry::Catalog&, const contracts::OperationKey&, contracts::ContractDigest,
      contracts::Shape, contracts::CppTypeToken, contracts::CppTypeToken);
  static contracts::Result<void> check(const registry::Catalog&, const NativeEntry&,
      contracts::CppTypeToken, contracts::CppTypeToken) noexcept;
  static void dispatch(const registry::Catalog&, const NativeEntry&, const void*,
                        contracts::WorkContext&, void*);
};
} // namespace ock::runtime::invocation
