#include "query_lifetime.hpp"
#include "summary_wire.hpp"
#include <mutex>
#include <ock/control/get_method.hpp>

namespace ock::control {
namespace policy = runtime::policy;
namespace {
template <class T> Result<T> invalid() {
  return foundation::make_unexpected(error(ProtocolErrc::InvalidRequest));
}
void text(data::PayloadBuilder &b, std::string_view key,
          std::string_view value) {
  (void)b.key(key);
  (void)b.string(value);
}
} // namespace
struct GetMethod::State : detail::QueryLifetime {
  std::shared_ptr<policy::SessionAuthority> session;
  std::shared_ptr<const policy::VerifiedCaller> caller;
  std::shared_ptr<ObservationTransport> transport;
  std::string id;
  struct Encoder final : policy::ProjectionEncoderPort {
    std::weak_ptr<State> state;
    explicit Encoder(std::shared_ptr<State> s) : state(s) {}
    Result<std::vector<std::byte>>
    encode(const policy::ProjectionSnapshot &projection,
           std::size_t maximum) override {
      auto s = state.lock();
      if (!s || projection.kind() != policy::ProjectionKind::Summary ||
          maximum <= 12)
        return invalid<std::vector<std::byte>>();
      const auto &summary =
          std::get<std::shared_ptr<const contracts::ExecutionSummary>>(
              projection.value())
              ->value();
      data::PayloadBuilder b;
      (void)b.begin_object();
      text(b, "jsonrpc", "2.0");
      text(b, "id", s->id);
      (void)b.key("result");
      (void)b.begin_object();
      detail::summary_fields(b,summary,false);
      (void)b.end_object();
      (void)b.end_object();
      auto value = b.freeze();
      if (!value)
        return foundation::make_unexpected(value.error());
      return encode_frame(*value, maximum - 12);
    }
  };
};
Result<std::shared_ptr<GetMethod>>
GetMethod::create(std::shared_ptr<policy::SessionAuthority> session,
                  std::shared_ptr<const policy::VerifiedCaller> caller,
                  std::shared_ptr<ObservationTransport> transport) {
  if (!session || !caller || !transport)
    return invalid<std::shared_ptr<GetMethod>>();
  auto state = std::make_shared<State>();
  state->session = std::move(session);
  state->caller = std::move(caller);
  state->transport = std::move(transport);
  auto sender =
      policy::SendCoordinator::create(state->session, std::make_shared<detail::QueryLifetime::Sink>(state,state->transport),
                                      std::make_shared<State::Encoder>(state));
  if (!sender)
    return foundation::make_unexpected(sender.error());
  state->sender = std::move(*sender);
  return std::shared_ptr<GetMethod>(new GetMethod(state));
}
Result<binding::CompiledSchema> GetMethod::parameters() {
  return binding::CompiledSchema::compile(
      R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"execution_ref":{"type":"object","properties":{"execution_id":{"type":"string","pattern":"^[0-9a-f]{32}$"}},"required":["execution_id"],"additionalProperties":false}},"required":["execution_ref"],"additionalProperties":false})");
}
Result<std::optional<data::Payload>>
GetMethod::dispatch(const policy::VerifiedCaller &caller, std::string_view id,
                    data::ValueView params) {
  auto s = state_;
  auto active=detail::QueryLifetime::enter(s);
  if (!active || &caller != s->caller.get() || id.empty() || id.size() > 96 ||
      std::any_of(id.begin(), id.end(),
                  [](unsigned char c) { return c < 33 || c > 126; }) ||
      params.kind() != data::Kind::Object || params.size() != 1 ||
      params.at("execution_ref").size() != 1)
    return invalid<std::optional<data::Payload>>();
  auto ref = wire_id<foundation::TaskId>(
      params.at("execution_ref").at("execution_id"));
  if (!ref)
    return foundation::make_unexpected(ref.error());
  auto summary = s->session->observations()->get(caller, {*ref},
                                                 policy::AccessUse::GetSummary);
  if (!summary)
    return foundation::make_unexpected(summary.error());
  s->id = id;
  auto queued = active->sender->enqueue_response(summary->response);
  s->id.clear();
  if (!queued)
    return foundation::make_unexpected(queued.error());
  return std::optional<data::Payload>{};
}
Result<policy::StartResult> GetMethod::pump() {
  auto s = state_;
  auto active=detail::QueryLifetime::enter(s);
  if (!active) return invalid<policy::StartResult>();
  return active->sender->start_next();
}
void GetMethod::disconnect() noexcept {
  state_->close(state_->transport);
}
GetMethod::~GetMethod() { disconnect(); }
} // namespace ock::control
