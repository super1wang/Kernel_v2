#pragma once
#include <ock/runtime/native_types.hpp>
namespace ock::runtime::invocation::detail {
// 仅源内纯预算验证；NativeEngine 与 Host 共用原乘加边界。
Result<void> validate_budget(NativeBudget);
}
