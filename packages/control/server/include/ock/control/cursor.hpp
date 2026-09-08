#pragma once
#include <ock/control_protocol/protocol.hpp>
namespace ock::control {
struct CursorContext {
  std::string host, caller, owner;
  std::optional<std::string> store;
  std::optional<std::uint64_t> restore;
  std::uint64_t view = 0;
  std::string phases = "nonterminal";
  // 生产 list 必填；仅基础 codec golden 允许无附加上下文。
  std::optional<std::string> connection;
  std::uint64_t delegation = 0;
  bool operator==(const CursorContext &) const = default;
};
struct CursorPosition {
  std::uint64_t upper, position, issued, expires;
};
class CursorClock {
public:
  virtual ~CursorClock() = default;
  virtual std::uint64_t utc_seconds() const noexcept = 0;
  virtual std::uint64_t monotonic_seconds() const noexcept = 0;
};
class CursorCodec {
public:
  static Result<CursorCodec> create(std::string host,
                                    std::shared_ptr<const CursorClock>);
  Result<std::string> issue(const CursorContext &,
                            const CursorPosition &) const;
  Result<std::string> issue(const CursorContext &, std::uint64_t upper,
                            std::uint64_t position) const;
  Result<CursorPosition> read(std::string_view, const CursorContext &) const;
  static constexpr std::size_t cursor_handles() noexcept { return 0; }

private:
  struct State;
  explicit CursorCodec(std::shared_ptr<const State> state)
      : state_(std::move(state)) {}
  static Result<CursorCodec> with_secret(std::string,
                                         std::shared_ptr<const CursorClock>,
                                         std::array<std::byte, 32>);
  std::shared_ptr<const State> state_;
  friend struct CursorTestAccess;
};
} // namespace ock::control
