#pragma once
#include <mutex>
#include <ock/control/subscription.hpp>

namespace ock::control::detail {
// get/list 的单连接调用租约。外部 source/encode/reserve 均不持连接锁。
struct QueryLifetime {
  std::mutex mutex;
  bool closed = false, busy = false;
  std::shared_ptr<runtime::policy::SendCoordinator> sender;
  struct Active {
    std::shared_ptr<QueryLifetime> state;
    std::shared_ptr<runtime::policy::SendCoordinator> sender;
    Active(std::shared_ptr<QueryLifetime> s)
        : state(std::move(s)), sender(state->sender) {}
    Active(const Active &) = delete;
    Active(Active &&) noexcept = default;
    ~Active() {
      if (state) {
        std::lock_guard lock(state->mutex);
        state->busy = false;
      }
      // sender 在锁释放后销毁，可能触发队列/票据清理。
    }
  };
  static std::optional<Active> enter(std::shared_ptr<QueryLifetime> state) {
    std::lock_guard lock(state->mutex);
    if (state->closed || state->busy)
      return {};
    state->busy = true;
    return Active(std::move(state));
  }
  void close(const std::shared_ptr<ObservationTransport> &transport) noexcept {
    std::shared_ptr<runtime::policy::SendCoordinator> retired;
    {
      std::lock_guard lock(mutex);
      if (closed)
        return;
      closed = true;
      retired = std::move(sender);
    }
    retired.reset();
    transport->close();
  }
  struct Sink final : runtime::policy::TransmissionStartPort {
    std::weak_ptr<QueryLifetime> state;
    std::shared_ptr<ObservationTransport> transport;
    Sink(std::shared_ptr<QueryLifetime> s,
         std::shared_ptr<ObservationTransport> t)
        : state(s), transport(std::move(t)) {}
    Result<std::unique_ptr<runtime::policy::TransmissionReservation>>
    reserve(std::size_t bytes) override {
      auto s = state.lock();
      if (!s)
        return foundation::make_unexpected(error(ProtocolErrc::Closed));
      {
        std::lock_guard lock(s->mutex);
        if (s->closed)
          return foundation::make_unexpected(error(ProtocolErrc::Closed));
      }
      auto reservation = transport->reserve(bytes);
      {
        std::lock_guard lock(s->mutex);
        if (s->closed)
          return foundation::make_unexpected(error(ProtocolErrc::Closed));
      }
      return reservation;
    }
    runtime::policy::StartResult
    start_now(const runtime::policy::PreparedTransmission &frame,
              runtime::policy::TransmissionReservation &reservation) noexcept
        override {
      auto s = state.lock();
      if (!s)
        return runtime::policy::StartResult::NotStarted;
      std::lock_guard lock(s->mutex);
      if (s->closed)
        return runtime::policy::StartResult::NotStarted;
      // 仅此可信非阻塞、不可重入的首字节起点与 close 共用短仲裁。
      return transport->start_now(frame, reservation);
    }
  };
};
} // namespace ock::control::detail
