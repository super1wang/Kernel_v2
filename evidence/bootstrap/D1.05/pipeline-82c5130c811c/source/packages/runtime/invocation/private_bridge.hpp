#pragma once
// D1.05 内部桥：只供 Invocation 调用，不属于 SDK Runtime 表面。
#include <ock/contracts/operation.hpp>
#include "packages/runtime/registry/registry.hpp"

namespace ock::runtime::invocation {
struct NativeEntry final {
  std::uint32_t slot;
  std::shared_ptr<const contracts::DefinitionSnapshot> definition;
  contracts::Shape shape;
  contracts::ExecutionRequirements execution;
  std::size_t resource_count;
  bool executable;
};

class NativeEngine;
namespace detail {
class NativeInvocation;
}
class NativeAccess final {
  friend class NativeEngine;
  friend class detail::NativeInvocation;
  static contracts::Result<NativeEntry>
  inspect(const registry::Catalog &, const contracts::OperationKey &,
          contracts::ContractDigest, contracts::Shape,
          contracts::CppTypeToken, contracts::CppTypeToken);
  static contracts::Result<void>
  invoke(const registry::Catalog &, const NativeEntry &, const void *,
         contracts::WorkContext &, void *);
};
namespace detail {
class NativeInvocation final {
public:
  static contracts::Result<void> call(const registry::Catalog &, const NativeEntry &,
                                      const void *, contracts::WorkContext &, void *);
};
}
} // namespace ock::runtime::invocation
