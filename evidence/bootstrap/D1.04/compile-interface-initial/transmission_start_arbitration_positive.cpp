#include <array>
#include "packages/runtime/policy/policy.hpp"
using namespace ock::contracts;using namespace ock::runtime::policy;
struct ReservedSlot final : TransmissionReservation {
  explicit ReservedSlot(std::size_t size) : size_(size) {}
  std::size_t capacity() const noexcept override { return size_; }
private:
  std::size_t size_;
};
struct CompilableSink final : TransmissionStartPort {
  Result<std::unique_ptr<TransmissionReservation>> reserve(std::size_t size) override {
    if (!size || size > received.size())
      return make_unexpected(ock::contracts::error(ContractsErrc::BudgetExceeded));
    auto slot = std::make_unique<ReservedSlot>(size);
    issued = slot.get();
    return std::unique_ptr<TransmissionReservation>(std::move(slot));
  }
  StartResult start_now(const PreparedTransmission& transmission,
                        TransmissionReservation& reservation) noexcept override {
    auto bytes = transmission.bytes();
    (void)transmission.binding(); (void)transmission.projection();
    if (&reservation != issued || bytes.empty() || bytes.size() > reservation.capacity())
      return StartResult::NotStarted;
    for (std::size_t i = 0; i < bytes.size(); ++i) received[i] = bytes[i];
    issued = nullptr;
    return StartResult::Started;
  }
  std::array<std::byte, 1024> received{};
  ReservedSlot* issued = nullptr;
};

