#pragma once
#include <ock/foundation/foundation.hpp>
#include <optional>
#include <span>
#include <vector>

namespace ock::data {
using foundation::Result;
enum class DataErrc {
  InvalidJson = 1,
  BudgetExceeded,
  DuplicateKey,
  InvalidState,
  WrongType,
  OutOfRange
};
inline constexpr foundation::ErrorDomain data_domain{"ock.data"};
inline foundation::Error error(DataErrc e) noexcept {
  return foundation::Error{
      foundation::ErrorCode::make<data_domain>(static_cast<std::uint32_t>(e))};
}
struct Budget {
  static constexpr std::size_t max_supported_depth = 64;
  std::size_t frame_bytes = 4 * 1024 * 1024, token_bytes = 2 * 1024 * 1024;
  std::size_t depth = 64, nodes = 100000, container_items = 100000;
  std::size_t text_bytes = 2 * 1024 * 1024, allocation_bytes = 32 * 1024 * 1024;
};
enum class Kind {
  Missing,
  Null,
  Boolean,
  Int64,
  UInt64,
  Number,
  String,
  Array,
  Object
};
namespace detail {
struct Owner;
struct BuilderState;
struct BackendAccess;
} // namespace detail
// 借用只在原 Payload/SharedPayload owner 存活期间有效；不持有共享引用。
class ValueView {
public:
  Kind kind() const noexcept;
  bool missing() const noexcept { return kind() == Kind::Missing; }
  std::size_t size() const noexcept;
  ValueView at(std::string_view key) const noexcept;
  ValueView at(std::size_t index) const noexcept;
  Result<std::string_view> key_at(std::size_t index) const noexcept;
  Result<std::string_view> string() const noexcept;
  Result<bool> boolean() const noexcept;
  Result<std::int64_t> int64() const noexcept;
  Result<std::uint64_t> uint64() const noexcept;
  Result<double> number() const noexcept;

private:
  explicit ValueView(const void *node = nullptr) noexcept : node_(node) {}
  const void *node_;
  friend class Payload;
  friend class SharedPayload;
  friend struct detail::BackendAccess;
};
class SharedPayload;
class Payload {
public:
  Payload() noexcept;
  ~Payload();
  Payload(Payload &&) noexcept;
  Payload &operator=(Payload &&) noexcept;
  Payload(const Payload &) = delete;
  Payload &operator=(const Payload &) = delete;
  static Result<Payload> parse(std::string_view json, Budget budget = {});
  ValueView view() const & noexcept;
  ValueView view() const && = delete;
  Result<Payload> clone(Budget budget = {}) const;
  SharedPayload share() &&;
  std::size_t allocated_bytes() const noexcept;
  Result<std::string> encode(std::size_t max_bytes = 4 * 1024 * 1024) const;

private:
  explicit Payload(std::unique_ptr<detail::Owner>) noexcept;
  std::unique_ptr<detail::Owner> owner_;
  friend class PayloadBuilder;
};
class SharedPayload {
public:
  ValueView view() const & noexcept;
  ValueView view() const && = delete;

private:
  explicit SharedPayload(std::shared_ptr<const detail::Owner> owner)
      : owner_(std::move(owner)) {}
  std::shared_ptr<const detail::Owner> owner_;
  friend class Payload;
};
// 单次事件构建：无 mutable View，任何错误为 sticky，freeze 后所有修改均拒绝。
class PayloadBuilder {
public:
  explicit PayloadBuilder(Budget budget = {});
  ~PayloadBuilder();
  PayloadBuilder(PayloadBuilder &&) noexcept;
  PayloadBuilder &operator=(PayloadBuilder &&) noexcept;
  PayloadBuilder(const PayloadBuilder &) = delete;
  Result<void> begin_object();
  Result<void> end_object();
  Result<void> begin_array();
  Result<void> end_array();
  Result<void> key(std::string_view);
  Result<void> string(std::string_view);
  Result<void> null();
  Result<void> boolean(bool);
  Result<void> int64(std::int64_t);
  Result<void> uint64(std::uint64_t);
  Result<void> number(double);
  Result<Payload> freeze();

private:
  std::unique_ptr<detail::BuilderState> state_;
  std::optional<DataErrc> initial_error_;
  friend class Payload;
};
// 大数据字节有独立 owner；不转换为逐点通用节点。
class Buffer {
public:
  static Result<Buffer> copy(std::span<const std::byte> bytes,
                             std::size_t max_bytes);
  std::span<const std::byte> bytes() const noexcept { return *bytes_; }

private:
  explicit Buffer(std::shared_ptr<const std::vector<std::byte>> bytes)
      : bytes_(std::move(bytes)) {}
  std::shared_ptr<const std::vector<std::byte>> bytes_;
};
struct DataTag;
struct DataRef {
  foundation::Tagged128<DataTag> id;
  std::uint64_t offset{}, bytes{};
};
} // namespace ock::data
