#pragma once
#include <ock/control_protocol/protocol.hpp>
namespace ock::control {
enum class FramePriority { Control, Notification };
enum class ByteStart { NotStarted, Started, Unknown };
class FrameBufferReservation {
public:
  virtual ~FrameBufferReservation() = default;
  virtual std::size_t capacity() const noexcept = 0;
protected:
  FrameBufferReservation() = default;
};
// 可信字节端口；Start 的事实判据同 Policy 起点合同，不能把异步排队当 Started。
class FrameSink {
public:
  virtual ~FrameSink() = default;
  virtual Result<void> queue_frame(std::span<const std::byte>, FramePriority) = 0;
  virtual Result<std::unique_ptr<FrameBufferReservation>> reserve_frame(std::size_t, FramePriority) = 0;
  virtual ByteStart start_frame(FrameBufferReservation &, std::span<const std::byte>) noexcept = 0;
  virtual void close() noexcept = 0;
};
}
