#pragma once
// D1.01 Foundation：单一 expected 后端及最小拥有型原语。
#include <tl/expected.hpp>
#if TL_EXPECTED_VERSION_MAJOR != 1 || TL_EXPECTED_VERSION_MINOR != 1 || TL_EXPECTED_VERSION_PATCH != 0
#error OCK Foundation requires tl::expected 1.1.0
#endif
#if !defined(TL_EXPECTED_EXCEPTIONS_ENABLED) || (defined(_MSC_VER) && !defined(_CPPUNWIND))
#error OCK Foundation requires the locked exception-enabled backend mode
#endif
#if __cplusplus < 202002L
#error OCK Foundation requires C++20 and the correct __cplusplus mode
#endif
#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace ock::foundation {

class Error;
template<class T, class E = Error> using Result = tl::expected<T, E>;
template<class E> using Unexpected = tl::unexpected<E>;
using tl::make_unexpected;

// 域名称在常量求值期间复制，码工厂只接受静态存储期域的引用模板参数。
class ErrorDomain {
public:
    template<std::size_t N>
    consteval explicit ErrorDomain(const char (&name)[N]) : length_(N - 1) {
        static_assert(N > 1 && N <= 64, "error domain name must contain 1..63 bytes");
        if (name[N - 1] != '\0') throw "error domain name must be terminated";
        for (std::size_t i = 0; i < N - 1; ++i) {
            if (name[i] < 0x21 || name[i] > 0x7e) throw "error domain name must be printable ASCII";
            name_[i] = name[i];
        }
    }
    ErrorDomain& operator=(const ErrorDomain&) = delete;
    constexpr std::string_view name() const noexcept { return {name_.data(), length_}; }
private:
    std::array<char, 63> name_{};
    std::size_t length_;
};
inline constexpr ErrorDomain foundation_domain{"ock.foundation"};

class ErrorCode {
public:
    constexpr ErrorCode() noexcept = default;
    template<const ErrorDomain& Domain>
    static constexpr ErrorCode make(std::uint32_t value) noexcept { return ErrorCode{&Domain, value}; }
    constexpr const ErrorDomain& domain() const noexcept { return *domain_; }
    constexpr std::uint32_t value() const noexcept { return value_; }
    constexpr bool operator==(const ErrorCode&) const noexcept = default;
private:
    constexpr ErrorCode(const ErrorDomain* domain, std::uint32_t value) noexcept : domain_(domain), value_(value) {}
    const ErrorDomain* domain_ = &foundation_domain;
    std::uint32_t value_ = 0;
};

enum class FoundationErrc : std::uint32_t {
    invalid_name = 1, invalid_utf8, detail_too_large, invalid_identity,
    arithmetic_overflow, arithmetic_underflow, narrowing, budget_exceeded,
    wrong_registry, stale_generation, invalid_slot
};
class ErrorInfo;
class Error {
public:
    Error() noexcept = default;
    explicit Error(ErrorCode code) noexcept : code_(code) {}
    Error(ErrorCode code, std::shared_ptr<const ErrorInfo> info) noexcept : code_(code), info_(std::move(info)) {}
    ErrorCode code() const noexcept { return code_; }
    const std::shared_ptr<const ErrorInfo>& info() const noexcept { return info_; }
    static Result<Error> with_details(ErrorCode code, std::string_view text);
private:
    ErrorCode code_;
    std::shared_ptr<const ErrorInfo> info_;
};
inline Error make_error(FoundationErrc code) noexcept {
    return Error{ErrorCode::make<foundation_domain>(static_cast<std::uint32_t>(code))};
}

namespace detail {
// 仅验证 UTF-8 编码，不执行归一化、字符分类或完整 Unicode 操作。
inline bool valid_utf8(std::string_view text) noexcept {
    std::size_t i = 0;
    while (i < text.size()) {
        auto lead = static_cast<unsigned char>(text[i++]);
        if (lead <= 0x7f) continue;
        unsigned count = 0;
        std::uint32_t code = 0, minimum = 0;
        if (lead >= 0xc2 && lead <= 0xdf) { count = 1; code = lead & 0x1f; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { count = 2; code = lead & 0x0f; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { count = 3; code = lead & 0x07; minimum = 0x10000; }
        else return false;
        if (text.size() - i < count) return false;
        for (unsigned n = 0; n < count; ++n) {
            auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}
}

class ErrorInfo {
public:
    ErrorInfo(const ErrorInfo&) = delete;
    ErrorInfo(ErrorInfo&&) = delete;
    ErrorInfo& operator=(const ErrorInfo&) = delete;
    ErrorInfo& operator=(ErrorInfo&&) = delete;
    static constexpr std::size_t max_bytes = 4096;
    static Result<std::shared_ptr<const ErrorInfo>> create(std::string_view text) {
        if (text.empty()) return std::shared_ptr<const ErrorInfo>{};
        if (text.size() > max_bytes) return make_unexpected(make_error(FoundationErrc::detail_too_large));
        if (!detail::valid_utf8(text)) return make_unexpected(make_error(FoundationErrc::invalid_utf8));
        // 私有构造保证每份拥有型详情均先通过字节预算及编码检查；bad_alloc自然传播。
        return std::shared_ptr<const ErrorInfo>(new ErrorInfo(text));
    }
    std::string_view text() const noexcept { return text_; }
private:
    explicit ErrorInfo(std::string_view text) : text_(text) {}
    std::string text_;
};
inline Result<Error> Error::with_details(ErrorCode code, std::string_view text) {
    auto info = ErrorInfo::create(text);
    if (!info) return make_unexpected(info.error());
    return Error{code, std::move(*info)};
}

class Name {
public:
    static constexpr std::size_t max_bytes = 96;
    static Result<Name> parse(std::string_view text) noexcept {
        if (text.empty() || text.size() > max_bytes) return make_unexpected(make_error(FoundationErrc::invalid_name));
        for (unsigned char byte : text)
            if (byte < 0x21 || byte > 0x7e) return make_unexpected(make_error(FoundationErrc::invalid_name));
        Name value;
        for (std::size_t i = 0; i < text.size(); ++i) value.bytes_[i] = text[i];
        value.length_ = static_cast<std::uint8_t>(text.size());
        return value;
    }
    std::string_view view() const noexcept { return {bytes_.data(), length_}; }
    bool operator==(const Name& other) const noexcept { return view() == other.view(); }
    std::strong_ordering operator<=>(const Name& other) const noexcept { return view() <=> other.view(); }
private:
    Name() noexcept = default;
    std::array<char, max_bytes> bytes_{};
    std::uint8_t length_ = 0;
};

template<class Tag>
struct Tagged128 {
    std::array<std::uint8_t, 16> bytes{};
    constexpr auto operator<=>(const Tagged128&) const noexcept = default;
    constexpr bool empty() const noexcept { return *this == Tagged128{}; }
};
struct TaskTag {};
struct ObjectTag {};
struct RegistryTag {};
struct RegistryGenerationTag {};
using TaskId = Tagged128<TaskTag>;
using ObjectId = Tagged128<ObjectTag>;
using RegistryId = Tagged128<RegistryTag>;
using RegistryGeneration = Tagged128<RegistryGenerationTag>;

template<class Tag>
Result<Tagged128<Tag>> parse_id(std::string_view text) noexcept {
    if (text.size() != 32) return make_unexpected(make_error(FoundationErrc::invalid_identity));
    auto digit = [](unsigned char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    Tagged128<Tag> id;
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        int high = digit(static_cast<unsigned char>(text[2 * i]));
        int low = digit(static_cast<unsigned char>(text[2 * i + 1]));
        if (high < 0 || low < 0) return make_unexpected(make_error(FoundationErrc::invalid_identity));
        id.bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return id;
}
template<class Tag>
constexpr std::array<char, 32> encode_id(const Tagged128<Tag>& id) noexcept {
    constexpr char digits[] = "0123456789abcdef";
    std::array<char, 32> text{};
    for (std::size_t i = 0; i < id.bytes.size(); ++i) {
        text[2 * i] = digits[id.bytes[i] >> 4];
        text[2 * i + 1] = digits[id.bytes[i] & 0xf];
    }
    return text;
}

template<class Tag>
struct BoundHandle {
    RegistryId registry{};
    RegistryGeneration generation{};
    std::uint32_t slot = (std::numeric_limits<std::uint32_t>::max)();
    constexpr bool operator==(const BoundHandle&) const noexcept = default;
};
template<class Tag>
Result<std::uint32_t> resolve_slot(const BoundHandle<Tag>& handle, RegistryId registry,
                                   RegistryGeneration generation, std::size_t capacity) noexcept {
    // 校验次序为合同：身份与世代拒绝后不观察slot，更不索引调用者容器。
    if (registry.empty() || handle.registry != registry) return make_unexpected(make_error(FoundationErrc::wrong_registry));
    if (generation.empty() || handle.generation != generation) return make_unexpected(make_error(FoundationErrc::stale_generation));
    if (handle.slot == (std::numeric_limits<std::uint32_t>::max)() || handle.slot >= capacity)
        return make_unexpected(make_error(FoundationErrc::invalid_slot));
    return handle.slot;
}

template<class T>
concept UnsignedInteger = std::unsigned_integral<T> && !std::same_as<std::remove_cv_t<T>, bool>;
template<UnsignedInteger T>
Result<T> checked_add(T left, T right) noexcept {
    if (right > (std::numeric_limits<T>::max)() - left) return make_unexpected(make_error(FoundationErrc::arithmetic_overflow));
    return static_cast<T>(left + right);
}
template<UnsignedInteger T>
Result<T> checked_sub(T left, T right) noexcept {
    if (right > left) return make_unexpected(make_error(FoundationErrc::arithmetic_underflow));
    return static_cast<T>(left - right);
}
template<UnsignedInteger T>
Result<T> checked_mul(T left, T right) noexcept {
    if (right != 0 && left > (std::numeric_limits<T>::max)() / right) return make_unexpected(make_error(FoundationErrc::arithmetic_overflow));
    return static_cast<T>(left * right);
}
template<UnsignedInteger To, UnsignedInteger From>
Result<To> checked_narrow(From value) noexcept {
    if constexpr (std::numeric_limits<To>::digits < std::numeric_limits<From>::digits)
        if (value > static_cast<From>((std::numeric_limits<To>::max)())) return make_unexpected(make_error(FoundationErrc::narrowing));
    return static_cast<To>(value);
}
template<UnsignedInteger T>
class CheckedCount {
public:
    static Result<CheckedCount> create(T value = 0, T limit = (std::numeric_limits<T>::max)()) noexcept {
        if (value > limit) return make_unexpected(make_error(FoundationErrc::budget_exceeded));
        return CheckedCount{value, limit};
    }
    T value() const noexcept { return value_; }
    T limit() const noexcept { return limit_; }
    Result<void> try_add(T amount) noexcept {
        auto next = checked_add(value_, amount);
        if (!next) return make_unexpected(next.error());
        if (*next > limit_) return make_unexpected(make_error(FoundationErrc::budget_exceeded));
        value_ = *next;
        return {};
    }
    Result<void> try_sub(T amount) noexcept {
        auto next = checked_sub(value_, amount);
        if (!next) return make_unexpected(next.error());
        value_ = *next;
        return {};
    }
private:
    CheckedCount(T value, T limit) noexcept : value_(value), limit_(limit) {}
    T value_, limit_;
};
class RuntimeEpoch {
public:
    explicit constexpr RuntimeEpoch(std::uint64_t value = 0) noexcept : value_(value) {}
    constexpr std::uint64_t value() const noexcept { return value_; }
    constexpr bool operator==(const RuntimeEpoch&) const noexcept = default;
    Result<void> advance() noexcept {
        auto next = checked_add(value_, std::uint64_t{1});
        if (!next) return make_unexpected(next.error());
        value_ = *next;
        return {};
    }
private:
    std::uint64_t value_;
};
[[noreturn]] inline void fail_fast() noexcept { std::abort(); }
inline void invariant(bool condition) noexcept { if (!condition) fail_fast(); }

} // namespace ock::foundation
