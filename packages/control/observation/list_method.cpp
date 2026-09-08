#include "query_lifetime.hpp"
#include <mutex>
#include <ock/control/list_method.hpp>
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
template <class Id>
void ref(data::PayloadBuilder &b, std::string_view key, std::string_view field,
         const Id &id) {
  (void)b.key(key);
  (void)b.begin_object();
  text(b, field, wire_text(id));
  (void)b.end_object();
}
} // namespace
struct ListMethod::State : detail::QueryLifetime {
  std::shared_ptr<policy::SessionAuthority> session;
  std::shared_ptr<const policy::VerifiedCaller> caller;
  std::shared_ptr<ObservationTransport> transport;
  CursorCodec codec;
  CursorContext identity;
  std::string id;
  std::optional<std::string> next;
  std::optional<CursorPosition> resumed_position;
  State(CursorCodec c, CursorContext context)
      : codec(std::move(c)), identity(std::move(context)) {}
  CursorContext context(const policy::PageBindingData &page) const {
    auto result = identity;
    result.host = wire_text(page.host);
    result.caller =
        wire_text(caller->view().description().principal.principal_id);
    result.owner = wire_text(page.owner.principal_id);
    result.view = page.permission_generation;
    result.connection=wire_text(page.connection);
    result.delegation=page.delegation_generation;
    result.phases = page.phases == contracts::PhaseSet::All ? "all"
                    : page.phases == contracts::PhaseSet::Terminal
                        ? "terminal"
                        : "nonterminal";
    return result;
  }
  struct Resume final : policy::PageContinuationPort {
    State &state;
    std::string_view token;
    Resume(State &s, std::string_view t) : state(s), token(t) {}
    Result<contracts::KeysetPosition>
    restore(const policy::PageBindingData &page) override {
      if (wire_text(page.host) != state.identity.host ||
          (page.restore == policy::RestoreMode::Present) !=
              state.identity.store.has_value())
        return invalid<contracts::KeysetPosition>();
      auto position = state.codec.read(token, state.context(page));
      if (!position)
        return foundation::make_unexpected(position.error());
      state.resumed_position=*position;
      return contracts::KeysetPosition{page.host, position->upper,
                                       position->position};
    }
  };
  struct Encoder final : policy::ProjectionEncoderPort {
    std::weak_ptr<State> owner;
    explicit Encoder(std::shared_ptr<State> s) : owner(s) {}
    Result<std::vector<std::byte>>
    encode(const policy::ProjectionSnapshot &projection,
           std::size_t maximum) override {
      auto s = owner.lock();
      if (!s || projection.kind() != policy::ProjectionKind::Page ||
          maximum <= 12)
        return invalid<std::vector<std::byte>>();
      const auto &page = std::get<contracts::ListPage>(projection.value());
      if (wire_text(page.host) != s->identity.host ||
          page.retention_scope.view() != "managed_active_and_retained_terminal")
        return invalid<std::vector<std::byte>>();
      data::PayloadBuilder b;
      (void)b.begin_object();
      text(b, "jsonrpc", "2.0");
      text(b, "id", s->id);
      (void)b.key("result");
      (void)b.begin_object();
      text(b, "consistency", "live_keyset");
      text(b, "host_incarnation", wire_text(page.host));
      text(b, "retention_scope", page.retention_scope.view());
      if (s->next)
        text(b, "next_cursor", *s->next);
      (void)b.key("items");
      (void)b.begin_array();
      constexpr std::string_view phases[] = {
          "Queued",     "WaitingResources", "Running", "WaitingChild",
          "Finalizing", "Suspended",        "Terminal"};
      for (const auto &item : page.items) {
        const auto &v = item.summary->value();
        (void)b.begin_object();
        ref(b, "execution_ref", "execution_id", v.execution.execution_id);
        (void)b.key("operation");
        (void)b.begin_object();
        text(b, "name", v.operation.name.view());
        text(b, "version", v.operation.version.text());
        (void)b.end_object();
        ref(b, "owner", "principal_id", v.owner.principal_id);
        if (v.parent)
          ref(b, "parent", "execution_id", v.parent->execution_id);
        text(b, "phase", phases[static_cast<unsigned>(v.phase)]);
        text(b, "observation_version", std::to_string(v.version.value()));
        (void)b.key("progress");
        (void)b.begin_object();
        text(b, "completed", std::to_string(v.progress.completed));
        text(b, "total", std::to_string(v.progress.total));
        (void)b.key("published");
        (void)b.boolean(false);
        (void)b.end_object();
        (void)b.key("facts");
        (void)b.begin_array();
        for (const auto &f : v.facts) {
          if (!f.reference || (f.kind != contracts::FactKind::Published &&
                               f.kind != contracts::FactKind::Effect &&
                               f.kind != contracts::FactKind::Lifecycle))
            continue;
          (void)b.begin_object();
          text(b, "kind",
               f.kind == contracts::FactKind::Published ? "StateCommitted"
               : f.kind == contracts::FactKind::Effect  ? "EffectResolved"
                                                        : "LifecycleResolved");
          std::visit(
              [&](const auto &id) { text(b, "reference", wire_text(id)); },
              *f.reference);
          (void)b.key("published");
          (void)b.boolean(f.kind == contracts::FactKind::Published);
          (void)b.end_object();
        }
        (void)b.end_array();
        (void)b.end_object();
      }
      (void)b.end_array();
      (void)b.end_object();
      (void)b.end_object();
      auto value = b.freeze();
      if (!value)
        return foundation::make_unexpected(value.error());
      return encode_frame(*value, maximum - 12);
    }
  };
};
Result<std::shared_ptr<ListMethod>>
ListMethod::create(std::shared_ptr<policy::SessionAuthority> session,
                   std::shared_ptr<const policy::VerifiedCaller> caller,
                   std::shared_ptr<ObservationTransport> transport,
                   CursorCodec codec, CursorContext context) {
  if (!session || !caller || !transport || context.host.size() != 32 ||
      context.store.has_value() != context.restore.has_value())
    return invalid<std::shared_ptr<ListMethod>>();
  auto s = std::make_shared<State>(std::move(codec), std::move(context));
  s->session = std::move(session);
  s->caller = std::move(caller);
  s->transport = std::move(transport);
  auto sender = policy::SendCoordinator::create(
      s->session, std::make_shared<detail::QueryLifetime::Sink>(s,s->transport), std::make_shared<State::Encoder>(s));
  if (!sender)
    return foundation::make_unexpected(sender.error());
  s->sender = std::move(*sender);
  return std::shared_ptr<ListMethod>(new ListMethod(s));
}
Result<std::optional<data::Payload>>
ListMethod::dispatch(const policy::VerifiedCaller &caller, std::string_view id,
                     data::ValueView params) {
  auto s = state_;
  auto active=detail::QueryLifetime::enter(s);
  if (!active || &caller != s->caller.get() || id.empty() || id.size() > 96 ||
      std::any_of(id.begin(), id.end(),
                  [](unsigned char c) { return c < 33 || c > 126; }))
    return invalid<std::optional<data::Payload>>();
  auto request = parse_list(params, caller.view().description().principal);
  if (!request)
    return foundation::make_unexpected(request.error());
  std::optional<policy::PageBinding> continuation;
  s->resumed_position.reset();
  if (request->cursor) {
    State::Resume verifier(*s, *request->cursor);
    auto restored =
        s->session->observations()->resume(caller, request->request, verifier);
    if (!restored)
      return foundation::make_unexpected(restored.error());
    continuation = *restored;
  }
  auto page =
      s->session->observations()->list(caller, request->request, continuation);
  if (!page)
    return foundation::make_unexpected(page.error());
  s->id = id;
  s->next.reset();
  if (page->continuation) {
    auto &b = page->continuation->value();
    if (wire_text(b.host) != s->identity.host ||
        (b.restore == policy::RestoreMode::Present) !=
            s->identity.store.has_value())
      return invalid<std::optional<data::Payload>>();
    auto token = s->resumed_position
        ? s->codec.issue(s->context(b),CursorPosition{b.upper_ordinal,b.before_ordinal,
                            s->resumed_position->issued,s->resumed_position->expires})
        : s->codec.issue(s->context(b), b.upper_ordinal, b.before_ordinal);
    if (!token)
      return foundation::make_unexpected(token.error());
    s->next = std::move(*token);
  }
  auto queued = active->sender->enqueue_response(page->response);
  s->id.clear();
  s->next.reset();
  if (!queued)
    return foundation::make_unexpected(queued.error());
  return std::optional<data::Payload>{};
}
Result<policy::StartResult> ListMethod::pump() {
  auto s = state_;
  auto active=detail::QueryLifetime::enter(s);
  if (!active) return invalid<policy::StartResult>();
  return active->sender->start_next();
}
void ListMethod::disconnect() noexcept {
  state_->close(state_->transport);
}
ListMethod::~ListMethod() { disconnect(); }
} // namespace ock::control
