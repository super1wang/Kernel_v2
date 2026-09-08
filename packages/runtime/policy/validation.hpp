#pragma once
#include <ock/runtime/policy.hpp>
namespace ock::runtime::policy::detail {
// Host 深拥有前的无回调预检；Store::create 仍完成全部原验证。
Result<void> validate_configuration(PolicyBudget, const PolicyConfiguration&);
}
