#include <ock/control/observation_transport.hpp>
namespace ock::control {
namespace {
struct Reservation final : runtime::policy::TransmissionReservation {
  const ProtocolObservationTransport *owner;
  std::unique_ptr<FrameBufferReservation> buffer;
  bool used = false;
  Reservation(const ProtocolObservationTransport *o,std::unique_ptr<FrameBufferReservation> b) : owner(o),buffer(std::move(b)) {}
  std::size_t capacity() const noexcept override { return buffer->capacity(); }
};
}
Result<std::shared_ptr<ProtocolObservationTransport>> ProtocolObservationTransport::create(std::shared_ptr<FrameSink> sink,FramePriority priority) {
  if(!sink||(priority!=FramePriority::Control&&priority!=FramePriority::Notification)) return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  return std::shared_ptr<ProtocolObservationTransport>(new ProtocolObservationTransport(std::move(sink),priority));
}
Result<void> ProtocolObservationTransport::queue_ack(std::span<const std::byte> bytes) {
  return sink_->queue_frame(bytes,FramePriority::Control);
}
Result<std::unique_ptr<runtime::policy::TransmissionReservation>> ProtocolObservationTransport::reserve(std::size_t size) {
  auto buffer = sink_->reserve_frame(size,priority_);
  if(!buffer) return foundation::make_unexpected(buffer.error());
  return std::unique_ptr<runtime::policy::TransmissionReservation>(new Reservation(this,std::move(*buffer)));
}
runtime::policy::StartResult ProtocolObservationTransport::start_now(const runtime::policy::PreparedTransmission &frame,
    runtime::policy::TransmissionReservation &ticket) noexcept {
  using runtime::policy::StartResult;
  auto own = dynamic_cast<Reservation*>(&ticket);
  if(!own || own->owner != this || own->used || own->capacity() != frame.bytes().size()) return StartResult::NotStarted;
  const auto started = sink_->start_frame(*own->buffer,frame.bytes());
  if(started == ByteStart::NotStarted) return StartResult::NotStarted;
  own->used = true;
  return started == ByteStart::Started ? StartResult::Started : StartResult::Unknown;
}
void ProtocolObservationTransport::close() noexcept { sink_->close(); }
}
