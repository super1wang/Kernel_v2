#include <ock/control/subscription_methods.hpp>
namespace ock::control {
namespace {
class SubscriptionMethod final : public MethodPort {
public:
  SubscriptionMethod(std::shared_ptr<SubscriptionConnection> connection,
                     std::shared_ptr<const runtime::policy::VerifiedCaller> caller,
                     bool subscribe)
      : connection_(std::move(connection)), caller_(std::move(caller)),
        subscribe_(subscribe) {}
  Result<std::optional<data::Payload>> dispatch(
      const runtime::policy::VerifiedCaller &caller, std::string_view id,
      data::ValueView params) override {
    if (&caller != caller_.get() || !caller.view().revalidate())
      return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
    if (subscribe_) {
      auto request = parse_subscribe(params, caller.view().description().principal);
      if (!request)
        return foundation::make_unexpected(request.error());
      auto ack = connection_->subscribe(id, *request);
      if (!ack)
        return foundation::make_unexpected(ack.error());
      return std::optional<data::Payload>{};
    }
    auto request = parse_unsubscribe(params);
    if (!request)
      return foundation::make_unexpected(request.error());
    auto removed = connection_->unsubscribe(*request);
    if (!removed)
      return foundation::make_unexpected(removed.error());
    data::PayloadBuilder b;
    (void)b.begin_object();
    (void)b.key("removed");
    (void)b.boolean(*removed);
    (void)b.end_object();
    auto value = b.freeze();
    if (!value)
      return foundation::make_unexpected(value.error());
    return std::optional<data::Payload>(std::move(*value));
  }
  void disconnect() noexcept override {
    try { connection_->close(); } catch (...) {}
  }
private:
  std::shared_ptr<SubscriptionConnection> connection_;
  std::shared_ptr<const runtime::policy::VerifiedCaller> caller_;
  bool subscribe_;
};
}
Result<std::vector<Method>> subscription_methods(
    std::shared_ptr<SubscriptionConnection> connection,
    std::shared_ptr<const runtime::policy::VerifiedCaller> caller,
    const binding::SchemaResources &resources) {
  if (!connection || !caller)
    return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
  auto subscribe = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"urn:ock:rpc:notifications:1#/$defs/subscribe/properties/params"})", resources);
  if (!subscribe)
    return foundation::make_unexpected(subscribe.error());
  auto unsubscribe = binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","$ref":"urn:ock:rpc:notifications:1#/$defs/unsubscribe/properties/params"})", resources);
  if (!unsubscribe)
    return foundation::make_unexpected(unsubscribe.error());
  return std::vector<Method>{
      {"notifications.subscribe", *subscribe,
       std::make_shared<SubscriptionMethod>(connection, caller, true)},
      {"notifications.unsubscribe", *unsubscribe,
       std::make_shared<SubscriptionMethod>(connection, caller, false)}};
}
} // namespace ock::control
