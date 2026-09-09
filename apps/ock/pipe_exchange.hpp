#pragma once
#include <ock/control_client/client.hpp>
#include <ock/control_client/watch.hpp>
#include <ock/local_ipc/pipe.hpp>
#include <condition_variable>
#include <mutex>
#include <deque>
namespace ock::cli {
class PipeExchange final : public control_client::ExchangePort, public control_client::NotificationPort {
  struct State {
    std::mutex mutex;
    std::condition_variable changed;
    std::shared_ptr<local_ipc::Connection> connection;
    std::optional<data::Payload> response;
    std::deque<std::pair<data::Payload,std::size_t>> notifications;
    std::size_t notification_bytes = 0;
    bool gap = false;
    bool pending = false, closed = false;
  };
  std::shared_ptr<State> state_;
  explicit PipeExchange(std::shared_ptr<State> state) : state_(std::move(state)) {}
public:
  static foundation::Result<std::shared_ptr<PipeExchange>> open(local_ipc::PipeOptions options) {
    auto connection = local_ipc::connect(options);
    if(!connection) return foundation::make_unexpected(connection.error());
    auto state = std::make_shared<State>(); state->connection = std::move(*connection);
    std::weak_ptr<State> weak = state;
    state->connection->start([weak](data::Payload value) {
      if(auto state = weak.lock()) {
        std::lock_guard lock(state->mutex);
        if(value.view().at("method").string()=="notifications.event" && value.view().at("id").missing()) {
          const auto bytes=value.allocated_bytes();
          if(bytes>1024*1024) state->gap=true;
          else {
            while(!state->notifications.empty() && (state->notifications.size()>=128 || bytes>1024*1024-state->notification_bytes)) {
              state->notification_bytes-=state->notifications.front().second;
              state->notifications.pop_front(); state->gap=true;
            }
            state->notifications.emplace_back(std::move(value),bytes); state->notification_bytes+=bytes;
          }
        } else if(!state->pending || state->response) { state->closed = true; state->connection->close(); }
        else state->response.emplace(std::move(value));
        state->changed.notify_all();
      }
    },[weak] { if(auto state = weak.lock()) { std::lock_guard lock(state->mutex); state->closed = true; state->changed.notify_all(); } });
    return std::shared_ptr<PipeExchange>(new PipeExchange(std::move(state)));
  }
  ~PipeExchange() override { state_->connection->close(); state_->connection->wait_closed(std::chrono::seconds(5)); }
  void close_notifications() noexcept override { state_->connection->close(); }
  foundation::Result<control_client::Notification> next_notification(std::chrono::milliseconds timeout,std::stop_token stop) override {
    auto state=state_;
    std::unique_lock lock(state->mutex);
    std::stop_callback interrupted(stop,[&] { state->changed.notify_all(); });
    const bool ready=state->changed.wait_for(lock,timeout,[&] { return state->closed || !state->notifications.empty() || state->gap || stop.stop_requested(); });
    if(stop.stop_requested()) return foundation::make_unexpected(control_client::error(control_client::ClientErrc::Interrupted));
    if(state->closed) return foundation::make_unexpected(control_client::error(control_client::ClientErrc::Transport));
    control_client::Notification notification{{},std::exchange(state->gap,false)};
    if(ready && !state->notifications.empty()) {
      notification.frame.emplace(std::move(state->notifications.front().first));
      state->notification_bytes-=state->notifications.front().second; state->notifications.pop_front();
    }
    return notification;
  }
  foundation::Result<data::Payload> exchange(const data::Payload &request,
      std::chrono::milliseconds timeout,std::stop_token stop) override {
    auto state = state_;
    auto frame = control::encode_frame(request);
    if(!frame) return foundation::make_unexpected(frame.error());
    std::unique_lock lock(state->mutex);
    if(state->closed || state->pending) return foundation::make_unexpected(control_client::error(control_client::ClientErrc::Transport));
    state->pending = true;
    auto sent = state->connection->send(*frame);
    if(!sent) { state->pending = false; return foundation::make_unexpected(sent.error()); }
    std::stop_callback interrupted(stop,[&] { state->changed.notify_all(); });
    const bool ready = state->changed.wait_for(lock,timeout,[&] { return state->response || state->closed || stop.stop_requested(); });
    state->pending = false;
    if(stop.stop_requested() || !ready) {
      state->closed = true; state->connection->close();
      return foundation::make_unexpected(control_client::error(stop.stop_requested() ? control_client::ClientErrc::Interrupted : control_client::ClientErrc::Timeout));
    }
    if(!state->response) return foundation::make_unexpected(control_client::error(control_client::ClientErrc::Transport));
    auto result = std::move(*state->response); state->response.reset();
    return result;
  }
};
}
