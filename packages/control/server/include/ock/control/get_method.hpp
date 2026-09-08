#pragma once
#include <ock/control/router.hpp>
#include <ock/control/subscription.hpp>

namespace ock::control {
// B2 查询骨架只投影现有 ObservationPort 的摘要；不伪造完整任务结果。
class GetMethod final : public MethodPort {
public:
  static Result<std::shared_ptr<GetMethod>>
      create(std::shared_ptr<runtime::policy::SessionAuthority>,
             std::shared_ptr<const runtime::policy::VerifiedCaller>,
             std::shared_ptr<ObservationTransport>);
  static Result<binding::CompiledSchema> parameters();
  Result<std::optional<data::Payload>>
  dispatch(const runtime::policy::VerifiedCaller &, std::string_view,
           data::ValueView) override;
  Result<runtime::policy::StartResult> pump();
  void disconnect() noexcept override;
  ~GetMethod() override;

private:
  struct State;
  explicit GetMethod(std::shared_ptr<State> state) : state_(std::move(state)) {}
  std::shared_ptr<State> state_;
};
} // namespace ock::control
