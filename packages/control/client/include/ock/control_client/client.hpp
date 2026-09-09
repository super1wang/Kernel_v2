#pragma once
#include <ock/control_protocol/protocol.hpp>
#include <chrono>
#include <memory>
#include <stop_token>

namespace ock::control_client {
enum class ClientErrc : std::uint32_t {
  InvalidInput = 1, Incompatible, MissingCapability, Transport, Timeout,
  Interrupted, Protocol, IntentConflict, File
};
inline constexpr foundation::ErrorDomain client_domain{"ock.control.client"};
inline foundation::Error error(ClientErrc c) {
  return foundation::Error{foundation::ErrorCode::make<client_domain>(static_cast<std::uint32_t>(c))};
}
// 只负责请求/响应字节通道；实现由前端装配，ControlClient 不链接 LocalIPC/Runtime。
class ExchangePort {
public:
  virtual ~ExchangePort() = default;
  virtual foundation::Result<data::Payload> exchange(const data::Payload &,
      std::chrono::milliseconds, std::stop_token) = 0;
};
struct Hello {
  std::string application_id, instance_id, host_incarnation;
  std::optional<std::string> dedup_epoch;
  std::vector<std::string> supported_methods;
  std::string observation_backend;
  std::size_t frame_bytes = 0;
};
class Client {
public:
  static foundation::Result<Client> open(std::shared_ptr<ExchangePort>,
      std::chrono::milliseconds timeout = std::chrono::milliseconds(5000), std::stop_token = {});
  const Hello &hello() const noexcept { return hello_; }
  bool supports(std::string_view) const noexcept;
  // 单调用者会话；返回完整且已经校验 id/方向的 JSON-RPC 应答，包括远端 error。
  foundation::Result<data::Payload> call(std::string_view method, const data::Payload &params,
      std::stop_token = {});
private:
  Client(std::shared_ptr<ExchangePort> port, std::chrono::milliseconds timeout)
      : port_(std::move(port)), timeout_(timeout) {}
  foundation::Result<data::Payload> exchange(std::string_view, const data::Payload &, std::stop_token);
  std::shared_ptr<ExchangePort> port_;
  std::chrono::milliseconds timeout_;
  Hello hello_;
  std::uint64_t sequence_ = 0;
};
// 单份机器结果的固定退出码；Accepted 仅表示准入，不表示后台成功。
int exit_code(data::ValueView response) noexcept;
foundation::Result<std::string> quote(std::string_view);
} // namespace ock::control_client
