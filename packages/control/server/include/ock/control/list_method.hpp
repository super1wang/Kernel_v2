#pragma once
#include <ock/control/cursor.hpp>
#include <ock/control/router.hpp>
#include <ock/control/subscription.hpp>
namespace ock::control {
class ListMethod final : public MethodPort {
public:
  static Result<std::shared_ptr<ListMethod>>
      create(std::shared_ptr<const runtime::policy::SessionAuthority>,
             std::shared_ptr<const runtime::policy::VerifiedCaller>,
             std::shared_ptr<ObservationTransport>, CursorCodec, CursorContext);
  Result<std::optional<data::Payload>>
  dispatch(const runtime::policy::VerifiedCaller &, std::string_view,
           data::ValueView) override;
  Result<runtime::policy::StartResult> pump();
  void disconnect() noexcept override;
  ~ListMethod() override;

private:
  struct State;
  explicit ListMethod(std::shared_ptr<State> state)
      : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};
} // namespace ock::control
