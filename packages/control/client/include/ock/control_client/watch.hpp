#pragma once
#include <ock/control_client/client.hpp>
namespace ock::control_client {
struct Notification {
  std::optional<data::Payload> frame;
  bool local_gap = false;
};
class NotificationPort {
public:
  virtual ~NotificationPort() = default;
  virtual foundation::Result<Notification> next_notification(std::chrono::milliseconds,std::stop_token) = 0;
  virtual void close_notifications() noexcept = 0;
};
// Client/NotificationPort 应来自同一独立观察连接，均须比 Watch 活得久。
class Watch {
public:
  static foundation::Result<std::unique_ptr<Watch>> open(Client &,NotificationPort &,std::string execution_id,std::stop_token = {});
  ~Watch();
  foundation::Result<data::Payload> snapshot() const;
  foundation::Result<std::optional<data::Payload>> next(std::chrono::milliseconds,std::stop_token = {});
  void close() noexcept;
private:
  Watch(Client &client,NotificationPort &port,std::string id) : client_(client),port_(port),execution_(std::move(id)) {}
  foundation::Result<data::Payload> refresh(std::stop_token);
  Client &client_;
  NotificationPort &port_;
  std::string execution_,subscription_,stream_;
  std::optional<data::Payload> snapshot_;
  std::uint64_t version_ = 0,sequence_ = 0;
  bool closed_ = false;
};
}
