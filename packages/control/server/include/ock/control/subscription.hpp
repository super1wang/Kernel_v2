#pragma once
#include <ock/control_protocol/observation_wire.hpp>
#include <ock/runtime/policy.hpp>
namespace ock::control {
class ObservationTransport : public runtime::policy::TransmissionStartPort {
public:
  // 同一连接有界发送序列；成功返回表示 ack 已排在后来 start_now 的事件之前。
  virtual Result<void> queue_ack(std::span<const std::byte>) = 0;
  virtual void close() noexcept = 0;
};
struct SubscriptionBudget {
  std::size_t subscriptions = 8, pending_per_subscription = 128,
              queued_bytes = 1024 * 1024, frame_bytes = 16384;
  std::uint64_t sequence_limit = UINT64_MAX;
  std::chrono::milliseconds transport_timeout{30000};
};
class SubscriptionConnection {
public:
  static Result<std::unique_ptr<SubscriptionConnection>>
      create(std::shared_ptr<runtime::policy::SessionAuthority>,
             std::shared_ptr<const runtime::policy::VerifiedCaller>,
             std::shared_ptr<contracts::ObservationPort>,
             std::shared_ptr<ObservationTransport>,
             std::shared_ptr<runtime::policy::ClockPort>,
             contracts::HostIncarnation, SubscriptionBudget = {});
  ~SubscriptionConnection();
  Result<UnsubscribeRequest> subscribe(std::string_view request_id,
                                       const SubscribeRequest &);
  Result<bool> unsubscribe(const UnsubscribeRequest &);
  Result<runtime::policy::StartResult> pump();
  void close();
  std::size_t queued_bytes() const noexcept;

private:
  struct State;
  explicit SubscriptionConnection(std::shared_ptr<State> state)
      : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};
} // namespace ock::control
