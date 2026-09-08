#pragma once
#include <ock/control/router.hpp>
#include <ock/control/subscription.hpp>
namespace ock::control {
// 参数 Schema 来自装配时注册的冻结 N1 资源；只装配本连接的订阅实例。
Result<std::vector<Method>> subscription_methods(
    std::shared_ptr<SubscriptionConnection>,
    std::shared_ptr<const runtime::policy::VerifiedCaller>,
    const binding::SchemaResources &);
} // namespace ock::control
