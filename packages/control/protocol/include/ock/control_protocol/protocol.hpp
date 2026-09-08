#pragma once
#include <ock/data/payload.hpp>
namespace ock::control {
using foundation::Result;
enum class ProtocolErrc {
  InvalidFrame = 1,
  InvalidRequest,
  WrongDirection,
  Closed,
  BudgetExceeded
};
inline constexpr foundation::ErrorDomain protocol_domain{
    "ock.control.protocol"};
inline foundation::Error error(ProtocolErrc code) {
  return foundation::Error{foundation::ErrorCode::make<protocol_domain>(
      static_cast<std::uint32_t>(code))};
}
struct Decoded {
  std::size_t consumed;
  std::optional<data::Payload> message;
};
class FrameDecoder {
public:
  explicit FrameDecoder(data::Budget budget = {}) : budget_(budget) {}
  Result<Decoded> consume(std::span<const std::byte>);
  Result<void> finish() {
    if (closed_ || header_size_) {
      closed_ = true;
      return foundation::make_unexpected(error(ProtocolErrc::InvalidFrame));
    }
    closed_ = true;
    return {};
  }
  bool closed() const noexcept { return closed_; }

private:
  data::Budget budget_;
  std::array<std::byte, 12> header_{};
  std::size_t header_size_ = 0, expected_ = 0;
  std::string body_;
  bool closed_ = false;
};
Result<std::vector<std::byte>>
encode_frame(const data::Payload &, std::size_t maximum = 4 * 1024 * 1024);
struct Request {
  std::string_view id, method;
  data::ValueView params;
};
// 借用输入；仅客户端有响应请求。服务端 event 使用独立方向的解码入口。
Result<Request> request(data::ValueView);
} // namespace ock::control
