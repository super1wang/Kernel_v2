#pragma once
#include <ock/control_protocol/transport.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace ock::local_ipc {
enum class IpcErrc : std::uint32_t {
  InvalidEndpoint = 1, System, Authentication, Closed, Budget, Timeout
};
inline constexpr foundation::ErrorDomain ipc_domain{"ock.local_ipc"};
inline foundation::Error error(IpcErrc code) {
  return foundation::Error{foundation::ErrorCode::make<ipc_domain>(
      static_cast<std::uint32_t>(code))};
}
// 只能从操作系统采集；JSON/命令行中的角色或 SID 不构成此身份。
class PeerIdentity {
public:
  const std::string &sid() const noexcept { return sid_; }
  std::uint32_t process_id() const noexcept { return process_id_; }
private:
  friend struct NativeSecurity;
  PeerIdentity(std::string sid, std::uint32_t pid)
      : sid_(std::move(sid)), process_id_(pid) {}
  std::string sid_;
  std::uint32_t process_id_;
};
struct PipeOptions {
  std::string instance;
  std::string expected_server_sid;
  std::vector<std::string> allowed_client_sids;
  // 传输限额均包括 12 字节帧头；Protocol 的 body budget 对应减去 12。
  std::size_t frame_bytes = 4 * 1024 * 1024;
  std::size_t notification_bytes = 16384;
  std::size_t control_queue_bytes = 4 * 1024 * 1024;
  std::size_t notification_queue_bytes = 1024 * 1024;
  std::size_t connections = 32;
  std::chrono::milliseconds io_timeout{30000};
};
foundation::Result<std::string> current_user_sid();
using Queue = control::FramePriority;
using Start = control::ByteStart;
class FrameReservation : public control::FrameBufferReservation {
public:
  ~FrameReservation();
  FrameReservation(const FrameReservation &) = delete;
  FrameReservation &operator=(const FrameReservation &) = delete;
  std::size_t size() const noexcept;
  std::size_t capacity() const noexcept override { return size(); }
private:
  friend class Connection;
  struct State;
  explicit FrameReservation(std::unique_ptr<State>);
  std::unique_ptr<State> state_;
};
class Connection : public control::FrameSink {
public:
  using Message = std::function<void(data::Payload)>;
  using Closed = std::function<void()>;
  using Tick = std::function<void()>;
  ~Connection();
  Connection(const Connection &) = delete;
  Connection &operator=(const Connection &) = delete;
  // 服务端首个 Message 回调前返回 nullopt；只有非空结果才是已验证身份。
  std::optional<PeerIdentity> peer() const;
  // 每个连接的回调串行；回调不能阻塞等待同一连接。所有队列均有界。
  // 可选 Tick 每 10 ms 在同一 I/O 线程执行；必须有界且非阻塞，关闭后排空。
  void start(Message, Closed, Tick = {});
  foundation::Result<void> send(std::span<const std::byte>, Queue = Queue::Control);
  foundation::Result<std::unique_ptr<FrameReservation>> reserve(std::span<const std::byte>, Queue);
  // 唯一同步首字节起点；不等待、不分配。返回 Started 前真实前缀已写入 OS 管道。
  Start start_now(FrameReservation &, std::span<const std::byte> exact_bytes = {}) noexcept;
  foundation::Result<void> queue_frame(std::span<const std::byte> bytes, Queue queue) override { return send(bytes,queue); }
  foundation::Result<std::unique_ptr<control::FrameBufferReservation>> reserve_frame(std::size_t, Queue) override;
  Start start_frame(control::FrameBufferReservation &, std::span<const std::byte>) noexcept override;
  void close() noexcept;
  void close_after_flush() noexcept;
  // 宿主线程关闭后等待回调排空；禁止在本连接回调内调用。
  bool wait_closed(std::chrono::milliseconds timeout);
private:
  friend class Server;
  friend class FrameReservation;
  friend foundation::Result<std::shared_ptr<Connection>> connect(const PipeOptions &);
  struct State;
  explicit Connection(std::shared_ptr<State>);
  std::shared_ptr<State> state_;
};
class Server {
public:
  using Accepted = std::function<void(std::shared_ptr<Connection>)>;
  static foundation::Result<std::unique_ptr<Server>> listen(PipeOptions, Accepted);
  ~Server();
  void close() noexcept;
private:
  struct State;
  explicit Server(std::shared_ptr<State>);
  std::shared_ptr<State> state_;
};
foundation::Result<std::shared_ptr<Connection>> connect(const PipeOptions &);
} // namespace ock::local_ipc
