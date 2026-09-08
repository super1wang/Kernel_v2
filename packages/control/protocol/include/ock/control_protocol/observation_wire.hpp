#pragma once
#include <ock/contracts/observation.hpp>
#include <ock/control_protocol/protocol.hpp>
namespace ock::control {
struct SubscriptionTag;
struct StreamTag;
using SubscriptionId = foundation::Tagged128<SubscriptionTag>;
using StreamGeneration = foundation::Tagged128<StreamTag>;
template <class Id> Result<Id> wire_id(data::ValueView view) {
  auto text = view.string();
  if (!text || text->size() != 32)
    return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  Id value{};
  constexpr std::string_view hex = "0123456789abcdef";
  for (std::size_t i = 0; i < 16; ++i) {
    auto high = hex.find((*text)[i * 2]), low = hex.find((*text)[i * 2 + 1]);
    if (high == hex.npos || low == hex.npos)
      return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
    value.bytes[i] = static_cast<std::uint8_t>((high << 4) | low);
  }
  if (value.empty())
    return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  return value;
}
template <class Id> std::string wire_text(const Id &value) {
  std::string text;
  for (auto b : value.bytes) {
    text += "0123456789abcdef"[b >> 4];
    text += "0123456789abcdef"[b & 15];
  }
  return text;
}
Result<std::uint64_t> wire_count(data::ValueView);
struct SubscribeRequest {
  contracts::ObservationFilter filter;
  std::chrono::milliseconds interval{100};
};
struct UnsubscribeRequest {
  SubscriptionId subscription;
  StreamGeneration stream;
};
struct WireListRequest {
  contracts::ListRequest request;
  std::optional<std::string> cursor;
};
Result<SubscribeRequest> parse_subscribe(data::ValueView,
                                         contracts::PrincipalRef);
Result<UnsubscribeRequest> parse_unsubscribe(data::ValueView);
Result<WireListRequest> parse_list(data::ValueView, contracts::PrincipalRef);
} // namespace ock::control
