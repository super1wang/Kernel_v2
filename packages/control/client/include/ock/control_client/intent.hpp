#pragma once
#include <ock/control_client/client.hpp>
#include <filesystem>
namespace ock::control_client {
// 成功握手后、首次业务请求发送前调用。已有文件永不自动覆盖。
foundation::Result<data::Payload> ensure_intent(const std::filesystem::path &,
    const Hello &, const data::Payload &logical_request);
}
