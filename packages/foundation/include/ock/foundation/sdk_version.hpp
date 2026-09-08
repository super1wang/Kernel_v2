#pragma once
#include <string_view>

#define OCK_SDK_VERSION_MAJOR 0
#define OCK_SDK_VERSION_MINOR 1
#define OCK_SDK_VERSION_PATCH 0
#define OCK_SDK_VERSION "0.1.0-dev.2"
#define OCK_CONTRACT_BASELINE 0

namespace ock::sdk {
inline constexpr std::string_view version = OCK_SDK_VERSION;
inline constexpr bool runtime_available = true;
inline constexpr std::string_view implementation_stage = "NativeSubset";
// 元数据不代替调用方所需组件与能力核验。
consteval unsigned major_version() noexcept { return OCK_SDK_VERSION_MAJOR; }
}
