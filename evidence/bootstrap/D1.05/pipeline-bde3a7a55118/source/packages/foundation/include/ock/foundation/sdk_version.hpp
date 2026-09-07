#pragma once
#include <string_view>

#define OCK_SDK_VERSION_MAJOR 0
#define OCK_SDK_VERSION_MINOR 1
#define OCK_SDK_VERSION_PATCH 0
#define OCK_SDK_VERSION "0.1.0-dev.1"
#define OCK_CONTRACT_BASELINE 1

namespace ock::sdk {
inline constexpr std::string_view version = OCK_SDK_VERSION;
inline constexpr bool runtime_available = false;
// 此头只提供 D0.02 版本元数据，不提供运行时能力。
consteval unsigned major_version() noexcept { return OCK_SDK_VERSION_MAJOR; }
}
