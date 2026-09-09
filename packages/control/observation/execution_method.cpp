#include "query_lifetime.hpp"
#include "summary_wire.hpp"
#include <ock/control/execution_method.hpp>
#include <algorithm>
namespace ock::control {
namespace policy=runtime::policy;
namespace {
template<class T> Result<T> invalid(ProtocolErrc code=ProtocolErrc::InvalidRequest) {
  return foundation::make_unexpected(error(code));
}
bool valid_id(std::string_view id) {
  return !id.empty()&&id.size()<=96&&std::none_of(id.begin(),id.end(),[](unsigned char c){return c<33||c>126;});
}
void ref_fields(data::PayloadBuilder& b,const contracts::SummaryInput& summary) {
  (void)b.key("execution_ref");(void)b.begin_object();
  outcome_wire::text(b,"execution_id",wire_text(summary.execution.execution_id));(void)b.end_object();
  outcome_wire::text(b,"host_incarnation",wire_text(summary.host));
}
}
struct ExecutionMethod::State:detail::QueryLifetime {
  Kind kind;
  std::shared_ptr<runtime::host::HostSession> host_session;
  std::shared_ptr<policy::ObservationAuthorization> observations;
  std::shared_ptr<const policy::VerifiedCaller> caller;
  std::shared_ptr<ObservationTransport> transport;
  std::shared_ptr<policy::ClockPort> clock;
  std::vector<ExecutionResultBinding> bindings;
  std::chrono::milliseconds maximum_wait;
  struct Pending {std::string id;std::unique_ptr<runtime::host::ExecutionWaitPort> wait;};
  std::optional<Pending> pending;
  std::string id;
  std::optional<data::Payload> result;
  runtime::host::ExecutionWaitState wait_state=runtime::host::ExecutionWaitState::Terminal;
  contracts::CancelDisposition cancel_state=contracts::CancelDisposition::Requested;
  const ExecutionResultBinding* binding(const contracts::OperationKey& key) const {
    auto found=std::find_if(bindings.begin(),bindings.end(),[&](const auto& b){return b.key_==key;});
    return found==bindings.end()?nullptr:&*found;
  }
  struct Encoder final:policy::ProjectionEncoderPort {
    std::weak_ptr<State> state;
    explicit Encoder(std::shared_ptr<State> s):state(s) {}
    Result<std::vector<std::byte>> encode(const policy::ProjectionSnapshot& projection,std::size_t maximum) override {
      auto s=state.lock();if(!s||maximum<=12||projection.kind()!=policy::ProjectionKind::Summary)return invalid<std::vector<std::byte>>();
      const auto& summary=std::get<std::shared_ptr<const contracts::ExecutionSummary>>(projection.value())->value();
      data::PayloadBuilder b;(void)b.begin_object();outcome_wire::text(b,"jsonrpc","2.0");
      outcome_wire::text(b,"id",s->id);(void)b.key("result");(void)b.begin_object();
      if(s->kind==Kind::Result) {
        if(!s->result)return invalid<std::vector<std::byte>>();
        outcome_wire::text(b,"projection","full");ref_fields(b,summary);
        (void)b.key("reply");auto copied=outcome_wire::value(b,s->result->view());
        if(!copied)return foundation::make_unexpected(copied.error());
      } else if(s->kind==Kind::Cancel) {
        ref_fields(b,summary);
        constexpr std::string_view dispositions[]={"Requested","AlreadyClaimed","AlreadyTerminal"};
        outcome_wire::text(b,"disposition",dispositions[static_cast<unsigned>(s->cancel_state)]);
      } else {
        bool available=false;
        if(auto binding=s->binding(summary.operation))available=binding->reader_->available(*s->host_session,*s->caller,summary.execution);
        detail::summary_fields(b,summary,available);
        if(s->kind==Kind::Wait) {
          constexpr std::string_view states[]={"Terminal","Timeout","Cancelled"};
          outcome_wire::text(b,"wait_state",states[static_cast<unsigned>(s->wait_state)]);
        }
      }
      (void)b.end_object();(void)b.end_object();auto value=b.freeze();
      if(!value)return foundation::make_unexpected(value.error());return encode_frame(*value,maximum-12);
    }
  };
  Result<void> unavailable(std::string_view request_id) {
    data::PayloadBuilder b;(void)b.begin_object();outcome_wire::text(b,"jsonrpc","2.0");
    outcome_wire::text(b,"id",request_id);(void)b.key("error");(void)b.begin_object();
    (void)b.key("code");(void)b.int64(-32010);outcome_wire::text(b,"message","NotAvailable");
    (void)b.end_object();(void)b.end_object();auto value=b.freeze();if(!value)return foundation::make_unexpected(value.error());
    auto frame=encode_frame(*value);if(!frame)return foundation::make_unexpected(frame.error());
    return transport->queue_ack(*frame);
  }
};
Result<std::shared_ptr<ExecutionMethod>> ExecutionMethod::create(Kind kind,
    std::shared_ptr<runtime::host::HostSession> session,std::shared_ptr<const policy::VerifiedCaller> caller,
    std::shared_ptr<ObservationTransport> transport,std::vector<ExecutionResultBinding> bindings,
    std::shared_ptr<policy::ClockPort> clock,std::chrono::milliseconds maximum_wait) {
  if(static_cast<unsigned>(kind)>3||!session||!caller||!transport||!clock||bindings.size()>128||maximum_wait.count()<1||maximum_wait>std::chrono::seconds(30))
    return invalid<std::shared_ptr<ExecutionMethod>>();
  for(std::size_t i=0;i<bindings.size();++i)for(std::size_t j=0;j<i;++j)
    if(bindings[i].key_==bindings[j].key_)return invalid<std::shared_ptr<ExecutionMethod>>();
  auto context=session->catalog_context();if(!context)return foundation::make_unexpected(context.error());
  auto s=std::make_shared<State>();s->kind=kind;s->host_session=std::move(session);s->caller=std::move(caller);
  s->transport=std::move(transport);s->bindings=std::move(bindings);s->clock=std::move(clock);s->maximum_wait=maximum_wait;
  s->observations=context->authorization->observations();
  auto sender=s->host_session->send_coordinator(std::make_shared<detail::QueryLifetime::Sink>(s,s->transport),std::make_shared<State::Encoder>(s));
  if(!sender)return foundation::make_unexpected(sender.error());s->sender=std::move(*sender);
  return std::shared_ptr<ExecutionMethod>(new ExecutionMethod(std::move(s)));
}
std::string_view ExecutionMethod::name(Kind kind) noexcept {
  constexpr std::string_view names[]={"execution.get","execution.wait","execution.cancel","result.read"};
  return static_cast<unsigned>(kind)<=3?names[static_cast<unsigned>(kind)]:std::string_view{};
}
Result<binding::CompiledSchema> ExecutionMethod::parameters(Kind kind) {
  if(static_cast<unsigned>(kind)>3)return invalid<binding::CompiledSchema>();
  std::string schema=R"({"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object","properties":{"execution_ref":{"type":"object","properties":{"execution_id":{"type":"string","pattern":"^[0-9a-f]{32}$"}},"required":["execution_id"],"additionalProperties":false})";
  if(kind==Kind::Wait)schema+=R"(,"wait_timeout_ms":{"type":"integer","minimum":0,"maximum":30000})";
  schema+=R"(},"required":["execution_ref"],"additionalProperties":false})";
  return binding::CompiledSchema::compile(schema);
}
Result<std::optional<data::Payload>> ExecutionMethod::dispatch(const policy::VerifiedCaller& caller,
    std::string_view id,data::ValueView params) {
  auto s=state_;auto active=detail::QueryLifetime::enter(s);
  if(!active||&caller!=s->caller.get()||!valid_id(id)||params.kind()!=data::Kind::Object||
      params.at("execution_ref").kind()!=data::Kind::Object||params.at("execution_ref").size()!=1)
    return invalid<std::optional<data::Payload>>();
  const auto timeout=params.at("wait_timeout_ms");
  if(params.size()!=(s->kind==Kind::Wait&&!timeout.missing()?2u:1u))return invalid<std::optional<data::Payload>>();
  auto ref=wire_id<foundation::TaskId>(params.at("execution_ref").at("execution_id"));
  if(!ref)return foundation::make_unexpected(ref.error());
  if(s->kind==Kind::Wait) {
    {std::lock_guard lock(s->mutex);if(s->pending)return invalid<std::optional<data::Payload>>(ProtocolErrc::BudgetExceeded);}
    auto duration=s->maximum_wait;
    if(!timeout.missing()) {
      auto count=timeout.uint64();if(!count||*count>static_cast<std::uint64_t>(s->maximum_wait.count()))return invalid<std::optional<data::Payload>>();
      duration=std::chrono::milliseconds(*count);
    }
    auto now=s->clock->now();if(now>policy::TimePoint::max()-duration)return invalid<std::optional<data::Payload>>();
    auto wait=s->host_session->prepare_wait(s->caller,{*ref},now+duration);
    if(!wait)return foundation::make_unexpected(wait.error());
    State::Pending pending{std::string(id),std::move(*wait)};
    {std::lock_guard lock(s->mutex);if(s->closed)return invalid<std::optional<data::Payload>>(ProtocolErrc::Closed);s->pending.emplace(std::move(pending));}
    return std::optional<data::Payload>{};
  }
  auto use=s->kind==Kind::Result?policy::AccessUse::ReadResult:
      s->kind==Kind::Cancel?policy::AccessUse::CancelExecution:policy::AccessUse::GetSummary;
  auto summary=s->observations->get(caller,{*ref},use);if(!summary)return foundation::make_unexpected(summary.error());
  if(s->kind==Kind::Result) {
    auto binding=s->binding(summary->summary->value().operation);if(!binding)return foundation::make_unexpected(contracts::error(contracts::ContractsErrc::Rejected));
    auto value=binding->reader_->read(*s->host_session,caller,{*ref});if(!value)return foundation::make_unexpected(value.error());
    s->result.emplace(std::move(value->value));summary->response=std::move(value->response);
  } else if(s->kind==Kind::Cancel) {
    auto cancelled=s->host_session->cancel(caller,{*ref});if(!cancelled)return foundation::make_unexpected(cancelled.error());
    s->cancel_state=*cancelled;
  }
  struct Clear {State& state;~Clear(){state.id.clear();state.result.reset();}} clear{*s};
  s->id=id;auto queued=active->sender->enqueue_response(summary->response);
  if(!queued)return foundation::make_unexpected(queued.error());return std::optional<data::Payload>{};
}
Result<policy::StartResult> ExecutionMethod::pump() {
  auto s=state_;auto active=detail::QueryLifetime::enter(s);if(!active)return invalid<policy::StartResult>(ProtocolErrc::Closed);
  std::optional<State::Pending> pending;
  {std::lock_guard lock(s->mutex);pending=std::move(s->pending);s->pending.reset();}
  if(pending) {
    auto waited=pending->wait->poll();
    if(waited&&!*waited) {
      std::lock_guard lock(s->mutex);if(!s->closed)s->pending=std::move(pending);
    } else {
      Result<void> queued;
      if(waited) {
        s->id=pending->id;s->wait_state=(**waited).state;
        queued=active->sender->enqueue_response((**waited).observed.response);s->id.clear();
      }
      if(!waited||!queued) {auto sent=s->unavailable(pending->id);if(!sent)return foundation::make_unexpected(sent.error());}
    }
  }
  return active->sender->start_next();
}
void ExecutionMethod::disconnect() noexcept {
  auto s=state_;s->close(s->transport);std::optional<State::Pending> pending;
  {std::lock_guard lock(s->mutex);pending=std::move(s->pending);s->pending.reset();}
}
ExecutionMethod::~ExecutionMethod(){disconnect();}
}
