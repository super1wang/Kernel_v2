#pragma once
#include <ock/control/subscription.hpp>
#include <ock/control_protocol/transport.hpp>
namespace ock::control {
// 通过 Protocol 的字节端口接线，Control 不依赖具体 LocalIPC/Asio。
class ProtocolObservationTransport final : public ObservationTransport {
public:
  static Result<std::shared_ptr<ProtocolObservationTransport>> create(std::shared_ptr<FrameSink>);
  Result<void> queue_ack(std::span<const std::byte>) override;
  Result<std::unique_ptr<runtime::policy::TransmissionReservation>> reserve(std::size_t) override;
  runtime::policy::StartResult start_now(const runtime::policy::PreparedTransmission &,
      runtime::policy::TransmissionReservation &) noexcept override;
  void close() noexcept override;
private:
  explicit ProtocolObservationTransport(std::shared_ptr<FrameSink> sink) : sink_(std::move(sink)) {}
  std::shared_ptr<FrameSink> sink_;
};
}
