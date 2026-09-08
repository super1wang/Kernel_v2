#include <algorithm>
#include <ock/control_protocol/protocol.hpp>
namespace ock::control {
Result<Decoded> FrameDecoder::consume(std::span<const std::byte> bytes) {
  if (closed_)
    return foundation::make_unexpected(error(ProtocolErrc::Closed));
  auto fail = [&](ProtocolErrc code) -> Result<Decoded> {
    closed_ = true;
    body_.clear();
    return foundation::make_unexpected(error(code));
  };
  std::size_t used = 0;
  while (header_size_ < 12 && used < bytes.size())
    header_[header_size_++] = bytes[used++];
  if (header_size_ < 12)
    return Decoded{used, {}};
  if (!expected_) {
    constexpr std::array<std::byte, 8> prefix{
        std::byte{'O'}, std::byte{'C'}, std::byte{'K'}, std::byte{'1'},
        std::byte{0},   std::byte{1},   std::byte{0},   std::byte{0}};
    if (!std::equal(prefix.begin(), prefix.end(), header_.begin()))
      return fail(ProtocolErrc::InvalidFrame);
    for (std::size_t i = 8; i < 12; ++i)
      expected_ = (expected_ << 8) | std::to_integer<unsigned char>(header_[i]);
    if (!expected_ || expected_ > budget_.frame_bytes)
      return fail(ProtocolErrc::BudgetExceeded);
    body_.reserve(expected_);
  }
  const auto n = (std::min)(expected_ - body_.size(), bytes.size() - used);
  if (n)
    body_.append(reinterpret_cast<const char *>(bytes.data() + used), n);
  used += n;
  if (body_.size() != expected_)
    return Decoded{used, {}};
  auto message = data::Payload::parse(body_, budget_);
  if (!message) {
    closed_ = true;
    return foundation::make_unexpected(message.error());
  }
  body_.clear();
  header_size_ = expected_ = 0;
  return Decoded{used, std::move(*message)};
}
Result<std::vector<std::byte>> encode_frame(const data::Payload &payload,
                                            std::size_t maximum) {
  if (maximum > UINT32_MAX)
    return foundation::make_unexpected(error(ProtocolErrc::BudgetExceeded));
  auto text = payload.encode();
  if (!text)
    return foundation::make_unexpected(text.error());
  if (text->empty() || text->size() > maximum)
    return foundation::make_unexpected(error(ProtocolErrc::BudgetExceeded));
  std::vector<std::byte> result{std::byte{'O'}, std::byte{'C'}, std::byte{'K'},
                                std::byte{'1'}, std::byte{0},   std::byte{1},
                                std::byte{0},   std::byte{0}};
  for (int shift = 24; shift >= 0; shift -= 8)
    result.push_back(std::byte((text->size() >> shift) & 255));
  auto bytes = std::as_bytes(std::span(*text));
  result.insert(result.end(), bytes.begin(), bytes.end());
  return result;
}
Result<Request> request(data::ValueView value) {
  auto fail = [](ProtocolErrc code =
                     ProtocolErrc::InvalidRequest) -> Result<Request> {
    return foundation::make_unexpected(error(code));
  };
  if (value.kind() != data::Kind::Object ||
      value.at("jsonrpc").string() != "2.0")
    return fail();
  auto method = value.at("method").string();
  if (!method || method->empty() || method->size() > 128)
    return fail();
  if (*method == "notifications.event")
    return fail(ProtocolErrc::WrongDirection);
  auto id = value.at("id").string();
  if (!id || id->empty() || id->size() > 96 ||
      std::any_of(id->begin(), id->end(),
                  [](unsigned char c) { return c < 0x21 || c > 0x7e; }))
    return fail();
  for (std::size_t i = 0; i < value.size(); ++i) {
    auto key = *value.key_at(i);
    if (key != "id" && key != "method" && key != "jsonrpc" && key != "params")
      return fail();
  }
  auto params = value.at("params");
  if (!params.missing() && params.kind() != data::Kind::Object)
    return fail();
  return Request{*id, *method, params};
}
} // namespace ock::control
